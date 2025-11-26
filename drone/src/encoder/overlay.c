/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "overlay.h"
#include "encoder/encoder.h"

#define OVERLAY_BPP 4   /* ARGB_8888 = 4 bytes per pixel */
static uint8_t* overlay_buffer = NULL;
static int overlay_buffer_width = 0;
static int overlay_buffer_height = 0;

/* Global ARGB8888 overlay buffer */
//static uint8_t g_overlay_buffer[OVERLAY_BUFFER_SIZE];

/* Return pointer to internal buffer (if you need direct access) */
uint8_t *overlay_get_buffer(void)
{
    return overlay_buffer;
}

int overlay_init(void)
{
    encoder_config_t *enc_cfg = encoder_get_input_image_format(); // ensure encoder is initialized
    if (enc_cfg == NULL) {
        printf("[ OVERLAY ] ERROR: Failed to get encoder config\n");
    }
    if (enc_cfg->width <=0 || enc_cfg->height <=0) {
        printf("[ OVERLAY ] ERROR: Invalid encoder dimensions: %dx%d\n", enc_cfg->width, enc_cfg->height);
        return -1;
    }

    overlay_buffer_width = enc_cfg->width;
    overlay_buffer_height = enc_cfg->height;

    if (!overlay_buffer) {
        printf("[ OVERLAY ] Overlay buffer initialized");
        free(overlay_buffer);
        overlay_buffer = NULL;
    }

    overlay_buffer = malloc(overlay_buffer_width * overlay_buffer_height * OVERLAY_BPP);
    if (!overlay_buffer) {
        printf("[ OVERLAY ] ERROR: Failed to allocate overlay buffer\n");
        overlay_buffer_width = 0;
        overlay_buffer_height = 0;
        return -1;
    }

    overlay_clear();

    return 0;

}

void overlay_deinit(void)
{
    if (overlay_buffer) {
        free(overlay_buffer);
        overlay_buffer = NULL;
    }
}

/* Clear overlay: fully transparent black */
void overlay_clear(void)
{
    /* ARGB: 0x00000000 => A=0 (fully transparent) */
    memset(overlay_buffer, 0x00, (size_t)overlay_buffer_width * (size_t)overlay_buffer_height * OVERLAY_BPP);
}

/* Internal helper: set one pixel with clipping */
static inline void overlay_set_pixel(int x, int y, uint32_t argb)
{
    if (x < 0 || x >= overlay_buffer_width ||
        y < 0 || y >= overlay_buffer_height) {
        return;
    }

    /* ARGB_8888, 4 bytes per pixel, linear buffer */
    uint32_t *ptr = (uint32_t *)(overlay_buffer +
                                 (size_t)y * (overlay_buffer_width * OVERLAY_BPP) +
                                 (size_t)x * OVERLAY_BPP);
    *ptr = argb;
}

void overlay_draw_line(int x1, int y1, int x2, int y2,
                       uint32_t argb_color, int thickness)
{
    /* Bresenham's line algorithm with thickness */
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        // Draw pixel with thickness
        for (int tx = -thickness / 2; tx <= thickness / 2; ++tx) {
            for (int ty = -thickness / 2; ty <= thickness / 2; ++ty) {
                overlay_set_pixel(x1 + tx, y1 + ty, argb_color);
            }
        }

        if (x1 == x2 && y1 == y2) break;
        int err2 = err * 2;
        if (err2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (err2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/* Draw rectangle border (axis-aligned) with given thickness (>=1) */
void overlay_draw_rect(int x1, int y1, int x2, int y2,
                       uint32_t argb_color, int thickness)
{
    if (thickness <= 0)
        thickness = 1;

    /* Normalize coordinates: ensure x1 <= x2, y1 <= y2 */
    if (x1 > x2) { int tmp = x1; x1 = x2; x2 = tmp; }
    if (y1 > y2) { int tmp = y1; y1 = y2; y2 = tmp; }

    /* Clip rectangle to overlay bounds (rough clipping) */
    if (x2 < 0 || y2 < 0 ||
        x1 >= overlay_buffer_width || y1 >= overlay_buffer_height) {
        /* Completely outside */
        return;
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= overlay_buffer_width)  x2 = overlay_buffer_width  - 1;
    if (y2 >= overlay_buffer_height) y2 = overlay_buffer_height - 1;

    /* Draw border lines with thickness */
    for (int t = 0; t < thickness; ++t) {
        int yt_top    = y1 + t;
        int yt_bottom = y2 - t;
        int xt_left   = x1 + t;
        int xt_right  = x2 - t;

        /* If rectangle becomes inverted due to large thickness, stop */
        if (yt_top > yt_bottom || xt_left > xt_right)
            break;

        /* Top horizontal line */
        for (int x = xt_left; x <= xt_right; ++x) {
            overlay_set_pixel(x, yt_top, argb_color);
        }

        /* Bottom horizontal line */
        if (yt_bottom != yt_top) {
            for (int x = xt_left; x <= xt_right; ++x) {
                overlay_set_pixel(x, yt_bottom, argb_color);
            }
        }

        /* Left vertical line */
        for (int y = yt_top; y <= yt_bottom; ++y) {
            overlay_set_pixel(xt_left, y, argb_color);
        }

        /* Right vertical line */
        if (xt_right != xt_left) {
            for (int y = yt_top; y <= yt_bottom; ++y) {
                overlay_set_pixel(xt_right, y, argb_color);
            }
        }
    }
}

void overlay_draw_crosshair(int x, int y, int size,
                       uint32_t argb_color, int thickness)
{
    int half_size = size / 2;

    // Diagonal lines to form an X
    overlay_draw_line(x - half_size, y - half_size, x + half_size, y + half_size, argb_color, thickness);
    overlay_draw_line(x - half_size, y + half_size, x + half_size, y - half_size, argb_color, thickness);
}

void overlay_draw_text(int x, int y, const char *text,
                       uint32_t argb_color, int size)
{
    // Simple placeholder: draw a rectangle representing text area
    int text_width = (int)(size * 0.6f * strlen(text)); // Approximate width
    int text_height = size; // Approximate height

    overlay_draw_rect(x, y, x + text_width, y + text_height, argb_color, 1); // replace with actual text rendering
}

/* Send current overlay buffer to encoder */
int overlay_push_to_encoder(void)
{
    return encoder_draw_overlay_buffer(overlay_buffer,overlay_buffer_width, overlay_buffer_height);
}

void overlay_get_overlay_size(int *width, int *height)
{
    if (width)
        *width = overlay_buffer_width;
    if (height)
        *height = overlay_buffer_height;
}