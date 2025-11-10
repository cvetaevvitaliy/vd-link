/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "screensaver.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int alloc_nv12(int w, int h, screensaver_nv12_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (w <= 0 || h <= 0 || (w & 1) || (h & 1)) {
        fprintf(stderr, "screensaver: NV12 requires even WxH (got %dx%d)\n", w, h);
        return -1;
    }
    size_t y = (size_t)w * (size_t)h;
    size_t uv = y / 2; // NV12: interleaved UV (4:2:0)
    size_t sz = y + uv;

    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) {
        fprintf(stderr, "screensaver: OOM (%zu bytes)\n", sz);
        return -1;
    }

    out->width = w;
    out->height = h;
    out->size_bytes = sz;
    out->data = buf;
    return 0;
}

int screensaver_create_nv12_solid(int w, int h, uint8_t y, uint8_t u, uint8_t v, screensaver_nv12_t *out)
{
    if (alloc_nv12(w, h, out) != 0) return -1;

    size_t y_size = (size_t)w * (size_t)h;
    size_t uv_size = y_size / 2;

    // Fill Y plane
    memset(out->data, y, y_size);

    // Fill interleaved UV plane
    uint8_t *uvp = out->data + y_size;
    for (size_t i = 0; i < uv_size; i += 2) {
        uvp[i + 0] = u; // Cb
        uvp[i + 1] = v; // Cr
    }
    return 0;
}

int screensaver_create_nv12_checker(int w, int h, int block,
                                    uint8_t y0, uint8_t u0, uint8_t v0,
                                    uint8_t y1, uint8_t u1, uint8_t v1,
                                    screensaver_nv12_t *out)
{
    if (block < 2) block = 2; // minimal block to respect subsampling
    if (alloc_nv12(w, h, out) != 0) return -1;

    size_t y_stride = (size_t)w;
    size_t y_size = y_stride * (size_t)h;
    uint8_t *Y = out->data;
    uint8_t *UV = out->data + y_size;

    // Luma checker
    for (int j = 0; j < h; ++j) {
        uint8_t *row = Y + (size_t)j * y_stride;
        int by = (j / block) & 1;
        for (int i = 0; i < w; ++i) {
            int bx = (i / block) & 1;
            int sel = (bx ^ by);
            row[i] = sel ? y1 : y0;
        }
    }

    // Chroma checker (subsampled 2x2): each UV sample covers a 2x2 luma block
    int cw = w / 2;
    int ch = h / 2;
    for (int j = 0; j < ch; ++j) {
        uint8_t *row = UV + (size_t)j * cw * 2; // *2 because interleaved
        int by = ((j*2) / block) & 1; // scale back to luma coordinates
        for (int i = 0; i < cw; ++i) {
            int bx = ((i*2) / block) & 1;
            int sel = (bx ^ by);
            row[i*2 + 0] = sel ? u1 : u0; // Cb
            row[i*2 + 1] = sel ? v1 : v0; // Cr
        }
    }
    return 0;
}

void screensaver_free(screensaver_nv12_t *img)
{
    if (!img) return;
    if (img->data) {
        free(img->data);
        img->data = NULL;
    }
    img->width = img->height = 0;
    img->size_bytes = 0;
}

// No camera icon bitmap (32x32 pixels)
static const uint8_t no_camera_bitmap[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20, 
    0x20, 0x00, 0x00, 0x40, 0x10, 0x00, 0x00, 0x80, 0x08, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 
    0x02, 0x00, 0x04, 0x00, 0x1f, 0xff, 0xff, 0x80, 0x10, 0x80, 0x10, 0x82, 0x10, 0x40, 0x20, 0x86, 
    0x10, 0x20, 0x40, 0x8a, 0x10, 0x10, 0x80, 0x92, 0x10, 0x09, 0x00, 0xa2, 0x10, 0x06, 0x00, 0xc2, 
    0x10, 0x06, 0x00, 0xc2, 0x10, 0x09, 0x00, 0xa2, 0x10, 0x10, 0x80, 0x92, 0x10, 0x20, 0x40, 0x8a, 
    0x10, 0x40, 0x20, 0x86, 0x10, 0x80, 0x10, 0x82, 0x1f, 0xff, 0xff, 0x80, 0x02, 0x00, 0x04, 0x00, 
    0x04, 0x00, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x80, 0x20, 0x00, 0x00, 0x40, 
    0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Helper function to draw a thick pixel (NxN square)
static void draw_thick_pixel_nv12(screensaver_nv12_t *img, int x, int y, int thickness,
                                 uint8_t y_col, uint8_t u_col, uint8_t v_col) {
    if (!img || !img->data) return;
    
    size_t y_size = (size_t)img->width * (size_t)img->height;
    uint8_t *y_plane = img->data;
    uint8_t *uv_plane = img->data + y_size;
    
    int half_thick = thickness / 2;
    
    for (int dy = -half_thick; dy <= half_thick; dy++) {
        for (int dx = -half_thick; dx <= half_thick; dx++) {
            int px = x + dx;
            int py = y + dy;
            
            // Check bounds
            if (px >= 0 && px < img->width && py >= 0 && py < img->height) {
                // Draw Y pixel
                y_plane[py * img->width + px] = y_col;
                
                // Draw UV pixels (subsampled)
                if ((px & 1) == 0 && (py & 1) == 0) {
                    int uv_x = px / 2;
                    int uv_y = py / 2;
                    int uv_stride = img->width / 2;
                    
                    if (uv_x < uv_stride && uv_y < (img->height / 2)) {
                        int uv_offset = (uv_y * uv_stride + uv_x) * 2;
                        uv_plane[uv_offset + 0] = u_col;
                        uv_plane[uv_offset + 1] = v_col;
                    }
                }
            }
        }
    }
}

int screensaver_add_no_camera_bmp_nv12(screensaver_nv12_t *img, const char *text, int x, int y,
                                       uint8_t y_col, uint8_t u_col, uint8_t v_col)
{
    // Draw the "No Camera" bitmap icon
    if (!img || !img->data) return -1;
    
    // Bitmap parameters
    const int bitmap_width = 32;   // Original bitmap is 32x32 pixels
    const int bitmap_height = 32;
    const int scale_factor = 8;    // Scale up 8x for better visibility
    const int line_thickness = 3;  // Make lines thicker
    
    // Calculate scaled dimensions
    int scaled_width = bitmap_width * scale_factor;
    int scaled_height = bitmap_height * scale_factor;
    
    // Center the icon if x,y are at center of image
    int start_x = x - scaled_width / 2;
    int start_y = y - scaled_height / 2;
    
    // Draw the bitmap
    for (int row = 0; row < bitmap_height; row++) {
        for (int col = 0; col < bitmap_width; col++) {
            // Get the byte index and bit position
            int byte_index = (row * bitmap_width + col) / 8;
            int bit_index = 7 - ((row * bitmap_width + col) % 8);
            
            // Check if this pixel should be drawn
            if (no_camera_bitmap[byte_index] & (1 << bit_index)) {
                // Calculate scaled pixel position
                int pixel_x = start_x + (col * scale_factor);
                int pixel_y = start_y + (row * scale_factor);
                
                // Draw scaled pixel block with thickness
                for (int sy = 0; sy < scale_factor; sy++) {
                    for (int sx = 0; sx < scale_factor; sx++) {
                        draw_thick_pixel_nv12(img, 
                                             pixel_x + sx, 
                                             pixel_y + sy, 
                                             line_thickness,
                                             y_col, u_col, v_col);
                    }
                }
            }
        }
    }

    return 0;
}

int screensaver_prepare_no_camera_screen(int width, int height, screensaver_nv12_t *out)
{
    int ret = screensaver_create_nv12_solid(width, height,
                                    0x10 /*Y black*/, 0x80 /*U*/, 0x80 /*V*/,
                                    out);
    if (ret != 0) {
        printf("Failed to create screensaver frame\n");
        return ret;
    }
    // Add "Camera Not Found" text to screensaver
    ret = screensaver_add_no_camera_bmp_nv12(out, "Camera Not Found", 
                                width / 2 - 100, height / 2 - 10,
                                0xFF, 0x80, 0x80); // White text
    if (ret != 0) {
        printf("Failed to add text to screensaver\n");
        screensaver_free(out);
        return ret; 
    }
    return 0;
}
