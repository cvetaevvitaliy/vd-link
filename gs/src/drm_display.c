/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "drm_display.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <pthread.h>
#include <rockchip/rk_mpi.h>
#include <sys/ioctl.h>
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/rga.h>
#include <linux/dma-heap.h>
#include <dirent.h>
#include <math.h>
#include <stdatomic.h>
#include <linux/dma-buf.h>

#define DRM_DEBUG 0
#define DRM_DEBUG_ROTATE 0

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

static struct drm_context_t drm_context = {
        .display_info = {0},
        .connector = NULL,
        .drm_fd = -1,
        .nv12_plane_id = -1,
        .argb888_plane_id = -1,
        .osd_plane_props = {0},
        .video_plane_props = {0},
};

struct drm_fb_cleanup {
    int drm_fd;
    struct drm_context_t *ctx;
    uint32_t fb_osd_id;
    uint32_t fb_video_id;
    int rotate_idx;
};

static struct drm_fb_cleanup cleanup;

static drmEventContext evctx = {
    .version = DRM_EVENT_CONTEXT_VERSION,
    .vblank_handler = NULL,
    .page_flip_handler = NULL, // used drm_page_flip_handler
    .page_flip_handler2 = NULL,
    .sequence_handler = NULL
};

//static pthread_mutex_t drm_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int running = 0;
static pthread_t drm_thread;
static atomic_int pending_commit = 0;

#define OSD_BUF_COUNT 2
#define MAX_VIDEO_BUFS 16
#define ROTATE_BUF_COUNT (MAX_VIDEO_BUFS)

#define OSD_WIDTH  1280
#define OSD_HEIGHT  720

struct drm_fb_t osd_bufs[OSD_BUF_COUNT];

struct {
    int dirty[OSD_BUF_COUNT];
    int osd_width;
    int osd_height;
    int cur;
    int next;
} osd_db = { {0,0}, 0, 1 };

struct {
    int dma_fd[MAX_VIDEO_BUFS];
    uint32_t fb_id[MAX_VIDEO_BUFS];
    int video_width;
    int video_height;
    int dirty[MAX_VIDEO_BUFS];
    int count;
    int cur;
} video_buf_map = { .count = 0, .cur = 0 };

struct rotate_video_pool_t {
    int w, h, hor_stride, ver_stride;
    int dma_fd[ROTATE_BUF_COUNT];
    uint32_t fb_id[ROTATE_BUF_COUNT];
    int count;
} rotate_video_pool = {0};


static int drm_get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *name);
static int64_t drm_get_prop_value(int fd, uint32_t obj_id, uint32_t obj_type, const char *name);
static void* compositor_thread(void* arg);
static void drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);
static void drm_init_event_context(void);
static int rga_nv12_rotate(int src_fd, int dst_fd, int src_width, int src_height, int wstride, int hstride, int rotation);
static int rga_argb8888_rotate(int src_fd, int dst_fd, int src_width, int src_height, int rotation);
static int drm_create_osd_buff_pool(struct drm_context_t *ctx);

static void drm_print_modes(struct drm_context_t *drm_ctx)
{
    drmModeRes *res;
    drmModeConnector *conn;
    drmModeModeInfo info;
    uint prev_h, prev_v, prev_refresh = 0;
    int at_least_one = 0;

    res = drmModeGetResources(drm_ctx->drm_fd);
    if (!res) {
        fprintf(stderr, "[ DRM ] cannot retrieve DRM resources (%d): %m\n",
                errno);
        return;
    }

    for (int i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(drm_ctx->drm_fd, res->connectors[i]);
        if (!conn) {
            fprintf(stderr, "[ DRM ] cannot retrieve DRM connector %u:%u (%d): %m\n",
                    i, res->connectors[i], errno);
            continue;
        }
        for (int i = 0; i < conn->count_modes; i++) {
            info = conn->modes[i];
            // Assuming modes list is sorted
            if (info.hdisplay == prev_h && info.vdisplay == prev_v && info.vrefresh == prev_refresh)
                continue;
            printf("[ DRM ] Found display: %dx%d@%d\n", info.hdisplay, info.vdisplay, info.vrefresh);
            prev_h = info.hdisplay;
            prev_v = info.vdisplay;
            prev_refresh = info.vrefresh;
            at_least_one = 1;
        }
        drmModeFreeConnector(conn);
    }
    if (!at_least_one) {
        fprintf(stderr, "[ DRM ] No displays found\n");
    }
    drmModeFreeResources(res);
}

static int drm_find_crct(struct drm_context_t *drm_ctx, drmModeRes *res)
{
    for (unsigned int i = 0; i < res->count_crtcs; ++i) {
        drmModeCrtc *crtc = drmModeGetCrtc(drm_ctx->drm_fd, res->crtcs[i]);
        if (!crtc) {
            fprintf(stderr, "[ DRM ] cannot retrieve CRTC %u (%d): %m\n", res->crtcs[i], errno);
            continue;
        }
        if (crtc->mode_valid && crtc->mode.hdisplay == drm_ctx->connector->modes[0].hdisplay && crtc->mode.vdisplay ==  drm_ctx->connector->modes[0].vdisplay) {
            drm_ctx->crtc = crtc;
            return res->crtcs[i];
        }
        drmModeFreeCrtc(crtc);
    }

    return -1;
}

static int drm_find_plane(struct drm_context_t *drm_ctx, uint32_t plane_format)
{
    drmModePlaneResPtr plane_res;
    bool found_plane = false;
    int ret = -1;

    printf("[ DRM ] Searching for plane with format %s\n", drmGetFormatName(plane_format));

    plane_res = drmModeGetPlaneResources(drm_ctx->drm_fd);
    if (!plane_res) {
        fprintf(stderr, "[ DRM ] drmModeGetPlaneResources failed: %s\n", strerror(errno));
        return -1;
    }

    for (int i = 0; (i < plane_res->count_planes) && !found_plane; i++) {
        int plane_id = (int)plane_res->planes[i];

        drmModePlanePtr plane = drmModeGetPlane(drm_ctx->drm_fd, plane_id);
        if (!plane) {
            fprintf(stderr, "[ DRM ] drmModeGetPlane(%u) failed: %s\n", plane_id, strerror(errno));
            continue;
        }

        if (plane->possible_crtcs & (1 << 0)) { // Assuming we are looking for the first CRTC
            for (int j = 0; j < plane->count_formats; j++) {
                //printf("[ DRM ] Checking plane %d format %s\n", plane_id, drmGetFormatName(plane->formats[j]));
                if (plane->formats[j] == plane_format) {
                    found_plane = true;
                    printf("[ DRM ] Found plane %d with format %s\n", plane_id, drmGetFormatName(plane_format));
                    ret = plane_id;
                    drmModeFreePlaneResources(plane_res);
                    return ret;
                }
            }
        }

        drmModeFreePlane(plane);
    }

    printf("[ DRM ] No suitable plane found for format %s\n", drmGetFormatName(plane_format));

    drmModeFreePlaneResources(plane_res);

    return ret;
}

static int drm_modeset(struct drm_context_t *drm_ctx)
{
    drmModeRes *res;
    unsigned int i;
    int ret;
    drm_ctx->connector = NULL;

    res = drmModeGetResources(drm_ctx->drm_fd);
    if (!res) {
        fprintf(stderr, "[ DRM ] cannot retrieve DRM resources (%d): %m\n", errno);
        return -errno;
    }

    for (i = 0; i < res->count_connectors; ++i) {
        drm_ctx->connector = drmModeGetConnector(drm_ctx->drm_fd, res->connectors[i]);
        if (!drm_ctx->connector) {
            fprintf(stderr, "[ DRM ] cannot retrieve DRM connector %u:%u (%d): %m\n", i, res->connectors[i], errno);
            continue;
        }

        if (drm_ctx->connector->connection == DRM_MODE_CONNECTED && drm_ctx->connector->count_modes > 0) {
            printf("[ DRM ] Using connector %d with mode %dx%d@%d clock %d\n",
                   drm_ctx->connector->connector_id,
                   drm_ctx->connector->modes[0].hdisplay,
                   drm_ctx->connector->modes[0].vdisplay,
                   drm_ctx->connector->modes[0].vrefresh,
                   drm_ctx->connector->modes[0].clock);

            drm_ctx->display_info.hdisplay = drm_ctx->connector->modes[0].hdisplay;
            drm_ctx->display_info.vdisplay = drm_ctx->connector->modes[0].vdisplay;
            drm_ctx->display_info.vrefresh = drm_ctx->connector->modes[0].vrefresh;
            drm_ctx->display_info.clock = drm_ctx->connector->modes[0].clock;
            drm_ctx->display_info.hsync_start = drm_ctx->connector->modes[0].hsync_start;
            drm_ctx->display_info.hsync_end = drm_ctx->connector->modes[0].hsync_end;
            drm_ctx->display_info.htotal = drm_ctx->connector->modes[0].htotal;
            drm_ctx->display_info.hskew = drm_ctx->connector->modes[0].hskew;
            drm_ctx->display_info.vsync_start = drm_ctx->connector->modes[0].vsync_start;
            drm_ctx->display_info.vsync_end = drm_ctx->connector->modes[0].vsync_end;
            drm_ctx->display_info.vtotal = drm_ctx->connector->modes[0].vtotal;
            drm_ctx->display_info.vscan = drm_ctx->connector->modes[0].vscan;
            drm_ctx->display_info.flags = drm_ctx->connector->modes[0].flags;
            drm_ctx->display_info.type = drm_ctx->connector->modes[0].type;

            drm_find_crct(drm_ctx, res);

            break;
        }
        drmModeFreeConnector(drm_ctx->connector);
    }

    if (!drm_ctx->connector) {
        drmModeFreeResources(res);
        fprintf(stderr, "[ DRM ] No connected connector found!\n");
        return -ENODEV;
    }

    drm_ctx->crtc = drmModeGetCrtc(drm_ctx->drm_fd, res->crtcs[0]);
    if (!drm_ctx->crtc) {
        fprintf(stderr, "[ DRM ] Failed to get first available CRTC (id=%u)\n", res->crtcs[0]);
        drmModeFreeResources(res);
        return -ENODEV;
    }
    printf("[ DRM ] Using CRTC %d for connector %d\n", drm_ctx->crtc->crtc_id, drm_ctx->connector->connector_id);

    drmModeFreeResources(res);

    return 0;
}

static int drm_create_dumb_argb8888_fb(struct drm_context_t *ctx, int width, int height, struct drm_fb_t *fb)
{
    struct drm_mode_create_dumb creq = { .width = width, .height = height, .bpp = 32 };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_CREATE_DUMB");
        return -1;
    }
    struct drm_mode_fb_cmd2 cmd = {
            .width = width, .height = height, .pixel_format = DRM_FORMAT_ARGB8888,
            .handles = {creq.handle},
            .pitches = {creq.pitch},
            .offsets = {0}
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &cmd) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_ADDFB2 (ARGB8888)");
        return -1;
    }
    fb->fb_id = cmd.fb_id;
    fb->handles[0] = creq.handle;
    fb->pitches[0] = creq.pitch;
    fb->offsets[0] = 0;
    fb->size = creq.size;
    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_MAP_DUMB");
        return -1;
    }
    fb->buff_addr = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->drm_fd, mreq.offset);
    if (fb->buff_addr == MAP_FAILED) {
        perror("[ DRM ] mmap ARGB8888 dumb");
        return -1;
    }
    return 0;
}

static int drm_create_dumb_nv12_fb(struct drm_context_t *ctx, int width, int height, struct drm_fb_t *fb)
{
    struct drm_mode_create_dumb creq = { .width = width, .height = height * 3 / 2, .bpp = 8 };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_CREATE_DUMB (NV12)");
        return -1;
    }
    struct drm_mode_fb_cmd2 cmd = {
            .width = width, .height = height, .pixel_format = DRM_FORMAT_NV12,
            .handles = {creq.handle, creq.handle},
            .pitches = {width, width},
            .offsets = {0, width*height}
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &cmd) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_ADDFB2 (NV12)");
        return -1;
    }
    fb->fb_id = cmd.fb_id;
    fb->handles[0] = creq.handle;
    fb->pitches[0] = width;
    fb->pitches[1] = width;
    fb->offsets[0] = 0;
    fb->offsets[1] = width * height;
    fb->size = creq.size;
    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_MAP_DUMB (NV12)");
        return -1;
    }
    fb->buff_addr = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->drm_fd, mreq.offset);
    if (fb->buff_addr == MAP_FAILED) {
        perror("[ DRM ] mmap NV12 dumb");
        return -1;
    }
    return 0;
}

static int drm_get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *name)
{
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    int prop_id = -1;
    if (!props) return -1;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        //printf("plane %d property: %s\n", obj_id, p->name);
        if (p && strcmp(p->name, name) == 0) { prop_id = p->prop_id; drmModeFreeProperty(p); break; }
        if (p) drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return prop_id;
}

static int64_t drm_get_prop_value(int fd, uint32_t obj_id, uint32_t obj_type, const char *name)
{
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    int64_t value = 0;
    if (!props) return 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (p && strcmp(p->name, name) == 0) {
            value = (int64_t)props->prop_values[i];
            drmModeFreeProperty(p);
            break;
        }
        if (p) drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return value;
}

static void drm_fill_plane_props(int drm_fd, uint32_t plane_id, struct drm_plane_props *props, uint32_t connector_id, uint32_t crtc_id, drmModeModeInfo *mode)
{
    props->connector_crtc_id = drm_get_prop_id(drm_fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

    props->mode_id = drm_get_prop_id(drm_fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    props->active  = drm_get_prop_id(drm_fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");

    if (mode && props->mode_id > 0) {
        uint32_t blob_id = 0;
        if (drmModeCreatePropertyBlob(drm_fd, mode, sizeof(*mode), &blob_id) == 0)
            props->mode_blob_id = blob_id;
        else
            props->mode_blob_id = 0;
    } else {
        props->mode_blob_id = 0;
    }

    props->fb_id    = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    props->crtc_id  = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    props->src_x    = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    props->src_y    = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    props->src_w    = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    props->src_h    = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    props->crtc_x   = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    props->crtc_y   = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    props->crtc_w   = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    props->crtc_h   = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    props->zpos     = drm_get_prop_id(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "zpos");
    props->zpos_value = drm_get_prop_value(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE, "zpos");
}

static int drm_prepare_nv12_fb(struct drm_context_t *ctx, int dma_fd, int width, int height, int hor_stride, int ver_stride)
{
    printf("[ DRM ] Preparing NV12 framebuffer with DMA-FD %d, size: %dx%d, stride: %dx%d\n",
           dma_fd, width, height, hor_stride, ver_stride);
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    // Import DMA-BUF as DRM buffer handle
    struct drm_prime_handle prime = {
            .fd = dma_fd,
            .flags = 0,
            .handle = 0,
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime) < 0) {
        perror("[ DRM ] DRM_IOCTL_PRIME_FD_TO_HANDLE");
        return -1;
    }

    // Calculate aligned offsets
    uint32_t y_stride = hor_stride;
    uint32_t uv_stride = hor_stride;
    uint32_t y_size = y_stride * ver_stride;
    uint32_t uv_offset = ALIGN(y_size, 16); // Required alignment for UV on RK3566

    // Setup fb2 structure for NV12 (single buffer with 2 planes)
    struct drm_mode_fb_cmd2 fb2 = {
            .width  = width,
            .height = height,
            .pixel_format = DRM_FORMAT_NV12,
            .handles = { prime.handle, prime.handle },
            .pitches = { y_stride, uv_stride },
            .offsets = { 0, uv_offset },
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_ADDFB2");
        fprintf(stderr, "[ DRM ] Failed to add FB2: fd=%d, handle=%u, pitch=%d, offset=%d\n",
                dma_fd, prime.handle, hor_stride, hor_stride * ver_stride);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);
    long usec = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000;
    printf("[ DRM ] Created framebuffer: fb_id=%u took time: %ld us\n", fb2.fb_id, usec);
    return fb2.fb_id;
}

static int drm_prepare_argb8888_fb(struct drm_context_t *ctx, int dma_fd, int width, int height)
{
    struct drm_prime_handle prime = {
        .fd = dma_fd,
        .flags = 0,
        .handle = 0,
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime) < 0) {
        perror("[ DRM ] DRM_IOCTL_PRIME_FD_TO_HANDLE (ARGB8888)");
        return -1;
    }

    struct drm_mode_fb_cmd2 fb2 = {
        .width  = width,
        .height = height,
        .pixel_format = DRM_FORMAT_ARGB8888,
        .handles = { prime.handle },
        .pitches = { width * 4 },
        .offsets = { 0 },
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_ADDFB2 (ARGB8888)");
        return -1;
    }
    return fb2.fb_id;
}

int drm_atomic_commit_all_buffers(struct drm_context_t *ctx, struct drm_fb_t *osd_fb, int osd_width, int osd_height,
                                  uint32_t video_fb_id, int dma_fd, int video_width, int video_height)
{
    if (ctx->argb888_plane_id < 0 && ctx->nv12_plane_id < 0) {
        fprintf(stderr, "[ DRM ] No planes available for atomic commit!\n");
        return -1;
    }

#if DRM_DEBUG
    printf("[ DRM ] drm_fd=%d Committing video plane %d with FB %d, dma_fd %d size %dx%d, osd plane %d with FB %d, size %dx%d,\n", ctx->drm_fd, ctx->nv12_plane_id, video_fb_id, dma_fd,
           video_width, video_height, ctx->argb888_plane_id, osd_fb->fb_id, osd_width, osd_height);
#endif

    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) {
        fprintf(stderr, "[ DRM ] Failed to allocate atomic request\n");
        return -1;
    }

    struct drm_plane_props *video_plane_props = &ctx->video_plane_props;
    struct drm_plane_props *osd_plane_props = &ctx->osd_plane_props;

    if (!ctx || !ctx->connector || !ctx->crtc || !video_plane_props) {
        fprintf(stderr, "NULL pointer in DRM context!\n");
        drmModeAtomicFree(req);
        return -1;
    }

    // Calculate the output area with aspect ratio preservation (letterbox/pillarbox)
    uint32_t crtc_video_w = ctx->display_info.hdisplay;
    uint32_t crtc_video_h = ctx->display_info.vdisplay;
    float video_ratio = (float)video_width / (float)video_height;

    // Choose the largest area that fits while preserving aspect ratio
    if ((float)crtc_video_w / video_ratio > (float)crtc_video_h) {
        // Black bars on the sides (pillarbox)
        crtc_video_w = crtc_video_h * video_ratio;
        // crtc_video_h remains unchanged
    } else {
        // Black bars on top/bottom (letterbox)
        // crtc_video_w remains unchanged
        crtc_video_h = crtc_video_w / video_ratio;
    }
    // Center the video on the screen
    int crtc_video_x = (int)(ctx->display_info.hdisplay - crtc_video_w) / 2;
    int crtc_video_y = (int)(ctx->display_info.vdisplay - crtc_video_h) / 2;

    // Calculate aspect-ratio-preserving scaling and centering for OSD
    uint32_t screen_w = ctx->display_info.hdisplay;
    uint32_t screen_h = ctx->display_info.vdisplay;
    float ar_src = (float)osd_width / osd_height;
    float ar_dst = (float)screen_w / screen_h;
    uint32_t crtc_osd_w, crtc_osd_h, crtc_osd_x, crtc_osd_y;

    if (ar_dst > ar_src) {
        // Display is wider than image: pad left/right
        crtc_osd_h = screen_h;
        crtc_osd_w = (int)(screen_h * ar_src);
        crtc_osd_x = (screen_w - crtc_osd_w) / 2;
        crtc_osd_y = 0;
    } else {
        // Display is taller than image: pad top/bottom
        crtc_osd_w = screen_w;
        crtc_osd_h = (int)(screen_w / ar_src);
        crtc_osd_x = 0;
        crtc_osd_y = (screen_h - crtc_osd_h) / 2;
    }

    // Set connector property CRTC_ID if present
    if (video_plane_props->connector_crtc_id > 0) {
        drmModeAtomicAddProperty(req, ctx->connector->connector_id, video_plane_props->connector_crtc_id,ctx->crtc->crtc_id);
        drmModeAtomicAddProperty(req, ctx->connector->connector_id, osd_plane_props->connector_crtc_id,ctx->crtc->crtc_id);
    }

    // Set CRTC properties if present (MODE_ID and ACTIVE)
    if (video_plane_props->mode_id > 0) {
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, video_plane_props->mode_id, video_plane_props->mode_blob_id);
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, osd_plane_props->mode_id, osd_plane_props->mode_blob_id);
    }
    if (video_plane_props->active > 0) {
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, video_plane_props->active, 1);
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, osd_plane_props->active, 1);
    }

    // Set video plane & OSD plane properties
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->crtc_id, ctx->crtc->crtc_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->crtc_id, ctx->crtc->crtc_id);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->fb_id,   video_fb_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->fb_id,   osd_fb->fb_id);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->src_x,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->src_x,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->src_y,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->src_y,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->src_w,   video_width << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->src_w,   osd_width << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->src_h,   video_height << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->src_h,   osd_height << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->crtc_x, crtc_video_x);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->crtc_x, crtc_osd_x);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->crtc_y, crtc_video_y);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->crtc_y, crtc_osd_y);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->crtc_w, crtc_video_w);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->crtc_w, crtc_osd_w);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->crtc_h, crtc_video_h);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->crtc_h, crtc_osd_h);

    // Set Z-position if property is available
    if (video_plane_props->zpos > 0) {
        drmModeAtomicAddProperty(req, ctx->nv12_plane_id, video_plane_props->zpos, 0);
        drmModeAtomicAddProperty(req, ctx->argb888_plane_id, osd_plane_props->zpos, 1);
    }


    cleanup.drm_fd = ctx->drm_fd;
    cleanup.fb_video_id = video_fb_id;
    cleanup.fb_osd_id = osd_fb->fb_id;
    cleanup.ctx = ctx;

    if (drmModeAtomicCommit(ctx->drm_fd, req, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &cleanup) < 0) {
        fprintf(stderr, "[ DRM ] Atomic commit failed for all planes %s\n", strerror(errno));
        drmModeAtomicFree(req);
        atomic_store(&pending_commit, 1);
        return -1;
    }

#if DRM_DEBUG
    printf("[ DRM ] Atomic commit completed: video plane %d with FB %d, size %dx%d, osd plane %d with FB %d, size %dx%d\n",
           ctx->nv12_plane_id, video_fb_id, video_width, video_height,
           ctx->argb888_plane_id, osd_fb->fb_id, osd_width, osd_height);
#endif

    drmModeAtomicFree(req);

    return 0;

}

static void fill_rainbow_argb8888(struct drm_fb_t *fb, int width, int height)
{
    static const uint32_t rainbow[] = {
            0xFFFF0000, // Red
            0xFFFF7F00, // Orange
            0xFFFFFF00, // Yellow
            0xFF00FF00, // Green
            0xFF00FFFF, // Cyan
            0xFF0000FF, // Blue
            0xFF8B00FF  // Violet
    };
    int n = sizeof(rainbow) / sizeof(rainbow[0]);
    uint32_t *p = (uint32_t *)fb->buff_addr;
    int stride = fb->pitches[0] / 4;

    int rainbow_w = width / 2;
    int rainbow_h = height / 2;
    int x0 = (width - rainbow_w) / 2;
    int y0 = (height - rainbow_h) / 2;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x >= x0 && x < x0 + rainbow_w && y >= y0 && y < y0 + rainbow_h) {
                int rel_y = y - y0;
                int band = (rel_y * n) / rainbow_h;
                uint32_t c1 = rainbow[band];
                uint32_t c2 = rainbow[(band + 1 < n) ? band + 1 : band];
                float t = ((float)(rel_y * n) / rainbow_h) - band;
                uint8_t r = ((1.0f - t) * ((c1 >> 16) & 0xFF) + t * ((c2 >> 16) & 0xFF));
                uint8_t g = ((1.0f - t) * ((c1 >> 8) & 0xFF) + t * ((c2 >> 8) & 0xFF));
                uint8_t b = ((1.0f - t) * ((c1 >> 0) & 0xFF) + t * ((c2 >> 0) & 0xFF));
                uint32_t color = 0x1F000000 | (r << 16) | (g << 8) | b;
                p[y * stride + x] = color;
            } else {
                p[y * stride + x] = 0x00000000; // Transparent
            }
        }
    }
}

static void fill_rainbow_checker_nv12(uint8_t *buf, int width, int height)
{
    static const struct { uint8_t y, u, v; } rainbow[] = {
            {  76,  84, 255}, // Red
            {179,  43, 226}, // Orange
            {226,   0, 149}, // Yellow
            {149,  43,  21}, // Green
            { 91, 170,  34}, // Cyan
            { 29, 255, 107}, // Blue
            {105, 212, 234}  // Violet
    };
    int n = sizeof(rainbow)/sizeof(rainbow[0]);

    int check_size = 64; // px, розмір клітинки (адаптуй)
    int band_height = height / n;

    uint8_t *y_plane = buf;
    uint8_t *uv_plane = buf + width * height;

    // --- Y plane (checkerboard) ---
    for (int y = 0; y < height; ++y) {
        int band = y / band_height;
        if (band >= n) band = n-1;
        uint8_t y0 = rainbow[band].y;
        uint8_t y1 = 220; // світлі шахи
        for (int x = 0; x < width; ++x) {
            int ch = ((x / check_size) ^ (y / check_size)) & 1;
            y_plane[y * width + x] = ch ? y0 : y1;
        }
    }

    // --- UV plane (colorful checker) ---
    for (int y = 0; y < height/2; ++y) {
        int y_real = y * 2;
        int band = y_real / band_height;
        if (band >= n) band = n-1;
        uint8_t u0 = rainbow[band].u;
        uint8_t v0 = rainbow[band].v;
        uint8_t u1 = 128, v1 = 128; // сірий (без кольору)
        for (int x = 0; x < width; x += 2) {
            int ch = ((x / check_size) ^ (y_real / check_size)) & 1;
            uv_plane[y * width + x]     = ch ? u0 : u1; // U
            uv_plane[y * width + x + 1] = ch ? v0 : v1; // V
        }
    }
}

static int alloc_dmabuf_fd(size_t size)
{
    int heap_fd = open("/dev/dma_heap/system", O_RDWR);
    if (heap_fd < 0) {
        perror("open /dev/dma_heap/system");
        return -1;
    }

    struct dma_heap_allocation_data alloc = {
            .len = size,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0,
    };

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
        perror("DMA_HEAP_IOCTL_ALLOC");
        close(heap_fd);
        return -1;
    }

    close(heap_fd);
    return alloc.fd;
}

static int alloc_nv12_dmabuf_from_ram(void *nv12_ptr, int width, int height)
{
    size_t size = width * height * 3 / 2;
    int heap_fd = -1, dma_fd = -1;
    void *dst = NULL;

    heap_fd = open("/dev/dma_heap/system", O_RDWR);
    if (heap_fd < 0) {
        perror("open /dev/dma_heap/system");
        return -1;
    }

    struct dma_heap_allocation_data alloc = {
            .len = size,
            .fd_flags = O_RDWR | O_CLOEXEC,
            .heap_flags = 0,
    };
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
        perror("DMA_HEAP_IOCTL_ALLOC");
        close(heap_fd);
        return -1;
    }
    close(heap_fd);

    dma_fd = alloc.fd;

    dst = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
    if (dst == MAP_FAILED) {
        perror("mmap dma-buf");
        close(dma_fd);
        return -1;
    }

    memcpy(dst, nv12_ptr, size);

    munmap(dst, size);

    return dma_fd;
}

static int find_rotation_in_dt(const char *base)
{
    DIR *dir = opendir(base);
    if (!dir) return -1;

    struct dirent *entry;
    char path[PATH_MAX];
    int rotation = -1;

    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;

        if (snprintf(path, sizeof(path), "%s/%s/rotation", base, entry->d_name) >= (int)sizeof(path))
            continue;

        FILE *f = fopen(path, "rb");
        if (f) {
            unsigned char buf[4];
            size_t n = fread(buf, 1, 4, f);
            fclose(f);
            if (n == 4) {
                rotation = (buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | buf[3];
                break;
            }
        }

        if (snprintf(path, sizeof(path), "%s/%s", base, entry->d_name) >= (int)sizeof(path))
            continue;

        DIR *sub = opendir(path);
        if (sub) {
            closedir(sub);
            rotation = find_rotation_in_dt(path);
            if (rotation != -1) break;
        }
    }
    closedir(dir);
    return rotation;
}

static int drm_activate_crtc(struct drm_context_t *ctx)
{
    if (!ctx || !ctx->connector || !ctx->crtc) {
        fprintf(stderr, "[ DRM ] Invalid DRM context for activation!\n");
        return -EINVAL;
    }

    int active_prop_id = drm_get_prop_id(ctx->drm_fd, ctx->crtc->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    int mode_id_prop_id = drm_get_prop_id(ctx->drm_fd, ctx->crtc->crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    int crtc_id_prop_id = drm_get_prop_id(ctx->drm_fd, ctx->connector->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    if (active_prop_id < 0 || mode_id_prop_id < 0 || crtc_id_prop_id < 0) {
        fprintf(stderr, "[ DRM ] Cannot find CRTC/connector properties for activation!\n");
        return -1;
    }

    // Check current ACTIVE value
    int64_t active = drm_get_prop_value(ctx->drm_fd, ctx->crtc->crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    if (active == 1) {
        printf("[ DRM ] CRTC already active, no activation needed.\n");
        return 0;
    }

    // Create mode property blob
    uint32_t mode_blob_id = 0;
    int ret = drmModeCreatePropertyBlob(ctx->drm_fd, &ctx->connector->modes[0], sizeof(drmModeModeInfo), &mode_blob_id);
    if (ret != 0) {
        fprintf(stderr, "[ DRM ] Failed to create MODE_ID blob for activation!\n");
        return -1;
    }

    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) {
        fprintf(stderr, "[ DRM ] drmModeAtomicAlloc failed!\n");
        drmModeDestroyPropertyBlob(ctx->drm_fd, mode_blob_id);
        return -1;
    }

    drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, mode_id_prop_id, mode_blob_id);
    drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, active_prop_id, 1);
    drmModeAtomicAddProperty(req, ctx->connector->connector_id, crtc_id_prop_id, ctx->crtc->crtc_id);

    ret = drmModeAtomicCommit(ctx->drm_fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    if (ret < 0) {
        fprintf(stderr, "[ DRM ] Atomic commit for CRTC activation failed: %s\n", strerror(errno));
    } else {
        printf("[ DRM ] Successfully activated CRTC and connector.\n");
    }

    drmModeAtomicFree(req);
    drmModeDestroyPropertyBlob(ctx->drm_fd, mode_blob_id);

    return ret;
}

void drm_disable_unused_planes(int drm_fd, uint32_t crtc_id, uint32_t plane_video_id, uint32_t plane_osd_id)
{
    drmModePlaneRes* plane_res = drmModeGetPlaneResources(drm_fd);
    if (!plane_res)
        return;

    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        uint32_t plane_id = plane_res->planes[i];

        // Skip video and OSD planes
        if (plane_id == plane_video_id || plane_id == plane_osd_id)
            continue;

        drmModePlane* plane = drmModeGetPlane(drm_fd, plane_id);
        if (!plane)
            continue;

        // Find CRTC index (possible_crtcs is a bitmap of indexes, not IDs)
        int crtc_index = -1;
        drmModeRes* res = drmModeGetResources(drm_fd);
        for (int c = 0; c < res->count_crtcs; ++c)
            if (res->crtcs[c] == crtc_id)
                crtc_index = c;
        drmModeFreeResources(res);
        if (crtc_index < 0) {
            drmModeFreePlane(plane);
            continue;
        }

        // Check if this plane can be assigned to our CRTC
        if (!(plane->possible_crtcs & (1 << crtc_index))) {
            drmModeFreePlane(plane);
            continue;
        }

        // Only disable if plane is active (has FB assigned)
        if (plane->fb_id == 0) {
            drmModeFreePlane(plane);
            continue;
        }

        drmModeAtomicReq* req = drmModeAtomicAlloc();
        if (!req) {
            drmModeFreePlane(plane);
            continue;
        }

        drmModeObjectProperties* props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
        if (!props) {
            drmModeAtomicFree(req);
            drmModeFreePlane(plane);
            continue;
        }
        uint32_t prop_fb_id = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes* prop = drmModeGetProperty(drm_fd, props->props[j]);
            if (!strcmp(prop->name, "FB_ID"))
                prop_fb_id = prop->prop_id;
            drmModeFreeProperty(prop);
        }

        // Only set FB_ID=0 to disable plane
        if (prop_fb_id) {
            drmModeAtomicAddProperty(req, plane_id, prop_fb_id, 0);

            int ret = drmModeAtomicCommit(drm_fd, req, 0, NULL);
            if (ret) {
                fprintf(stderr, "[ DRM ] Could not disable plane %u: %s\n", plane_id, strerror(errno));
            }
        }

        drmModeAtomicFree(req);
        drmModeFreeObjectProperties(props);
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(plane_res);
}

int drm_init(char *device, struct config_t *cfg)
{
    int ret = 0;
    uint64_t cap;

    if (!device || strlen(device) == 0 || !cfg) {
        fprintf(stderr, "[ DRM ] No device specified\n");
        return -EINVAL;
    }

    drm_context.drm_fd = open(device, O_RDWR | O_CLOEXEC);
    if (drm_context.drm_fd < 0) {
        fprintf(stderr, "[ DRM ] Failed to open DRM device %s: %s\n", device, strerror(errno));
        return -errno;
    }
    printf("[ DRM ] Opened DRM device %s successfully, fb_id %d\n", device, drm_context.drm_fd);

    ret = drmSetClientCap(drm_context.drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (ret) {
        fprintf(stderr, "[ DRM ] Failed to set universal planes capability: %s\n", strerror(errno));
        close(drm_context.drm_fd);
        return -errno;
    }

    printf("[ DRM ] Set universal planes capability successfully\n");

    ret = drmSetClientCap(drm_context.drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ret) {
        fprintf(stderr, "failed to set atomic cap, %d", ret);
        return ret;
    }
    printf("[ DRM ] Set atomic capability successfully\n");

    if (drmGetCap(drm_context.drm_fd, DRM_CAP_DUMB_BUFFER, &cap) < 0 || !cap) {
        fprintf(stderr, "drm device '%s' does not support dumb buffers\n", device);
        close(drm_context.drm_fd);
        return -EOPNOTSUPP;
    }
    printf("[ DRM ] Device supports dumb buffers\n");

    if (drmGetCap(drm_context.drm_fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) < 0 || !cap) {
        fprintf(stderr, "drm device '%s' does not support atomic KMS\n", device);
        close(drm_context.drm_fd);
        return -EOPNOTSUPP;
    }
    printf("[ DRM ] Device supports atomic KMS\n");

    // For debug, print all available modes
    drm_print_modes(&drm_context);

    drm_modeset(&drm_context);

    drm_activate_crtc(&drm_context);


    if (!drm_context.crtc) {
        fprintf(stderr, "[ DRM ] CRTC is not available, aborting further DRM setup!\n");
        return -1;
    }

    drm_context.nv12_plane_id = drm_find_plane(&drm_context, DRM_FORMAT_NV12);
    if (drm_context.nv12_plane_id < 0) {
        fprintf(stderr, "[ DRM ] Failed to find suitable plane for format NV12\n");
        return -1;
    }

    // Fill plane properties for NV12 plane
    drm_fill_plane_props(drm_context.drm_fd, drm_context.nv12_plane_id, &drm_context.video_plane_props,
                         drm_context.connector->connector_id, drm_context.crtc->crtc_id, &drm_context.connector->modes[0]);

    drm_context.argb888_plane_id = drm_find_plane(&drm_context, DRM_FORMAT_ARGB8888);
    if (drm_context.argb888_plane_id < 0) {
        fprintf(stderr, "[ DRM ] Failed to find suitable plane for format ARGB8888\n");
        return -1;
    }

    // Fill plane properties for ARGB8888 plane
    drm_fill_plane_props(drm_context.drm_fd, drm_context.argb888_plane_id, &drm_context.osd_plane_props,
                         drm_context.connector->connector_id, drm_context.crtc->crtc_id, &drm_context.connector->modes[0]);

    printf("[ DRM ] Found NV12 plane ID: %d for video, ARGB8888 plane ID: %d for OSD\n", drm_context.nv12_plane_id, drm_context.argb888_plane_id);

    drm_disable_unused_planes(drm_context.drm_fd, drm_context.crtc->crtc_id, drm_context.nv12_plane_id, drm_context.argb888_plane_id);

    drm_context.rotate = find_rotation_in_dt("/proc/device-tree");
    if (drm_context.rotate == -1) {
        printf("[ DRM ] Rotation not found in device-tree, fallback to 0\n");
        drm_context.rotate = 0;
    }
    printf("[ DRM ] Detected rotation: %d degrees\n", drm_context.rotate);
    //drm_context.rotate = 0; // Force set no rotation for testing

    drm_create_osd_buff_pool(&drm_context);

    int expected = 0;
    if (!atomic_compare_exchange_strong(&running, &expected, 1)) {
        printf("[ DRM ] Already running thread\n");
        return -1;
    }
    ret = pthread_create(&drm_thread, NULL, compositor_thread, &drm_context);

    return ret;
}

static int test_draw_all_plane(struct drm_context_t *ctx)
{
    int ret = 0;
    int width = ctx->display_info.hdisplay;
    int height = ctx->display_info.vdisplay;
    struct drm_fb_t osd_frame_buffer = {0};

    drm_create_dumb_argb8888_fb(ctx, width, height, &osd_frame_buffer);
    if (osd_frame_buffer.buff_addr == MAP_FAILED) {
        fprintf(stderr, "[ DRM ] Failed to create ARGB8888 framebuffer: %s\n", strerror(errno));
        return -1;
    }
    printf("[ DRM TEST ] Created OSD framebuffer: fb_id=%u, size=%zu\n", osd_frame_buffer.fb_id, osd_frame_buffer.size);

    // Fill the OSD framebuffer with a rainbow pattern
    fill_rainbow_argb8888(&osd_frame_buffer, width, height);

    void *src_nv12 = malloc(width * height * 3 / 2);
    if (src_nv12 == NULL) {
        fprintf(stderr, "[ DRM ] Failed to allocate memory for NV12 buffer\n");
        return -1;
    }

    fill_rainbow_checker_nv12(src_nv12, width, height);
    printf("[ DRM TEST ] Filled NV12 buffer with rainbow checker pattern\n");

    int nv12_dmabuf_fd = alloc_nv12_dmabuf_from_ram(src_nv12, width, height);
    if (nv12_dmabuf_fd < 0) {
        fprintf(stderr, "[ DRM ] Failed to allocate NV12 dmabuf from RAM\n");
        free(src_nv12);
        return -1;
    }
    free(src_nv12);

    printf("[ DRM TEST ] Allocated NV12 dmabuf fd: %d\n", nv12_dmabuf_fd);

    struct drm_prime_handle prime = {
        .fd = nv12_dmabuf_fd,
        .flags = 0,
        .handle = 0,
    };
    ioctl(ctx->drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime);

    struct drm_mode_fb_cmd2 fb2 = {
        .width  = width,
        .height = height,
        .pixel_format = DRM_FORMAT_NV12,
        .handles = { prime.handle, prime.handle },
        .pitches = { width, width },
        .offsets = { 0, width * height },
    };
    ret = ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb2);
    if (ret < 0) {
        perror("DRM_IOCTL_MODE_ADDFB2");
        printf("  handle0=%u handle1=%u pitch0=%u pitch1=%u\n",
               prime.handle, prime.handle, width, width);
        printf("  fd=%d\n", nv12_dmabuf_fd);
        close(nv12_dmabuf_fd);
    }
    printf("[ DRM TEST ] Created framebuffer: fb_id=%u\n", fb2.fb_id);


    ret = drm_atomic_commit_all_buffers(ctx, &osd_frame_buffer, width, height, fb2.fb_id, 0,
                                        width, height);
    if (ret < 0) {
        fprintf(stderr, "[ DRM ] Failed to commit video framebuffer: %s\n", strerror(errno));
    }

    return ret;

}

static void drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
    if (!atomic_load(&running)) {
        return;
    }
#if DRM_DEBUG
    static uint64_t prev_sec = 0;
    static uint64_t prev_usec = 0;

    if (prev_sec != 0) {
        double dt = (double)(sec - prev_sec) * 1000.0 + (double)(usec - prev_usec) / 1000.0;
        printf("[ DRM ] Done! Frame flip! Δt=%.2f ms (%.2f FPS)\n", dt, 1000.0 / dt);
    }
    prev_sec = sec;
    prev_usec = usec;
#endif

    if (cleanup.drm_fd >= 0) {

        if (osd_db.dirty[osd_db.next]) {
            osd_db.cur ^= 1;
            osd_db.next ^= 1;
            osd_db.dirty[osd_db.cur] = 0;
        }

        if (video_buf_map.dirty[video_buf_map.cur]) {
            video_buf_map.dirty[video_buf_map.cur] = 0;
        }
        int video_cur = video_buf_map.cur;

        drm_atomic_commit_all_buffers(cleanup.ctx, &osd_bufs[osd_db.cur], osd_db.osd_width, osd_db.osd_height,
                                      video_buf_map.fb_id[video_cur], video_buf_map.dma_fd[video_cur],
                                      video_buf_map.video_width, video_buf_map.video_height);
    }
}

static void drm_init_event_context(void)
{
    evctx.page_flip_handler = drm_page_flip_handler;
}

static int drm_create_osd_buff_pool(struct drm_context_t *ctx)
{
    if (!ctx || ctx->drm_fd < 0) {
        fprintf(stderr, "[ DRM ] Invalid DRM context\n");
        return -EINVAL;
    }

    int ret = 0;
    int width = OSD_WIDTH;
    int height = OSD_HEIGHT;
    if (ctx->rotate == 90 || ctx->rotate == 270) {
        // Swap width and height for 90/270 degree rotation
        width = OSD_HEIGHT;
        height = OSD_WIDTH;
    }

    for (int i = 0; i < OSD_BUF_COUNT; ++i) {
        ret = drm_create_dumb_argb8888_fb(ctx, width, height, &osd_bufs[i]);
        if (ret < 0) {
            fprintf(stderr, "OSD dumb fb init failed for slot %d\n", i);
        }
        osd_db.dirty[i] = 0;
    }
    osd_db.osd_width = width;
    osd_db.osd_height = height;
    osd_db.cur = 0;
    osd_db.next = 1;

    printf("[ DRM ] OSD buffer pool created successfully\n");
    return ret;

}

static void rotate_video_pool_cleanup(struct drm_context_t *ctx)
{
    printf("[ DRM ] Cleaning up video rotate pool\n");
    for (int i = 0; i < ROTATE_BUF_COUNT; ++i) {
        if (rotate_video_pool.fb_id[i] > 0) {
            if (ctx && ctx->drm_fd > 0) {
                drmModeRmFB(ctx->drm_fd, rotate_video_pool.fb_id[i]);
                printf("[ DRM ] Removed video rotate pool FB %d\n", rotate_video_pool.fb_id[i]);
            }
            rotate_video_pool.fb_id[i] = 0;
        }
        if (rotate_video_pool.dma_fd[i] > 0) {
            close(rotate_video_pool.dma_fd[i]);
            printf("[ DRM ] Closed video rotate pool DMA FD %d\n", rotate_video_pool.dma_fd[i]);
            rotate_video_pool.dma_fd[i] = 0;
        }
    }
    memset(&rotate_video_pool, 0, sizeof(rotate_video_pool));
}

static void rotate_video_pool_init(struct drm_context_t *ctx, int width, int height, int hor_stride, int ver_stride)
{
    if (rotate_video_pool.w == width || rotate_video_pool.h == height) return;

    printf("[ DRM ] Initializing video rotate pool, size: %dx%d, stride: %dx%d\n", width, height, hor_stride, ver_stride);

    rotate_video_pool.w = width;
    rotate_video_pool.h = height;
    rotate_video_pool.hor_stride = hor_stride;
    rotate_video_pool.ver_stride = ver_stride;
    for (int i = 0; i < ROTATE_BUF_COUNT; ++i) {
        if (rotate_video_pool.fb_id[i] > 0) {
            drmModeRmFB(ctx->drm_fd, rotate_video_pool.fb_id[i]);
            rotate_video_pool.fb_id[i] = 0;
        }
        if (rotate_video_pool.dma_fd[i] > 0) {
            close(rotate_video_pool.dma_fd[i]);
            rotate_video_pool.dma_fd[i] = -1;
        }
        rotate_video_pool.dma_fd[i] = alloc_dmabuf_fd(hor_stride * ver_stride * 3 / 2);
        rotate_video_pool.fb_id[i] = drm_prepare_nv12_fb(ctx, rotate_video_pool.dma_fd[i], width, height, hor_stride, ver_stride);
    }
    rotate_video_pool.count = 0;
}

static void video_buf_map_cleanup(struct drm_context_t *ctx)
{
    printf("[ DRM ] Cleaning up video buffer map\n");
    for (int i = 0; i < MAX_VIDEO_BUFS; ++i) {
        if (video_buf_map.fb_id[i] > 0 && ctx->drm_fd > 0) {
            drmModeRmFB(ctx->drm_fd, video_buf_map.fb_id[i]);
            printf("[ DRM ] Removed video buffer FB %d\n", video_buf_map.fb_id[i]);
            video_buf_map.fb_id[i] = 0;
        }
        if (video_buf_map.dma_fd[i] > 0) {
            close(video_buf_map.dma_fd[i]);
            printf("[ DRM ] Closed video buffer DMA FD %d\n", video_buf_map.dma_fd[i]);
            video_buf_map.dma_fd[i] = -1;
        }
        video_buf_map.dirty[i] = 0;
    }
    video_buf_map.count = 0;
    video_buf_map.cur = 0;
}

static void drm_cleanup_osd_buff_pool(struct drm_context_t *ctx)
{
    printf("[ DRM ] Cleaning up OSD buffer pool\n");
    for (int i = 0; i < OSD_BUF_COUNT; ++i) {
        if (osd_bufs[i].fb_id > 0 && ctx->drm_fd > 0) {
            drmModeRmFB(ctx->drm_fd, osd_bufs[i].fb_id);
            printf("[ DRM ] Removed OSD buffer FB %d\n", osd_bufs[i].fb_id);
            osd_bufs[i].fb_id = 0;
        }
        if (osd_bufs[i].buff_addr && osd_bufs[i].buff_addr != MAP_FAILED) {
            munmap(osd_bufs[i].buff_addr, osd_bufs[i].size);
            printf("[ DRM ] Unmapped OSD buffer %d\n", i);
            osd_bufs[i].buff_addr = NULL;
        }
        osd_bufs[i].handles[0] = 0;
        osd_bufs[i].pitches[0] = 0;
        osd_bufs[i].size = 0;
        osd_db.dirty[i] = 0;
    }
    osd_db.osd_width = 0;
    osd_db.osd_height = 0;
    osd_db.cur = 0;
    osd_db.next = 1;
}

int drm_get_osd_frame_size(int *width, int *height, int *rotate)
{
    if (width) *width = osd_db.osd_width;
    if (height) *height = osd_db.osd_height;
    if (rotate) *rotate = drm_context.rotate;
    if (osd_db.osd_width <= 0 || osd_db.osd_height <= 0) {
        fprintf(stderr, "[ DRM ] OSD frame size is not initialized!\n");
        return -1;
    }
    return 0;
}

void drm_push_new_osd_frame(void)
{
    osd_db.dirty[osd_db.next] = 1;
}

void *drm_get_next_osd_fb(void)
{
    if (osd_db.dirty[osd_db.next] == 0 && osd_bufs[osd_db.next].buff_addr) {
        return osd_bufs[osd_db.next].buff_addr;
    }
    fprintf(stderr, "[ DRM ] OSD buffer %d is dirty or not available\n", osd_db.next);
    // force dirty to ensure next frame
    osd_db.cur ^= 1;
    osd_db.next ^= 1;
    osd_db.dirty[osd_db.cur] = 0;
    return NULL;
}

static int get_next_rotate_dma_fd(struct drm_context_t *ctx, int width, int height, int hor_stride, int ver_stride)
{
    if (rotate_video_pool.w != width || rotate_video_pool.h != height ||
        rotate_video_pool.hor_stride != hor_stride || rotate_video_pool.ver_stride != ver_stride) {
        rotate_video_pool_cleanup(ctx); // cleanup old buffers
        rotate_video_pool_init(ctx, width, height, hor_stride, ver_stride);
        rotate_video_pool.count = 0;
        video_buf_map.count = 0; // Reset video buffer map
    }

    int idx = rotate_video_pool.count;
    rotate_video_pool.count = (rotate_video_pool.count + 1) % ROTATE_BUF_COUNT;
    //printf("[ DRM ] Using rotate pool buffer %d for rotation, size %dx%d (stride %dx%d)\n", idx, width, height, hor_stride, ver_stride);
    return rotate_video_pool.dma_fd[idx];
}

void drm_push_new_video_frame(int dma_fd, int width, int height, int hor_stride, int ver_stride)
{
    //printf("[ DRM ] New Video Frame, DMA FD: %d, size: %dx%d (stride %dx%d)\n", dma_fd, width, height, hor_stride, ver_stride);
    struct drm_context_t *ctx = drm_get_ctx();
    int need_rotate = (ctx->rotate == 90 || ctx->rotate == 270 || ctx->rotate == 180);
    int out_width = width, out_height = height;
    int out_hor_stride = hor_stride, out_ver_stride = ver_stride;
    int current_dma_fd = -1;
    int idx = -1;

    if (need_rotate) {
        out_width = height;
        out_height = width;
        out_hor_stride = ver_stride;
        out_ver_stride = hor_stride;
        current_dma_fd = get_next_rotate_dma_fd(ctx, out_width, out_height, out_hor_stride, out_ver_stride);
        if (current_dma_fd < 0) {
            fprintf(stderr, "[ DRM ] All rotate buffers busy, dropping frame!\n");
            return;
        }
        int rotate = 0;
        if(ctx->rotate == 90) {
            rotate = IM_HAL_TRANSFORM_ROT_90;
        } else if (ctx->rotate == 270) {
            rotate = IM_HAL_TRANSFORM_ROT_270;
        } else if (ctx->rotate == 180) {
            rotate = IM_HAL_TRANSFORM_ROT_180;
            out_width = width;
            out_height = height;
        }

        // Rotate the current_dma_fd using RGA
        int rga_ret = rga_nv12_rotate(dma_fd, current_dma_fd, width, height, hor_stride, ver_stride, rotate);
        if (rga_ret != 0) {
            fprintf(stderr, "[ DRM ] RGA rotation failed\n");
            rotate_video_pool_cleanup(ctx);
            rotate_video_pool_init(ctx, width, height, hor_stride, ver_stride);
            return;
        }
    } else {
        current_dma_fd = dma_fd;
    }

    // Check if we already have this DMA FD in the video buffer map
    for (int i = 0; i < video_buf_map.count; ++i) {
        if (video_buf_map.dma_fd[i] == current_dma_fd) {
            idx = i;
            break;
        }
    }

    // If not found, we need to allocate a new DMA FD buffer
    if (idx < 0) {
        if (video_buf_map.count >= MAX_VIDEO_BUFS) {
            int to_cleanup = (video_buf_map.cur + 1) % MAX_VIDEO_BUFS;

            if (video_buf_map.fb_id[to_cleanup] > 0) {
                drmModeRmFB(ctx->drm_fd, video_buf_map.fb_id[to_cleanup]);
                printf("[ DRM ] Cleaned up old video FB %d\n", video_buf_map.fb_id[to_cleanup]);
            }
            if (video_buf_map.dma_fd[to_cleanup] > 0) {
                close(video_buf_map.dma_fd[to_cleanup]);
                printf("[ DRM ] Closed old DMA FD %d\n", video_buf_map.dma_fd[to_cleanup]);
            }

            idx = to_cleanup;
        } else {
            idx = video_buf_map.count++;
        }

        int current_fb_id = drm_prepare_nv12_fb(ctx, current_dma_fd, out_width, out_height, out_hor_stride, out_ver_stride);
        if (current_fb_id < 0) {
            printf("[ DRM ] Failed to register new NV12 FB\n");
            return;
        }
        printf("[ DRM ] Registered new NV12 video buffer with fd %d\n", current_dma_fd);
        video_buf_map.video_height = out_height;
        video_buf_map.video_width = out_width;
        video_buf_map.dma_fd[idx] = current_dma_fd;
        video_buf_map.fb_id[idx] = current_fb_id;
        video_buf_map.dirty[idx] = 1;
    } else {
        video_buf_map.dirty[idx] = 1;
    }

    if (idx >= 0) {
        video_buf_map.cur = idx;
#if DRM_DEBUG
        printf("[ DRM ] Pushed new video frame to buffer %d (DMA FD: %d, FB ID: %u)\n",
               idx, video_buf_map.dma_fd[idx], video_buf_map.fb_id[idx]);
#endif
    }
    struct dma_buf_sync sync = {0};
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    ioctl(dma_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

static void fill_transparent_argb8888(struct drm_fb_t *fb, int width, int height)
{
    uint32_t *p = (uint32_t *)fb->buff_addr;
    int stride = fb->pitches[0] / 4;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            p[y * stride + x] = 0x00000000;
}

static void fill_black_nv12(uint8_t *buf, int width, int height)
{
    memset(buf, 0, width * height);
    memset(buf + width * height, 128, width * height / 2);
}

static void fill_rainbow_nv12(uint8_t *buf, int width, int height)
{
    uint8_t *y_plane = buf;
    uint8_t *uv_plane = buf + width * height;

    // Fill Y plane: horizontal gradient
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            y_plane[y * width + x] = (x * 255) / width;  // gradient in luminance
        }
    }

    // Fill UV plane: alternating colorful pattern (each 2x2 block shares 1 UV)
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            int i = (y * width) + (x * 2); // two bytes per UV sample (U, V)

            // cycle through color hues (this is arbitrary, not real HSV → YUV mapping)
            uint8_t u = 128 + 50 * sin((float)x / (width / 10.0f));
            uint8_t v = 128 + 50 * cosf((float)x / (width / 10.0f));

            uv_plane[i] = u; // U
            uv_plane[i + 1] = v; // V
        }
    }
}

static void* compositor_thread(void* arg)
{
    struct drm_context_t *ctx = (struct drm_context_t *)arg;

    if (!ctx || ctx->drm_fd < 0) {
        fprintf(stderr, "[ DRM ] Invalid DRM context in compositor thread\n");
        return NULL;
    } else {
        printf("[ DRM ] Compositor thread started with DRM fd %d\n", ctx->drm_fd);
    }

    for (int i = 0; i < OSD_BUF_COUNT; ++i) {
        fill_transparent_argb8888(&osd_bufs[i], osd_db.osd_width, osd_db.osd_height);
        //fill_rainbow_argb8888(&osd_bufs[i], osd_db.osd_width, osd_db.osd_height);
        osd_db.dirty[i] = 0;
    }

    if (video_buf_map.count == 0) {
        int w = ctx->display_info.hdisplay;
        int h = ctx->display_info.vdisplay;
        size_t sz = w * h * 3 / 2;
        uint8_t *buff = malloc(sz);
        //fill_black_nv12(buff, w, h);
        fill_rainbow_nv12(buff, w, h); // Maybe add screen saver later
        int dma_fd = alloc_nv12_dmabuf_from_ram(buff, w, h);
        free(buff);
        if (dma_fd >= 0) {
            video_buf_map.dma_fd[0] = dma_fd;
            video_buf_map.fb_id[0] = drm_prepare_nv12_fb(ctx, dma_fd, w, h, w, h);
            video_buf_map.video_width = w;
            video_buf_map.video_height = h;
            video_buf_map.count = 1;
            video_buf_map.cur = 0;
            video_buf_map.dirty[0] = 0;
        }
    }

    drm_init_event_context();

    // Force initial commit to display the first frame
    int video_cur = video_buf_map.cur;
    drm_atomic_commit_all_buffers(ctx,
                                  &osd_bufs[osd_db.cur], osd_db.osd_width, osd_db.osd_height,
                                  video_buf_map.fb_id[video_cur], video_buf_map.dma_fd[video_cur],
                                  video_buf_map.video_width, video_buf_map.video_height);

    atomic_store(&pending_commit, 1);

    while (atomic_load(&running)) {
        if (atomic_load(&pending_commit)) {
            video_cur = video_buf_map.cur;
            drm_atomic_commit_all_buffers(ctx,
                                          &osd_bufs[osd_db.cur], osd_db.osd_width, osd_db.osd_height,
                                          video_buf_map.fb_id[video_cur], video_buf_map.dma_fd[video_cur],
                                          video_buf_map.video_width, video_buf_map.video_height);
            atomic_store(&pending_commit, 0);
        }
        drmHandleEvent(ctx->drm_fd, &evctx);
        usleep(5000); // Sleep for 5 ms to avoid busy-waiting new events
    }

    printf("[ DRM ] Compositor thread exiting\n");
    return NULL;
}

struct drm_context_t *drm_get_ctx(void)
{
    if (drm_context.drm_fd < 0) {
        fprintf(stderr, "[ DRM ] DRM context not initialized\n");
        return NULL;
    }
    return &drm_context;
}

static int rga_argb8888_rotate(int src_fd, int dst_fd, int src_width, int src_height, int rotation)
{
#if DRM_DEBUG_ROTATE
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
#endif
    im_handle_param_t src_param = {
        .width = src_width,
        .height = src_height,
        .format = RK_FORMAT_ARGB_8888
    };
    im_handle_param_t dst_param = {
        .width = src_height,
        .height = src_width,
        .format = RK_FORMAT_ARGB_8888
    };

    rga_buffer_handle_t src_handle = importbuffer_fd(src_fd, &src_param);
    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_fd, &dst_param);

    if (src_handle == 0 || dst_handle == 0) {
        fprintf(stderr, "[RGA] importbuffer_fd failed\n");
        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        return -1;
    }
    rga_buffer_t src = wrapbuffer_handle(src_handle, src_width, src_height, RK_FORMAT_ARGB_8888);
    rga_buffer_t dst = wrapbuffer_handle(dst_handle, src_height, src_width, RK_FORMAT_ARGB_8888);

    imrotate(src, dst, rotation);

    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

#if DRM_DEBUG_ROTATE
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long usec = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000;
    double ms = usec / 1000.0;
    printf("[ RGA ] Rotation completed %.3f ms\n", ms);
#endif
    return 0;
}

static int rga_nv12_rotate(int src_fd, int dst_fd, int src_width, int src_height, int wstride, int hstride, int rotation)
{
#if DRM_DEBUG_ROTATE
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
#endif

    im_handle_param_t src_param = {
        .width = src_width,
        .height = src_height,
        .format = RK_FORMAT_YCbCr_420_SP
    };
    im_handle_param_t dst_param = {
        .width = src_height,
        .height = src_width,
        .format = RK_FORMAT_YCbCr_420_SP
    };

    // Import dma_buf into RGA
    rga_buffer_handle_t src_handle = importbuffer_fd(src_fd, &src_param);
    rga_buffer_handle_t dst_handle = importbuffer_fd(dst_fd, &dst_param);

    if (src_handle == 0 || dst_handle == 0) {
        fprintf(stderr, "[RGA] importbuffer_fd failed\n");
        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        return -1;
    }

    const rga_buffer_t src = wrapbuffer_handle_t(src_handle, src_width, src_height, wstride, hstride, RK_FORMAT_YCbCr_420_SP);
    const rga_buffer_t dst = wrapbuffer_handle_t(dst_handle, src_height, src_width, hstride, wstride, RK_FORMAT_YCbCr_420_SP);

    // rotate the image using RGA
    int ret = imrotate(src, dst, rotation);
    if (ret != IM_STATUS_SUCCESS) {
        printf("Error: imrotate failed: %d\n", ret);
        releasebuffer_handle(src_handle);
        releasebuffer_handle(dst_handle);
        return -1;
    }

    // Release RGA handles, do not close dma_fd here!
    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

#if DRM_DEBUG_ROTATE
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long usec = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000;
    double ms = usec / 1000.0;
    printf("[ RGA ] Rotation completed %.3f ms\n", ms);
#endif
    return 0;
}

void drm_close(void)
{
    //pthread_mutex_destroy(&drm_mutex);

    printf("[ DRM ] Close...\n");
    if (!atomic_load(&running)) {
        printf("[ DRM ] Not running, nothing to stop\n");
    } else {
        atomic_store(&running, 0);
        pthread_join(drm_thread, NULL);
        printf("[ DRM ] Stopped compositor thread\n");
    }

    if (drm_context.drm_fd > 0) {
        close(drm_context.drm_fd);
        drm_context.drm_fd = -1;
        printf("[ DRM ] Closed DRM device\n");
    } else {
        fprintf(stderr, "[ DRM ] DRM device not initialized or already closed\n");
    }

    rotate_video_pool_cleanup(&drm_context);
    video_buf_map_cleanup(&drm_context);
    drm_cleanup_osd_buff_pool(&drm_context);
}
