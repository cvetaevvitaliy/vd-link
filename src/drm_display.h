/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
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

    // OSD planes
    int argb888_plane_id;
    struct drm_plane_props overlay_plane_props;

    // Video planes
    int nv12_plane_id;
    struct drm_plane_props video_plane_props;

    // Rotation: 0, 90, 180, 270 (degrees)
    int rotate;
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

void drm_push_new_video_frame(int dma_fd, int width, int height, int hor_stride, int ver_stride);

int drm_get_overlay_frame_size(int *width, int *height, int *rotate);

void drm_push_new_overlay_frame(void);

void *drm_get_next_overlay_fb(void);

void drm_close(void);

#endif //VRX_DRM_H
