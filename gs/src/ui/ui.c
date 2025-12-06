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
#include "msp-osd.h"
#include "lang/lang.h"
#include "lvgl/lvgl.h"

#ifdef PLATFORM_DESKTOP
#include "sdl2_display.h"
#include <sdl2_lvgl_input.h>
#endif

#ifdef PLATFORM_ROCKCHIP
#include "drm_display.h"
#include <rga/im2d_buffer.h>
#include <rga/im2d_single.h>
#include <rga/rga.h>
#endif

#include <string.h>
#include <unistd.h>
#include "screens/screens.h"
#include "lvgl/src/display/lv_display_private.h"

static void *lvgl_buf1 = NULL;
static void *lvgl_buf2 = NULL;
static lv_display_t *disp = NULL;
static pthread_t tick_tid;
static volatile bool tick_running = false;
static void *fb_addr = NULL;
static pthread_mutex_t lvgl_mutex = PTHREAD_MUTEX_INITIALIZER;
static float fps = 0;
static uint32_t server_ping = 0;

#ifdef PLATFORM_DESKTOP
static void blend_rgba8888_src_over(const uint32_t *src, uint32_t *dst, int width, int height)
{
    int count = width * height;

    for (int i = 0; i < count; ++i) {
        uint32_t s = src[i];
        uint32_t d = dst[i];

        uint8_t sa = (uint8_t)(s >> 24);
        if (sa == 0) {
            // fully transparent src -> keep dst
            continue;
        } else if (sa == 255) {
            // fully opaque src -> just replace
            dst[i] = s;
            continue;
        }

        uint8_t sr = (uint8_t)(s >> 16);
        uint8_t sg = (uint8_t)(s >> 8);
        uint8_t sb = (uint8_t)(s >> 0);

        uint8_t da = (uint8_t)(d >> 24);
        uint8_t dr = (uint8_t)(d >> 16);
        uint8_t dg = (uint8_t)(d >> 8);
        uint8_t db = (uint8_t)(d >> 0);

        uint8_t inv_sa = (uint8_t)(255 - sa);

        // Straight alpha SRC_OVER: out = src + dst * (1 - alpha_src)
        uint8_t out_r = (uint8_t)((sr * sa + dr * inv_sa + 127) / 255);
        uint8_t out_g = (uint8_t)((sg * sa + dg * inv_sa + 127) / 255);
        uint8_t out_b = (uint8_t)((sb * sa + db * inv_sa + 127) / 255);
        uint8_t out_a = (uint8_t)((sa + (da * inv_sa + 127) / 255));

        dst[i] = ((uint32_t)out_a << 24) | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | ((uint32_t)out_b << 0);
    }
}
#endif


static void *tick_thread(void *arg)
{
    (void)arg;
    printf("[ UI ] Tick thread started\n");
    struct timespec prev, now;
    clock_gettime(CLOCK_MONOTONIC, &prev);

    while (tick_running) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        pthread_mutex_lock(&lvgl_mutex);
        int ms = (int)(now.tv_sec - prev.tv_sec) * 1000 + (now.tv_nsec - prev.tv_nsec) / 1000000;
        if (ms > 0) {
            lv_tick_inc(ms);
            prev = now;
        }
        lv_timer_handler();
        pthread_mutex_unlock(&lvgl_mutex);
        usleep(1000);
    }

    printf("[ UI ] Tick thread exiting\n");
    return NULL;
}

static void ui_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map_u8)
{
    if (!tick_running) return;
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
                if (color.alpha < 32) {
                    fb[fb_offset + 3] = 0;
                } else {
                    fb[fb_offset + 3] = color.alpha;
                }
            }
        }
    }
#ifdef PLATFORM_ROCKCHIP
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

    rga_buffer_t src_osd = wrapbuffer_virtualaddr(osd_buf,  width, height, RK_FORMAT_RGBA_8888);
    rga_buffer_t dst = wrapbuffer_virtualaddr(dst_buf,  width, height, RK_FORMAT_RGBA_8888);

    IM_STATUS ret = imblend(src_osd, dst, IM_ALPHA_BLEND_SRC_OVER);

    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA: imblend failed: %d\n", ret);
        return;
    }

    // Push the new squashed frame to DRM
    drm_push_new_osd_frame(fb_addr, LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);
#endif

#ifdef PLATFORM_DESKTOP
    // Squash OSD framebuffer with LVGL framebuffer in software
    void *osd_buf = msp_osd_get_fb_addr();
    if (osd_buf == NULL) {
        // No MSP OSD buff - draw only LVGL framebuffer
        sdl2_push_new_osd_frame(fb_addr, LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);
        return;
    }

    // osd_buf and fb_addr same RGBA8888/BGRA8888 size!!!
    blend_rgba8888_src_over((const uint32_t *)osd_buf, (uint32_t *)fb_addr,LVGL_BUFF_WIDTH,LVGL_BUFF_HEIGHT);
    sdl2_push_new_osd_frame(fb_addr, LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);
    lv_display_flush_ready(disp); // thread safe call
#endif
}

void drm_osd_frame_done_cb(void)
{
#ifdef PLATFORM_ROCKCHIP
    lv_display_flush_ready(disp);
#endif
}

int ui_init(void)
{
    pthread_mutex_lock(&lvgl_mutex);
    lv_init();

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

    printf("[ UI ] Initialized LVGL display with size %dx%d\n", LVGL_BUFF_WIDTH, LVGL_BUFF_HEIGHT);

    lv_display_set_flush_cb(disp, ui_flush_cb);
    pthread_mutex_unlock(&lvgl_mutex);
#ifdef PLATFORM_ROCKCHIP
    drm_set_osd_frame_done_callback(drm_osd_frame_done_cb);
#endif

#ifdef PLATFORM_DESKTOP
    sdl2_set_osd_frame_done_callback(drm_osd_frame_done_cb);
#endif

#ifdef PLATFORM_DESKTOP
    if (sdl2_lvgl_input_init() != 0) {
        printf("SDL2 input LVGL initialization failed\n");
    }
#endif

    //lang_set_english();

    lang_set_ukrainian();

    // Create a transparent background style
    static lv_style_t style_transp_bg;
    lv_style_init(&style_transp_bg);
    lv_style_set_bg_opa(&style_transp_bg, LV_OPA_TRANSP);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_font(lv_scr_act(), &montserrat_cyrillic_medium_20, LV_STYLE_STATE_CMP_SAME);
    lv_obj_add_style(lv_screen_active(), &style_transp_bg, LV_STYLE_STATE_CMP_SAME);
    lv_obj_set_style_text_font(disp->perf_label, &montserrat_cyrillic_medium_16, LV_STYLE_STATE_CMP_SAME);

    /* Disable scrolling and scrollbars on root screen */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    screens_init();
#if 0
    lv_obj_t *black_square = lv_obj_create(lv_scr_act());
    lv_obj_set_size(black_square, 170, 60);
    lv_obj_align(black_square, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_bg_color(black_square, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(black_square, LV_OPA_50, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(black_square);
    lv_label_set_text(label,  lang_get_str(STR_HELLO));
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *red_square = lv_obj_create(lv_scr_act());
    lv_obj_set_size(red_square, 50, 50);
    lv_obj_align(red_square, LV_ALIGN_BOTTOM_MID, -60, 0);
    lv_obj_set_style_bg_color(red_square, lv_color_make(255, 0, 0 ), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(red_square, 200, LV_PART_MAIN);

    // Create a green square
    lv_obj_t *green_square = lv_obj_create(lv_scr_act());
    lv_obj_set_size(green_square, 50, 50);
    lv_obj_align(green_square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(green_square, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(green_square, 200, LV_PART_MAIN);

    // Create a blue square
    lv_obj_t *blue_square = lv_obj_create(lv_scr_act());
    lv_obj_set_size(blue_square, 50, 50);
    lv_obj_align(blue_square, LV_ALIGN_BOTTOM_MID, 60, 0);
    lv_obj_set_style_bg_color(blue_square, lv_color_make(0, 0, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(blue_square, 128, LV_PART_MAIN);
#endif

    tick_running = true;
    pthread_create(&tick_tid, NULL, tick_thread, NULL);
    return 0;
}

void ui_deinit(void)
{
    if (!tick_running) {
        printf("[ UI ] Not running, nothing to stop\n");
        return;
    }
#if defined(PLATFORM_DESKTOP)
    sdl2_lvgl_input_deinit();
#endif
    pthread_mutex_lock(&lvgl_mutex);
    if (lv_is_initialized()) {
        lv_deinit();
    }
    pthread_mutex_unlock(&lvgl_mutex);

    tick_running = false;
    pthread_join(tick_tid, NULL);
    pthread_mutex_destroy(&lvgl_mutex);

    if (lvgl_buf1) {
        free(lvgl_buf1);
        lvgl_buf1 = NULL;
    }
    if (lvgl_buf2) {
        free(lvgl_buf2);
        lvgl_buf2 = NULL;
    }

    if (fb_addr) {
        free(fb_addr);
        fb_addr = NULL;
    }

}

float ui_get_fps(void)
{
    return fps;
}

void ui_set_fps(float data)
{
    fps = data;
}

void ui_set_server_ping(uint32_t data)
{
    server_ping = data;
}

void perf_update_timer_cb(lv_timer_t * t)
{
    lv_display_t * disp = lv_timer_get_user_data(t);

    uint32_t LV_SYSMON_GET_IDLE(void);

    lv_sysmon_perf_info_t * info = &disp->perf_sysmon_info;
    info->calculated.run_cnt++;

    uint32_t time_since_last_report = lv_tick_elaps(info->measured.last_report_timestamp);
    uint32_t disp_refr_period = LV_DEF_REFR_PERIOD;

    info->calculated.fps = info->measured.refr_interval_sum ? (1000 * info->measured.refr_cnt / time_since_last_report) : 0;
    info->calculated.fps = LV_MIN(info->calculated.fps,
                                  1000 / disp_refr_period);

    info->calculated.cpu = 100 - LV_SYSMON_GET_IDLE();
    info->calculated.refr_avg_time = info->measured.refr_cnt ? (info->measured.refr_elaps_sum / info->measured.refr_cnt) :
                                     0;

    info->calculated.cpu_avg_total = ((info->calculated.cpu_avg_total * (info->calculated.run_cnt - 1)) +
                                      info->calculated.cpu) / info->calculated.run_cnt;
    info->calculated.fps_avg_total = ((info->calculated.fps_avg_total * (info->calculated.run_cnt - 1)) +
                                      info->calculated.fps) / info->calculated.run_cnt;

    lv_subject_set_pointer(&disp->perf_sysmon_backend.subject, info);

    lv_sysmon_perf_info_t prev_info = *info;
    lv_memzero(info, sizeof(lv_sysmon_perf_info_t));
    info->measured.refr_start = prev_info.measured.refr_start;
    info->calculated.cpu_avg_total = prev_info.calculated.cpu_avg_total;
    info->calculated.fps_avg_total = prev_info.calculated.fps_avg_total;
    info->calculated.run_cnt = prev_info.calculated.run_cnt;

    info->measured.last_report_timestamp = lv_tick_get();
}

void perf_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    const lv_sysmon_perf_info_t * perf = lv_subject_get_pointer(subject);

    lv_obj_t * label = lv_observer_get_target(observer);

    lv_label_set_text_fmt(
        label,
        "%s: %" LV_PRIu32" FPS UI: %d FPS\n%" LV_PRIu32 "%% CPU\n"
        "%s: %" LV_PRIu32" ms",
        lang_get_str(STR_VIDEO), (uint32_t)fps, perf->calculated.fps, perf->calculated.cpu, lang_get_str(STR_SERVER_PING), server_ping
    );
}
