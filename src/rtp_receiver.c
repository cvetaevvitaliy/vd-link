/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "rtp_receiver.h"
#include "log.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <stdatomic.h>
#include <pthread.h>
#include "rtp-demuxer.h"
#include "decoder.h"
#include "rtp-profile.h"


static pthread_t rtp_thread;
static atomic_int running = 0;


static const char* codec_type_name(codec_type_t codec)
{
    switch (codec) {
    case CODEC_H264:
        return "H264";
    case CODEC_H265:
        return "H265";
    default:
        return "UNKNOWN";
    }
}

static codec_type_t detect_rtp_codec(const uint8_t* payload, int payload_len)
{
    if (!payload || payload_len < 1)
        return CODEC_UNKNOWN;

    if (payload_len < 64)
        return CODEC_UNKNOWN;

    uint8_t nalu_hdr = payload[0];

    // H.265
    uint8_t h265_type = (nalu_hdr >> 1) & 0x3F;
    INFO_M("RTP DEMUXER", "NALU header: 0x%02X, H.265 type: %d", nalu_hdr, h265_type);
    if ((h265_type >= 0 && h265_type <= 31) || (h265_type >= 32 && h265_type <= 34) || h265_type == 39)
        return CODEC_H265;
    if ((h265_type == 48 || h265_type == 49) && payload_len >= 3) {
        uint8_t fu_h265_type = (payload[2] >> 1) & 0x3F;
        if ((fu_h265_type >= 0 && fu_h265_type <= 31) || (fu_h265_type >= 32 && fu_h265_type <= 34) ||
            fu_h265_type == 39)
            return CODEC_H265;
    }

    // H.264
    uint8_t h264_type = nalu_hdr & 0x1F;
    INFO_M("RTP DEMUXER", "NALU header: 0x%02X, H.264 type: %d", nalu_hdr, h264_type);
    if ((h264_type >= 1 && h264_type <= 23) || h264_type == 5)
        return CODEC_H264;
    if (h264_type == 28 || h264_type == 29) {
        if (payload_len < 2)
            return CODEC_UNKNOWN;
        uint8_t fu_type = payload[1] & 0x1F;
        if ((fu_type >= 1 && fu_type <= 23) || fu_type == 5)
            return CODEC_H264;
    }

    return CODEC_UNKNOWN;
}


// callback for codec detection, called until the codec is found
static int detect_codec_cb(void* param, const void* packet, int bytes, uint32_t timestamp, int flags)
{
    codec_type_t* codec_ptr = (codec_type_t*)param;
    codec_type_t codec = detect_rtp_codec(packet, bytes);
    if (codec != CODEC_UNKNOWN) {
        *codec_ptr = codec;
        INFO_M("RTP DEMUXER", "Detected codec: %s", codec_type_name(codec));
        return 1; // Stop demuxing after detection
    }
    return 0; // Continue demuxing
}

// Main callback for packet processing after codec is known
static int main_rtp_cb(void* param, const void* packet, int bytes, uint32_t timestamp, int flags)
{
    struct config_t* cfg = (struct config_t*)param;

#if 0 // debug rtp
    printf("[RTP] Codec: %s, Packet size: %d, Timestamp: %u, Flags: 0x%x\n", codec_type_name(cfg->codec), bytes, timestamp, flags);
    if (bytes == 25) {
        printf("SPS :");
        for (int i = 0; i < bytes && i < 25; i++) {
            printf("%02x ", ((const uint8_t*)packet)[i]);
        }
        printf("\n");
    }

    if (bytes == 4) {
        printf("PPS :");
        for (int i = 0; i < bytes && i < 4; i++) {
            printf("%02x ", ((const uint8_t*)packet)[i]);
        }
        printf("\n");
    }
#endif

    decoder_put_frame(cfg, (void*)packet, bytes);

    return 0;
}

// Stub for HW encoder initialization
void encoder_hw_init(struct config_t *ctx)
{
    INFO_M("ENCODER INIT", "HW encoder will be initialized for codec: %s", codec_type_name(ctx->codec));
    // TODO: Place HW encoder initialization here (e.g., MPP/VPU)

    decoder_start(ctx);
}

static void* rtp_receiver_thread(void *arg)
{
    if (!arg) {
        ERROR_M("RTP", "Invalid context provided to RTP receiver thread");
        return NULL;
    }

    struct config_t *ctx = (struct config_t *)arg;

    INFO_M("RTP", "Starting RTP receiver thread on %s:%d", ctx->ip, ctx->port);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { PERROR_M("RTP", "socket: "); return NULL; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->port);
    if (inet_aton(ctx->ip, &addr.sin_addr) == 0) {
        ERROR_M("RTP", "Invalid IP: %s", ctx->ip);
        close(sock);
        return NULL;
    }
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        PERROR_M("RTP", "bind: ");
        close(sock);
        return NULL;
    }

    INFO_M("RTP", "Listening on %s:%d", ctx->ip, ctx->port);

    // RTP demuxer for detection
    uint8_t buffer[1600];
    struct pollfd fds[] = { { .fd = sock, .events = POLLIN } };
    codec_type_t detected_codec = CODEC_UNKNOWN;
    struct rtp_demuxer_t* demuxer = rtp_demuxer_create(
            100, 90000, ctx->pt, NULL, detect_codec_cb, &detected_codec
    );
    if (!demuxer) {
        ERROR_M("RTP", "Failed to create RTP demuxer for detection");
        close(sock);
        return NULL;
    }

    // Wait until codec is detected (single-thread, blocking)
    while (running && detected_codec == CODEC_UNKNOWN) {
        int ret = poll(fds, 1, 5000);
        if (ret < 0) { if (errno == EINTR) continue; PERROR_M("RTP", "poll: "); break; }
        if (ret == 0) continue;
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in peer; socklen_t len = sizeof(peer);
            ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&peer, &len);
            if (n > 0) rtp_demuxer_input(demuxer, buffer, (int)n);
        }
    }
    rtp_demuxer_destroy(&demuxer);

    if (detected_codec == CODEC_UNKNOWN) {
        ERROR_M("RTP", "Failed to detect codec from RTP stream!");
        close(sock);
        return NULL;
    }

    ctx->codec = detected_codec;
    ctx->pt = RTP_PAYLOAD_DYNAMIC;

    // HW encoder initialization (replace with your code)
    encoder_hw_init(ctx);
    char *codec_name = NULL;
    if (ctx->codec == CODEC_H264) {
        codec_name = "H264";
    } else if (ctx->codec == CODEC_H265) {
        codec_name = "H265";
    } else {
        ERROR_M("RTP", "Unsupported codec detected: %s", codec_type_name(ctx->codec));
        close(sock);
        return NULL;
    }


    // Create main demuxer for packet processing
    demuxer = rtp_demuxer_create(
            10, 90000, ctx->pt,
            codec_name,
            main_rtp_cb, ctx
    );
    if (!demuxer) {
        ERROR_M("RTP", "Failed to create main RTP demuxer");
        close(sock);
        return NULL;
    }

    // packet reception loop
    while (atomic_load(&running)) {
        int ret = poll(fds, 1, 1000);
        if (ret < 0) { if (errno == EINTR) continue; PERROR_M("RTP", "poll: "); break; }
        if (ret == 0) continue;
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in peer; socklen_t len = sizeof(peer);
            ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&peer, &len);
            if (n > 0) rtp_demuxer_input(demuxer, buffer, (int)n);
        }
    }

    rtp_demuxer_destroy(&demuxer);
    close(sock);

    INFO_M("RTP", "Exiting RTP receiver thread");

    atomic_store(&running, 0);

    return NULL;
}

int rtp_receiver_start(struct config_t *cfg)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong(&running, &expected, 1)) {
        INFO_M("RTP", "Already running RTP receiver thread");
        return -1;
    }
    return pthread_create(&rtp_thread, NULL, rtp_receiver_thread, cfg);
}

void rtp_receiver_stop(void)
{
    if (!atomic_load(&running))
        return;

    atomic_store(&running, 0);
    pthread_join(rtp_thread, NULL);

    decoder_stop();
}
