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

