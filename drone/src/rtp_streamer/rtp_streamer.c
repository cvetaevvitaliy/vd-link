/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "rtp_streamer/rtp_streamer.h"
#include <rtp-payload.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

#define DEFAULT_FRAME_SIZE (1400)
#define RTP_PAYLOAD_TYPE_DYNAMIC (96)

static struct rtp_payload_encode_t* encoder = NULL;
static int out_socket = -1;
static struct sockaddr_in dst_addr = {0};

static void* rtp_alloc(void* param, int bytes)
{
    (void)(param);
    return malloc(bytes);
}

static void rtp_free(void* param, void* packet)
{
    (void)(param);
    free(packet);
}

static int rtp_encode_packet(void* param, const void* packet, int bytes, uint32_t timestamp, int flags)
{
    (void)(timestamp);
    (void)(flags);

    int sock = *(int*)param;

    sendto(sock, packet, bytes, 0, (struct sockaddr*)&dst_addr, sizeof(dst_addr));

    return 0;
}

static int rtp_socket_open(const char* ip, int port)
{
    out_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_socket < 0) {
        perror("socket");
        return -1;
    }

    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dst_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }

    return 0;
}

int rtp_streamer_init(common_config_t *cfg)
{
    int ret = 0;
    rtp_packet_setsize(DEFAULT_FRAME_SIZE);

    struct rtp_payload_t handler = {0};
    handler.alloc  = rtp_alloc;
    handler.free   = rtp_free;
    handler.packet = rtp_encode_packet;

    uint16_t seq = (uint16_t)(rand() & 0xFFFF);  // random start sequence
    uint32_t ssrc = (uint32_t)rand();            // random SSRC

    encoder = rtp_payload_encode_create(RTP_PAYLOAD_TYPE_DYNAMIC, cfg->encoder_config.codec == CODEC_H264 ? "H264" : "H265", seq, ssrc, &handler, &out_socket);
    if (!encoder) {
        printf("RTP encoder creation failed\n");
        return -1;
    }

    ret = rtp_socket_open(cfg->rtp_streamer_config.ip, cfg->rtp_streamer_config.port);
    if (ret < 0) {
        printf("RTP socket open failed\n");
        rtp_payload_encode_destroy(encoder);
        encoder = NULL;
        return -1;
    }

    return 0;
}

int rtp_streamer_push_frame(void *data, int size, uint32_t timestamp)
{
    if (!encoder) {
        return -1;
    }

    return rtp_payload_encode_input(encoder, data, size, timestamp);
}

void rtp_streamer_deinit(void)
{

    if (encoder) {
        rtp_payload_encode_destroy(encoder);
        encoder = NULL;
    }

    if (out_socket >= 0) {
        close(out_socket);
        out_socket = -1;
    }
}
