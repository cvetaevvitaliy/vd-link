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
#include <sys/time.h>
#include <errno.h>
#include "log.h"

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0

typedef struct {
    bool waiting;
    bool response_ready;
    link_command_id_t cmd_id;
    link_subcommand_id_t subcmd_id;
    void* resp_data;
    size_t resp_size;
    size_t max_resp_size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} sync_cmd_ctx_t;

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
static sync_cmd_ctx_t sync_cmd_ctx = {0};

static int link_process_incoming_data(const char* data, size_t size);

static void* link_listener_thread_func(void* arg)
{
    (void)arg;
    char buffer[4096];
    struct sockaddr_in received_from_addr;  // Separate variable for received packet sender
    while (run) {
        socklen_t addr_len = sizeof(received_from_addr);
        ssize_t bytes_received = recvfrom(link_ctx.listen_sockfd, buffer, sizeof(buffer), 0,
                                          (struct sockaddr*)&received_from_addr, &addr_len);
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
                
                // Check if this is a response to a synchronous command
                pthread_mutex_lock(&sync_cmd_ctx.mutex);
                if (sync_cmd_ctx.waiting && 
                    cmd_pkt->subcmd_id == sync_cmd_ctx.subcmd_id &&
                    (cmd_pkt->cmd_id == LINK_CMD_ACK || cmd_pkt->cmd_id == LINK_CMD_NACK)) {
                    
                    // Copy response data if available and buffer has space
                    if (cmd_pkt->size > 0 && sync_cmd_ctx.resp_data && sync_cmd_ctx.max_resp_size > 0) {
                        size_t copy_size = (cmd_pkt->size < sync_cmd_ctx.max_resp_size) ? 
                                          cmd_pkt->size : sync_cmd_ctx.max_resp_size;
                        memcpy(sync_cmd_ctx.resp_data, cmd_pkt->data, copy_size);
                        sync_cmd_ctx.resp_size = copy_size;
                    } else {
                        sync_cmd_ctx.resp_size = 0;
                    }
                    
                    sync_cmd_ctx.cmd_id = cmd_pkt->cmd_id;  // Store if it was ACK or NACK
                    sync_cmd_ctx.response_ready = true;
                    pthread_cond_signal(&sync_cmd_ctx.cond);
                    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
                } else {
                    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
                    
                    // Handle as regular command callback
                    if (link_callbacks.cmd_cb) {
                        link_callbacks.cmd_cb(cmd_pkt->cmd_id, cmd_pkt->subcmd_id, cmd_pkt->data, cmd_pkt->size);
                    } else {
                        ERROR("No command callback registered");
                    }
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
#ifdef LINK_USE_WFB_NG_TUNNEL
    // WFB-ng tunnel mode - use single port for both directions
    link_ctx.listener_port = LINK_PORT_RX;
#else
    // Direct port mode - configure ports based on role:
    // Drone: listens on 5611 (commands from GS), sends to 5610 (data to GS)
    // GS: listens on 5610 (data from drone), sends to 5611 (commands to drone)
    if (is_gs == LINK_GROUND_STATION) {
        link_ctx.listener_port = LINK_PORT_DATA;  // Listen for data from drone (5610)
    } else {
        link_ctx.listener_port = LINK_PORT_CMD;   // Listen for commands from GS (5611)
    }
#endif

    // Initialize synchronous command context
    if (pthread_mutex_init(&sync_cmd_ctx.mutex, NULL) != 0) {
        PERROR("Failed to initialize sync command mutex");
        return -1;
    }
    if (pthread_cond_init(&sync_cmd_ctx.cond, NULL) != 0) {
        PERROR("Failed to initialize sync command condition variable");
        pthread_mutex_destroy(&sync_cmd_ctx.mutex);
        return -1;
    }
    sync_cmd_ctx.waiting = false;
    sync_cmd_ctx.response_ready = false;

    // Create UDP socket
    link_ctx.send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link_ctx.send_sockfd < 0) {
        PERROR("Failed to create send socket");
        pthread_cond_destroy(&sync_cmd_ctx.cond);
        pthread_mutex_destroy(&sync_cmd_ctx.mutex);
        return -1;
    }

    link_ctx.listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (link_ctx.listen_sockfd < 0) {
        PERROR("Failed to create listen socket");
        close(link_ctx.send_sockfd);
        pthread_cond_destroy(&sync_cmd_ctx.cond);
        pthread_mutex_destroy(&sync_cmd_ctx.mutex);
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
    } else {
        DEBUG("Listener socket bound to port %d", link_ctx.listener_port);
    }

    // --- Configure and bind sender socket ---
    memset(&link_ctx.sender_addr, 0, sizeof(link_ctx.sender_addr));
    link_ctx.sender_addr.sin_family = AF_INET;
    
#ifdef LINK_USE_WFB_NG_TUNNEL
    // WFB-ng tunnel mode - use tunnel IPs and single port
    inet_pton(AF_INET, is_gs == LINK_GROUND_STATION ? LINK_DRONE_IP : LINK_GS_IP, &link_ctx.sender_addr.sin_addr);
    link_ctx.sender_addr.sin_port = htons(LINK_PORT_RX);
#else
    // Direct port mode - both drone and GS send to localhost (127.0.0.1)
    inet_pton(AF_INET, "127.0.0.1", &link_ctx.sender_addr.sin_addr);
    
    // Configure target port based on role:
    // Drone sends to GS on port 5610 (data port)
    // GS sends to drone on port 5611 (command port)
    if (is_gs == LINK_GROUND_STATION) {
        link_ctx.sender_addr.sin_port = htons(LINK_PORT_CMD);   // GS sends commands to drone (5611)
    } else {
        link_ctx.sender_addr.sin_port = htons(LINK_PORT_DATA);  // Drone sends data to GS (5610)
    }
#endif

    INFO("UDP sockets initialized and bound - Listen port: %d, Send port: %d", 
         link_ctx.listener_port, ntohs(link_ctx.sender_addr.sin_port));
#ifdef LINK_USE_WFB_NG_TUNNEL
    INFO("Using WFB-ng tunnel mode");
#else
    INFO("Using direct port mode");
#endif
    INFO("Start listener thread");

    // Start listener thread
    run = true;
    if (pthread_create(&link_listener_thread, NULL, link_listener_thread_func, NULL) != 0) {
        PERROR("Failed to create listener thread");
        close(link_ctx.listen_sockfd);
        close(link_ctx.send_sockfd);
        pthread_cond_destroy(&sync_cmd_ctx.cond);
        pthread_mutex_destroy(&sync_cmd_ctx.mutex);
        return -1;
    }

    return 0;
}

void link_deinit(void)
{
    run = false;
    
    // Wake up any waiting synchronous command
    pthread_mutex_lock(&sync_cmd_ctx.mutex);
    if (sync_cmd_ctx.waiting) {
        sync_cmd_ctx.response_ready = true;
        sync_cmd_ctx.cmd_id = LINK_CMD_NACK;  // Signal failure due to shutdown
        pthread_cond_signal(&sync_cmd_ctx.cond);
    }
    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
    
    pthread_join(link_listener_thread, NULL);
    close(link_ctx.listen_sockfd);
    close(link_ctx.send_sockfd);
    
    pthread_cond_destroy(&sync_cmd_ctx.cond);
    pthread_mutex_destroy(&sync_cmd_ctx.mutex);
    
    INFO("Link deinitialized");
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
    if (size > LINK_MAX_DISPLAYPORT_SIZE) {
        ERROR("Displayport data size %zu exceeds maximum allowed %d", size, LINK_MAX_DISPLAYPORT_SIZE);
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

    // Check for maximum packet size to prevent "Invalid argument" error
    if (size > sizeof(((link_command_pkt_t*)0)->data)) {
        ERROR("Command data size %zu exceeds maximum allowed %zu", size, sizeof(((link_command_pkt_t*)0)->data));
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

    // Calculate actual packet size more accurately
    size_t actual_packet_size = sizeof(link_packet_header_t) + sizeof(cmd_pkt.cmd_id) + sizeof(cmd_pkt.subcmd_id) + sizeof(cmd_pkt.size) + size;

    ssize_t sent = sendto(link_ctx.send_sockfd, &cmd_pkt, actual_packet_size, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send command packet");
        return -1;
    }
    DEBUG("Sent command packet: cmd_id=%d, subcmd_id=%d, size=%zu", cmd_id, subcmd_id, sent);

    return 0;
}

int link_send_cmd_sync(link_command_id_t cmd_id, link_subcommand_id_t subcmd_id, const void* data, size_t size, void* resp_data, size_t* resp_size, uint32_t timeout_ms)
{
    if (resp_size == NULL) {
        ERROR("resp_size parameter cannot be NULL");
        return -1;
    }
    
    size_t max_resp_size = *resp_size;
    *resp_size = 0;  // Initialize to 0
    
    // Set up synchronous command context
    pthread_mutex_lock(&sync_cmd_ctx.mutex);
    
    if (sync_cmd_ctx.waiting) {
        pthread_mutex_unlock(&sync_cmd_ctx.mutex);
        ERROR("Another synchronous command is already in progress");
        return -1;
    }
    
    sync_cmd_ctx.waiting = true;
    sync_cmd_ctx.response_ready = false;
    sync_cmd_ctx.subcmd_id = subcmd_id;
    sync_cmd_ctx.resp_data = resp_data;
    sync_cmd_ctx.resp_size = 0;
    sync_cmd_ctx.max_resp_size = max_resp_size;
    
    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
    
    // Send the command
    int send_result = link_send_cmd(cmd_id, subcmd_id, data, size);
    if (send_result < 0) {
        // Reset sync context on send failure
        pthread_mutex_lock(&sync_cmd_ctx.mutex);
        sync_cmd_ctx.waiting = false;
        pthread_mutex_unlock(&sync_cmd_ctx.mutex);
        ERROR("Failed to send synchronous command");
        return -1;
    }
    
    // Wait for response with timeout
    struct timespec timeout;
    struct timeval now;
    gettimeofday(&now, NULL);
    
    timeout.tv_sec = now.tv_sec + (timeout_ms / 1000);
    timeout.tv_nsec = (now.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec++;
        timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&sync_cmd_ctx.mutex);
    
    int wait_result = 0;
    while (!sync_cmd_ctx.response_ready && wait_result == 0) {
        wait_result = pthread_cond_timedwait(&sync_cmd_ctx.cond, &sync_cmd_ctx.mutex, &timeout);
    }
    
    // Extract results
    int result = -1;
    if (wait_result == 0 && sync_cmd_ctx.response_ready) {
        if (sync_cmd_ctx.cmd_id == LINK_CMD_ACK) {
            result = 0;  // Success
            *resp_size = sync_cmd_ctx.resp_size;
            DEBUG("Synchronous command succeeded, response size: %zu", sync_cmd_ctx.resp_size);
        } else {
            result = -2;  // NACK received
            ERROR("Synchronous command was NACKed");
        }
    } else if (wait_result == ETIMEDOUT) {
        result = -3;  // Timeout
        ERROR("Synchronous command timed out after %d ms", timeout_ms);
    } else {
        result = -4;  // Other error
        ERROR("Synchronous command failed with wait error: %d", wait_result);
    }
    
    // Reset sync context
    sync_cmd_ctx.waiting = false;
    sync_cmd_ctx.response_ready = false;
    
    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
    
    return result;
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

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0