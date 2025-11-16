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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define OSD_DEFAULT_CHAR_X    53
#define OSD_DEFAULT_CHAR_Y    20
#define SEND_OSD_ON_CHANGE_ONLY 0
#define MSP_AGGREGATION_TIMEOUT_MSEC     1500
#define THREAD_MSP_WRITE_SLEEP_MSEC      2000 /* 2 seconds */

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
    char board_info[64];
    bool uid_ready;
    bool name_ready;
    bool fc_variant_ready;
    bool fc_version_ready;
    bool board_info_ready;
} fc_properties_t;

typedef enum {
    MSP_DISPLAYPORT_KEEPALIVE,
    MSP_DISPLAYPORT_CLOSE,
    MSP_DISPLAYPORT_CLEAR,
    MSP_DISPLAYPORT_DRAW_STRING,
    MSP_DISPLAYPORT_DRAW_SCREEN,
    MSP_DISPLAYPORT_SET_OPTIONS,
    MSP_DISPLAYPORT_DRAW_SYSTEM
} msp_displayport_cmd_e;

static msp_displayport_cb_t displayport_cb = NULL;
static volatile bool run = false;
static fc_properties_t fc_properties = {0};

static pthread_t fc_read_thread;
static pthread_t fc_write_thread;
static pthread_mutex_t aggr_mutex = PTHREAD_MUTEX_INITIALIZER;

static aggregated_buffer_t aggregation_buffer[2] = {{NULL, 0, 0}, {NULL, 0, 0}};
static uint8_t current_aggregation_buffer = 0;
#if SEND_OSD_ON_CHANGE_ONLY
static uint16_t aggregation_timeout = MSP_AGGREGATION_TIMEOUT_MSEC;
static uint64_t last_aggregation_send = 0;
#endif
static uint64_t last_aggregation_update = 0;  /* time of last frame append into aggregation buffer */
static uint16_t aggregation_mtu = MSP_AGGR_MTU;
static msp_interface_t msp_interface = { 0 };
static volatile bool fc_ready = false;

// OSD keepalive logic
static pthread_mutex_t osd_mutex = PTHREAD_MUTEX_INITIALIZER;

// CRSF LinkStatistics structure for telemetry
typedef struct {
    uint8_t uplink_rssi_1;      // RSSI of antenna 1 (dBm + 130)
    uint8_t uplink_rssi_2;      // RSSI of antenna 2 (dBm + 130)
    uint8_t uplink_link_quality; // Uplink link quality (0-100%)
    int8_t  uplink_snr;         // SNR (dB)
    uint8_t active_antenna;     // Active antenna (0 or 1)
    uint8_t rf_mode;           // RF mode (0-7)
    uint8_t uplink_tx_power;   // TX power (0-8, actual power = 2^value mW)
    uint8_t downlink_rssi;     // Downlink RSSI (dBm + 130)
    uint8_t downlink_link_quality; // Downlink link quality (0-100%)
    int8_t  downlink_snr;      // Downlink SNR (dB)
} __attribute__((packed)) crsf_link_statistics_t;

// Telemetry data
static crsf_link_statistics_t link_stats = {0};

static uint64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static inline aggregated_buffer_t* aggr_cur(void)
{
    return &aggregation_buffer[current_aggregation_buffer];
}

static inline aggregated_buffer_t* aggr_prev(void)
{
    return &aggregation_buffer[current_aggregation_buffer ^ 1];
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

#if SEND_OSD_ON_CHANGE_ONLY
    if (aggr_cur()->buffer && aggr_cur()->size > 0) {
        if (aggr_cur()->size == aggr_prev()->size &&
            memcmp(aggr_cur()->buffer, aggr_prev()->buffer, aggr_cur()->size) == 0) {
            // No change from last sent buffer, skip sending
            switch_aggregation_buffer();
            return;
        }
    }
#endif

    ssize_t sent = displayport_cb ? displayport_cb((const char*)cur->buffer, cur->size) : cur->size;
    if (sent < 0) {
        fprintf(stderr, "Error: displayport_cb() returned %zd\n", sent);
        // Keep buffer to retry on next tick if you want; here we still switch to avoid blocking.
    }

    switch_aggregation_buffer();
#if SEND_OSD_ON_CHANGE_ONLY
    last_aggregation_send = get_time_ms();
#endif
}

static void send_display_size(uint8_t canvas_size_x, uint8_t canvas_size_y)
{
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

static void send_displayport_heartbeat(void)
{
    uint8_t buffer[10] = {0};
    uint8_t cmd = MSP_DISPLAYPORT_KEEPALIVE;
    int len = construct_msp_command_v1((uint8_t*)buffer, MSP_DISPLAYPORT, &cmd, sizeof(cmd), MSP_OUTBOUND);
    if (len > 0) (void)msp_interface_write(&msp_interface, buffer, sizeof(buffer));
    // printf("[MSP] DisplayPort Keepalive sent\n");
}

void msp_send_update_rssi(int rssi)
{
    send_fc_tx_info((uint8_t)rssi);
}

// RX callback: assemble raw MSP frame and append into the current aggregation buffer.
// No sending here — flush thread handles cadence.
static void rx_msp_callback(uint8_t owner, msp_version_t msp_version, uint16_t msp_cmd, uint16_t data_size, const uint8_t *payload)
{
    (void)owner;
    (void)msp_version;
    bool need_send = false;

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
        fc_properties.uid_ready = true;

        printf("[MSP] Device UID received: %s\n", fc_properties.device_uid);
    }

    else if (msp_cmd == MSP_NAME) {
        memcpy(fc_properties.name, payload, data_size < 256 ? data_size : 256);
        fc_properties.name[sizeof(fc_properties.name) - 1] = '\0';
        fc_properties.name_ready = true;

        printf("[MSP] Device Name received: %s\n", fc_properties.name);
    }

    else if (msp_cmd == MSP_FC_VARIANT) {
        memcpy(fc_properties.fc_variant, payload, data_size < sizeof(fc_properties.fc_variant) ? data_size : sizeof(fc_properties.fc_variant));
        fc_properties.fc_variant[sizeof(fc_properties.fc_variant) - 1] = '\0';
        fc_properties.fc_variant_ready = true;

        printf("[MSP] FC Variant received: %s\n", fc_properties.fc_variant);
    }

    else if (msp_cmd == MSP_FC_VERSION) {
        sprintf(fc_properties.fc_version, "%d.%d.%d",
                payload[0], payload[1], payload[2]);
        fc_properties.fc_version_ready = true;
        printf("[MSP] FC Version received: %s\n", fc_properties.fc_version);
    }

    else if (msp_cmd == MSP_API_VERSION) {
        if (data_size >= 2) {
            uint16_t api_version = (uint16_t)(payload[0] | (payload[1] << 8));
            printf("[MSP] API Version received: %u\n", api_version);
        }
    }

    else if (msp_cmd == MSP_BOARD_INFO) {
        if (data_size >= 2) {
            if (memcmp(payload, "ARDU", 4) == 0) {
                printf("[MSP] Detected ArduPilot board info\n");
                uint16_t hw_version = (uint16_t)(payload[4] | (payload[5] << 8));
                uint8_t aio_flags = payload[6];
                uint8_t capabilities = payload[7];
                uint8_t fw_string_len = payload[8];
                (void)hw_version;
                (void)aio_flags;
                (void)capabilities;
                if (fw_string_len > 0 && fw_string_len <= data_size - 9) {
                    memcpy(fc_properties.board_info, &payload[9], fw_string_len < sizeof(fc_properties.board_info) - 1 ? fw_string_len : sizeof(fc_properties.board_info) - 1);
                    fc_properties.board_info[fw_string_len] = '\0';
                    fc_properties.board_info_ready = true;
                    printf("[MSP] ArduPilot Board Info received: %s\n", fc_properties.board_info);
                }
            } else {
                memcpy(fc_properties.board_info, payload, data_size < sizeof(fc_properties.board_info) ? data_size : sizeof(fc_properties.board_info));
                fc_properties.board_info[data_size] = '\0';
                fc_properties.board_info_ready = true;
                printf("[MSP] Board Info received: %s\n", fc_properties.board_info);
            }
        }
    }

    // Filter only desired MSP commands; extend if needed
    else if (msp_cmd == MSP_DISPLAYPORT) {
        msp_displayport_cmd_e sub_cmd = payload[0];
        switch(sub_cmd) {
        case MSP_DISPLAYPORT_KEEPALIVE: // 0 -> Open/Keep-Alive DisplayPort
        {
            if (!fc_ready) {
                send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
                fc_ready = true;
            }
        }
            break;
        case MSP_DISPLAYPORT_CLOSE: // 1 -> Close DisplayPort
            break;
        case MSP_DISPLAYPORT_CLEAR: // 2 -> Clear Screen
            break;
        case MSP_DISPLAYPORT_DRAW_STRING: // 3 -> Draw String
            break;
        case MSP_DISPLAYPORT_DRAW_SCREEN: // 4 -> Draw Screen
            need_send = true;
            if (fc_properties.fc_variant_ready && fc_properties.fc_variant[0] == 'I') // INAV variant
            {
                /* Send DisplayPort heartbeat only after we receive all FC properties
                   In other case FC could not answer to our requests intime */
                if (fc_ready) {
                    send_displayport_heartbeat();
                }
            }
            break;
        case MSP_DISPLAYPORT_SET_OPTIONS: // 5 -> Set Options (HDZero/iNav)
            break;
        default:
            break;
        }
    }

    uint8_t frame[3 + 1 + 1 + 255 + 1] = {0}; // "$M<" len cmd payload cksum"
    if (!data_size) return;
    int len = construct_msp_command_v1(frame, msp_cmd, payload, (uint8_t)data_size, MSP_INBOUND);

    pthread_mutex_lock(&aggr_mutex);

    aggregated_buffer_t* cur = aggr_cur();

    // If frame does not fit — flush immediately once, then retry append.
    if ((uint32_t)cur->size + len > cur->cap || need_send) {
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
    last_aggregation_update = get_time_ms();  /* track time of last new data appended */

    pthread_mutex_unlock(&aggr_mutex);

    /* Send selected MSP command path through to Ground Station */
    switch (msp_cmd) {
    case MSP_FC_VARIANT: 
    // case OTHER_MSP_COMMAND_YOU_WANT_TO_FORWARD:
        {
            char variant_str[257] = {0};
            memcpy(variant_str, payload, data_size < 256 ? data_size : 256);
            printf("[MSP] FC Variant received: %s\n", variant_str);
            break;
        }
    default:
        break;
    }
}

static void* fc_write_thread_fn(void *arg)
{
    (void)arg;

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = THREAD_MSP_WRITE_SLEEP_MSEC * 1000000L;

    while (run) {
        /* Periodically request FC variant */
        send_variant_request();
        nanosleep(&ts, NULL);
    }

    return NULL;
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
        if (!is_all_fc_properties_ready()) {
            request_fc_info();
            usleep(500 * 1000);
        } else {
            if (!fc_ready) {
                printf("[MSP] All FC properties are ready\n");
                fc_ready = true;
                send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
            }
        }

        int ret = msp_interface_read(&msp_interface, &run);
        if (ret != MSP_INTERFACE_OK) {
            if (ret == MSP_INTERFACE_RX_TIME_OUT) {
                fc_ready = false;
            } else {
                fprintf(stderr, "[MSP] UART receive error (%d)\n", ret);
                fc_ready = false;
                // Very rare case. Detect whether FC was reflashed to another firmware
                // Maybe need to request all info again?
                fc_properties.fc_variant_ready = false;
            }
        }

#if SEND_OSD_ON_CHANGE_ONLY
        if (aggr_cur()->size > 0 && (get_time_ms() - last_aggregation_send >= aggregation_timeout)) {
            pthread_mutex_lock(&aggr_mutex);
            send_aggregated_buffer();
            pthread_mutex_unlock(&aggr_mutex);
        }
#endif
    }

    msp_interface_deinit(&msp_interface);
    return NULL;
}

int request_fc_info(void)
{
    //send_display_size(OSD_DEFAULT_CHAR_X, OSD_DEFAULT_CHAR_Y);
    if (!fc_properties.uid_ready) {
        send_uid_request();
    }
    if (!fc_properties.name_ready) {
        send_name_request();
    }
    if (!fc_properties.fc_version_ready) {
        send_fc_version_request();
    }
    if (!fc_properties.board_info_ready) {
        send_board_info_request();
    }
    if (!fc_properties.fc_variant_ready) {
        send_variant_request();
    }

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
    last_aggregation_update = 0;

    // Start threads
    if (pthread_create(&fc_read_thread, NULL, fc_read_thread_fn, NULL) != 0) {
        fprintf(stderr, "Failed to create FC read thread\n");
        return -1;
    }

    if (pthread_create(&fc_write_thread, NULL, fc_write_thread_fn, NULL) != 0) {
        fprintf(stderr, "Failed to create FC write thread\n");
        return -1;
    }

    return 0;
}

void disconnect_from_fc(void)
{
    run = false;

    // Join threads
    pthread_join(fc_write_thread, NULL);
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
    if (fc_properties.fc_variant_ready && !strncmp(fc_properties.fc_variant, "ARDU", 4)) {
        if (fc_properties.uid_ready)
            sprintf(fc_properties.name, "Ardu-%s", fc_properties.device_uid);
        else {
            return "Ardu-Untitled";
        }
    }
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

bool is_all_fc_properties_ready(void)
{
    return fc_properties.uid_ready &&
           fc_properties.name_ready &&
           fc_properties.fc_variant_ready &&
           fc_properties.fc_version_ready &&
           fc_properties.board_info_ready;
}

// Update link statistics data
void update_telemetry_stats(uint8_t uplink_rssi_1, uint8_t uplink_rssi_2, 
                           uint8_t uplink_quality, int8_t uplink_snr,
                           uint8_t downlink_rssi, uint8_t downlink_quality, 
                           int8_t downlink_snr, uint8_t active_antenna, 
                           uint8_t rf_mode, uint8_t tx_power)
{
    link_stats.uplink_rssi_1 = uplink_rssi_1;
    link_stats.uplink_rssi_2 = uplink_rssi_2;
    link_stats.uplink_link_quality = uplink_quality;
    link_stats.uplink_snr = uplink_snr;
    link_stats.downlink_rssi = downlink_rssi;
    link_stats.downlink_link_quality = downlink_quality;
    link_stats.downlink_snr = downlink_snr;
    link_stats.active_antenna = active_antenna;
    link_stats.rf_mode = rf_mode;
    link_stats.uplink_tx_power = tx_power;
}

// Send telemetry to flight controller via UDP
void send_telemetry_to_fc(void)
{
    static int udp_socket = -1;
    static struct sockaddr_in fc_addr;
    static bool addr_initialized = false;
    
    // Initialize socket and address on first call
    if (udp_socket == -1) {
        udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0) {
            perror("Failed to create UDP socket for FC telemetry");
            return;
        }
    }
    
    if (!addr_initialized) {
        memset(&fc_addr, 0, sizeof(fc_addr));
        fc_addr.sin_family = AF_INET;
        fc_addr.sin_port = htons(5613);
        inet_pton(AF_INET, "127.0.0.1", &fc_addr.sin_addr);
        addr_initialized = true;
    }
    
    // Send the telemetry structure
    sendto(udp_socket, &link_stats, sizeof(link_stats), 0,
           (struct sockaddr*)&fc_addr, sizeof(fc_addr));
}
