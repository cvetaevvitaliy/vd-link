/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef SCREENSAVER_H
#define SCREENSAVER_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int width;          // frame width (must be even for NV12)
    int height;         // frame height (must be even for NV12)
    size_t size_bytes;  // width * height * 3 / 2
    uint8_t *data;      // Y followed by interleaved UV (Cb,Cr)
} screensaver_nv12_t;

/**
 * Create a solid-color NV12 image.
 * @param w,h  frame dimensions (must be even due to 4:2:0 chroma)
 * @param y    luma value (0..255), e.g. 0x10 for studio black
 * @param u    chroma Cb value (0..255), e.g. 0x80 for neutral
 * @param v    chroma Cr value (0..255), e.g. 0x80 for neutral
 * @param out  output descriptor; on success, out->data != NULL
 * @return 0 on success, -1 on error
 */
int screensaver_create_nv12_solid(int w, int h, uint8_t y, uint8_t u, uint8_t v, screensaver_nv12_t *out);

/**
 * Create a simple checkerboard pattern in NV12.
 * Useful to visually confirm correct stride/format.
 * @param block  block size in pixels (e.g. 32)
 * @param y0,u0,v0  color for even blocks
 * @param y1,u1,v1  color for odd blocks
 */
int screensaver_create_nv12_checker(int w, int h, int block,
                                    uint8_t y0, uint8_t u0, uint8_t v0,
                                    uint8_t y1, uint8_t u1, uint8_t v1,
                                    screensaver_nv12_t *out);

/* Prepare a "No Camera" screensaver NV12 image
*/
int screensaver_prepare_no_camera_screen(int width, int height, screensaver_nv12_t *out);
/**
 * Free buffer allocated by any screensaver_create_* function.
 * Safe to call with NULL or already-empty descriptor.
 */
void screensaver_free(screensaver_nv12_t *img);

#endif //SCREENSAVER_H
