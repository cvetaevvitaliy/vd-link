/**
 * @file drm.h this is part of project 'vd-link'
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

#ifndef VRX_DRM_H
#define VRX_DRM_H
#include <stdint.h>
#include "common.h"
#include <xf86drmMode.h>

struct display_info_t {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
};

struct drm_plane_props {
    int fb_id;
    int crtc_id;
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int crtc_x;
    int crtc_y;
    int crtc_w;
    int crtc_h;
    int64_t zpos;
    int64_t zpos_value;
    int connector_crtc_id;
    int mode_id;
    int active;
    int mode_blob_id;
};

struct drm_context_t {
    struct display_info_t display_info;
    drmModeConnector *connector;
    drmModeCrtc *crtc;
    int drm_fd;
    int drm_flags;

    // OSD planes
    int argb888_plane_id;
    struct drm_plane_props osd_plane_props;

    // Video planes
    int nv12_plane_id;
    struct drm_plane_props video_plane_props;

    // Rotation: 0, 90, 180, 270 (degrees)
    int rotate;
    int rotate_dma_fd;
    size_t rotate_buf_size;
    int rotate_buf_w, rotate_buf_h;
};

struct drm_fb_t {
    uint32_t fb_id;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    void *buff_addr;
    size_t size;
};


int drm_init(char *device, struct config_t *cfg);

struct drm_context_t *drm_get_ctx(void);

int drm_osd_buffer_flush(struct drm_context_t *ctx, struct drm_fb_t *osd_fb);

int drm_nv12_frame_flush(int dma_fd, int width, int height);

void drm_close(void);

#endif //VRX_DRM_H
