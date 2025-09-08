/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include "link.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include "log.h"

typedef struct {
    int send_sockfd;
    int listen_sockfd;
    struct sockaddr_in sender_addr;
    struct sockaddr_in listener_addr;
    uint32_t listener_port;
} link_context_t;

typedef struct {
    detection_cmd_rx_cb_t detection_cb;
    sys_telemetry_cmd_rx_cb_t sys_telemetry_cb;
    displayport_cmd_rx_cb_t displayport_cb;
    cmd_rx_cb_t cmd_cb;
} link_callbacks_t;

static const char* module_name_str = "LINK";
static link_context_t link_ctx;
static pthread_t link_listener_thread;
static volatile bool run = true;
static link_callbacks_t link_callbacks = {0};

static int link_process_incoming_data(const char* data, size_t size);

static void* link_listener_thread_func(void* arg)
{
    (void)arg;
    char buffer[4096];
    while (run) {
        socklen_t addr_len = sizeof(link_ctx.sender_addr);
        ssize_t bytes_received = recvfrom(link_ctx.listen_sockfd, buffer, sizeof(buffer), 0,
                                          (struct sockaddr*)&link_ctx.sender_addr, &addr_len);
        if (bytes_received < 0) {
            PERROR("recvfrom");
            continue;
        }
        // Process the received data
        link_process_incoming_data(buffer, bytes_received);
    }
    return NULL;
}

static int link_process_incoming_data(const char* data, size_t size)
{
    if (data == NULL || size == 0) {
        ERROR("Received empty data");
        return -1;
    }
    
    if (size < sizeof(link_packet_header_t)) {
        ERROR("Received packet too small for header");
        return -1;
    }
    
    link_packet_header_t* header = (link_packet_header_t*)data;
    switch (header->type) {
        case PKT_ACK:
            // Handle ACK packet
            DEBUG("Received ACK packet");
            break;
        case PKT_DETECTION:
            // Handle detection results
            DEBUG("Received detection results");
            link_detection_pkt_t* detection_pkt = (link_detection_pkt_t*)data;
            if (link_callbacks.detection_cb) {
                link_callbacks.detection_cb(detection_pkt->results, detection_pkt->count);
            } else {
                ERROR("No detection callback registered");
            }
            break;
        case PKT_SYS_TELEMETRY:
            {
                // Handle system telemetry
                // DEBUG("Received system telemetry");
                link_sys_telemetry_pkt_t* telemetry_pkt = (link_sys_telemetry_pkt_t*)data;
                if (link_callbacks.sys_telemetry_cb) {
                    link_callbacks.sys_telemetry_cb(telemetry_pkt->cpu_temperature, telemetry_pkt->cpu_usage_percent);
                } else {
                    ERROR("No system telemetry callback registered");
                }
            }
            break;
        case PKT_CMD:
            {
                DEBUG("Received command packet");
                link_command_pkt_t* cmd_pkt = (link_command_pkt_t*)data;
                if (link_callbacks.cmd_cb) {
                    link_callbacks.cmd_cb(cmd_pkt->cmd_id,  cmd_pkt->subcmd_id, cmd_pkt->data, cmd_pkt->size);
                } else {
                    ERROR("No command callback registered");
                }
            }
            break;
        case PKT_MSP_DISPLAYPORT:
            {
                // Handle displayport data
                // DEBUG("Received displayport data");
                link_msp_displayport_pkt_t* displayport_pkt = (link_msp_displayport_pkt_t*)data;
                if (link_callbacks.displayport_cb) {
                    link_callbacks.displayport_cb(displayport_pkt->data, displayport_pkt->header.size);
                } else {
                    ERROR("No displayport callback registered");
                }
            }
            break;
        default:
            ERROR("Unknown packet type: %d", header->type);
            return -1;
    }

    return 0;
}

int link_init(link_role_t is_gs)
{
    link_ctx.listener_port = LINK_PORT_RX;

    // Create UDP socket
    link_ctx.send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link_ctx.send_sockfd < 0) {
        PERROR("Failed to create send socket");
        return -1;
    }

    link_ctx.listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link_ctx.listen_sockfd < 0) {
        PERROR("Failed to create listen socket");
        close(link_ctx.send_sockfd);
        return -1;
    }

    // --- Configure and bind listener socket ---
    memset(&link_ctx.listener_addr, 0, sizeof(link_ctx.listener_addr));
    link_ctx.listener_addr.sin_family = AF_INET;
    link_ctx.listener_addr.sin_addr.s_addr = INADDR_ANY;
    link_ctx.listener_addr.sin_port = htons(link_ctx.listener_port);

    if (bind(link_ctx.listen_sockfd,
             (struct sockaddr*)&link_ctx.listener_addr,
             sizeof(link_ctx.listener_addr)) < 0) {
        PERROR("Failed to bind listener socket");
        close(link_ctx.listen_sockfd);
        close(link_ctx.send_sockfd);
        return -1;
    }

    // --- Configure and bind sender socket ---
    memset(&link_ctx.sender_addr, 0, sizeof(link_ctx.sender_addr));
    link_ctx.sender_addr.sin_family = AF_INET;
    inet_pton(AF_INET, is_gs == LINK_GROUND_STATION ? LINK_DRONE_IP : LINK_GS_IP, &link_ctx.sender_addr.sin_addr);
    link_ctx.sender_addr.sin_port = htons(link_ctx.listener_port);

    INFO("UDP sockets initialized and bound to port L:%d", link_ctx.listener_port);
    INFO("Start listener thread");

    // Start listener thread
    if (pthread_create(&link_listener_thread, NULL, link_listener_thread_func, NULL) != 0) {
        PERROR("Failed to create listener thread");
        close(link_ctx.listen_sockfd);
        close(link_ctx.send_sockfd);
        return -1;
    }

    return 0;
}

int link_send_ack(uint32_t ack_id)
{
    link_packet_header_t header;
    header.type = PKT_ACK;
    header.size = sizeof(ack_id);

    // Send ACK packet
    ssize_t sent = sendto(link_ctx.send_sockfd, &header, sizeof(header), 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send ACK packet");
        return -1;
    }
    return 0;
}


int link_send_displayport(const char* data, size_t size)
{
    if (data == NULL || size == 0) {
        ERROR("No data to send for displayport");
        return -1;
    }
    link_msp_displayport_pkt_t pkt;
    pkt.header.type = PKT_MSP_DISPLAYPORT;
    pkt.header.size = size;
    memcpy(pkt.data, data, size);

    ssize_t sent = sendto(link_ctx.send_sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send displayport packet");
        return -1;
    }

    return 0;
}

int link_send_detection(const link_detection_box_t* results, size_t count)
{
    DEBUG("Sending detection results: %zu", count);

    link_detection_pkt_t packet;
    packet.header.type = PKT_DETECTION;
    packet.header.size = sizeof(link_detection_box_t) * count + sizeof(packet.count);

    packet.count = count;
    // Calculate the size of the packet to send
    size_t bytes = packet.header.size + sizeof(link_packet_header_t);
    memcpy(packet.results, results, sizeof(link_detection_box_t) * count);

    // Send the packet over the network
    ssize_t sent = sendto(link_ctx.send_sockfd, &packet, bytes, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send detection packet");
        return -1;
    }

    return 0;
}

int link_send_sys_telemetry(float cpu_temp, float cpu_usage)
{
    link_sys_telemetry_pkt_t telemetry_pkt;
    telemetry_pkt.header.type = PKT_SYS_TELEMETRY;
    telemetry_pkt.header.size = sizeof(link_sys_telemetry_pkt_t) - sizeof(link_packet_header_t);
    telemetry_pkt.cpu_temperature = cpu_temp;
    telemetry_pkt.cpu_usage_percent = cpu_usage;

    // Send the telemetry packet
    ssize_t sent = sendto(link_ctx.send_sockfd, &telemetry_pkt, sizeof(telemetry_pkt), 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send system telemetry packet");
        return -1;
    }

    return 0;
}

int link_send_cmd(link_command_id_t cmd_id, link_subcommand_id_t subcmd_id, const void* data, size_t size)
{
    if (data == NULL && size != 0) {
        ERROR("No data to send for command");
        return -1;
    }

    link_command_pkt_t cmd_pkt;
    cmd_pkt.header.type = PKT_CMD;
    cmd_pkt.header.size = size + sizeof(link_command_pkt_t) - sizeof(link_packet_header_t) - sizeof(cmd_pkt.data);
    cmd_pkt.cmd_id = cmd_id;
    cmd_pkt.subcmd_id = subcmd_id;
    cmd_pkt.size = size;
    // Copy the command data into the packet
    if (size > 0) {
        memcpy(cmd_pkt.data, data, size);
    }

    uint32_t cmd_size = cmd_pkt.header.size + sizeof(link_packet_header_t);

    ssize_t sent = sendto(link_ctx.send_sockfd, &cmd_pkt, cmd_size, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send command packet");
        return -1;
    }
    DEBUG("Sent command packet: cmd_id=%d, subcmd_id=%d, size=%zu", cmd_id, subcmd_id, sent);

    return 0;
}


void link_register_detection_rx_cb(detection_cmd_rx_cb_t cb)
{
    link_callbacks.detection_cb = cb;
    INFO("Detection callback registered");
}
void link_register_sys_telemetry_rx_cb(sys_telemetry_cmd_rx_cb_t cb)
{
    link_callbacks.sys_telemetry_cb = cb;
    INFO("System telemetry callback registered");
}
void link_register_displayport_rx_cb(displayport_cmd_rx_cb_t cb)
{
    link_callbacks.displayport_cb = cb;
    INFO("DisplayPort callback registered");
}
void link_register_cmd_rx_cb(cmd_rx_cb_t cb)
{
    link_callbacks.cmd_cb = cb;
    INFO("Command callback registered");
}