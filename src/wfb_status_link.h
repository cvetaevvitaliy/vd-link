/**
 * @file wfb_status_link.h this is part of project 'vd-link'
 * Copyright © vitalii.nimych@gmail.com 2025
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Created vitalii.nimych@gmail.com 12-07-2025
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
