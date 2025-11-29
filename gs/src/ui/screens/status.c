/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "status.h"

#include "lvgl/lvgl.h"
#include "ui/lang/lang.h"
#include "ui/ui.h"
#include "ui/lang/lang.h"

/* --------------------------------------------------------------------------
 * Layout defines (for easy tuning)
 * -------------------------------------------------------------------------- */

#define UI_SCREEN_WIDTH              LVGL_BUFF_WIDTH
#define UI_SCREEN_HEIGHT             LVGL_BUFF_HEIGHT

/* Status bar geometry */
#define STATUS_BAR_MARGIN_X          20      /* left/right margin from screen edge */
#define STATUS_BAR_MARGIN_TOP        12      /* distance from top of screen */
#define STATUS_BAR_HEIGHT            40

/* Status bar style */
#define STATUS_BAR_BG_OPA            LV_OPA_30   /* semi-transparent */
#define STATUS_BAR_BG_RADIUS         10
#define STATUS_BAR_BORDER_WIDTH      0
#define STATUS_BAR_BORDER_OPA        STATUS_BAR_BG_OPA

/* Optional content layout (labels/icons placeholders) */
#define STATUS_PAD_LEFT              16
#define STATUS_PAD_RIGHT             16
#define STATUS_PAD_TOP               6
#define STATUS_PAD_BOTTOM            6
#define STATUS_LABEL_GAP             24

/* Example: left label (e.g. mode / profile) */
#define STATUS_LABEL_LEFT_X_OFFSET   0   /* relative inside bar, we use ALIGN_LEFT_MID */
#define STATUS_LABEL_LEFT_Y_OFFSET   0

/* Example: center label (e.g. status text) */
#define STATUS_LABEL_CENTER_X_OFFSET 0
#define STATUS_LABEL_CENTER_Y_OFFSET 0

/* Example: right label (e.g. time / link) */
#define STATUS_LABEL_RIGHT_X_OFFSET  0
#define STATUS_LABEL_RIGHT_Y_OFFSET  0

/* --------------------------------------------------------------------------
 * Local objects
 * -------------------------------------------------------------------------- */

static lv_obj_t *g_status_bar         = NULL;
static lv_obj_t *g_status_label_left  = NULL;
static lv_obj_t *g_status_label_mid   = NULL;
static lv_obj_t *g_status_label_right = NULL;

static lv_style_t g_status_style_bar;
static lv_style_t g_status_style_label;

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static void status_init_styles(void)
{
    static bool styles_inited = false;
    if (styles_inited) {
        return;
    }
    styles_inited = true;

    /* Style for the bar background */
    lv_style_init(&g_status_style_bar);
    lv_style_set_bg_opa(&g_status_style_bar, STATUS_BAR_BG_OPA);
    lv_style_set_bg_color(&g_status_style_bar, lv_color_black());
    lv_style_set_radius(&g_status_style_bar, STATUS_BAR_BG_RADIUS);
    lv_style_set_border_width(&g_status_style_bar, STATUS_BAR_BORDER_WIDTH);
    lv_style_set_border_opa(&g_status_style_bar, STATUS_BAR_BORDER_OPA);
    lv_style_set_border_color(&g_status_style_bar, lv_color_make(80, 80, 80));
    lv_style_set_pad_left(&g_status_style_bar, STATUS_PAD_LEFT);
    lv_style_set_pad_right(&g_status_style_bar, STATUS_PAD_RIGHT);
    lv_style_set_pad_top(&g_status_style_bar, STATUS_PAD_TOP);
    lv_style_set_pad_bottom(&g_status_style_bar, STATUS_PAD_BOTTOM);

    /* Common label style */
    lv_style_init(&g_status_style_label);
    lv_style_set_text_color(&g_status_style_label, lv_color_white());
    //lv_style_set_text_color(&g_status_style_label, lv_color_make(100, 255, 100));
    //lv_style_set_text_font(&g_status_style_label, &montserrat_cyrillic_20);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
void screen_status_init(void)
{
    status_init_styles();

    lv_obj_t *root = lv_scr_act();
    if (!root) {
        return;
    }

    /* Create bar container */
    int bar_width = UI_SCREEN_WIDTH - 2 * STATUS_BAR_MARGIN_X;

    g_status_bar = lv_obj_create(root);
    lv_obj_remove_style_all(g_status_bar); /* start from clean object */
    lv_obj_add_style(g_status_bar, &g_status_style_bar, LV_PART_MAIN);

    lv_obj_set_size(g_status_bar, bar_width, STATUS_BAR_HEIGHT);
    lv_obj_align(g_status_bar, LV_ALIGN_TOP_MID, 0, STATUS_BAR_MARGIN_TOP);

    /* Left label: e.g. "Battery" */
    g_status_label_left = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_left, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_left, lang_get_str(STR_BATT));
    lv_obj_align(g_status_label_left, LV_ALIGN_LEFT_MID, STATUS_LABEL_LEFT_X_OFFSET, STATUS_LABEL_LEFT_Y_OFFSET);

    /* Center label: e.g. link status / bitrate / etc. */
    g_status_label_mid = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_mid, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_mid, lang_get_str(STR_STATUS));
    lv_obj_align(g_status_label_mid, LV_ALIGN_CENTER, STATUS_LABEL_CENTER_X_OFFSET, STATUS_LABEL_CENTER_Y_OFFSET);

    /* Right label: e.g. time / voltage / RSSI */
    g_status_label_right = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_right, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_right, "00:00");
    lv_obj_align(g_status_label_right, LV_ALIGN_RIGHT_MID, STATUS_LABEL_RIGHT_X_OFFSET, STATUS_LABEL_RIGHT_Y_OFFSET);

    /* Optional: if you want equal spacing, you can later replace manual
     * aligns with flex layout, but all tunable values remain in defines.
     */

}