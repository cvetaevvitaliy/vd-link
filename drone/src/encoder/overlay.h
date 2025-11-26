/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef OVERLAY_H
#define OVERLAY_H
#include <stdint.h>


/* ARGB color helper: 0xAARRGGBB */
#define ARGB(a, r, g, b) \
( ((uint32_t)((a) & 0xFF) << 24) | \
((uint32_t)((r) & 0xFF) << 16) | \
((uint32_t)((g) & 0xFF) <<  8) | \
((uint32_t)((b) & 0xFF) <<  0) )

int overlay_init(void);
void overlay_deinit(void);
void overlay_clear(void);
void overlay_draw_rect(int x1, int y1, int x2, int y2, uint32_t argb_color, int thickness);
void overlay_draw_crosshair(int x, int y, int size,
                            uint32_t argb_color, int thickness);
void overlay_draw_text(int x, int y, const char *text,
                       uint32_t argb_color, int size);
void overlay_measure_text(const char *text, int size, int *width, int *height);
void overlay_draw_line(int x1, int y1, int x2, int y2,
                       uint32_t argb_color, int thickness);
int  overlay_push_to_encoder(void);
void overlay_get_overlay_size(int *width, int *height);

/* Optional: direct access to raw buffer if needed */
uint8_t *overlay_get_buffer(void);

#endif //OVERLAY_H
