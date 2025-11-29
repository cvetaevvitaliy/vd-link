/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include"decoder.h"
#include"sdl2_display.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

/* FFmpeg */
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>


/* ---------------------- common helpers ---------------------- */

static uint64_t get_time_ms(void)
{
    struct timespec spec;
    if (clock_gettime(CLOCK_MONOTONIC, &spec) == -1) {
        return 0;
    }
    return (uint64_t)spec.tv_sec * 1000ULL +
           (uint64_t)spec.tv_nsec / 1000000ULL;
}

/* ---------------------- decoder state ----------------------- */

static AVCodecContext *g_dec_ctx = NULL;
static AVFrame        *g_frame   = NULL;

static struct SwsContext *g_sws_ctx          = NULL;
static enum AVPixelFormat g_sws_src_fmt      = AV_PIX_FMT_NONE;
static int                g_sws_w            = 0;
static int                g_sws_h            = 0;
static uint8_t           *g_sws_buf          = NULL;
static int                g_sws_buf_size     = 0;
static uint8_t           *g_sws_dst_data[4]  = {0};
static int                g_sws_dst_linesize[4] = {0};

static pthread_t  g_decoder_thread;
static atomic_int g_decoder_running = 0;

/* ---------------------- packet queue ------------------------ */

#define DEC_PKT_QUEUE_SIZE 64

struct pkt_item {
    uint8_t *data;
    int      size;
};

static struct pkt_item g_pkt_queue[DEC_PKT_QUEUE_SIZE];
static int             g_pkt_head = 0;
static int             g_pkt_tail = 0;

static pthread_mutex_t g_pkt_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_pkt_cond  = PTHREAD_COND_INITIALIZER;

static int pkt_queue_push(const void *data, int size)
{
    pthread_mutex_lock(&g_pkt_mutex);

    int next_tail = (g_pkt_tail + 1) % DEC_PKT_QUEUE_SIZE;
    if (next_tail == g_pkt_head) {
        /* queue full – дропимо найстаріший пакет (low-latency варіант) */
        struct pkt_item *old = &g_pkt_queue[g_pkt_head];
        free(old->data);
        old->data = NULL;
        old->size = 0;
        g_pkt_head = (g_pkt_head + 1) % DEC_PKT_QUEUE_SIZE;
    }

    struct pkt_item *item = &g_pkt_queue[g_pkt_tail];
    item->data = (uint8_t *)malloc((size_t)size);
    if (!item->data) {
        pthread_mutex_unlock(&g_pkt_mutex);
        return -1;
    }
    memcpy(item->data, data, (size_t)size);
    item->size = size;

    g_pkt_tail = next_tail;

    pthread_cond_signal(&g_pkt_cond);
    pthread_mutex_unlock(&g_pkt_mutex);

    return 0;
}

static int pkt_queue_pop(struct pkt_item *out)
{
    pthread_mutex_lock(&g_pkt_mutex);

    while (g_pkt_head == g_pkt_tail && atomic_load(&g_decoder_running)) {
        pthread_cond_wait(&g_pkt_cond, &g_pkt_mutex);
    }

    if (!atomic_load(&g_decoder_running) && g_pkt_head == g_pkt_tail) {
        pthread_mutex_unlock(&g_pkt_mutex);
        return -1;
    }

    *out = g_pkt_queue[g_pkt_head];
    g_pkt_queue[g_pkt_head].data = NULL;
    g_pkt_queue[g_pkt_head].size = 0;

    g_pkt_head = (g_pkt_head + 1) % DEC_PKT_QUEUE_SIZE;

    pthread_mutex_unlock(&g_pkt_mutex);
    return 0;
}

static void pkt_queue_flush(void)
{
    pthread_mutex_lock(&g_pkt_mutex);
    while (g_pkt_head != g_pkt_tail) {
        struct pkt_item *item = &g_pkt_queue[g_pkt_head];
        free(item->data);
        item->data = NULL;
        item->size = 0;
        g_pkt_head = (g_pkt_head + 1) % DEC_PKT_QUEUE_SIZE;
    }
    pthread_mutex_unlock(&g_pkt_mutex);
}

/* ---------------------- SWS (to YUV420P) -------------------- */

static int ensure_sws(int width, int height, enum AVPixelFormat src_fmt)
{
    if (g_sws_ctx &&
        g_sws_w == width &&
        g_sws_h == height &&
        g_sws_src_fmt == src_fmt) {
        return 0;
    }

    if (g_sws_ctx) {
        sws_freeContext(g_sws_ctx);
        g_sws_ctx = NULL;
    }
    if (g_sws_buf) {
        av_freep(&g_sws_buf);
        g_sws_buf_size = 0;
    }

    g_sws_ctx = sws_getContext(width, height, src_fmt,
                               width, height, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, NULL, NULL, NULL);
    if (!g_sws_ctx) {
        printf("[DECODER] sws_getContext failed\n");
        return -1;
    }

    int ret = av_image_alloc(g_sws_dst_data,
                             g_sws_dst_linesize,
                             width,
                             height,
                             AV_PIX_FMT_YUV420P,
                             1);
    if (ret < 0) {
        printf("[DECODER] av_image_alloc failed: %d\n", ret);
        sws_freeContext(g_sws_ctx);
        g_sws_ctx = NULL;
        return -1;
    }

    g_sws_buf      = g_sws_dst_data[0];
    g_sws_buf_size = ret;
    g_sws_w        = width;
    g_sws_h        = height;
    g_sws_src_fmt  = src_fmt;

    return 0;
}

/* ---------------------- decoder thread ---------------------- */

static void *decoder_thread_func(void *arg)
{
    (void)arg;
    printf("[DECODER] libavcodec decoder thread started\n");

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        printf("[DECODER] av_packet_alloc failed\n");
        return NULL;
    }

    uint64_t last_fps_time = 0;
    int      frames_in_sec = 0;
    double   current_fps   = 0.0;

    while (atomic_load(&g_decoder_running)) {
        struct pkt_item item = {0};

        if (pkt_queue_pop(&item) < 0) {
            break; /* stopped */
        }

        if (!item.data || item.size <= 0) {
            free(item.data);
            continue;
        }

        if (!g_dec_ctx) {
            free(item.data);
            continue;
        }

        av_packet_unref(pkt);
        if (av_new_packet(pkt, item.size) < 0) {
            printf("[DECODER] av_new_packet failed\n");
            free(item.data);
            continue;
        }
        memcpy(pkt->data, item.data, (size_t)item.size);
        pkt->pts = (int64_t)get_time_ms();

        free(item.data);

        int ret = avcodec_send_packet(g_dec_ctx, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("[DECODER] avcodec_send_packet error: %s\n", errbuf);
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(g_dec_ctx, g_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                printf("[DECODER] avcodec_receive_frame error: %s\n", errbuf);
                break;
            }

            int width  = g_frame->width;
            int height = g_frame->height;

            if (width <= 0 || height <= 0) {
                continue;
            }

            if (g_frame->format == AV_PIX_FMT_YUV420P) {
                /* вже те, що треба */
                sdl2_push_new_video_frame(
                    g_frame->data[0],
                    g_frame->data[1],
                    g_frame->data[2],
                    width,
                    height,
                    g_frame->linesize[0],
                    g_frame->linesize[1]
                );
            } else {
                /* конвертація в YUV420P */
                if (ensure_sws(width, height, (enum AVPixelFormat)g_frame->format) == 0) {
                    sws_scale(g_sws_ctx,
                              (const uint8_t * const *)g_frame->data,
                              g_frame->linesize,
                              0,
                              height,
                              g_sws_dst_data,
                              g_sws_dst_linesize);

                    sdl2_push_new_video_frame(
                        g_sws_dst_data[0],
                        g_sws_dst_data[1],
                        g_sws_dst_data[2],
                        width,
                        height,
                        g_sws_dst_linesize[0],
                        g_sws_dst_linesize[1]
                    );
                }
            }

            /* FPS */
            uint64_t now = get_time_ms();
            if (!last_fps_time)
                last_fps_time = now;
            frames_in_sec++;

            if (now - last_fps_time >= 1000) {
                current_fps = frames_in_sec * 1000.0 / (double)(now - last_fps_time);
#if 1
                printf("[DECODER] FPS: %.2f\n", current_fps);
#endif
                frames_in_sec = 0;
                last_fps_time = now;
            }
        }
    }

    av_packet_free(&pkt);
    printf("[DECODER] decoder thread exiting\n");
    return NULL;
}

/* ---------------------- public API -------------------------- */

int decoder_start(struct config_t *cfg)
{
    if (!cfg) {
        printf("[DECODER] cfg is NULL\n");
        return -1;
    }

    enum AVCodecID codec_id;
    if (cfg->codec == CODEC_H264) {
        codec_id = AV_CODEC_ID_H264;
        printf("[DECODER] Using libavcodec H.264 decoder\n");
    } else if (cfg->codec == CODEC_H265 || cfg->codec == CODEC_HEVC) {
        codec_id = AV_CODEC_ID_HEVC;
        printf("[DECODER] Using libavcodec H.265 decoder\n");
    } else {
        printf("[DECODER] Unsupported codec in cfg: %d\n", cfg->codec);
        return -1;
    }

    printf("[DECODER] Initializing libavcodec decoder...\n");

    avcodec_register_all();

    const AVCodec *codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        printf("[DECODER] avcodec_find_decoder failed\n");
        return -1;
    }

    g_dec_ctx = avcodec_alloc_context3(codec);
    if (!g_dec_ctx) {
        printf("[DECODER] avcodec_alloc_context3 failed\n");
        return -1;
    }

#ifdef SLOW_PC_MODE
    g_dec_ctx->thread_count = 4; // limit to 4 threads on slow PCs
    g_dec_ctx->thread_type  = FF_THREAD_SLICE; // use slice threading
#else
    g_dec_ctx->thread_count = 1; // single thread for low-latency
    g_dec_ctx->thread_type  = 0; // no threading
#endif

    // low-latency hints
    g_dec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    g_dec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(g_dec_ctx, codec, NULL) < 0) {
        printf("[DECODER] avcodec_open2 failed\n");
        avcodec_free_context(&g_dec_ctx);
        return -1;
    }

    g_frame = av_frame_alloc();
    if (!g_frame) {
        printf("[DECODER] av_frame_alloc failed\n");
        avcodec_free_context(&g_dec_ctx);
        return -1;
    }

    atomic_store(&g_decoder_running, 1);
    if (pthread_create(&g_decoder_thread, NULL, decoder_thread_func, NULL) != 0) {
        printf("[DECODER] pthread_create failed\n");
        atomic_store(&g_decoder_running, 0);
        av_frame_free(&g_frame);
        avcodec_free_context(&g_dec_ctx);
        return -1;
    }
    
    printf("[DECODER] libavcodec decoder started\n");

    return 0;
}

int decoder_put_frame(struct config_t *cfg, void *data, int size)
{
    (void)cfg;
    if (!data || size <= 0)
        return -1;

    if (!g_dec_ctx || !atomic_load(&g_decoder_running)) {
        return -1;
    }
    
    if (pkt_queue_push(data, size) < 0) {
        printf("[DECODER] pkt_queue_push failed (drop)\n");
        return -1;
    }

    return 0;
}

int decoder_stop(void)
{
    if (!g_dec_ctx) {
        printf("[DECODER] decoder not initialized\n");
        return -1;
    }

    atomic_store(&g_decoder_running, 0);

    pthread_mutex_lock(&g_pkt_mutex);
    pthread_cond_broadcast(&g_pkt_cond);
    pthread_mutex_unlock(&g_pkt_mutex);

    pthread_join(g_decoder_thread, NULL);
    pkt_queue_flush();

    if (g_sws_buf) {
        av_freep(&g_sws_buf);
        g_sws_buf_size = 0;
    }
    g_sws_dst_data[0] = g_sws_dst_data[1] = g_sws_dst_data[2] = NULL;
    g_sws_dst_linesize[0] = g_sws_dst_linesize[1] = g_sws_dst_linesize[2] = 0;

    if (g_sws_ctx) {
        sws_freeContext(g_sws_ctx);
        g_sws_ctx = NULL;
    }

    if (g_frame) {
        av_frame_free(&g_frame);
    }

    if (g_dec_ctx) {
        avcodec_free_context(&g_dec_ctx);
    }

    printf("[DECODER] decoder stopped\n");
    return 0;
}
