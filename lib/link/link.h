/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#ifndef LINK_H
#define LINK_H

#include <stdio.h>
#include <stdint.h>
// #include "detection_types.h"

// Uncomment to use WFB-ng tunnel mode, comment out for direct port mode
// #define LINK_USE_WFB_NG_TUNNEL

#ifdef LINK_USE_WFB_NG_TUNNEL
    // WFB-ng tunnel mode - use tunnel IPs and single port
    #define LINK_PORT_RX 6211
    #define LINK_GS_IP "10.5.0.2"
    #define LINK_DRONE_IP "10.5.0.1"
#else
    // Direct port mode - use localhost and separate ports for data/commands
    #define LINK_PORT_DATA 5610    // Port for data communication (drone->GS)
    #define LINK_PORT_CMD 5611     // Port for command communication (GS->drone)
    #define LINK_GS_IP "127.0.0.1"
    #define LINK_DRONE_IP "127.0.0.1"
#endif
#define DETECTION_OBJ_NUM_MAX_SIZE 64
#define LINK_MAX_DISPLAYPORT_SIZE 1500
#define LINK_MAX_RC_CH_NUM 16
#define LINK_MAX_CMD_SIZE 256

/* Protocol description 
    * 1. Link uses UDP sockets for communication
    * 2. Each packet has a header with type and size
    * 3. Packet types:
    *    - ACK: Acknowledgment packet
    *    - MSP_DISPLAYPORT: DisplayPort data packet
    *    - DETECTION: Detection results packet
    *    - SYS_TELEMETRY: System telemetry data packet
    *    - CMD: Command packet
    * 4. Commands can be sent to the drone or ground station
    *    - Commands could be:
    *      - GET: Get information
    *      - SET: Set configuration
    *      - ACK: Acknowledge command
    *   - Subcommands for specific actions like setting FPS, GOP, payload size, etc.
    * 5. After command SET or GET sent, ACK/NACK packet should be sent back with the actual/updated data
    * 6. Packet structure:
        +-------------------+-------------------+-------------------+
        | Packet Type       | Header Fields    | Payload Fields    |
        +-------------------+-------------------+-------------------+
        | ACK               | type, size       | None              |
        +-------------------+-------------------+-------------------+
        | MSP_DISPLAYPORT   | type, size       | data[256]         |
        +-------------------+-------------------+-------------------+
        | DETECTION         | type, size       | count, results[]  |
        +-------------------+-------------------+-------------------+
        | SYS_TELEMETRY     | type, size       | cpu_temp,         |
        |                   |                  | cpu_usage         |
        +-------------------+-------------------+-------------------+
        | CMD               | type, size       | pkt_id, cmd_id,   |
        |                   |                  | size, data[256]   |
        +-------------------+-------------------+-------------------+
        | CMD-ACK           | type, size       | pkt_id, cmd_id,   |
        |                   |                  | size, data[256]   |
        +-------------------+-------------------+-------------------+
        | RC                | type, size       | ch_count,         |
        |                   |                  | ch_values[16]     |
        +-------------------+-------------------+-------------------+
*/

typedef enum {
    LINK_DRONE = 0,
    LINK_GROUND_STATION = 1,
} link_role_t;

typedef enum {
    PKT_ACK = 0,
    PKT_MSP_DISPLAYPORT = 1,
    PKT_DETECTION = 2,
    PKT_SYS_TELEMETRY = 3,
    PKT_CMD = 4,
    PKT_RC = 5,
    PKT_LAST
} link_packet_type_t;

typedef enum {
    LINK_CMD_GET = 0,
    LINK_CMD_SET = 1,
    LINK_CMD_ACK = 2,
    LINK_CMD_NACK = 3,
} link_command_id_t;

typedef enum {
    LINK_SUBCMD_SYS_INFO = 0,
    LINK_SUBCMD_DETECTION,
    LINK_SUBCMD_FOCUS_MODE,
    LINK_SUBCMD_FPS,
    LINK_SUBCMD_BITRATE,
    LINK_SUBCMD_WFB_KEY,
    LINK_SUBCMD_GOP,
    LINK_SUBCMD_PAYLOAD_SIZE,
    LINK_SUBCMD_VBR,
    LINK_SUBCMD_SWITCH_CAMERAS,
    LINK_SUBCMD_CODEC,
} link_subcommand_id_t;

typedef struct {
    link_packet_type_t type;
    uint32_t size; // Size of the payload
    // max data size if 4078 bytes. You can send more but packet will be fragmented
} link_packet_header_t;

typedef struct {
    float x;
    float y;
    float width;
    float height;
} link_detection_box_t;

typedef struct {
    link_packet_header_t header;
    uint8_t count;
    link_detection_box_t results[DETECTION_OBJ_NUM_MAX_SIZE];
} link_detection_pkt_t;

typedef struct {
    link_packet_header_t header;
    uint8_t cmd_id;
    uint8_t subcmd_id;
    uint8_t size;
    char data[LINK_MAX_CMD_SIZE];
} link_command_pkt_t;

typedef struct {
    link_packet_header_t header;
    float cpu_temperature;
    float cpu_usage_percent;
} link_sys_telemetry_pkt_t;

typedef struct {
    link_packet_header_t header;
    char data[LINK_MAX_DISPLAYPORT_SIZE];
} link_msp_displayport_pkt_t;

typedef struct {
    link_packet_header_t header;
    uint8_t ch_cnt;
    uint16_t ch_values[LINK_MAX_RC_CH_NUM];
} link_rc_pkt_t;

typedef void (*detection_cmd_rx_cb_t)(const link_detection_box_t* results, size_t count);
typedef void (*sys_telemetry_cmd_rx_cb_t)(float cpu_temp, float cpu_usage);
typedef void (*displayport_cmd_rx_cb_t)(const char* data, size_t size);
typedef void (*cmd_rx_cb_t)(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);
typedef void (*rc_cmd_rx_cb_t)(const uint16_t* channel_values, size_t channel_count);

int link_init(link_role_t is_gs);
void link_deinit(void);

int link_send_ack(uint32_t ack_id);
int link_send_displayport(const char* data, size_t size);
int link_send_detection(const link_detection_box_t* data, size_t count);
int link_send_sys_telemetry(float cpu_temp, float cpu_usage);
int link_send_cmd(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);
int link_send_cmd_sync(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size, void* resp_data, size_t* resp_size, uint32_t timeout_ms);
int link_send_rc(const uint16_t* channel_values, size_t channel_count);

void link_register_detection_rx_cb(detection_cmd_rx_cb_t cb);
void link_register_sys_telemetry_rx_cb(sys_telemetry_cmd_rx_cb_t cb);
void link_register_displayport_rx_cb(displayport_cmd_rx_cb_t cb);
void link_register_cmd_rx_cb(cmd_rx_cb_t cb);
void link_register_rc_rx_cb(rc_cmd_rx_cb_t cb);

#endif // LINK_H