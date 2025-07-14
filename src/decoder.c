/**
 * @file decoder.c this is part of project 'vd-link'
 *  Copyright © vitalii.nimych@gmail.com 2025
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
 * Created vitalii.nimych@gmail.com 30-06-2025
 */
#include "decoder.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_mpi_cmd.h>
#include <rockchip/rk_type.h>
#include <rockchip/mpp_rc_api.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_venc_ref.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm_fourcc.h>
#include <linux/dma-buf.h>
#include "src/drm_display.h"

#define DECODER_DEBUG 0

static pthread_t decoder_thread;
static atomic_int decoder_running = 0;

static MppCtx ctx = NULL;
static MppApi *mpi = NULL;
static MppBufferGroup frm_grp = NULL;
static MppCodingType type = MPP_VIDEO_CodingUnused;

struct video_frame_t {
    size_t size;            // Size of the video frame data
    int width;              // Width of the video frame
    int height;             // Height of the video frame
    int hor_stride;         // Horizontal stride (aligned width)
    int ver_stride;         // Vertical stride (aligned height)
    MppFrameFormat fmt;     // Format of the video frame (e.g., YUV420SP)
};

struct video_frame_t video_frame_info = {
    .size = 0,
    .width = 0,
    .height = 0,
    .hor_stride = 0,
    .ver_stride = 0,
    .fmt = MPP_FMT_YUV420SP
};

static inline int align16(int x) { return (x + 15) & ~15; }

uint64_t get_time_ms()
{
    struct timespec spec;
    if (clock_gettime(CLOCK_MONOTONIC, &spec) == -1) {
        abort();
    }
    return spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
}

static int decoder_buff_init_internal(int w, int h, MppFrameFormat format)
{
    if ((format != MPP_FMT_YUV420SP) && (format != MPP_FMT_YUV420SP_10BIT)) {
        printf("[ DECODER ] Unsupported format : %d\n", format);
        return -1;
    }
    video_frame_info.width = w;
    video_frame_info.height = h;
    video_frame_info.fmt = format;

    // Release previous buffer group if exists
    if (frm_grp) {
        mpp_buffer_group_clear(frm_grp);
        mpp_buffer_group_put(frm_grp);
        frm_grp = NULL;
    }

    // 1. Create internal buffer group (ION/Normal type)
    MPP_RET ret = mpp_buffer_group_get_internal(&frm_grp, MPP_BUFFER_TYPE_ION); // or MPP_BUFFER_TYPE_NORMAL
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to get internal buffer group: %d\n", ret);
        return -1;
    }

    // 2. Calculate buffer stride and size (YUV420SP: 3/2)
    video_frame_info.hor_stride = align16(video_frame_info.width);
    video_frame_info.ver_stride = align16(video_frame_info.height);
    size_t frame_buf_size = video_frame_info.hor_stride * video_frame_info.ver_stride * 3 / 2;
    int num_buffers = 4; // adjust for your pipeline

    // 3. Allocate buffers and put them into the group
    for (int i = 0; i < num_buffers; i++) {
        MppBuffer buf = NULL;
        ret = mpp_buffer_get(frm_grp, &buf, frame_buf_size);
        if (ret != MPP_OK) {
            printf("[ DECODER ] Failed to alloc mpp_buffer %d\n", i);
            return -1;
        }
        // Optionally store pointers to buffers if you need direct access
        // Otherwise, the group will manage their lifecycle
    }

    // 4. Attach group to decoder (MPP_DEC_SET_EXT_BUF_GROUP for internal)
    ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, frm_grp);
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to set external buffer group: %d\n", ret);
        return -1;
    }

    // 5. Inform decoder that new buffers are ready (info change ack)
    ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to set info change ready: %d\n", ret);
        return -1;
    }

    printf("[ DECODER ] Internal buffer group initialized for %dx%d (stride %dx%d), %d buffers\n",
           video_frame_info.width, video_frame_info.height, video_frame_info.hor_stride, video_frame_info.ver_stride, num_buffers);

    return 0;
}

static int decoder_buff_init_dma_heap(int w, int h, MppFrameFormat format)
{
    // Check supported formats and calculate buffer size
    video_frame_info.hor_stride = align16(w);
    video_frame_info.ver_stride = align16(h);
    video_frame_info.size = 0;
    int num_buffers = 8;

    switch (format) {
    case MPP_FMT_YUV420SP:
    case MPP_FMT_YUV420P:
        // NV12/YUV420: 1.5 bytes per pixel
        video_frame_info.size = video_frame_info.hor_stride * video_frame_info.ver_stride * 3 / 2;
        break;
    case MPP_FMT_YUV422SP:
    case MPP_FMT_YUV422P:
        // YUV422: 2 bytes per pixel
        video_frame_info.size = video_frame_info.hor_stride * video_frame_info.ver_stride * 2;
        break;
    case MPP_FMT_YUV420SP_10BIT:
        // YUV420 10bit: up to 2 bytes per pixel (depends on packing)
        video_frame_info.size = video_frame_info.hor_stride * video_frame_info.ver_stride * 2;
        break;
    case MPP_FMT_YUV422SP_10BIT:
        // YUV422 10bit: 4 bytes per pixel
        video_frame_info.size = video_frame_info.hor_stride * video_frame_info.ver_stride * 4;
        break;
    default:
        printf("[ DECODER ] Unsupported format: %d\n", format);
        return -1;
    }

    video_frame_info.width = w;
    video_frame_info.height = h;
    video_frame_info.fmt = format;
    video_frame_info.size = video_frame_info.size;

    // Free old buffer group if exists
    if (frm_grp) {
        mpp_buffer_group_clear(frm_grp);
        mpp_buffer_group_put(frm_grp);
        frm_grp = NULL;
    }

    // Create DMA-HEAP internal buffer group for MPP
    MPP_RET ret = mpp_buffer_group_get_internal(&frm_grp, MPP_BUFFER_TYPE_DMA_HEAP);
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to get DMA-HEAP buffer group: %d\n", ret);
        return -1;
    }

    // Allocate frame buffers and add them to the group
    for (int i = 0; i < num_buffers; i++) {
        MppBuffer buf = NULL;
        ret = mpp_buffer_get(frm_grp, &buf, video_frame_info.size);
        if (ret != MPP_OK) {
            printf("[ DECODER ] Failed to allocate mpp_buffer %d\n", i);
            return -1;
        }
        printf("[ DECODER ] Allocated DMA buffer [%d] fd: %d with size: %zu\n", i, mpp_buffer_get_fd(buf), video_frame_info.size);
        // No need to keep MppBuffer pointer here: buffer group owns them
    }

    // Attach buffer group to decoder
    ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, frm_grp);
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to set external buffer group: %d\n", ret);
        return -1;
    }

    // Inform decoder that new buffers are ready
    ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
    if (ret != MPP_OK) {
        printf("[ DECODER ] Failed to set info change ready: %d\n", ret);
        return -1;
    }

    printf("[ DECODER ] DMA-HEAP buffer group initialized for %dx%d (stride %dx%d), %d buffers\n",
           w, h, video_frame_info.hor_stride, video_frame_info.ver_stride, num_buffers);

    return 0;
}

void decoder_release_buffers(void)
{
    if (frm_grp) {
        mpp_buffer_group_clear(frm_grp);
        mpp_buffer_group_put(frm_grp);
        frm_grp = NULL;
    }

    printf("[ DECODER ] Released decoder buffers\n");
}

static void* decoder_thread_func(void* arg)
{
    printf("[ DECODER ] Decoder thread started\n");
    (void)arg;
    int first_frames = 0;

     while (atomic_load(&decoder_running)) {
        MppFrame frame = NULL;
        // Get a decoded frame from MPP (non-blocking)
        MPP_RET ret = mpi->decode_get_frame(ctx, &frame);
        if (ret == MPP_OK && frame) {
            if (mpp_frame_get_info_change(frame)) {
                // Decoder has detected a change in video info (resolution, format, etc.)
                int width = mpp_frame_get_width(frame);
                int height = mpp_frame_get_height(frame);
                MppFrameFormat fmt = mpp_frame_get_fmt(frame);
                printf("[ DECODER ] Info change: %dx%d fmt=%d\n", width, height, fmt);
                // (Re)allocate buffers for new resolution/format
                //decoder_buff_init_internal(width, height, fmt); // If you want to use internal buffers (ION/Normal type)
                // For DMA-HEAP buffers
                decoder_buff_init_dma_heap(width, height, fmt);
                mpp_frame_deinit(&frame);
                first_frames = 0;

            } else if (mpp_frame_get_eos(frame)) {
                // End-of-stream received: stop decoding loop
                printf("[ DECODER ] EOS\n");
                atomic_store(&decoder_running, 0);
                mpp_frame_deinit(&frame);
            } else {
                // Skip first few frames to allow decoder to stabilize buffer pool
                if (first_frames < 6) {
                    first_frames++;
                    usleep(50000);
                    mpp_frame_deinit(&frame);
                    continue;
                }
                // Frame is ready for rendering, DRM, or further processing
                int width = (int)mpp_frame_get_width(frame);
                int height = (int)mpp_frame_get_height(frame);
                int ver_stride = (int)mpp_frame_get_ver_stride(frame);
                int hor_stride = (int)mpp_frame_get_hor_stride(frame);
                int dma_fd = mpp_buffer_get_fd(mpp_frame_get_buffer(frame));
                struct dma_buf_sync sync;
                sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
                ioctl(dma_fd, DMA_BUF_IOCTL_SYNC, &sync);
#if DECODER_DEBUG
                printf("[ DECODER ] Frame ready: %dx%d, stride(%dx%d) dma_fd=%d\n", width, height, hor_stride, ver_stride, dma_fd);
#endif
                drm_push_new_video_frame(dma_fd, width, height, hor_stride, ver_stride);
                mpp_frame_deinit(&frame);

                // FPS calculation block
                static uint64_t last_fps_time = 0;
                static int frames_in_sec = 0;
                static double current_fps = 0.0;

                uint64_t now = get_time_ms();
                if (!last_fps_time) last_fps_time = now;
                frames_in_sec++;

                // Check if 1 second has passed
                if (now - last_fps_time >= 1000) {
                    current_fps = frames_in_sec * 1000.0 / (now - last_fps_time);
#if DECODER_DEBUG
                    printf("[ DECODER ] FPS: %.2f\n", current_fps);
#endif
                    frames_in_sec = 0;
                    last_fps_time = now;
                }
            }
        } else {
            // No frame available, sleep briefly to avoid busy loop
            usleep(1000);
        }
    }

    decoder_release_buffers();

    printf("[ DECODER ] Decoder thread exiting\n");

    return NULL;
}

int decoder_start(struct config_t *cfg)
{

    if (cfg == NULL) {
        printf("[ DECODER ] Configuration is NULL\n");
        return -1;
    }

    if (cfg->codec == CODEC_H265) {
        printf("[ DECODER ] Using H.265 codec\n");
        type = MPP_VIDEO_CodingHEVC;
    } else if (cfg->codec == CODEC_H264) {
        printf("[ DECODER ] Using H.264 codec\n");
        type = MPP_VIDEO_CodingAVC;
    } else {
        printf("[ DECODER ] Unsupported codec: %d\n", cfg->codec);
        return -1;
    }

    MPP_RET ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        printf("[ DECODER ] mpp_create failed: %d\n", ret);
        return -1;
    }

    ret = mpp_init(ctx, MPP_CTX_DEC, type);
    if (ret != MPP_OK) {
        printf("[ DECODER ] mpp_init failed: %d\n", ret);
        mpp_destroy(ctx);
        return -1;
    }

    MppDecCfg mpp_cfg = NULL;
    mpp_dec_cfg_init(&mpp_cfg);
    ret = mpi->control(ctx, MPP_DEC_GET_CFG, mpp_cfg);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_GET_CFG failed: %d\n", ret);
        mpp_destroy(ctx);
        return -1;
    }

    RK_U32 need_split = 1;
    ret = mpp_dec_cfg_set_u32(mpp_cfg, "base:split_parse", need_split);
    ret |= mpp_dec_cfg_set_u32(mpp_cfg, "base:fast_parse", 1);
    if (ret) {
        printf("[ DECODER ] mpp_dec_cfg_set_u32 failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    int mpp_split_mode = 1; // Enable split mode
    ret = mpi->control(ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &mpp_split_mode);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_SET_PARSER_SPLIT_MODE failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    int disable_error = 1; // Disable error handling
    ret = mpi->control(ctx, MPP_DEC_SET_DISABLE_ERROR, &disable_error);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_SET_DISABLE_ERROR failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    int immediate_out = 1; // Enable immediate output
    ret = mpi->control(ctx, MPP_DEC_SET_IMMEDIATE_OUT, &immediate_out);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_SET_IMMEDIATE_OUT failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    int fast_play = 1; // Enable fast play mode
    ret = mpi->control(ctx, MPP_DEC_SET_ENABLE_FAST_PLAY, &fast_play);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_SET_ENABLE_FAST_PLAY failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    int fast_mode = 1;
    ret = mpi->control(ctx, MPP_DEC_SET_PARSER_FAST_MODE, &fast_mode);
    if (ret) {
        printf("[ DECODER ] MPP_DEC_SET_PARSER_FAST_MODE failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    /*
     * timeout setup, refer to  MPP_TIMEOUT_XXX
     * zero     - non block
     * negative - block with no timeout
     * positive - timeout in milisecond
     */
    RK_S64 block = 10; // 10 ms timeout for input/output operations for catch signals
    ret = mpi->control(ctx, MPP_SET_INPUT_TIMEOUT, &block);
    if (ret) {
        printf("[ DECODER ] MPP_SET_INPUT_TIMEOUT failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    ret = mpi->control(ctx, MPP_SET_OUTPUT_TIMEOUT, &block);
    if (ret) {
        printf("[ DECODER ] MPP_SET_OUTPUT_BLOCK failed: %d\n", ret);
        mpp_dec_cfg_deinit(mpp_cfg);
        mpp_destroy(ctx);
        return -1;
    }

    printf("[ DECODER ] Decoder initialized with all parameters: split: %d"
           " disable_error: %d"
           " immediate_out: %d"
           " fast_play: %d"
           " fast_mode: %d\n",
           mpp_split_mode, disable_error, immediate_out, fast_play, fast_mode);

    atomic_store(&decoder_running, 1);
    if (pthread_create(&decoder_thread, NULL, decoder_thread_func, NULL)) {
        printf("[ DECODER ] Can't create decode thread\n");
        atomic_store(&decoder_running, 0);
        mpp_destroy(ctx);
        ctx = NULL;
        mpi = NULL;
        return -1;
    }

    return 0;
}

int decoder_put_frame(struct config_t *cfg, void *data, int size)
{
    static int decoder_stalled_count=0;

    (void)cfg; // Unused parameter, can be removed if not needed
    if (ctx == NULL || mpi == NULL) {
        printf("[ DECODER ] Decoder not initialized\n");
        return -1;
    }

    MppPacket packet;
    MPP_RET ret = mpp_packet_init(&packet, data, size);
    if (ret != MPP_OK) {
        printf("[ DECODER ] mpp_packet_init failed: %d\n", ret);
        return -1;
    }

    mpp_packet_set_data(packet, data);
    mpp_packet_set_size(packet, size);
    mpp_packet_set_pos(packet, data);
    mpp_packet_set_length(packet, size);
    mpp_packet_set_pts(packet,(RK_S64) get_time_ms());

    uint64_t data_feed_begin = get_time_ms();
    while (MPP_OK != (ret = mpi->decode_put_packet(ctx, packet))) {
        printf("[ DECODER ] decode_put_packet returned %d, retrying...\n", ret);
        uint64_t elapsed = get_time_ms() - data_feed_begin;
        if (elapsed > 100) {
            decoder_stalled_count++;
            printf("[ DRM ] Cannot feed decoder, stalled %d \n?", decoder_stalled_count);
            mpp_packet_deinit(&packet);
            return -1;
        }
        usleep(1000);
    }

    mpp_packet_deinit(&packet);

    return 0;
}

int decoder_stop(void)
{
    if (ctx == NULL || mpi == NULL) {
        printf("[ DECODER ] Decoder not initialized or already stopped\n");
        return -1;
    }
    atomic_store(&decoder_running, 0);
    pthread_join(decoder_thread, NULL);
    mpp_destroy(ctx);
    mpp_destroy(mpi);
    type = MPP_VIDEO_CodingUnused;

    printf("[ DECODER ] decoder stopped\n");
    return 0;
}
