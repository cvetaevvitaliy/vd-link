/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
#include "ui.h"
#include "ui_interface.h"
#include "drm_display.h"
#include "msp-osd.h"
#include "lvgl/lvgl.h"
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/rga.h>
#include "menu.h"

#include <string.h>
#include <unistd.h>

#define LVGL_BUFF_WIDTH 1280
#define LVGL_BUFF_HEIGHT 720

static void *lvgl_buf1 = NULL;
static void *lvgl_buf2 = NULL;
static lv_display_t *disp = NULL;
static pthread_t tick_tid;
static int tick_running = 1;
static void *fb_addr = NULL;

static void *tick_thread(void *arg)
{
    (void)arg;
    printf("[ UI ] Tick thread started\n");
    struct timespec prev, now;
    clock_gettime(CLOCK_MONOTONIC, &prev);

    while (atomic_load(&tick_running)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int ms = (int)(now.tv_sec - prev.tv_sec) * 1000 + (now.tv_nsec - prev.tv_nsec) / 1000000;
        if (ms > 0) {
            lv_tick_inc(ms);
            prev = now;
        }
        lv_timer_handler();
        usleep(1000);
    }

    printf("[ UI ] Tick thread exiting\n");
    return NULL;
}

static void ui_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map_u8)
{
   //printf("[ UI ] Flush callback called for area: (%d, %d) - (%d, %d)\n", area->x1, area->y1, area->x2, area->y2);

    if (fb_addr == NULL) {
        printf("[ UI ] Failed to get OSD framebuffer address\n");
        return;
    }

    const lv_color32_t *px_map = (const lv_color32_t *)px_map_u8;
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    uint8_t *fb = (uint8_t *)fb_addr;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            lv_color32_t color = px_map[y * w + x];

            int32_t src_x = area->x1 + x;
            int32_t src_y = area->y1 + y;

            int32_t dst_x = src_x;
            int32_t dst_y = src_y;


            if (dst_x >= 0 && dst_x < LVGL_BUFF_WIDTH &&
                dst_y >= 0 && dst_y < LVGL_BUFF_HEIGHT) {

                size_t fb_offset = (dst_y * LVGL_BUFF_WIDTH + dst_x) * 4;

                // ARGB8888 → BGRA8888
                fb[fb_offset + 0] = color.blue;
                fb[fb_offset + 1] = color.green;
                fb[fb_offset + 2] = color.red;
                fb[fb_offset + 3] = color.alpha;
                //fb[fb_offset + 3] = 0x00;
            }
        }
    }

    // Squash OSD framebuffer with LVGL framebuffer
    void *osd_buf = msp_osd_get_fb_addr();
    if (osd_buf == NULL) {
        // push only LVGL framebuffer if OSD is not available
        drm_push_new_osd_frame(fb_addr, LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);
        return;
    }
    void *dst_buf = fb_addr;

    int width = LVGL_BUFF_WIDTH;
    int height = LVGL_BUFF_HEIGHT;

    rga_buffer_t src_osd = wrapbuffer_virtualaddr(osd_buf,  width, height, RK_FORMAT_BGRA_8888);
    rga_buffer_t dst = wrapbuffer_virtualaddr(dst_buf,  width, height, RK_FORMAT_BGRA_8888);

    IM_STATUS ret = imblend(src_osd, dst, IM_ALPHA_BLEND_SRC_OVER);

    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA: imblend failed: %d\n", ret);
    }

    // Push the new squashed frame to DRM
    drm_push_new_osd_frame(fb_addr, LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);
}

void drm_osd_frame_done_cb(void)
{
    // memset(lvgl_buf1, 0x00, LVGL_BUFF_WIDTH * LVGL_BUFF_HEIGHT * 4);
    // memset(lvgl_buf2, 0x00, LVGL_BUFF_WIDTH * LVGL_BUFF_HEIGHT * 4);
    lv_display_flush_ready(disp);
}

int ui_init(void)
{
    lv_init();

    atomic_store(&tick_running, 1);
    pthread_create(&tick_tid, NULL, tick_thread, NULL);

    int width = LVGL_BUFF_WIDTH;
    int height = LVGL_BUFF_HEIGHT;

    fb_addr = malloc(width * height * 4);
    if (fb_addr == NULL) {
        printf("[ UI ] Failed to allocate framebuffer memory\n");
        return -1;
    }

    disp = lv_display_create(width, height);

    size_t fb_size = LVGL_BUFF_WIDTH * LVGL_BUFF_HEIGHT * sizeof(lv_color32_t);

    lvgl_buf1 = malloc(fb_size);
    lvgl_buf2 = malloc(fb_size);

    if (!lvgl_buf1 || !lvgl_buf2) {
        printf("[ UI ] Failed to allocate LVGL buffers\n");
        if (lvgl_buf1)
            free(lvgl_buf1);
        if (lvgl_buf2)
            free(lvgl_buf2);
        return -1;
    }

    lv_display_set_buffers(disp, lvgl_buf1, lvgl_buf2, fb_size, LV_DISPLAY_RENDER_MODE_FULL);
    
    // Set color format to support alpha channel properly in LVGL 9
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);

    printf("[ UI ] Initialized LVGL display with size %dx%d\n", LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);

    lv_display_set_flush_cb(disp, ui_flush_cb);

    drm_set_osd_frame_done_callback(drm_osd_frame_done_cb);

    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_0, LV_PART_MAIN);

    // init_joystick();
    menu_create(lv_scr_act());

    ui_interface_init(disp);

    return 0;
}

void ui_deinit(void)
{
    atomic_store(&tick_running, 0);
    pthread_join(tick_tid, NULL);

    if (lvgl_buf1)
        free(lvgl_buf1);
    if (lvgl_buf2)
        free(lvgl_buf2);

    lv_deinit();
    if (fb_addr) {
        free(fb_addr);
        fb_addr = NULL;
    }
}
