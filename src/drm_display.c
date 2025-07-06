/**
 * @file drm.c this is part of project 'vd-link'
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
 * Created vitalii.nimych@gmail.com 02-07-2025
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
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/rga.h>
#include <linux/dma-heap.h>
#include <dirent.h>

#define DRM_DEBUG 0

static struct drm_context_t drm_context = {
        .display_info = {0},
        .connector = NULL,
        .drm_fd = -1,
        .nv12_plane_id = -1,
        .argb888_plane_id = -1,
        .osd_plane_props = {0},
        .video_plane_props = {0},
        .drm_flags = DRM_MODE_ATOMIC_NONBLOCK,
};

static int drm_get_prop_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *name);
static int64_t drm_get_prop_value(int fd, uint32_t obj_id, uint32_t obj_type, const char *name);

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

static int drm_atomic_commit_osd(struct drm_context_t *ctx, struct drm_fb_t *osd_fb, int width, int height)
{
    struct drm_plane_props *p = &ctx->osd_plane_props;
    drmModeAtomicReq *req = drmModeAtomicAlloc();

    // Calculate aspect-ratio-preserving scaling and centering for OSD
    int screen_w = ctx->display_info.hdisplay;
    int screen_h = ctx->display_info.vdisplay;
    float ar_src = (float)width / height;
    float ar_dst = (float)screen_w / screen_h;
    int crtc_w, crtc_h, crtc_x, crtc_y;

    if (ar_dst > ar_src) {
        // Display is wider than image: pad left/right
        crtc_h = screen_h;
        crtc_w = (int)(screen_h * ar_src);
        crtc_x = (screen_w - crtc_w) / 2;
        crtc_y = 0;
    } else {
        // Display is taller than image: pad top/bottom
        crtc_w = screen_w;
        crtc_h = (int)(screen_w / ar_src);
        crtc_x = 0;
        crtc_y = (screen_h - crtc_h) / 2;
    }

    // Set connector property: CRTC_ID
    if (p->connector_crtc_id > 0)
        drmModeAtomicAddProperty(req, ctx->connector->connector_id, p->connector_crtc_id, ctx->crtc->crtc_id);

    // Set CRTC properties: MODE_ID, ACTIVE
    if (p->mode_id > 0)
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, p->mode_id, p->mode_blob_id);
    if (p->active > 0)
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, p->active, 1);

    // Set plane properties (source area always whole image)
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->fb_id,   osd_fb->fb_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_id, ctx->crtc->crtc_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_x,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_y,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_w,   width << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_h,   height << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_x,  crtc_x);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_y,  crtc_y);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_w,  crtc_w);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_h,  crtc_h);

    // Set Z-position if available
    if (p->zpos > 0)
        drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->zpos, p->zpos_value);

    int ret = drmModeAtomicCommit(ctx->drm_fd, req, ctx->drm_flags, NULL);
    if (ret < 0) {
        fprintf(stderr, "[ DRM ] Atomic commit failed for OSD plane %d: %s\n", ctx->argb888_plane_id, strerror(errno));
    }
    drmModeAtomicFree(req);
    return ret;
}

static int drm_prepare_nv12_fb(struct drm_context_t *ctx, int dma_fd, int width, int height)
{
#if DRM_DEBUG
    printf("[ DRM ] Preparing NV12 framebuffer with DMA-FD %d, size %dx%d\n", dma_fd, width, height);
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
#endif
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

    // Prepare FB2 structure for NV12 (Y and UV in same buffer, with proper offsets)
    struct drm_mode_fb_cmd2 fb2 = {
            .width  = width,
            .height = height,
            .pixel_format = DRM_FORMAT_NV12,
            .handles = { prime.handle, prime.handle },
            .pitches = { width, width },// Y and UV both have same stride
            .offsets = { 0, width * height },// Y at 0, UV after Y
    };
    if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb2) < 0) {
        perror("[ DRM ] DRM_IOCTL_MODE_ADDFB2");
        printf("  handle0=%u handle1=%u pitch0=%u pitch1=%u\n",
               prime.handle, prime.handle, width, width);
        printf("  fd=%d\n", dma_fd);
        return -1;
    }

#if DRM_DEBUG
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long usec = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000;
    printf("[ DRM ] Created framebuffer: fb_id=%u took time: %ld us\n", fb2.fb_id, usec);
#endif
    return fb2.fb_id;
}

static int drm_atomic_commit_video(struct drm_context_t *ctx, int width, int height, uint32_t fb)
{
#if DRM_DEBUG
    printf("[ DRM ] drm_fd=%d Committing video plane %d with FB %d, size %dx%d\n", ctx->drm_fd, ctx->nv12_plane_id, fb, width, height);
#endif
    static uint32_t prev_fb_id = 0;

    struct drm_plane_props *p = &ctx->video_plane_props;
    drmModeAtomicReq *req = drmModeAtomicAlloc();

    if (!ctx || !ctx->connector || !ctx->crtc || !p) {
        fprintf(stderr, "NULL pointer in DRM context!\n");
        return -1;
    }

    // Calculate the output area with aspect ratio preservation (letterbox/pillarbox)
    uint32_t crtcw = ctx->display_info.hdisplay;
    uint32_t crtch = ctx->display_info.vdisplay;
    float video_ratio = (float)width / (float)height;

    // Choose the largest area that fits while preserving aspect ratio
    if ((float)crtcw / video_ratio > (float)crtch) {
        // Black bars on the sides (pillarbox)
        crtcw = crtch * video_ratio;
        // crtch remains unchanged
    } else {
        // Black bars on top/bottom (letterbox)
        // crtcw remains unchanged
        crtch = crtcw / video_ratio;
    }
    // Center the video on the screen
    int crtcx = (int)(ctx->display_info.hdisplay - crtcw) / 2;
    int crtcy = (int)(ctx->display_info.vdisplay - crtch) / 2;

    // Set connector property CRTC_ID if present
    if (p->connector_crtc_id > 0)
        drmModeAtomicAddProperty(req, ctx->connector->connector_id, p->connector_crtc_id, ctx->crtc->crtc_id);

    // Set CRTC properties if present (MODE_ID and ACTIVE)
    if (p->mode_id > 0)
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, p->mode_id, p->mode_blob_id);
    if (p->active > 0)
        drmModeAtomicAddProperty(req, ctx->crtc->crtc_id, p->active, 1);

    // Set video plane properties
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->crtc_id, ctx->crtc->crtc_id);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->fb_id,   fb);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->src_x,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->src_y,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->src_w,   width << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->src_h,   height << 16);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->crtc_x,  crtcx);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->crtc_y,  crtcy);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->crtc_w,  crtcw);
    drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->crtc_h,  crtch);

    // Set Z-position if property is available
    if (p->zpos > 0)
        drmModeAtomicAddProperty(req, ctx->nv12_plane_id, p->zpos, p->zpos_value);

    // Perform the atomic commit
    int ret = drmModeAtomicCommit(ctx->drm_fd, req, ctx->drm_flags, NULL);
    if (ret < 0) {
        ioctl(ctx->drm_fd, DRM_IOCTL_MODE_RMFB, &fb);
        drmModeAtomicFree(req);
        return ret;
    }

    if (prev_fb_id && prev_fb_id != fb) {
        if (ioctl(ctx->drm_fd, DRM_IOCTL_MODE_RMFB, &prev_fb_id) < 0) {
            perror("[ DRM ] Failed to remove previous video FB");
        }
    }
    prev_fb_id = fb;

    drmModeAtomicFree(req);
    return ret;
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

static int test_drm_output(struct drm_context_t *ctx)
{
    if (!ctx || ctx->drm_fd < 0) {
        fprintf(stderr, "[ DRM ] Invalid DRM context\n");
        return -EINVAL;
    }

    int ret = 0;
    int width = ctx->display_info.hdisplay;
    int height = ctx->display_info.vdisplay;
    struct drm_fb_t osd_frame_buffer = {0};

    drm_create_dumb_argb8888_fb(ctx, width, height, &osd_frame_buffer);
    if (osd_frame_buffer.buff_addr == MAP_FAILED) {
        fprintf(stderr, "[ DRM ] Failed to create ARGB8888 framebuffer: %s\n", strerror(errno));
        return -1;
    }

    // Fill the OSD framebuffer with a rainbow pattern
    //fill_rainbow_argb8888(&osd_frame_buffer, width, height);
    printf("[ DRM TEST ] Filled OSD framebuffer with rainbow pattern\n");

    ret = drm_atomic_commit_osd(ctx, &osd_frame_buffer, width, height);
    if (ret < 0) {
        fprintf(stderr, "[ DRM ] Failed to commit OSD framebuffer: %s\n", strerror(errno));
        munmap(osd_frame_buffer.buff_addr, osd_frame_buffer.size);
        return ret;
    }
    printf("[ DRM TEST ] OSD framebuffer committed successfully\n");

    void *src_nv12 = malloc(width * height * 3 / 2);
    if (src_nv12 == NULL) {
        fprintf(stderr, "[ DRM ] Failed to allocate memory for NV12 buffer\n");
        return -ENOMEM;
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

    ret = drm_atomic_commit_video(ctx, width, height, fb2.fb_id);
    if (ret < 0) {
        fprintf(stderr, "[ DRM ] Failed to commit video framebuffer: %s\n", strerror(errno));
        munmap(osd_frame_buffer.buff_addr, osd_frame_buffer.size);
        close(nv12_dmabuf_fd);
        return ret;
    }
    printf("[ DRM TEST ] Video framebuffer committed successfully\n");

    return 0;
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

int drm_init(char *device, struct config_t *cfg)
{
    int ret = 0;
    uint64_t cap;

    if (!device || strlen(device) == 0 || !cfg) {
        fprintf(stderr, "[ DRM ] No device specified\n");
        return -EINVAL;
    }

    if (cfg->vsync == true) {
        drm_context.drm_flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT;
        printf("[ DRM ] Using vsync mode for atomic commits\n");
    } else {
        drm_context.drm_flags = DRM_MODE_ATOMIC_NONBLOCK;
        printf("[ DRM ] Using non-vsync mode for atomic commits\n");
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

    drm_context.rotate = find_rotation_in_dt("/proc/device-tree");
    if (drm_context.rotate == -1) {
        printf("[ DRM ] Rotation not found in device-tree, fallback to 0\n");
        drm_context.rotate = 0;
    }
    printf("[ DRM ] Detected rotation: %d degrees\n", drm_context.rotate);

#if 1
    test_drm_output(&drm_context);
#endif

    return 0;
}

struct drm_context_t *drm_get_ctx(void)
{
    if (drm_context.drm_fd < 0) {
        fprintf(stderr, "[ DRM ] DRM context not initialized\n");
        return NULL;
    }
    return &drm_context;
}

int drm_osd_buffer_flush(struct drm_context_t *ctx, struct drm_fb_t *osd_fb)
{
    if (!ctx || ctx->drm_fd < 0 || ctx->argb888_plane_id < 0 || !osd_fb)
        return -EINVAL;

    int width  = ctx->display_info.hdisplay;
    int height = ctx->display_info.vdisplay;
    struct drm_plane_props *p = &ctx->osd_plane_props;

    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) return -ENOMEM;

    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->fb_id,   osd_fb->fb_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_id, ctx->crtc->crtc_id);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_x,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_y,   0 << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_w,   width << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->src_h,   height << 16);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_x,  0);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_y,  0);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_w,  width);
    drmModeAtomicAddProperty(req, ctx->argb888_plane_id, p->crtc_h,  height);

    int ret = drmModeAtomicCommit(ctx->drm_fd, req, ctx->drm_flags, NULL);
    drmModeAtomicFree(req);

    if (ret < 0) {
        fprintf(stderr, "[ DRM ] drm_osd_buffer_flush: drmModeAtomicCommit failed: %s\n", strerror(errno));
    }
    return ret;
}

static int rga_nv12_rotate(int src_fd, int dst_fd, int src_width, int src_height, int rotation)
{
#if DRM_DEBUG
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

    rga_buffer_t src = wrapbuffer_handle(src_handle, src_width, src_height, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t dst = wrapbuffer_handle(dst_handle, src_height, src_width, RK_FORMAT_YCbCr_420_SP);

    // rotate the image using RGA
    imrotate(src, dst, rotation);

    // Release RGA handles, do not close dma_fd here!
    releasebuffer_handle(src_handle);
    releasebuffer_handle(dst_handle);

#if DRM_DEBUG
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long usec = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000;
    double ms = usec / 1000.0;
    printf("[ RGA ] Rotation completed %.3f ms\n", ms);
#endif
    return 0;
}

static int ensure_rotate_dma_fd(struct drm_context_t *ctx, int w, int h)
{
    size_t sz = w * h * 3 / 2;
    if (ctx->rotate_dma_fd >= 0 && ctx->rotate_buf_w == w && ctx->rotate_buf_h == h)
        return ctx->rotate_dma_fd;

    int fd = alloc_dmabuf_fd(sz);
    if (fd < 0) return -1;

    ctx->rotate_dma_fd = fd;
    ctx->rotate_buf_size = sz;
    ctx->rotate_buf_w = w;
    ctx->rotate_buf_h = h;
    return fd;
}

int drm_nv12_frame_flush(int dma_fd, int width, int height)
{
    if (dma_fd < 0 || width <= 0 || height <= 0) {
        fprintf(stderr, "[ DRM ] Invalid parameters for drm_nv12_frame_flush\n");
        return -EINVAL;
    }

    struct drm_context_t *ctx = drm_get_ctx();
    if (!ctx) {
        fprintf(stderr, "[ DRM ] DRM context not initialized\n");
        return -ENODEV;
    }

    int fb_id = -1;

    if (ctx->rotate == 0 || ctx->rotate == 180) {
        // No rotation or 180°: direct display
        fb_id = drm_prepare_nv12_fb(ctx, dma_fd, width, height);
        if (fb_id < 0) {
            fprintf(stderr, "[ DRM ] Failed to prepare NV12 framebuffer\n");
            return fb_id;
        }
        return drm_atomic_commit_video(ctx, width, height, fb_id);

    } else if (ctx->rotate == 90 || ctx->rotate == 270) {
        // 90° or 270° rotation required: use RGA
        int dst_fd = ensure_rotate_dma_fd(ctx, height, width);
        if (dst_fd < 0) {
            fprintf(stderr, "Failed to alloc rotation buffer\n");
            return -ENOMEM;
        }
        int rga_ret = rga_nv12_rotate(dma_fd, dst_fd, width, height,
                                      ctx->rotate == 90 ? IM_HAL_TRANSFORM_ROT_90 : IM_HAL_TRANSFORM_ROT_270);
        if (rga_ret != 0) {
            fprintf(stderr, "[ DRM ] RGA rotation failed\n");
            return -1;
        }
        fb_id = drm_prepare_nv12_fb(ctx, dst_fd, height, width);
        if (fb_id < 0) {
            fprintf(stderr, "[ DRM ] Failed to prepare NV12 framebuffer\n");
            return fb_id;
        }
        drm_atomic_commit_video(ctx, height, width, fb_id);
        return 0;

    } else {
        fprintf(stderr, "[ DRM ] Invalid rotation value: %d\n", ctx->rotate);
        return -EINVAL;
    }
}

void drm_close(void)
{
    if (drm_context.drm_fd > 0) {
        close(drm_context.drm_fd);
        drm_context.drm_fd = -1;
        printf("[ DRM ] Closed DRM device\n");
    } else {
        fprintf(stderr, "[ DRM ] DRM device not initialized or already closed\n");
    }

    if (drm_context.rotate_dma_fd > 0) {
        close(drm_context.rotate_dma_fd);
        drm_context.rotate_dma_fd = -1;
        printf("[ DRM ] Closed rotate DMA buffer\n");
    } else {
        fprintf(stderr, "[ DRM ] Rotate DMA buffer not initialized or already closed\n");
    }
}
