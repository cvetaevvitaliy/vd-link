/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "status.h"

#include "lvgl/lvgl.h"
#include "ui/lang/lang.h"
#include "ui/ui.h"

/* --------------------------------------------------------------------------
 * Layout defines (for easy tuning)
 * -------------------------------------------------------------------------- */

#define UI_SCREEN_WIDTH              LVGL_BUFF_WIDTH
#define UI_SCREEN_HEIGHT             LVGL_BUFF_HEIGHT

/* Status bar geometry */
#define STATUS_BAR_MARGIN_X          20      /* horizontal margin from screen edges */
#define STATUS_BAR_MARGIN_TOP        12      /* visible Y-position (when shown) */
#define STATUS_BAR_HEIGHT            40

/* Hidden/visible Y positions */
#define STATUS_BAR_START_OFFSET_Y    (STATUS_BAR_HEIGHT + 20)   /* how far above screen */
#define STATUS_BAR_Y_VISIBLE         (STATUS_BAR_MARGIN_TOP)
#define STATUS_BAR_Y_HIDDEN          (-STATUS_BAR_START_OFFSET_Y)

/* Animation parameters */
#define STATUS_BAR_ANIM_TIME_MS      500

/* Timer parameters */
#define STATUS_BAR_SHOW_DELAY_MS     0    /* delay before first show animation */
#define STATUS_BAR_TIMER_PERIOD_MS   100     /* timer tick period */

/* Status bar style */
#define STATUS_BAR_BG_OPA            LV_OPA_30
#define STATUS_BAR_BG_RADIUS         10
#define STATUS_BAR_BORDER_WIDTH      0
#define STATUS_BAR_BORDER_OPA        STATUS_BAR_BG_OPA

/* Padding inside the bar */
#define STATUS_PAD_LEFT              16
#define STATUS_PAD_RIGHT             16
#define STATUS_PAD_TOP               6
#define STATUS_PAD_BOTTOM            6

/* Label positions (relative offsets) */
#define STATUS_LABEL_LEFT_X_OFFSET   0
#define STATUS_LABEL_LEFT_Y_OFFSET   0

#define STATUS_LABEL_CENTER_X_OFFSET 0
#define STATUS_LABEL_CENTER_Y_OFFSET 0

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

static bool g_status_styles_inited    = false;
static bool g_status_visible          = false;   /* real visual state */
static bool g_status_target_visible   = true;    /* desired state (flag) */

static lv_timer_t *g_status_timer     = NULL;
static uint32_t    g_status_elapsed_ms = 0;
static bool        g_status_first_show_done = false;
static bool        g_status_anim_in_progress = false;

/* --------------------------------------------------------------------------
 * Forward declarations (internal helpers)
 * -------------------------------------------------------------------------- */

static void status_bar_animate_to(int32_t target_y);
static void status_bar_timer_cb(lv_timer_t *timer);

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static void status_init_styles(void)
{
    if (g_status_styles_inited)
        return;

    g_status_styles_inited = true;

    /* Status bar background style */
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
}

/* Animation executor: updates the Y-position of the bar */
static void status_bar_anim_exec_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_y(obj, v);
}

/* Called when animation finishes, updates visibility and flags */
static void status_bar_anim_ready_cb(lv_anim_t *a)
{
    (void)a;

    g_status_anim_in_progress = false;

    if (!g_status_bar)
        return;

    int32_t y = lv_obj_get_y(g_status_bar);

    if (y == STATUS_BAR_Y_VISIBLE) {
        g_status_visible = true;
        //screen_status_hide(); // for testing auto-hide
    } else if (y == STATUS_BAR_Y_HIDDEN) {
        g_status_visible = false;
        //screen_status_show();  // for testing auto-show
    }
}

/* Starts sliding animation to the given Y position */
static void status_bar_animate_to(int32_t target_y)
{
    if (!g_status_bar)
        return;

    int32_t start_y = lv_obj_get_y(g_status_bar);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_status_bar);
    lv_anim_set_values(&a, start_y, target_y);
    lv_anim_set_duration(&a, STATUS_BAR_ANIM_TIME_MS);
    lv_anim_set_exec_cb(&a, status_bar_anim_exec_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, status_bar_anim_ready_cb);
    g_status_anim_in_progress = true;
    lv_anim_start(&a);
}

/* Timer callback: delayed first show + periodic visibility check */
static void status_bar_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* Phase 1: handle delayed first show */
    if (!g_status_first_show_done) {
        g_status_elapsed_ms += STATUS_BAR_TIMER_PERIOD_MS;

        if (g_status_elapsed_ms >= STATUS_BAR_SHOW_DELAY_MS) {
            g_status_first_show_done = true;

            if (g_status_target_visible && !g_status_visible && !g_status_anim_in_progress) {
                status_bar_animate_to(STATUS_BAR_Y_VISIBLE);
            }
        }
        return;
    }

    /* Phase 2: normal periodic check, sync real state with target flag */
    if (g_status_target_visible) {
        /* Should be visible */
        if (!g_status_visible && !g_status_anim_in_progress) {
            status_bar_animate_to(STATUS_BAR_Y_VISIBLE);
        }
    } else {
        /* Should be hidden */
        if (g_status_visible && !g_status_anim_in_progress) {
            status_bar_animate_to(STATUS_BAR_Y_HIDDEN);
        }
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */
void screen_status_init(void)
{
    status_init_styles();

    lv_obj_t *root = lv_scr_act();
    if (!root)
        return;

    /* Create the status bar container */
    int bar_width = UI_SCREEN_WIDTH - 2 * STATUS_BAR_MARGIN_X;

    g_status_bar = lv_obj_create(root);
    lv_obj_remove_style_all(g_status_bar);
    lv_obj_add_style(g_status_bar, &g_status_style_bar, LV_PART_MAIN);

    lv_obj_set_size(g_status_bar, bar_width, STATUS_BAR_HEIGHT);

    /* Initially place the bar above the screen (hidden) */
    lv_obj_align(g_status_bar, LV_ALIGN_TOP_MID, 0, STATUS_BAR_Y_HIDDEN);
    g_status_visible        = false;
    g_status_target_visible = true;   /* we want it to appear after delay */
    g_status_first_show_done = false;
    g_status_elapsed_ms      = 0;
    g_status_anim_in_progress = false;

    /* Left label */
    g_status_label_left = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_left, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_left, LV_SYMBOL_BATTERY_FULL);
    lv_obj_align(g_status_label_left, LV_ALIGN_LEFT_MID,
                 STATUS_LABEL_LEFT_X_OFFSET, STATUS_LABEL_LEFT_Y_OFFSET);

    /* Middle label */
    g_status_label_mid = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_mid, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_mid, "");
    lv_obj_align(g_status_label_mid, LV_ALIGN_CENTER,
                 STATUS_LABEL_CENTER_X_OFFSET, STATUS_LABEL_CENTER_Y_OFFSET);

    /* Right label */
    g_status_label_right = lv_label_create(g_status_bar);
    lv_obj_add_style(g_status_label_right, &g_status_style_label, LV_PART_MAIN);
    lv_label_set_text(g_status_label_right, "");
    lv_obj_align(g_status_label_right, LV_ALIGN_RIGHT_MID,
                 STATUS_LABEL_RIGHT_X_OFFSET, STATUS_LABEL_RIGHT_Y_OFFSET);

    /* font for LVGL icons  */
    lv_obj_set_style_text_font(g_status_label_left, &lv_font_montserrat_34,LV_STYLE_STATE_CMP_SAME);
    // lv_obj_set_style_text_font(g_status_label_mid, &lv_font_montserrat_34,LV_STYLE_STATE_CMP_SAME);
    // lv_obj_set_style_text_font(g_status_label_right, &lv_font_montserrat_34,LV_STYLE_STATE_CMP_SAME);

    /* Create periodic timer which:
     *  - waits STATUS_BAR_SHOW_DELAY_MS then shows the bar
     *  - afterwards periodically checks if bar should be shown/hidden
     */
    if (!g_status_timer) {
        g_status_timer = lv_timer_create(status_bar_timer_cb,STATUS_BAR_TIMER_PERIOD_MS, NULL);
    }

    // disable scrolling for the status bar
    lv_obj_clear_flag(g_status_bar, LV_OBJ_FLAG_SCROLLABLE);
}

/*
 * Set target state to "visible".
 * Real animation is started from the LVGL timer.
 */
void screen_status_show(void)
{
    g_status_target_visible = true;
}

/*
 * Set target state to "hidden".
 * Real animation is started from the LVGL timer.
 */
void screen_status_hide(void)
{
    g_status_target_visible = false;
}

void screen_status_update(enum status_bar_element_e element, int value)
{
    switch (element) {
    case STATUS_ELEMENT_LEFT:
        lv_label_set_text_fmt(g_status_label_left, "%s: %d%%", LV_SYMBOL_BATTERY_FULL, value);
        break;

    case STATUS_ELEMENT_MID:
        lv_label_set_text_fmt(g_status_label_mid, "%s: %d", lang_get_str(STR_RSSI), value);
        break;

    case STATUS_ELEMENT_RIGHT:
        lv_label_set_text_fmt(g_status_label_right, "%02d:%02d", value / 60, value % 60);
        break;

    default:
        break;
    }
}

void screen_status_deinit(void)
{
    /* Optional cleanup, if you destroy this screen dynamically */

    if (g_status_timer) {
        lv_timer_del(g_status_timer);
        g_status_timer = NULL;
    }

    if (g_status_bar) {
        lv_obj_del(g_status_bar);
        g_status_bar = NULL;
    }

    g_status_label_left  = NULL;
    g_status_label_mid   = NULL;
    g_status_label_right = NULL;

    g_status_visible        = false;
    g_status_target_visible = true;
    g_status_first_show_done = false;
    g_status_elapsed_ms      = 0;
    g_status_anim_in_progress = false;
}