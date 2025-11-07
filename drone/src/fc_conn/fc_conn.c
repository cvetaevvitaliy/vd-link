#include "fc_conn.h"
#include "msp.h"
#include "msp_interface.h"
#include "msp_protocol.h"

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
#define MSP_AGGR_MTU          ((3 + 1 + 1 + 255 + 1)*2)  /* "$M<|> len cmd payload cksum" * 2 full frames */
#endif

typedef struct  {
    uint8_t* buffer;
    uint16_t size;
    uint16_t cap;
} aggregated_buffer_t;

typedef struct {
    char device_uid[32];
    char name[64];
    char fc_variant[5];
    char fc_version[16];
    char board_info[16];
} fc_properties_t;

static msp_displayport_cb_t displayport_cb = NULL;
static volatile bool run = false;
static fc_properties_t fc_properties = {0};

static pthread_t fc_read_thread;
static pthread_mutex_t aggr_mutex = PTHREAD_MUTEX_INITIALIZER;

static aggregated_buffer_t aggregation_buffer[2] = {{NULL, 0, 0}, {NULL, 0, 0}};
static uint8_t current_aggregation_buffer = 0;
static uint16_t aggregation_mtu = MSP_AGGR_MTU;
static msp_interface_t msp_interface = { 0 };
static volatile bool fc_ready = false;

// OSD keepalive logic
static struct timeval last_osd_time = {0};
static volatile bool osd_received = false;
static pthread_mutex_t osd_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    if (!run || !fc_ready) {
        printf("[MSP] Interface not ready, skipping canvas size request\n");
        return;
    }
    
    uint8_t buffer[256] = {0}; // Initialize buffer
    uint8_t payload[2] = {canvas_size_x, canvas_size_y};
    int len = construct_msp_command_v1(buffer, MSP_SET_OSD_CANVAS, payload, 2, MSP_OUTBOUND);
    if (len > 0) {
        int ret = msp_interface_write(&msp_interface, buffer, (size_t)len);
        printf("[MSP] Set OSD canvas size request sent %d bytes (ret=%d)\n", len, ret);
    } else {
        printf("[MSP] ERROR: Failed to construct canvas size command\n");
    }
}

static void send_api_version_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_API_VERSION, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] API Version request sent %d bytes\n", len);
}

static void send_variant_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_FC_VARIANT, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] FC Variant request sent %d bytes\n", len);
}

static void send_fc_version_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_FC_VERSION, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] FC Version request sent %d bytes\n", len);
}

static void send_uid_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_UID, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] UID request sent %d bytes\n", len);
}

static void send_name_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_NAME, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] Name request sent %d bytes\n", len);
}

static void send_board_info_request(void)
{
    uint8_t buffer[256] = {0};
    int len = construct_msp_command_v1(buffer, MSP_BOARD_INFO, NULL, 0, MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, (size_t)len);
    printf("[MSP] Board Info request sent %d bytes\n", len);
}

static void send_fc_tx_info(uint8_t rssi)
{
    uint8_t buffer[10] = {0};
    int len = construct_msp_command_v1((uint8_t*)buffer, MSP_SET_TX_INFO, &rssi, sizeof(rssi), MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, sizeof(buffer));
    printf("[MSP] FC TX Info sent with RSSI %u\n", rssi);
}

void msp_send_update_rssi(int rssi)
{
    send_fc_tx_info((uint8_t)rssi);
}

// RX callback: assemble raw MSP frame and append into the current aggregation buffer.
// No sending here — flush thread handles cadence.
static void rx_msp_callback(uint8_t owner, msp_version_t msp_version, uint16_t msp_cmd, uint16_t data_size, const uint8_t *payload)
{
    // Safety check: validate payload pointer if data_size > 0
    if (data_size > 0 && payload == NULL) {
        printf("[MSP] ERROR: null payload with non-zero data_size=%u\n", data_size);
        return;
    }
    
    // Safety check: reasonable data size limit
    if (data_size > 255) {
        printf("[MSP] ERROR: excessive data_size=%u, dropping\n", data_size);
        return;
    }
    
    // printf("[MSP] RX callback: cmd=0x%02X size=%u\n", msp_message->cmd, msp_message->size);
    if (msp_cmd == MSP_UID && data_size >= 12) {
        // MSP_UID returns 12 bytes of unique device ID
        char uid_str[32];
        snprintf(uid_str, sizeof(uid_str), "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                 payload[0], payload[1], payload[2], payload[3],
                 payload[4], payload[5], payload[6], payload[7],
                 payload[8], payload[9], payload[10], payload[11]);

        strncpy(fc_properties.device_uid, uid_str, sizeof(fc_properties.device_uid) - 1);
        fc_properties.device_uid[sizeof(fc_properties.device_uid) - 1] = '\0';

        printf("[MSP] Device UID received: %s\n", fc_properties.device_uid);
        return;
    }

    else if (msp_cmd == MSP_NAME) {
        memcpy(fc_properties.name, payload, data_size < 256 ? data_size : 256);
        fc_properties.name[sizeof(fc_properties.name) - 1] = '\0';

        printf("[MSP] Device Name received: %s\n", fc_properties.name);
        return;
    }

    else if (msp_cmd == MSP_FC_VARIANT) {
        memcpy(fc_properties.fc_variant, payload, data_size < sizeof(fc_properties.fc_variant) ? data_size : sizeof(fc_properties.fc_variant));
        fc_properties.fc_variant[sizeof(fc_properties.fc_variant) - 1] = '\0';

        printf("[MSP] FC Variant received: %s\n", fc_properties.fc_variant);
        return;
    }

    else if (msp_cmd == MSP_FC_VERSION) {
        sprintf(fc_properties.fc_version, "%d.%d.%d",
                payload[0], payload[1], payload[2]);
        printf("[MSP] FC Version received: %s\n", fc_properties.fc_version);
        return;
    }

    else if (msp_cmd == MSP_API_VERSION) {
        if (data_size >= 2) {
            uint16_t api_version = (uint16_t)(payload[0] | (payload[1] << 8));
            printf("[MSP] API Version received: %u\n", api_version);
        }
        return;
    }

    else if (msp_cmd == MSP_BOARD_INFO) {
        if (data_size >= 2) {
            memcpy(fc_properties.board_info, payload, data_size < sizeof(fc_properties.board_info) ? data_size : sizeof(fc_properties.board_info));
            fc_properties.board_info[data_size] = '\0';
            printf("[MSP] Board Info received: %s\n", fc_properties.board_info);
        }
        return;
    }

    // Filter only desired MSP commands; extend if needed
    else if (msp_cmd == MSP_DISPLAYPORT) {
        uint8_t frame[3 + 1 + 1 + 255 + 1] = {0}; // "$M<" len cmd payload cksum"
        if (!data_size) return;
        int len = construct_msp_command_v1(frame, msp_cmd, payload, (uint8_t)data_size, MSP_INBOUND);

        // Update last OSD time (thread-safe)
        pthread_mutex_lock(&osd_mutex);
        gettimeofday(&last_osd_time, NULL);
        osd_received = true;
        pthread_mutex_unlock(&osd_mutex);

        pthread_mutex_lock(&aggr_mutex);

        aggregated_buffer_t* cur = aggr_cur();

        // If frame does not fit — flush immediately once, then retry append.
        if ((uint32_t)cur->size + len > cur->cap) {
            // Immediate flush (one-shot), to keep frame boundaries and MTU limits.
            send_aggregated_buffer();
            // Retry append; if still too large (oversize frame) — drop it (shouldn't happen)
            cur = aggr_cur();
            if ((uint32_t)cur->size + len > cur->cap) {
                printf("[MSP] Oversize MSP frame (%u bytes), dropped\n", len);
                pthread_mutex_unlock(&aggr_mutex);
                return;
            }
        }

        memcpy(cur->buffer + cur->size, frame, len);
        cur->size += len;

        pthread_mutex_unlock(&aggr_mutex);
    }

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

    int step = 0;
    while (run) {
        if (!fc_ready) {
            send_variant_request();
            request_fc_info();
            usleep(500 * 1000);
            fc_ready = true;
            // Initialize OSD timer (thread-safe)
            pthread_mutex_lock(&osd_mutex);
            gettimeofday(&last_osd_time, NULL);
            osd_received = false;
            pthread_mutex_unlock(&osd_mutex);
        }

        int ret = msp_interface_read(&msp_interface, &run);
        if (ret != MSP_INTERFACE_OK) {
            if (ret == MSP_INTERFACE_RX_TIME_OUT) {
                // Check if we need to send canvas keepalive (thread-safe)
                pthread_mutex_lock(&osd_mutex);
                bool should_check = osd_received;
                struct timeval last_time = last_osd_time;
                pthread_mutex_unlock(&osd_mutex);
                
                if (should_check) {
                    struct timeval current_time;
                    gettimeofday(&current_time, NULL);
                    
                    long time_diff = (current_time.tv_sec - last_time.tv_sec) * 1000000 +
                                    (current_time.tv_usec - last_time.tv_usec);
                    
                    // If no OSD for 3 seconds, send canvas size to wake up drone
                    if (time_diff > 3000000) { // 3 seconds in microseconds
                        printf("[MSP] No OSD for 3 seconds, sending canvas keepalive\n");
                        send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
                        
                        // Reset timer
                        pthread_mutex_lock(&osd_mutex);
                        gettimeofday(&last_osd_time, NULL);
                        pthread_mutex_unlock(&osd_mutex);
                    }
                }
                // Optional: verbose timeout (commented out to reduce spam)
                // fprintf(stderr, "[MSP] MSP_INTERFACE_RX_TIME_OUT\n");
            } else {
                fprintf(stderr, "[MSP] UART receive error (%d)\n", ret);
                fc_ready = false;
            }
        }
    }

    msp_interface_deinit(&msp_interface);
    return NULL;
}

int request_fc_info(void)
{
    
    send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
    send_uid_request();
    send_name_request();
    send_api_version_request();
    send_fc_version_request();
    send_board_info_request();

    return 0;
}

int connect_to_fc(const char *device, int baudrate)
{
    // Configure MSP interface (your driver)
    msp_interface.baud_rate = (uint32_t)baudrate;
    msp_interface.cb = rx_msp_callback;
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
    return fc_properties.device_uid[0] ? fc_properties.device_uid : NULL;
}

const char* get_craft_name(void)
{
    return fc_properties.name[0] ? fc_properties.name : NULL;
}

const char* get_fc_variant(void)
{
    return fc_properties.fc_variant[0] ? fc_properties.fc_variant : NULL;
}

const char* get_fc_version(void)
{
    return fc_properties.fc_version[0] ? fc_properties.fc_version : NULL;
}

const char* get_board_info(void)
{
    return fc_properties.board_info[0] ? fc_properties.board_info : NULL;
}

bool is_device_uid_ready(void)
{
    return fc_properties.device_uid[0] != '\0';
}
