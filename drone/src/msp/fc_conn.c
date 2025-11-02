#include "fc_conn.h"
#include "msp.h"
#include "msp_interface.h"
#include "../../lib/msp/src/msp_protocol.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>

#define OSD_DEFAULT_CHAR_X    53
#define OSD_DEFAULT_CHAR_Y    20

#ifndef MSP_AGGR_MTU
#define MSP_AGGR_MTU          ((3 + 1 + 1 + 255 + 1)*2)  // "$M<|> len cmd payload cksum" * 2 full frames
#endif

typedef struct  {
    uint8_t* buffer;
    uint16_t size;
    uint16_t cap;
} aggregated_buffer_t;

static msp_displayport_cb_t displayport_cb = NULL;
static volatile bool run = false;
static char device_uid[32] = {0}; // Store device UID from MSP_UID
static bool uid_received = false;

static pthread_t fc_read_thread;
static pthread_mutex_t aggr_mutex = PTHREAD_MUTEX_INITIALIZER;

static aggregated_buffer_t aggregation_buffer[2] = {{NULL, 0, 0}, {NULL, 0, 0}};
static uint8_t current_aggregation_buffer = 0;
static uint16_t aggregation_mtu = MSP_AGGR_MTU;
static msp_interface_t msp_interface = { 0 };
static volatile bool fc_ready = false;

static inline aggregated_buffer_t* aggr_cur(void)
{
    return &aggregation_buffer[current_aggregation_buffer];
}

// Switch to the other buffer; clear the *new current* buffer, keep the previous for duplicate check.
static void switch_aggregation_buffer(void)
{
    current_aggregation_buffer ^= 1;
    aggregated_buffer_t* cur = aggr_cur();
    if (cur->buffer && cur->cap) {
        memset(cur->buffer, 0, cur->cap);
        cur->size = 0;
    }
}

void register_msp_displayport_cb(msp_displayport_cb_t cb)
{
    displayport_cb = cb;
}

// Send current buffer via callback (with de-dup), then switch buffers.
static void send_aggregated_buffer(void)
{
    aggregated_buffer_t* cur = aggr_cur();
    if (!cur->buffer || cur->size == 0) return;

    ssize_t sent = displayport_cb((const char*)cur->buffer, cur->size);
    if (sent < 0) {
        fprintf(stderr, "Error: displayport_cb() returned %zd\n", sent);
        // Keep buffer to retry on next tick if you want; here we still switch to avoid blocking.
    }

    switch_aggregation_buffer();
}

static void send_display_size(uint8_t canvas_size_x, uint8_t canvas_size_y)
{
    uint8_t buffer[256];
    uint8_t payload[2] = {canvas_size_x, canvas_size_y};
    int len = construct_msp_command(buffer, MSP_CMD_SET_OSD_CANVAS, payload, 2, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
}

static void send_variant_request(void)
{
    uint8_t buffer[256];
    int len = construct_msp_command(buffer, MSP_CMD_FC_VARIANT, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
}

static void send_uid_request(void)
{
    uint8_t buffer[256];
    int len = construct_msp_command(buffer, MSP_UID, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
}

// RX callback: assemble raw MSP frame and append into the current aggregation buffer.
// No sending here — flush thread handles cadence.
static void rx_msp_callback(msp_msg_t *msp_message)
{
    // Filter only desired MSP commands; extend if needed
    if (!(msp_message->cmd == MSP_CMD_DISPLAYPORT ||
          msp_message->cmd == MSP_CMD_FC_VARIANT ||
          msp_message->cmd == MSP_CMD_API_VERSION ||
          msp_message->cmd == MSP_UID))
    {
        return;
    }

    // Handle MSP_UID response
    if (msp_message->cmd == MSP_UID && msp_message->size >= 12) {
        // MSP_UID returns 12 bytes of unique device ID
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 msp_message->payload[0], msp_message->payload[1], msp_message->payload[2], msp_message->payload[3],
                 msp_message->payload[4], msp_message->payload[5], msp_message->payload[6], msp_message->payload[7],
                 msp_message->payload[8], msp_message->payload[9], msp_message->payload[10], msp_message->payload[11]);
        
        strncpy(device_uid, uid_str, sizeof(device_uid) - 1);
        device_uid[sizeof(device_uid) - 1] = '\0';
        uid_received = true;
        
        printf("[MSP] Device UID received: %s\n", device_uid);
        return;
    }

    uint8_t frame[3 + 1 + 1 + 255 + 1]; // "$M<|> len cmd payload cksum"
    uint16_t flen = msp_data_from_msg(frame, msp_message);
    if (!flen) return;

    pthread_mutex_lock(&aggr_mutex);

    aggregated_buffer_t* cur = aggr_cur();

    // If frame does not fit — flush immediately once, then retry append.
    if ((uint32_t)cur->size + flen > cur->cap) {
        // Immediate flush (one-shot), to keep frame boundaries and MTU limits.
        send_aggregated_buffer();
        // Retry append; if still too large (oversize frame) — drop it (shouldn't happen)
        cur = aggr_cur();
        if ((uint32_t)cur->size + flen > cur->cap) {
            printf("Oversize MSP frame (%u bytes), dropped\n", flen);
            pthread_mutex_unlock(&aggr_mutex);
            return;
        }
    }

    memcpy(cur->buffer + cur->size, frame, flen);
    cur->size += flen;

    pthread_mutex_unlock(&aggr_mutex);

}

static void* fc_read_thread_fn(void *arg)
{
    (void)arg;

    if (msp_interface_init(&msp_interface) != MSP_INTERFACE_OK) {
        fprintf(stderr, "Error init MSP interface\n");
        return NULL;
    }

    run = true;

    // Allocate double buffers
    aggregation_buffer[0].cap = aggregation_mtu;
    aggregation_buffer[1].cap = aggregation_mtu;
    aggregation_buffer[0].size = 0;
    aggregation_buffer[1].size = 0;
    aggregation_buffer[0].buffer = (uint8_t*)malloc(aggregation_mtu);
    aggregation_buffer[1].buffer = (uint8_t*)malloc(aggregation_mtu);
    if (!aggregation_buffer[0].buffer || !aggregation_buffer[1].buffer) {
        fprintf(stderr, "Aggregation buffers alloc failed\n");
        run = false;
    } else {
        memset(aggregation_buffer[0].buffer, 0, aggregation_mtu);
        memset(aggregation_buffer[1].buffer, 0, aggregation_mtu);
    }

    while (run) {

        if (!fc_ready) {
            send_variant_request();
            usleep(500 * 1000);
            send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
            usleep(500 * 1000);
            send_uid_request();
            usleep(500 * 1000);
            fc_ready = true;
        }

        int ret = msp_interface_read(&msp_interface, &run);
        if (ret != MSP_INTERFACE_OK) {
            if (ret == MSP_INTERFACE_RX_TIME_OUT) {
                // Optional: verbose timeout
                fprintf(stderr, "MSP_INTERFACE_RX_TIME_OUT\n");
            } else {
                fprintf(stderr, "UART receive error (%d)\n", ret);
            }
            fc_ready = false;
        }
    }

    msp_interface_deinit(&msp_interface);
    return NULL;
}

int connect_to_fc(const char *device, int baudrate)
{
    // Configure MSP interface (your driver)
    msp_interface.baud_rate = (uint32_t)baudrate;
    msp_interface.msp_state.cb = rx_msp_callback;
    msp_interface.uart_name = device;
    msp_interface.telemetry_update = 10; // keep as you need

    // Reset state
    current_aggregation_buffer = 0;
    fc_ready = false;

    // Start threads
    if (pthread_create(&fc_read_thread, NULL, fc_read_thread_fn, NULL) != 0) {
        fprintf(stderr, "Failed to create FC read thread\n");
        return -1;
    }

    return 0;
}

void disconnect_from_fc(void)
{
    run = false;

    // Join threads
    pthread_join(fc_read_thread, NULL);

    // Free buffers & snapshots
    pthread_mutex_lock(&aggr_mutex);
    if (aggregation_buffer[0].buffer) {
        free(aggregation_buffer[0].buffer);
        aggregation_buffer[0].buffer = NULL;
    }
    if (aggregation_buffer[1].buffer) {
        free(aggregation_buffer[1].buffer);
        aggregation_buffer[1].buffer = NULL;
    }
    aggregation_buffer[0].size = aggregation_buffer[1].size = 0;
    aggregation_buffer[0].cap = aggregation_buffer[1].cap = 0;
    pthread_mutex_unlock(&aggr_mutex);

    fprintf(stderr, "Disconnected from flight controller\n");
}

const char* get_device_uid(void)
{
    return uid_received ? device_uid : NULL;
}

bool is_device_uid_ready(void)
{
    return uid_received;
}
