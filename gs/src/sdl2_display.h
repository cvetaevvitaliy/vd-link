/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VD_LINK_SDL2_DISPLAY_H
#define VD_LINK_SDL2_DISPLAY_H
#include <stdint.h>
#include "common.h"

typedef void (*drm_osd_frame_done_cb_t)(void);

int sdl2_display_init(struct config_t *cfg);

/**
 * Push new I420 video frame (YUV420p).
 * NOTE: This function only copies data into g_video, but does not render.
 *       Rendering is done in sdl2_push_new_osd_frame().
 */
int sdl2_push_new_video_frame(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                              int width, int height,
                              int y_stride, int uv_stride);

/** Register callback which will be called after OSD frame is rendered */
void sdl2_set_osd_frame_done_callback(drm_osd_frame_done_cb_t cb);

/**
 * Push new OSD frame (ARGB8888) and render:
 *  - update YUV texture from latest video frame (if any)
 *  - update overlay texture from src_addr
 *  - compute destination rect to fit window, keep aspect ratio
 *  - render video + overlay
 *  - call OSD done callback
 *
 * NOTE: must be called from the same thread where SDL was initialized.
 */
int sdl2_push_new_osd_frame(const void *src_addr, int width, int height);

/**
 * Process SDL events and render a frame if needed.
 * Must be called regularly from the main thread (e.g. in main loop).
 *
 * Returns:
 *   0  - ok, continue
 *  <0  - user requested quit (window close or ESC)
 */
int sdl2_display_poll(void);
int sdl2_display_deinit(void);

#endif //VD_LINK_SDL2_DISPLAY_H
