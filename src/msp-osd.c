/**
 * MSP-OSD port from https://github.com/fpv-wtf/msp-osd
 * @file msp-osd.c this is part of project 'vd-link'
 * Copyright © vitalii.nimych@gmail.com 2025
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
 * Created vitalii.nimych@gmail.com 06-07-2025
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>
#include <bits/pthreadtypes.h>
#include <stdatomic.h>
#include <pthread.h>
#include "msp-osd.h"
#include "msp/msp.h"
#include "msp/msp_displayport.h"
#include "util/debug.h"
#include "net/data_protocol.h"
#include "drm_display.h"
#include "font/font.h"
#include "toast/toast.h"
#include "fakehd/fakehd.h"

static pthread_t msp_thread;
static atomic_int running = 0;

static char current_fc_variant[5];

#define SPLASH_STRING "OSD WAITING..."
#define SHUTDOWN_STRING "SHUTTING DOWN..."

#define MAX_DISPLAY_X 60
#define MAX_DISPLAY_Y 22

#define BYTES_PER_PIXEL 4

static int display_width = 0;
static int display_height = 0;

static uint16_t msp_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static uint16_t msp_render_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static uint16_t overlay_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static displayport_vtable_t *display_driver;
struct timespec last_render;

static char current_fc_variant[5];

static uint8_t frame_waiting = 0;

// TODO: add support for different display modes FULL HD, HD, SD, etc.
static display_info_t sd_display_info = {
    .char_width = 30,
    .char_height = 15,
    .font_width = 36,
    .font_height = 54,
    .x_offset = 0,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t full_display_info = {
    .char_width = 60,
    .char_height = 22,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 0,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t hd_display_info = {
    .char_width = 50,
    .char_height = 18,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 0,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t overlay_display_info = {
    .char_width = 50,
    .char_height = 18,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 100,
    .y_offset = 650,
    .fonts = {NULL, NULL, NULL, NULL},
};

static enum display_mode_s {
    DISPLAY_DISABLED = 0,
    DISPLAY_RUNNING = 1,
    DISPLAY_WAITING = 2
} display_mode = DISPLAY_RUNNING;

static display_info_t *current_display_info;

static void draw_character(display_info_t *display_info, uint16_t character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y], uint32_t x, uint32_t y, uint16_t c)
{
    if ((x > (display_info->char_width - 1)) || (y > (display_info->char_height - 1))) {
        return;
    }
    character_map[x][y] = c;
}

static void display_print_string(uint8_t init_x, uint8_t y, const char *string, uint8_t len) {
    for(uint8_t x = 0; x < len; x++) {
        draw_character(&overlay_display_info, overlay_character_map, x + init_x, y, string[x]);
    }
}

static void msp_draw_character(uint32_t x, uint32_t y, uint16_t c) {
    draw_character(current_display_info, msp_character_map, x, y, c);
}

static void msp_clear_screen() {
    memset(msp_character_map, 0, sizeof(msp_character_map));
    memset(msp_render_character_map, 0, sizeof(msp_render_character_map));
}

static void clear_framebuffer() {
    void *fb_addr = drm_get_next_osd_fb();
    if (fb_addr == NULL) {
        DEBUG_PRINT("Failed to get framebuffer address\n");
        return;
    }
    // DJI has a backwards alpha channel - FF is transparent, 00 is opaque.
    memset(fb_addr, 0x00000000, display_width * display_height * BYTES_PER_PIXEL);
}

static void draw_character_map(display_info_t *display_info, void* restrict fb_addr, uint16_t character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y]) {
    if (display_info->fonts[0] == NULL) {
        DEBUG_PRINT("No font available, failed to draw.\n");
        return;
    }
    void* restrict font;
    for(int y = 0; y < display_info->char_height; y++) {
        for(int x = 0; x < display_info->char_width; x++) {
            uint16_t c = character_map[x][y];
            if (c != 0) {
                int page = (c & 0x300) >> 8;
                c = c & 0xFF;
                font = display_info->fonts[page];
                if(font == NULL) {
                    font = display_info->fonts[0];
                }
                uint32_t pixel_x = (x * display_info->font_width) + display_info->x_offset;
                uint32_t pixel_y = (y * display_info->font_height) + display_info->y_offset;
                uint32_t font_offset = (((display_info->font_height * display_info->font_width) * BYTES_PER_PIXEL) * c);
                uint32_t target_offset = ((pixel_x * BYTES_PER_PIXEL) + (pixel_y * display_width * BYTES_PER_PIXEL));
                for(uint8_t gy = 0; gy < display_info->font_height; gy++) {
                    for(uint8_t gx = 0; gx < display_info->font_width; gx++) {
                        *((uint8_t *)fb_addr + target_offset) = *(uint8_t *)((uint8_t *)font + font_offset + 2);
                        *((uint8_t *)fb_addr + target_offset + 1) = *(uint8_t *)((uint8_t *)font + font_offset + 1);
                        *((uint8_t *)fb_addr + target_offset + 2) = *(uint8_t *)((uint8_t *)font + font_offset);
                        *((uint8_t *)fb_addr + target_offset + 3) = *(uint8_t *)((uint8_t *)font + font_offset + 3);
                        font_offset += BYTES_PER_PIXEL;
                        target_offset += BYTES_PER_PIXEL;
                    }
                    target_offset += display_width * BYTES_PER_PIXEL - (display_info->font_width * BYTES_PER_PIXEL);
                }
                // DEBUG_PRINT("%c", c > 31 ? c : 20);
            }
            // DEBUG_PRINT(" ");
        }
        // DEBUG_PRINT("\n");
    }
}

static void draw_screen() {
    clear_framebuffer();

    void *fb_addr = drm_get_next_osd_fb();
    if (fb_addr== NULL) {
        DEBUG_PRINT("Failed to get framebuffer address\n");
        return;
    }

    if (fakehd_is_enabled()) {
        fakehd_map_sd_character_map_to_hd(msp_character_map, msp_render_character_map);
        draw_character_map(current_display_info, fb_addr, msp_render_character_map);
    } else {
        draw_character_map(current_display_info, fb_addr, msp_character_map);
    }
    draw_character_map(&overlay_display_info, fb_addr, overlay_character_map);
}

static void render_screen() {
    draw_screen();
    if (display_mode == DISPLAY_DISABLED) {
        clear_framebuffer();
    }
    drm_push_new_osd_frame();
    //DEBUG_PRINT("drew a frame\n");
    clock_gettime(CLOCK_MONOTONIC, &last_render);
}


static void msp_draw_complete()
{
    render_screen();
}

static void start_display(void)
{
    memset(msp_character_map, 0, sizeof(msp_character_map));
    memset(msp_render_character_map, 0, sizeof(msp_render_character_map));
    memset(overlay_character_map, 0, sizeof(overlay_character_map));

    display_print_string(0, 0, SPLASH_STRING, sizeof(SPLASH_STRING));
    msp_draw_complete();
}

static void msp_set_options(uint8_t font_num, msp_hd_options_e is_hd) {
    msp_clear_screen();

    switch (is_hd) {
    case MSP_HD_OPTION_60_22:
        fakehd_disable();
        current_display_info = &full_display_info;
        break;
    case MSP_HD_OPTION_50_18:
    case MSP_HD_OPTION_30_16:
        fakehd_disable();
        current_display_info = &hd_display_info;
        break;
    default:
        current_display_info = &sd_display_info;
        break;
    }
}

static void msp_callback(msp_msg_t *msp_message)
{
    displayport_process_message(display_driver, msp_message);
}

static void load_fonts(char* font_variant) {
    char file_path[255];
    get_font_path_with_extension(file_path, "font", ".png", 255, 0, font_variant);
    toast(file_path);
    load_font(&sd_display_info, font_variant);
    load_font(&hd_display_info, font_variant);
    load_font(&full_display_info, font_variant);
    load_font(&overlay_display_info, font_variant);
}

static void close_all_fonts() {
    close_font(&sd_display_info);
    close_font(&hd_display_info);
    close_font(&overlay_display_info);
    close_font(&full_display_info);
}

void fill_character_map_with_charset(uint16_t character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y], int char_w, int char_h) {
    int val = 0;
    for (int y = 0; y < char_h; y++) {
        for (int x = 0; x < char_w; x++) {
            character_map[x][y] = val++;
            if (val > 255)
                return;
        }
    }
}

static void* msp_osd_thread(void *arg)
{
    struct config_t *cfg = (struct config_t *)arg;
    printf("[ MSP OSD ] Starting MSP OSD thread\n");

    struct drm_context_t *drm_ctx = drm_get_ctx();
    if (drm_ctx == NULL) {
        printf("[ MSP OSD ] Failed to get DRM context, exiting thread\n");
        return NULL;
    }
    display_width = drm_ctx->display_info.hdisplay;
    display_height = drm_ctx->display_info.vdisplay;

    memset(current_fc_variant, 0, sizeof(current_fc_variant));

    toast_load_config();
    load_fakehd_config();

    fakehd_disable();
    current_display_info = &hd_display_info; // TODO: add presets for 1920x1080, 1280x720 display

    display_driver = calloc(1, sizeof(displayport_vtable_t));
    display_driver->draw_character = &msp_draw_character;
    display_driver->clear_screen = &msp_clear_screen;
    display_driver->draw_complete = &msp_draw_complete;
    display_driver->set_options = &msp_set_options;

    msp_state_t *msp_state = calloc(1, sizeof(msp_state_t));
    msp_state->cb = &msp_callback;

    load_fonts("btfl"); // TODO: all .png fonts should be cleaned of small dots for FULL HD modes

    start_display();
    usleep(100000);

    // test all characters
    uint8_t c = 0;
    for (int j = 0; j < MAX_DISPLAY_Y; ++j) {
        for (int i = 0; i < MAX_DISPLAY_X; ++i) {
            msp_draw_character(i, j, c++);
            if (c > 255)
                break;
        }
        if (c > 255)
            break;
    }
    msp_draw_complete();

    usleep(100000);

    int cnt = 0;
    char buffer[256];
    while (atomic_load(&running)) {
        cnt ++;
        sprintf(buffer, "TEST CNT %d", cnt);
        display_print_string(0, 1, buffer, strlen(buffer));
        msp_draw_complete();

        usleep(32000);  // Sleep for 32 ms to simulate frame rate 30FPS
    }

    free(display_driver);
    free(msp_state);

    close_all_fonts();

    printf("[ MSP OSD ] Stopped MSP OSD thread\n");

    return NULL;
}

int msp_osd_init(struct config_t *cfg)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong(&running, &expected, 1)) {
        printf("[ MSP OSD ] Already running thread\n");
        return -1;
    }
    return pthread_create(&msp_thread, NULL, msp_osd_thread, cfg);

}

void msp_osd_stop(void)
{
    if (!atomic_load(&running)) {
        printf("[ MSP OSD ] Not running, nothing to stop\n");
        return;
    }
    atomic_store(&running, 0);
    pthread_join(msp_thread, NULL);

}
