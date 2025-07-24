/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VD_LINK_WFB_STATUS_LINK_H
#define VD_LINK_WFB_STATUS_LINK_H
#include <stdint.h>

#define MAX_RX_PACKET_KEYS  16
#define MAX_RX_ANT_STATS    16
#define MAX_STR_LEN         64

typedef struct {
    char key[MAX_STR_LEN];
    int64_t delta;
    int64_t total;
    float bitrate_mbps;
} wfb_rx_packet;

typedef struct {
    int64_t freq, mcs, bw, ant_id;
    int64_t pkt_delta, rssi_min, rssi_avg, rssi_max;
    int64_t snr_min, snr_avg, snr_max;
    float bitrate_mbps;
} wfb_rx_ant;

typedef struct {
    char id[MAX_STR_LEN];
    wfb_rx_packet packets[MAX_RX_PACKET_KEYS];
    int packets_count;
    wfb_rx_ant ants[MAX_RX_ANT_STATS];
    int ants_count;
} wfb_rx_status;

typedef void (*wfb_status_link_rx_callback_t)(const wfb_rx_status *status);

void wfb_status_link_start(const char *host, int port, wfb_status_link_rx_callback_t cb);

void wfb_status_link_stop(void);

#endif //VD_LINK_WFB_STATUS_LINK_H
