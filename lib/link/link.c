/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include "link.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Cross-platform includes */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <time.h>
    #include <errno.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    /* Unix/Linux/macOS includes */
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <pthread.h>
    #include <sys/time.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <errno.h>
    #include <fcntl.h>
#endif

#define MODULE_NAME_STR "LINK"
#include "log.h"

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0

/* Cross-platform definitions */
#ifdef _WIN32
    /* Windows socket compatibility */
    #define close(s) closesocket(s)
    #define ssize_t int
    typedef int socklen_t;
    
    /* Windows error codes compatibility */
    #ifndef EINTR
        #define EINTR WSAEINTR
    #endif
    
    /* Time structures for Windows */
    #ifndef _TIMESPEC_DEFINED
        #define _TIMESPEC_DEFINED
        struct timespec {
            time_t tv_sec;
            long tv_nsec;
        };
    #endif
    
    #ifndef _TIMEVAL_DEFINED
        #define _TIMEVAL_DEFINED
        struct timeval {
            long tv_sec;
            long tv_usec;
        };
    #endif
    
    /* Windows pthread alternative */
    #define pthread_t HANDLE
    #define pthread_mutex_t CRITICAL_SECTION
    #define pthread_cond_t CONDITION_VARIABLE
    #define pthread_create(thread, attr, start_routine, arg) \
        ((*thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL)) == NULL ? -1 : 0)
    #define pthread_join(thread, retval) \
        (WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0 ? 0 : -1)
    #define pthread_mutex_init(mutex, attr) \
        (InitializeCriticalSection(mutex), 0)
    #define pthread_mutex_destroy(mutex) \
        (DeleteCriticalSection(mutex))
    #define pthread_mutex_lock(mutex) \
        (EnterCriticalSection(mutex), 0)
    #define pthread_mutex_unlock(mutex) \
        (LeaveCriticalSection(mutex), 0)
    #define pthread_cond_init(cond, attr) \
        (InitializeConditionVariable(cond), 0)
    #define pthread_cond_destroy(cond) \
        ((void)(cond))
    #define pthread_cond_signal(cond) \
        (WakeConditionVariable(cond))
    #define pthread_cond_timedwait(cond, mutex, abstime) \
        win32_cond_timedwait(cond, mutex, abstime)
#endif

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
    /* Optional overrides set at runtime via link_set_remote() */
    bool override_remote;
    char override_ip[64];
    uint32_t override_data_port;
    uint32_t override_cmd_port;
    uint32_t link_rtt; // Round-Trip Time in milliseconds
} link_context_t;

typedef struct {
    detection_cmd_rx_cb_t detection_cb;
    sys_telemetry_cmd_rx_cb_t sys_telemetry_cb;
    displayport_cmd_rx_cb_t displayport_cb;
    cmd_rx_cb_t cmd_cb;
    rc_cmd_rx_cb_t rc_cb;
} link_callbacks_t;

static const char* module_name_str = "LINK";
static link_context_t link_ctx;
static pthread_t link_listener_thread;
static pthread_t rtt_check_thread;
static volatile bool run = true;
static volatile bool rtt_check_enabled = false;
static int rtt_check_interval_ms = 5000; // Default 5 seconds
static link_callbacks_t link_callbacks = {0};
static sync_cmd_ctx_t sync_cmd_ctx = {0};

#ifdef _WIN32
/* Windows-specific utility functions */
static int gettimeofday(struct timeval *tv, void *tz) {
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    
    if (tv) {
        GetSystemTimeAsFileTime(&ft);
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
        tmpres /= 10;
        tmpres -= 11644473600000000ULL;
        tv->tv_sec = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }
    return 0;
}

/* Windows condition variable timedwait implementation */
static int win32_cond_timedwait(CONDITION_VARIABLE *cond, CRITICAL_SECTION *mutex, const struct timespec *abstime) {
    struct timeval now;
    DWORD timeout_ms;
    
    gettimeofday(&now, NULL);
    
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    long long abs_ms = abstime->tv_sec * 1000 + abstime->tv_nsec / 1000000;
    
    if (abs_ms <= now_ms) {
        return ETIMEDOUT;
    }
    
    timeout_ms = (DWORD)(abs_ms - now_ms);
    
    if (SleepConditionVariableCS(cond, mutex, timeout_ms)) {
        return 0;
    } else {
        return (GetLastError() == ERROR_TIMEOUT) ? ETIMEDOUT : -1;
    }
}
#endif
static int link_send_ping_response(link_ping_pkt_t* ping_pkt);
static uint64_t get_current_timestamp(void);

static void* link_listener_thread_func(void* arg);
static void* rtt_check_thread_func(void* arg);
static int link_process_incoming_data(const char* data, size_t size);

/* Set socket to non-blocking mode (cross platform) */
static int set_socket_nonblocking(int sockfd)
{
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(sockfd, FIONBIO, &mode) != 0) {
        int err = WSAGetLastError();
        ERROR("ioctlsocket(FIONBIO) failed: %d", err);
        return -1;
    }
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        PERROR("fcntl(F_GETFL) failed");
        return -1;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        PERROR("fcntl(F_SETFL O_NONBLOCK) failed");
        return -1;
    }
#endif
    return 0;
}

/* Non-blocking UDP send wrapper.
 * Drops packet if send would block instead of blocking the caller. */
static int link_send_packet(const void *buf, size_t len)
{
    if (link_ctx.send_sockfd < 0) {
        ERROR("Send socket is not initialized");
        return -1;
    }

    ssize_t sent = sendto(link_ctx.send_sockfd,
                          (const char*)buf,
                          (int)len,
                          0,
                          (struct sockaddr*)&link_ctx.sender_addr,
                          sizeof(link_ctx.sender_addr));
    if (sent < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            DEBUG("sendto would block, dropping packet (len=%zu)", len);
            return 0; // не вважаємо це фатальною помилкою
        }
        ERROR("sendto failed with error: %d", error);
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            DEBUG("sendto would block, dropping packet (len=%zu)", len);
            return 0; // не блокуємо, просто дроп
        }
        PERROR("sendto");
#endif
        return -1;
    }

    if ((size_t)sent != len) {
        ERROR("sendto sent partial packet: %zd/%zu bytes", sent, len);
        return -1;
    }

    return 0;
}

static void* rtt_check_thread_func(void* arg)
{
    (void)arg;
    INFO("Keepalive thread started with interval %d ms", rtt_check_interval_ms);
    
    while (run && rtt_check_enabled) {
        // Send keepalive packet
        if (link_send_ping() < 0) {
            ERROR("Failed to send keepalive packet");
        } else {
            DEBUG("Keepalive packet sent");
        }
        
        // Sleep for the specified interval
        // Use small increments to allow for quick shutdown
        int remaining_ms = rtt_check_interval_ms;
        while (remaining_ms > 0 && run && rtt_check_enabled) {
            int sleep_ms = remaining_ms > 100 ? 100 : remaining_ms;
#ifdef _WIN32
            Sleep(sleep_ms);
#else
            usleep(sleep_ms * 1000);
#endif
            remaining_ms -= sleep_ms;
        }
    }
    
    INFO("Keepalive thread finished");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

uint32_t link_get_last_rtt_ms(void)
{
    return link_ctx.link_rtt;
}

static void* link_listener_thread_func(void* arg)
{
    (void)arg;
    char buffer[4096];
    struct sockaddr_in received_from_addr;  // Separate variable for received packet sender
    
    INFO("Listener thread started");

#if 0
    while (run) {
        socklen_t addr_len = sizeof(received_from_addr);
        ssize_t bytes_received = recvfrom(link_ctx.listen_sockfd, buffer, sizeof(buffer), 0,
                                          (struct sockaddr*)&received_from_addr, &addr_len);
        if (bytes_received < 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAENOTSOCK || error == WSAEINVAL || error == WSAECONNABORTED) {
                // Socket was closed, exit gracefully
                DEBUG("Socket closed, listener thread exiting");
                break;
            } else if (error != WSAEINTR && error != WSAECONNRESET) {
                ERROR("recvfrom failed with error: %d", error);
            }
#else
            if (errno == EBADF || errno == ENOTSOCK) {
                // Socket was closed, exit gracefully
                DEBUG("Socket closed, listener thread exiting");
                break;
            } else if (errno != EINTR) {
                PERROR("recvfrom");
            }
#endif
            continue;
        }
        
        if (bytes_received == 0) {
            // This shouldn't happen with UDP, but just in case
            DEBUG("Received 0 bytes, continuing");
            continue;
        }
        
        // Process the received data
        if (memcmp(buffer, "subscribe", 9) == 0) {
            /* keepalive packet */
            continue;
        }
        link_process_incoming_data(buffer, bytes_received);
    }
#endif
    while (run) {
        socklen_t addr_len = sizeof(received_from_addr);
        ssize_t bytes_received = recvfrom(link_ctx.listen_sockfd,
                                          buffer,
                                          sizeof(buffer),
                                          0,
                                          (struct sockaddr*)&received_from_addr,
                                          &addr_len);
        if (bytes_received < 0) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                /* No data available right now, avoid busy loop */
                Sleep(1); // 1 ms
                continue;
            }
            if (error == WSAENOTSOCK || error == WSAEINVAL || error == WSAECONNABORTED) {
                // Socket was closed, exit gracefully
                DEBUG("Socket closed, listener thread exiting");
                break;
            } else if (error != WSAEINTR && error != WSAECONNRESET) {
                ERROR("recvfrom failed with error: %d", error);
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* No data available, sleep a bit and check run flag again */
                usleep(1000); // 1 ms
                continue;
            }
            if (errno == EBADF || errno == ENOTSOCK) {
                // Socket was closed, exit gracefully
                DEBUG("Socket closed, listener thread exiting");
                break;
            } else if (errno != EINTR) {
                PERROR("recvfrom");
            }
#endif
            continue;
        }

        if (bytes_received == 0) {
            // This shouldn't happen with UDP, but just in case
            DEBUG("Received 0 bytes, continuing");
            continue;
        }

        // Process the received data
        if (memcmp(buffer, "subscribe", 9) == 0) {
            /* keepalive packet */
            continue;
        }
        link_process_incoming_data(buffer, (size_t)bytes_received);
    }

    INFO("Listener thread finished");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
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
                link_sys_telemetry_t telemetry = telemetry_pkt->telemetry;
                if (link_callbacks.sys_telemetry_cb) {
                    link_callbacks.sys_telemetry_cb(&telemetry);
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
        case PKT_RC:
            {
                // Handle RC data
                link_rc_pkt_t* rc_pkt = (link_rc_pkt_t*)data;
                if (link_callbacks.rc_cb) {
                    link_callbacks.rc_cb(rc_pkt->ch_values, rc_pkt->ch_cnt);
                } else {
                    ERROR("No RC callback registered");
                }
            }
            break;
        case PKT_PING:
            {
                // Handle ping packet
                DEBUG("Received ping packet");
                link_ping_pkt_t* ping_pkt = (link_ping_pkt_t*)data;
                if (ping_pkt->pong) {
                    uint64_t timestamp = get_current_timestamp();
                    link_ctx.link_rtt = timestamp - ping_pkt->timestamp;
                    printf("Link RTT: %u ms\n", link_ctx.link_rtt);
                } else {
                    link_send_ping_response(ping_pkt);
                }
            }
            break;
        default:
        ERROR("Unknown packet type: %d ssize %zu", header->type, size);
        printf("pkt data: ");
        for (size_t i = 0; i < size; i++) {
            printf(" %02x ", ((uint8_t*)data)[i]);
        }
        printf("\n");
            return -1;
    }

    return 0;
}

int link_init(link_role_t is_gs)
{
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        ERROR("WSAStartup failed with error: %d", result);
        return -1;
    }
#endif

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
    if (link_ctx.override_cmd_port) {
        link_ctx.listener_port = link_ctx.override_cmd_port;
    }
#endif
    INFO("UDP sockets: - Listen port: %d", link_ctx.listener_port);

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

    // Initialize socket descriptors to invalid values
    link_ctx.send_sockfd = -1;
    link_ctx.listen_sockfd = -1;

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

    /* Make sockets non-blocking */
    if (set_socket_nonblocking(link_ctx.listen_sockfd) < 0) {
        ERROR("Failed to set listener socket non-blocking");
        close(link_ctx.listen_sockfd);
        close(link_ctx.send_sockfd);
        pthread_cond_destroy(&sync_cmd_ctx.cond);
        pthread_mutex_destroy(&sync_cmd_ctx.mutex);
        return -1;
    }

    if (set_socket_nonblocking(link_ctx.send_sockfd) < 0) {
        ERROR("Failed to set send socket non-blocking");
        close(link_ctx.listen_sockfd);
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
    // Direct port mode - target IP default to localhost unless overridden
    if (link_ctx.override_remote && link_ctx.override_ip[0] != '\0') {
        inet_pton(AF_INET, link_ctx.override_ip, &link_ctx.sender_addr.sin_addr);
    } else {
        inet_pton(AF_INET, "127.0.0.1", &link_ctx.sender_addr.sin_addr);
    }

    // Configure target port based on role, but allow overrides if provided
    if (is_gs == LINK_GROUND_STATION) {
        uint32_t port = link_ctx.override_cmd_port ? link_ctx.override_cmd_port : LINK_PORT_CMD;
        link_ctx.sender_addr.sin_port = htons(port);   // GS sends commands to drone
    } else {
        uint32_t port = link_ctx.override_data_port ? link_ctx.override_data_port : LINK_PORT_DATA;
        link_ctx.sender_addr.sin_port = htons(port);  // Drone sends data to GS
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

void link_set_remote(const char* remote_ip, int data_port, int cmd_port)
{
    if (remote_ip && remote_ip[0] != '\0') {
        strncpy(link_ctx.override_ip, remote_ip, sizeof(link_ctx.override_ip) - 1);
        link_ctx.override_ip[sizeof(link_ctx.override_ip) - 1] = '\0';
        link_ctx.override_remote = true;
    }
    if (data_port > 0) link_ctx.override_data_port = (uint32_t)data_port;
    if (cmd_port > 0) link_ctx.override_cmd_port = (uint32_t)cmd_port;
}

void link_deinit(void)
{
    INFO("Starting link deinitialization...");
    
    // Set the stop flag first
    run = false;
    
    // Stop keepalive thread if running
    if (rtt_check_enabled) {
        INFO("Stopping keepalive thread...");
        link_stop_rtt_check();
    }
    
    // Wake up any waiting synchronous command
    pthread_mutex_lock(&sync_cmd_ctx.mutex);
    if (sync_cmd_ctx.waiting) {
        sync_cmd_ctx.response_ready = true;
        sync_cmd_ctx.cmd_id = LINK_CMD_NACK;  // Signal failure due to shutdown
        pthread_cond_signal(&sync_cmd_ctx.cond);
    }
    pthread_mutex_unlock(&sync_cmd_ctx.mutex);
    
    // Close sockets first to interrupt any blocking recvfrom calls
    INFO("Closing sockets to interrupt listener thread...");
    if (link_ctx.listen_sockfd >= 0) {
        close(link_ctx.listen_sockfd);
        link_ctx.listen_sockfd = -1;
    }
    if (link_ctx.send_sockfd >= 0) {
        close(link_ctx.send_sockfd);
        link_ctx.send_sockfd = -1;
    }
    
    // Now wait for the thread to finish (should return quickly due to socket closure)
    INFO("Waiting for listener thread to finish...");
    if (pthread_join(link_listener_thread, NULL) != 0) {
        ERROR("Failed to join listener thread");
    } else {
        INFO("Listener thread finished successfully");
    }
    
    // Clean up synchronization objects
    pthread_cond_destroy(&sync_cmd_ctx.cond);
    pthread_mutex_destroy(&sync_cmd_ctx.mutex);
    
#ifdef _WIN32
    // Cleanup Winsock
    WSACleanup();
#endif
    
    INFO("Link deinitialized");
}

int link_send_ack(uint32_t ack_id)
{
    struct {
        link_packet_header_t header;
        uint32_t ack_id;
    } ack_packet;
    
    ack_packet.header.type = PKT_ACK;
    ack_packet.header.size = sizeof(ack_id);
    ack_packet.ack_id = ack_id;

    // Send complete ACK packet (header + data)
    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&ack_packet, sizeof(ack_packet), 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
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
    uint32_t size_to_send = sizeof(link_packet_header_t) + size;

    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&pkt, size_to_send, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
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
    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&packet, bytes, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send detection packet");
        return -1;
    }

    return 0;
}

int link_send_sys_telemetry(const link_sys_telemetry_t* telemetry)
{
    if (telemetry == NULL) {
        ERROR("No telemetry data to send");
        return -1;
    }

    link_sys_telemetry_pkt_t telemetry_pkt;
    telemetry_pkt.header.type = PKT_SYS_TELEMETRY;
    telemetry_pkt.header.size = sizeof(link_sys_telemetry_pkt_t) - sizeof(link_packet_header_t);
    telemetry_pkt.telemetry = *telemetry;

    // Send the telemetry packet
    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&telemetry_pkt, sizeof(telemetry_pkt), 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
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
    
    // Check for size field overflow (uint8_t can only hold 0-255)
    if (size > LINK_MAX_CMD_SIZE) {
        ERROR("Command data size %zu exceeds uint8_t limit (%d)", size, LINK_MAX_CMD_SIZE);
        return -1;
    }

    link_command_pkt_t cmd_pkt;
    cmd_pkt.header.type = PKT_CMD;
    cmd_pkt.header.size = size + sizeof(link_command_pkt_t) - sizeof(link_packet_header_t) - sizeof(cmd_pkt.data);
    cmd_pkt.cmd_id = cmd_id;
    cmd_pkt.subcmd_id = subcmd_id;
    cmd_pkt.size = (uint8_t)size;  // Safe cast after size check
    // Copy the command data into the packet
    if (size > 0) {
        memcpy(cmd_pkt.data, data, size);
    }

    // Calculate actual packet size more accurately
    size_t actual_packet_size = sizeof(link_packet_header_t) + sizeof(cmd_pkt.cmd_id) + sizeof(cmd_pkt.subcmd_id) + sizeof(cmd_pkt.size) + size;
    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&cmd_pkt, actual_packet_size, 0, (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send command packet");
        return -1;
    }
    DEBUG("Sent command packet: cmd_id=%d, subcmd_id=%d, data_size=%zu, sent_bytes=%d", cmd_id, subcmd_id, size, (int)sent);

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

int link_send_rc(const uint16_t* channel_values, size_t channel_count)
{
    if (channel_values == NULL || channel_count == 0 || channel_count > LINK_MAX_RC_CH_NUM) {
        ERROR("Invalid channel values or count for RC packet");
        return -1;
    }

    link_rc_pkt_t rc_pkt;
    rc_pkt.header.type = PKT_RC;
    rc_pkt.header.size = sizeof(uint8_t) + sizeof(uint16_t) * channel_count; // ch_cnt + ch_values
    rc_pkt.ch_cnt = (uint8_t)channel_count;
    memcpy(rc_pkt.ch_values, channel_values, sizeof(uint16_t) * channel_count);

    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&rc_pkt, sizeof(link_packet_header_t) + rc_pkt.header.size, 0,
                          (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send RC packet");
        return -1;
    }

    return 0;
}

static uint64_t get_current_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int link_send_ping(void)
{
    link_ping_pkt_t ping_pkt;
    ping_pkt.header.type = PKT_PING;
    ping_pkt.header.size = sizeof(link_ping_pkt_t) - sizeof(link_packet_header_t);
    ping_pkt.timestamp = get_current_timestamp();
    ping_pkt.pong = 0;

    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)&ping_pkt, sizeof(link_packet_header_t) + ping_pkt.header.size, 0,
                          (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send PING packet");
        return -1;
    }

    return 0;
}

static int link_send_ping_response(link_ping_pkt_t* ping_pkt)
{
    if (ping_pkt == NULL) {
        ERROR("No ping packet provided for response");
        return -1;
    }

    ping_pkt->pong = 1; // Indicate this is a pong response

    ssize_t sent = sendto(link_ctx.send_sockfd, (const char*)ping_pkt, sizeof(link_packet_header_t) + ping_pkt->header.size, 0,
                          (struct sockaddr*)&link_ctx.sender_addr, sizeof(link_ctx.sender_addr));
    if (sent < 0) {
        PERROR("Failed to send PONG packet");
        return -1;
    }

    return 0;
}

int link_start_rtt_check(int interval_ms)
{
    if (rtt_check_enabled) {
        ERROR("RTT check thread is already running");
        return -1;
    }
    
    if (interval_ms <= 0) {
        ERROR("Invalid RTT check interval: %d", interval_ms);
        return -1;
    }
    
    rtt_check_interval_ms = interval_ms;
    rtt_check_enabled = true;
    
    if (pthread_create(&rtt_check_thread, NULL, rtt_check_thread_func, NULL) != 0) {
        PERROR("Failed to create RTT check thread");
        rtt_check_enabled = false;
        return -1;
    }

    return 0;
}

int link_stop_rtt_check(void)
{
    if (!rtt_check_enabled) {
        DEBUG("RTT check thread is not running");
        return 0;
    }
    
    rtt_check_enabled = false;
    
    if (pthread_join(rtt_check_thread, NULL) != 0) {
        ERROR("Failed to join RTT check thread");
        return -1;
    }

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

void link_register_rc_rx_cb(rc_cmd_rx_cb_t cb)
{
    link_callbacks.rc_cb = cb;
    INFO("RC callback registered");
}

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0