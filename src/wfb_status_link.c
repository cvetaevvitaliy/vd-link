/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "wfb_status_link.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <msgpack.h>
#include <sys/poll.h>
#include "log.h"

static const char *module_name_str = "WFB_STATUS_LINK";
#define DEBUG_MSG 0

static pthread_t rx_thread;
static volatile bool rx_thread_running = 0;
static wfb_status_link_rx_callback_t user_cb = NULL;
static char server_host[64] = "0.0.0.0";
static int server_port = 8003;

static int recv_all(int sock, void *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = recv(sock, (char*)buf + done, len - done, 0);
        if (n <= 0) return n;
        done += n;
    }
    return done;
}

static int process_rx(const msgpack_object *packet)
{
    char id[64] = {0};
    msgpack_object packets = {0};
    msgpack_object rx_ant_stats = {0};
    wfb_rx_status status = {0};

    // Find fields in root MAP
    if (packet->type != MSGPACK_OBJECT_MAP) return -1;
    for (uint32_t i = 0; i < packet->via.map.size; ++i) {
        const msgpack_object_kv *kv = &packet->via.map.ptr[i];
        // id field
        if (kv->key.type == MSGPACK_OBJECT_STR &&
            kv->key.via.str.size == 2 &&
            strncmp(kv->key.via.str.ptr, "id", 2) == 0) {
            if (kv->val.type == MSGPACK_OBJECT_STR) {
                size_t cp = kv->val.via.str.size;
                if (cp > sizeof(id) - 1) cp = sizeof(id) - 1;
                memcpy(id, kv->val.via.str.ptr, cp);
                id[cp] = 0;
                memcpy(status.id, kv->val.via.str.ptr, cp);
                status.id[cp] = 0;
            }
        }
        // packets map
        else if (kv->key.type == MSGPACK_OBJECT_STR &&
                 kv->key.via.str.size == strlen("packets") &&
                 strncmp(kv->key.via.str.ptr, "packets", strlen("packets")) == 0 &&
                 kv->val.type == MSGPACK_OBJECT_MAP) {
            packets = kv->val;
        }
        // rx_ant_stats map
        else if (kv->key.type == MSGPACK_OBJECT_STR &&
                 kv->key.via.str.size == strlen("rx_ant_stats") &&
                 strncmp(kv->key.via.str.ptr, "rx_ant_stats", strlen("rx_ant_stats")) == 0 &&
                 kv->val.type == MSGPACK_OBJECT_MAP) {
            rx_ant_stats = kv->val;
        }
    }

#if DEBUG_MSG
    DEBUG("[RX] id=%s, %u packet fields, %u antennas", id, packets.type == MSGPACK_OBJECT_MAP ? packets.via.map.size : 0, rx_ant_stats.type == MSGPACK_OBJECT_MAP ? rx_ant_stats.via.map.size : 0);
#endif

    // Packets
    status.packets_count = 0;
    if (packets.type == MSGPACK_OBJECT_MAP) {
        for (uint32_t i = 0; i < packets.via.map.size; ++i) {
            const msgpack_object_kv *pkv = &packets.via.map.ptr[i];
            if (pkv->key.type == MSGPACK_OBJECT_STR &&
                pkv->val.type == MSGPACK_OBJECT_ARRAY &&
                pkv->val.via.array.size == 2)
            {
                char key[32] = {0};
                size_t klen = pkv->key.via.str.size;
                if (klen > sizeof(key) - 1) klen = sizeof(key) - 1;
                memcpy(key, pkv->key.via.str.ptr, klen);
                key[klen] = 0;

                int64_t delta = pkv->val.via.array.ptr[0].via.i64;
                int64_t total = pkv->val.via.array.ptr[1].via.i64;
                status.packets[status.packets_count].delta = delta;
                status.packets[status.packets_count].total = total;
                status.packets[status.packets_count].bitrate_mbps = ((float)delta * 8.0f) / (1024.0f);
                if (klen < sizeof(status.packets[status.packets_count].key)) {
                    memcpy(status.packets[status.packets_count].key, key, klen);
                } else {
                    ERROR("[ WFB STATUS LINK ] Packet key too long: %s", key);
                }
                status.packets_count++;
#if DEBUG_MSG
                DEBUG("  packets[%s]: delta=%lld total=%lld", key, (long long)delta, (long long)total);
#endif
            }
        }
    }

    // RX Antenna Stats
    status.ants_count = 0;
    if (rx_ant_stats.type == MSGPACK_OBJECT_MAP) {
        for (uint32_t i = 0; i < rx_ant_stats.via.map.size; ++i) {
            const msgpack_object_kv *akv = &rx_ant_stats.via.map.ptr[i];
            // Key: array [[freq,mcs,bw],ant_id]
            if (akv->key.type == MSGPACK_OBJECT_ARRAY &&
                akv->key.via.array.size == 2 &&
                akv->key.via.array.ptr[0].type == MSGPACK_OBJECT_ARRAY &&
                akv->key.via.array.ptr[0].via.array.size == 3)
            {
                int64_t freq = akv->key.via.array.ptr[0].via.array.ptr[0].via.i64;
                int64_t mcs  = akv->key.via.array.ptr[0].via.array.ptr[1].via.i64;
                int64_t bw   = akv->key.via.array.ptr[0].via.array.ptr[2].via.i64;
                int64_t ant_id = akv->key.via.array.ptr[1].via.i64;
                if (ant_id < 0) {
                    ERROR("[ WFB STATUS LINK ] Invalid antenna ID %lld", (long long)ant_id);
                    continue;
                }

                // Value: [pkt_delta, rssi_min, rssi_avg, rssi_max, snr_min, snr_avg, snr_max]
                if (akv->val.type == MSGPACK_OBJECT_ARRAY && akv->val.via.array.size >= 7) {
                    const msgpack_object* vals = akv->val.via.array.ptr;
                    int32_t packets_delta = vals[0].via.i64;
                    int32_t rssi_min  = vals[1].via.i64;
                    int32_t rssi_avg  = vals[2].via.i64;
                    int32_t rssi_max  = vals[3].via.i64;
                    int32_t snr_min   = vals[4].via.i64;
                    int32_t snr_avg   = vals[5].via.i64;
                    int32_t snr_max   = vals[6].via.i64;

                    // Copy antenna ID to status
                    status.ants[status.ants_count].freq = freq;
                    status.ants[status.ants_count].mcs = mcs;
                    status.ants[status.ants_count].bw = bw;
                    status.ants[status.ants_count].ant_id = ant_id;
                    status.ants[status.ants_count].pkt_delta = packets_delta;
                    status.ants[status.ants_count].rssi_min = rssi_min;
                    status.ants[status.ants_count].rssi_avg = rssi_avg;
                    status.ants[status.ants_count].rssi_max = rssi_max;
                    status.ants[status.ants_count].snr_min = snr_min;
                    status.ants[status.ants_count].snr_avg = snr_avg;
                    status.ants[status.ants_count].snr_max = snr_max;
                    double bitrate = ((double)packets_delta * 8.0f) / (1024.0f);
                    status.ants[status.ants_count].bitrate_mbps = (float)bitrate;
#if DEBUG_MSG
                    DEBUG("[ WFB STATUS LINK ] [ RX ] ANT[%lld] name='%s' freq=%lld mcs=%lld bw=%lld pkt/s=%d bitrate=%f rssi=[min=%d/avg=%d/max=%d] snr=[min=%d/avg=%d/max=%d]", (long long)ant_id, id, (long long)freq, (long long)mcs, (long long)bw, packets_delta, bitrate, rssi_min, rssi_avg, rssi_max, snr_min, snr_avg, snr_max);
#endif
                    status.ants_count++;
                    if (status.ants_count >= MAX_RX_ANT_STATS) {
                        ERROR("[ WFB STATUS LINK ] Too many antennas, max %d", MAX_RX_ANT_STATS);
                        break; // Prevent overflow
                    }
                }
            }
        }
    }

    if (user_cb)
        user_cb(&status);

    return 0;
}

static void process_tx(const msgpack_object *root)
{
    char id[64] = {0};
    msgpack_object packets = {0};
    msgpack_object rf_temperature = {0};

    // Iterate over all fields in TX object
    if (root->type != MSGPACK_OBJECT_MAP)
        return;
    for (uint32_t i = 0; i < root->via.map.size; ++i) {
        const msgpack_object_kv *kv = &root->via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR) {
            const char *k = kv->key.via.str.ptr;
            uint32_t l = kv->key.via.str.size;
            // Find "id"
            if (l == 2 && strncmp(k, "id", 2) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_STR) {
                    uint32_t cp = kv->val.via.str.size;
                    if (cp > sizeof(id) - 1) cp = sizeof(id) - 1;
                    memcpy(id, kv->val.via.str.ptr, cp);
                    id[cp] = 0;
                }
            } else if (l == strlen("packets") && strncmp(k, "packets", strlen("packets")) == 0 && kv->val.type == MSGPACK_OBJECT_MAP) {
                packets = kv->val;
            } else if (l == strlen("rf_temperature") && strncmp(k, "rf_temperature", strlen("rf_temperature")) == 0 && kv->val.type == MSGPACK_OBJECT_MAP) {
                rf_temperature = kv->val;
            }
        }
    }

#if DEBUG_MSG
    DEBUG("[ TX ] id=%s, %d packet fields :", id, packets.type == MSGPACK_OBJECT_MAP ? (int)packets.via.map.size : 0);
#endif

    if (packets.type == MSGPACK_OBJECT_MAP) {
        for (uint32_t i = 0; i < packets.via.map.size; ++i) {
            const msgpack_object_kv *kv = &packets.via.map.ptr[i];
            if (kv->key.type == MSGPACK_OBJECT_STR && kv->val.type == MSGPACK_OBJECT_ARRAY && kv->val.via.array.size == 2) {
                char key[32] = {0};
                uint32_t klen = kv->key.via.str.size;
                if (klen > sizeof(key) - 1) klen = sizeof(key) - 1;
                memcpy(key, kv->key.via.str.ptr, klen);
                key[klen] = 0;

                int64_t delta = kv->val.via.array.ptr[0].via.i64;
                int64_t total = kv->val.via.array.ptr[1].via.i64;
#if DEBUG_MSG
                DEBUG("  packets[%s]: delta=%lld total=%lld", key, (long long)delta, (long long)total);
#endif

            }
        }
    }

#if DEBUG_MSG
    DEBUG("  rf_temperature:");
#endif

    if (rf_temperature.type == MSGPACK_OBJECT_MAP) {
        for (uint32_t i = 0; i < rf_temperature.via.map.size; ++i) {
            const msgpack_object_kv *kv = &rf_temperature.via.map.ptr[i];
            int64_t antenna_id = -1, temperature = -1000;
            if (kv->key.type == MSGPACK_OBJECT_POSITIVE_INTEGER || kv->key.type == MSGPACK_OBJECT_NEGATIVE_INTEGER)
                antenna_id = kv->key.via.i64;
            if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER || kv->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER)
                temperature = kv->val.via.i64;
#if DEBUG_MSG
            DEBUG(" [%lld]=%lldC", (long long)antenna_id, (long long)temperature);
#endif

        }
    }
#if DEBUG_MSG
    DEBUG("");
#endif
}

static void process_title(const msgpack_object *root)
{
    char cli_title[128] = {0};
    int is_cluster = 0;
    uint32_t temp_overheat_warning = 0;

    if (root->type != MSGPACK_OBJECT_MAP)
        return;

    // Iterate through all key-value pairs in the map
    for (uint32_t i = 0; i < root->via.map.size; ++i) {
        const msgpack_object_kv *kv = &root->via.map.ptr[i];
        if (kv->key.type == MSGPACK_OBJECT_STR) {
            const char *k = kv->key.via.str.ptr;
            uint32_t l = kv->key.via.str.size;
            if (l == strlen("cli_title") && strncmp(k, "cli_title", strlen("cli_title")) == 0 && kv->val.type == MSGPACK_OBJECT_STR) {
                uint32_t cp = kv->val.via.str.size;
                if (cp > sizeof(cli_title) - 1) cp = sizeof(cli_title) - 1;
                memcpy(cli_title, kv->val.via.str.ptr, cp);
                cli_title[cp] = 0;
            }
            else if (l == strlen("is_cluster") && strncmp(k, "is_cluster",  strlen("is_cluster")) == 0 &&
                     (kv->val.type == MSGPACK_OBJECT_BOOLEAN || kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER)) {
                is_cluster = (kv->val.type == MSGPACK_OBJECT_BOOLEAN) ?
                                 kv->val.via.boolean : (kv->val.via.u64 != 0);
            }
            // temp_overheat_warning (uint)
            else if (l == strlen("temp_overheat_warning") && strncmp(k, "temp_overheat_warning", strlen("temp_overheat_warning")) == 0 &&
                     (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER)) {
                temp_overheat_warning = kv->val.via.u64;
            }
        }
    }

#if DEBUG_MSG
    DEBUG("[ WFB STATUS LINK ] [TITLE] cli_title=\"%s\" is_cluster=%d temp_overheat_warning=%u", cli_title, is_cluster, temp_overheat_warning);
#endif
}

static int process_packet(const msgpack_object* packet)
{
    char type[32] = {0};

    if (packet->type == MSGPACK_OBJECT_MAP) {
        for (uint32_t i = 0; i < packet->via.map.size; ++i) {
            const msgpack_object_kv* kv = &packet->via.map.ptr[i];
            if (kv->key.type == MSGPACK_OBJECT_STR && kv->val.type == MSGPACK_OBJECT_STR) {
                // Check if the key is "type"
                if (kv->key.via.str.size == 4 && strncmp(kv->key.via.str.ptr, "type", 4) == 0) {
                    // Copy value to type buffer
                    uint32_t vlen = kv->val.via.str.size;
                    if (vlen >= sizeof(type)) vlen = sizeof(type) - 1;
                    memcpy(type, kv->val.via.str.ptr, vlen);
                    type[vlen] = 0;
                    break;
                }
            }
        }
    }

    if (strcmp(type, "rx") == 0) {
        process_rx(packet);
    } else if (strcmp(type, "tx") == 0) {
        process_tx(packet);
    } else if (strcmp(type, "cli_title") == 0) {
        process_title(packet);
    } else {
        ERROR("Unknown wfbcli packet type '%s'", type);
        return -1;
    }
    return 0;
}

static void* rx_thread_fn(void *arg)
{
    (void)arg;
    while (rx_thread_running) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { PERROR("socket: %s", strerror(errno)); sleep(1); continue; }

        struct sockaddr_in srv = {0};
        srv.sin_family = AF_INET;
        srv.sin_port = htons(server_port);
        inet_pton(AF_INET, server_host, &srv.sin_addr);

        if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
            PERROR("connect: %s", strerror(errno));
            close(sock);
            sleep(1);
            continue;
        }
        INFO("Connected to %s:%d", server_host, server_port);

        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        while (rx_thread_running) {
            int pret = poll(&pfd, 1, 1000);
            if (pret < 0) {
                if (errno == EINTR) continue;
                PERROR("poll: %s", strerror(errno));
                break;
            }
            if (pret == 0) continue; // Timeout

            if (pfd.revents & POLLIN) {
                uint32_t sz;
                int r = recv_all(sock, &sz, 4);
                if (r != 4) { break; }
                sz = ntohl(sz);
                if (sz == 0 || sz > 1024*1024) break;

                char *buf = malloc(sz);
                if (!buf) { PERROR("malloc: %s", strerror(errno)); break; }
                if (recv_all(sock, buf, sz) != sz) { free(buf); break; }

                msgpack_unpacked result;
                msgpack_unpacked_init(&result);
                if (msgpack_unpack_next(&result, buf, sz, NULL)) {
                    msgpack_object root = result.data;
                    process_packet(&root);
                }
                msgpack_unpacked_destroy(&result);
                free(buf);
            }
        }
        close(sock);
        usleep(500000); // Wait before reconnect
    }
    return NULL;
}

void wfb_status_link_start(const char *host, int port, wfb_status_link_rx_callback_t cb)
{
    if (rx_thread_running) return;
    if (host) strncpy(server_host, host, sizeof(server_host)-1);
    server_port = port;
    user_cb = cb;
    rx_thread_running = 1;
    pthread_create(&rx_thread, NULL, rx_thread_fn, NULL);
}

void wfb_status_link_stop(void)
{
    rx_thread_running = 0;
    pthread_join(rx_thread, NULL);
}