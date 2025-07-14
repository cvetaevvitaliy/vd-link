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
#include "wfb_status_link.h"
#include "font/font.h"
#include "toast/toast.h"
#include "fakehd/fakehd.h"

#define DEBUG_PRINT_LINK 0

static pthread_t msp_thread;
static atomic_int running = 0;

static char current_fc_variant[5];

#define SPLASH_STRING "OSD WAITING..."
#define SHUTDOWN_STRING "SHUTTING DOWN..."

#define MAX_DISPLAY_X 53
#define MAX_DISPLAY_Y 20

#define BYTES_PER_PIXEL 4

static int display_width = 0;
static int display_height = 0;
static int rotation = 0; // 0, 90, 180, 270

static uint16_t msp_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static uint16_t msp_render_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static uint16_t overlay_character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y];
static displayport_vtable_t *display_driver;
struct timespec last_render;
static volatile bool need_render = false;

static void render_display(void);
static void need_render_display(void);

static char current_fc_variant[5];

// TODO: add support for different display modes FULL HD, HD, SD, etc.
static display_info_t sd_display_info = {
    .char_width = 53,
    .char_height = 20,
    .font_width = 36,
    .font_height = 54,
    .x_offset = 0,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t full_display_info = {
    .char_width = 53,
    .char_height = 20,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 0,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t hd_display_info = {
    .char_width = 53,
    .char_height = 20,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 5,
    .y_offset = 0,
    .fonts = {NULL, NULL, NULL, NULL},
};

static display_info_t overlay_display_info = {
    .char_width = 53,
    .char_height = 20,
    .font_width = 24,
    .font_height = 36,
    .x_offset = 5,
    .y_offset = 0,
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

static void display_print_string(uint8_t init_x, uint8_t y, const char *string, uint8_t len)
{
    for(uint8_t x = 0; x < len; x++) {
        draw_character(&overlay_display_info, overlay_character_map, x + init_x, y, string[x]);
    }
}

static void msp_draw_character(uint32_t x, uint32_t y, uint16_t c)
{
    draw_character(current_display_info, msp_character_map, x, y, c);
}

static void msp_clear_screen(void)
{
    memset(msp_character_map, 0, sizeof(msp_character_map));
    memset(msp_render_character_map, 0, sizeof(msp_render_character_map));
}

static void clear_framebuffer(void)
{
    void *fb_addr = drm_get_next_osd_fb();
    if (fb_addr == NULL) {
        DEBUG_PRINT("Failed to get framebuffer address\n");
        return;
    }
    // DJI has a backwards alpha channel - FF is transparent, 00 is opaque.
    memset(fb_addr, 0, display_width * display_height * BYTES_PER_PIXEL);
}

static void draw_character_map(display_info_t *display_info, void* restrict fb_addr, uint16_t character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y])
{
    if (display_info->fonts[0] == NULL) {
        DEBUG_PRINT("No font available, failed to draw.\n");
        return;
    }

    int fb_w = display_width;
    int fb_h = display_height;
    int osd_w = display_info->char_width * display_info->font_width;
    int osd_h = display_info->char_height * display_info->font_height;
    int x_offset = display_info->x_offset;
    int y_offset = display_info->y_offset;

    int rx_min = 0, ry_min = 0;

    switch (rotation) {
    case 0:
        rx_min = 0;
        ry_min = 0;
        break;
    case 90:
        rx_min = fb_h - (osd_h + y_offset * 2);
        ry_min = -x_offset;
        break;
    case 180:
        rx_min = fb_w - (osd_w + x_offset * 2);
        ry_min = fb_h - (osd_h + y_offset * 2);
        break;
    case 270:
        x_offset = -x_offset;
        y_offset = -y_offset;
        rx_min = y_offset;
        ry_min = fb_w - (osd_w + x_offset * 2) - (fb_h - osd_w);
        break;
    }
#if 0 // Debugging rotation and offsets
    printf("rx_min=%d, ry_min=%d\n", rx_min, ry_min);
    printf("Drawing character map with rotation %d, fb_w=%d, fb_h=%d, osd_w=%d, osd_h=%d, x_offset=%d, y_offset=%d\n", rotation, fb_w, fb_h, osd_w, osd_h, x_offset, y_offset);
#endif

    for (int y = 0; y < display_info->char_height; y++) {
        for (int x = 0; x < display_info->char_width; x++) {
            uint16_t c = character_map[x][y];
            if (c == 0) continue;

            int page = (c & 0x300) >> 8;
            c = c & 0xFF;
            void* font = display_info->fonts[page];
            if (!font) font = display_info->fonts[0];

            uint32_t src_x = x * display_info->font_width + x_offset;
            uint32_t src_y = y * display_info->font_height + y_offset;

            for (uint8_t gy = 0; gy < display_info->font_height; gy++) {
                for (uint8_t gx = 0; gx < display_info->font_width; gx++) {
                    uint32_t px = src_x + gx;
                    uint32_t py = src_y + gy;

                    int rx = 0, ry = 0;
                    switch (rotation) {
                    case 0:
                        rx = px - rx_min;
                        ry = py - ry_min;
                        break;
                    case 90:
                        rx = fb_h - 1 - py - rx_min;
                        ry = px - ry_min;
                        break;
                    case 180:
                        rx = fb_w - 1 - px - rx_min;
                        ry = fb_h - 1 - py - ry_min;
                        break;
                    case 270:
                        rx = py - rx_min;
                        ry = fb_w - 1 - px - ry_min;
                        break;
                    default:
                        rx = px - rx_min;
                        ry = py - ry_min;
                        break;
                    }

                    if (rx < 0 || ry < 0 || rx >= fb_w || ry >= fb_h) continue;

                    uint32_t fb_offset = (ry * fb_w + rx) * BYTES_PER_PIXEL;
                    uint32_t font_offset = (((display_info->font_height * display_info->font_width) * BYTES_PER_PIXEL) * c) + (gy * display_info->font_width + gx) * BYTES_PER_PIXEL;

                    *((uint8_t *)fb_addr + fb_offset + 0) = *((uint8_t *)font + font_offset + 2); // B
                    *((uint8_t *)fb_addr + fb_offset + 1) = *((uint8_t *)font + font_offset + 1); // G
                    *((uint8_t *)fb_addr + fb_offset + 2) = *((uint8_t *)font + font_offset + 0); // R
                    *((uint8_t *)fb_addr + fb_offset + 3) = *((uint8_t *)font + font_offset + 3); // A
                }
            }
        }
    }
}

static void draw_screen(void)
{
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

static void render_screen(void)
{
    draw_screen();
    if (display_mode == DISPLAY_DISABLED) {
        clear_framebuffer();
    }
    drm_push_new_osd_frame();
    //DEBUG_PRINT("drew a frame\n");
    clock_gettime(CLOCK_MONOTONIC, &last_render);
}


static void msp_draw_complete(void)
{
    render_screen();
}

static void start_display(void)
{
    memset(msp_character_map, 0, sizeof(msp_character_map));
    memset(msp_render_character_map, 0, sizeof(msp_render_character_map));
    memset(overlay_character_map, 0, sizeof(overlay_character_map));

    display_print_string(MAX_DISPLAY_X - sizeof(SPLASH_STRING), MAX_DISPLAY_Y - 1, SPLASH_STRING, sizeof(SPLASH_STRING));
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

static void load_fonts(char* font_variant)
{
    char file_path[255];
    get_font_path_with_extension(file_path, "font", ".png", 255, 0, font_variant);
    toast(file_path);
    load_font(&sd_display_info, font_variant);
    load_font(&hd_display_info, font_variant);
    load_font(&full_display_info, font_variant);
    load_font(&overlay_display_info, font_variant);
}

static void close_all_fonts(void)
{
    close_font(&sd_display_info);
    close_font(&hd_display_info);
    close_font(&overlay_display_info);
    close_font(&full_display_info);
}

void fill_character_map_with_charset(uint16_t character_map[MAX_DISPLAY_X][MAX_DISPLAY_Y], int char_w, int char_h)
{
    int val = 0;
    for (int y = 0; y < char_h; y++) {
        for (int x = 0; x < char_w; x++) {
            character_map[x][y] = val++;
            if (val > 255)
                return;
        }
    }
}

#define CHAR_LINK_LQ "\x7B"
#define CHAR_LINK_BW "\x70"
void wfb_status_link_callback(const wfb_rx_status *st)
{
    char str[128];
    int len = 0;
    if (st->ants_count > 0) {
        memset(overlay_character_map, 0, sizeof(overlay_character_map));
        // freq, link quality symbol, rssi_avg, bitrate
        len = snprintf(str, sizeof(str), "%d " CHAR_LINK_BW "%.1f " CHAR_LINK_LQ "%d",
                       (int)st->ants[0].freq,
                       st->ants[0].bitrate_mbps,
                       (int)st->ants[0].rssi_avg);

        for (int i = 1; i < st->ants_count; ++i) {
            // Additional antennas, only RSSI
            int l = snprintf(str + len, sizeof(str) - len,
                             " " CHAR_LINK_LQ "%d", (int)st->ants[i].rssi_avg);
            if (l > 0 && l < (int)(sizeof(str) - len)) len += l;
        }
        display_print_string(0, MAX_DISPLAY_Y - 1, str, strlen(str));
        need_render_display();  // TODO: need to synchronize with MSP data draw_complete
    }

#if DEBUG_PRINT_LINK
    for (int i = 0; i < st->ants_count; ++i) {
        printf("[MSP OSD] WFB status link ant[%d]: freq=%lld mcs=%lld bw=%lld ant_id=%lld pkt_delta=%lld bitrate=%.1f rssi=[%lld/%lld/%lld] snr=[%lld/%lld/%lld]\n",
               i,
               st->ants[i].freq, st->ants[i].mcs, st->ants[i].bw, st->ants[i].ant_id,
               st->ants[i].pkt_delta,
               st->ants[i].bitrate_mbps,
               st->ants[i].rssi_min, st->ants[i].rssi_avg, st->ants[i].rssi_max,
               st->ants[i].snr_min, st->ants[i].snr_avg, st->ants[i].snr_max);
    }
#endif
}

static void need_render_display(void)
{
    if (need_render) {
        return; // already scheduled
    }
    need_render = true;
    clock_gettime(CLOCK_MONOTONIC, &last_render);
}

static void render_display(void)
{
    msp_draw_complete();
    need_render = false;
}

static void* msp_osd_thread(void *arg)
{
    struct config_t *cfg = (struct config_t *)arg;
    printf("[ MSP OSD ] Starting MSP OSD thread\n");

    if (drm_get_osd_frame_size(&display_width, &display_height, &rotation) < 0) {
        printf("[ MSP OSD ] Failed to get OSD frame size\n");
        return NULL;
    }
    printf("[ MSP OSD ] OSD frame size: %dx%d, rotation: %d\n", display_width, display_height, rotation);

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

    load_fonts("btfl");

    start_display();
    usleep(100000);

    wfb_status_link_start(cfg->ip, cfg->wfb_port, wfb_status_link_callback);

#if 0    // test all characters
    uint8_t c = 0;
    for (int j = 0; j < MAX_DISPLAY_Y; ++j) {
        for (int i = 0; i < MAX_DISPLAY_X; ++i) {
            msp_draw_character(i, j,c);
            c++;
            if (c > 255)
                c = 0;
        }
    }
    usleep(100000);
    msp_draw_complete();
#endif
    while (atomic_load(&running)) {
        // noting now
        if(need_render) {
            render_display();
        }
        usleep(10000);
    }

    wfb_status_link_stop();

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
