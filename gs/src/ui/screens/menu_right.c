/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Right-side connection menu (slide-in/out) implementation.
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "menu_right.h"

#include "lvgl/lvgl.h"
#include "ui/ui.h"
#include "ui/lang/lang.h"
#include "system/conn_api.h"

#if defined(PLATFORM_DESKTOP)
#include "sdl2_lvgl_input.h"
#endif

#include "menu_left.h"

#include <stdbool.h>
#include <string.h>
#include <src/misc/lv_event_private.h>

/* --------------------------------------------------------------------------
 * Layout defines (for easy tuning)
 * -------------------------------------------------------------------------- */

#define UI_SCREEN_WIDTH              LVGL_BUFF_WIDTH
#define UI_SCREEN_HEIGHT             LVGL_BUFF_HEIGHT

/* Panel geometry (roughly 1/4 of the screen width) */
#define MENU_RIGHT_MARGIN_X          20
#define MENU_RIGHT_MARGIN_Y          70

#define MENU_RIGHT_WIDTH             (UI_SCREEN_WIDTH / 4)      /* ~1/4 of 1280 -> 320 */
#define MENU_RIGHT_HEIGHT            (UI_SCREEN_HEIGHT - 2 * MENU_RIGHT_MARGIN_Y)

/* X positions for slide animation */
#define MENU_RIGHT_X_VISIBLE         (UI_SCREEN_WIDTH - MENU_RIGHT_MARGIN_X - MENU_RIGHT_WIDTH)
#define MENU_RIGHT_X_HIDDEN          (UI_SCREEN_WIDTH + MENU_RIGHT_MARGIN_X)

/* Animation parameters */
#define MENU_RIGHT_ANIM_TIME_MS      500

/* Timer parameters */
#define MENU_RIGHT_TIMER_PERIOD_MS   200        /* timer period to check auto-hide + hover */
#define MENU_RIGHT_AUTOHIDE_MS       8000       /* hide after N ms of inactivity */

/* Mouse activation area on the right edge (in LVGL logical pixels) */
#define MENU_RIGHT_ACTIVATION_WIDTH  40

/* Panel style (similar corner radius/opa as status bar) */
#define MENU_RIGHT_BG_OPA            LV_OPA_30
#define MENU_RIGHT_BG_RADIUS         10
#define MENU_RIGHT_BORDER_WIDTH      0
#define MENU_RIGHT_BORDER_OPA        MENU_RIGHT_BG_OPA

/* Inner padding inside panel (flex layout uses these paddings) */
#define MENU_RIGHT_PAD_LEFT          16
#define MENU_RIGHT_PAD_RIGHT         16
#define MENU_RIGHT_PAD_TOP           16
#define MENU_RIGHT_PAD_BOTTOM        16

/* Flex layout parameters */
#define MENU_RIGHT_ROW_GAP           8          /* vertical gap between rows */

/* Element sizes (flex will handle vertical stacking) */
#define MENU_RIGHT_TA_HEIGHT         32         /* height for text areas */
#define MENU_RIGHT_CONNECT_BTN_H     32         /* height for connect button */

/* Text area limits */
#define MENU_RIGHT_TA_IP_MAXLEN      64
#define MENU_RIGHT_TA_LOGIN_MAXLEN   64
#define MENU_RIGHT_TA_PASS_MAXLEN    64

/* --------------------------------------------------------------------------
 * Local objects
 * -------------------------------------------------------------------------- */

static lv_obj_t *g_menu_panel          = NULL;

static lv_obj_t *g_label_ip            = NULL;
static lv_obj_t *g_ta_ip               = NULL;

static lv_obj_t *g_label_login         = NULL;
static lv_obj_t *g_ta_login            = NULL;

static lv_obj_t *g_label_pass          = NULL;
static lv_obj_t *g_ta_pass             = NULL;
static lv_obj_t *g_cb_show_pass        = NULL;

static lv_obj_t *g_cb_autoconnect      = NULL;

static lv_obj_t *g_btn_connect         = NULL;
static lv_obj_t *g_label_btn_connect   = NULL;

static lv_obj_t *g_label_status        = NULL;

/* Styles */
static lv_style_t g_menu_style_panel;
static lv_style_t g_menu_style_label;
static lv_style_t g_menu_style_textarea;
static lv_style_t g_menu_style_checkbox;
static lv_style_t g_menu_style_button;

static bool      g_menu_styles_inited  = false;
static bool      g_menu_visible        = false;

/* Timer and interaction time for auto-hide */
static lv_timer_t *g_menu_timer        = NULL;
static uint32_t   g_menu_last_interaction_ms = 0;

/* Forward declaration so helpers can call public API */
void menu_right_show(void);

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* Update last interaction timestamp (used by auto-hide timer) */
static void menu_right_touch(void)
{
    g_menu_last_interaction_ms = lv_tick_get();
}

/* Check if panel is currently visible on screen (geometry-based). */
static bool menu_right_is_visible(void)
{
    if (!g_menu_panel)
        return false;

    int32_t x = lv_obj_get_x(g_menu_panel);

    /* Visible if panel intersects screen area. */
    return (x + MENU_RIGHT_WIDTH) > 0 && x < UI_SCREEN_WIDTH;
}

/* Initialize LVGL styles for this menu once */
static void menu_right_init_styles(void)
{
    if (g_menu_styles_inited)
        return;

    g_menu_styles_inited = true;

    /* Panel background style */
    lv_style_init(&g_menu_style_panel);
    lv_style_set_bg_opa(&g_menu_style_panel, MENU_RIGHT_BG_OPA);
    lv_style_set_bg_color(&g_menu_style_panel, lv_color_black());
    lv_style_set_radius(&g_menu_style_panel, MENU_RIGHT_BG_RADIUS);
    lv_style_set_border_width(&g_menu_style_panel, MENU_RIGHT_BORDER_WIDTH);
    lv_style_set_border_opa(&g_menu_style_panel, MENU_RIGHT_BORDER_OPA);
    lv_style_set_border_color(&g_menu_style_panel, lv_color_make(80, 80, 80));
    lv_style_set_pad_left(&g_menu_style_panel, MENU_RIGHT_PAD_LEFT);
    lv_style_set_pad_right(&g_menu_style_panel, MENU_RIGHT_PAD_RIGHT);
    lv_style_set_pad_top(&g_menu_style_panel, MENU_RIGHT_PAD_TOP);
    lv_style_set_pad_bottom(&g_menu_style_panel, MENU_RIGHT_PAD_BOTTOM);

    /* Label style */
    lv_style_init(&g_menu_style_label);
    lv_style_set_text_color(&g_menu_style_label, lv_color_white());

    /* Textarea style */
    lv_style_init(&g_menu_style_textarea);
    lv_style_set_text_color(&g_menu_style_textarea, lv_color_white());

    /* Vertical padding inside textareas (applied to all fields) */
    lv_style_set_pad_top(&g_menu_style_textarea, 4);     /* inner top padding */
    lv_style_set_pad_bottom(&g_menu_style_textarea, 4);  /* inner bottom padding */
    lv_style_set_pad_left(&g_menu_style_textarea, 6);    /* left padding */
    lv_style_set_pad_right(&g_menu_style_textarea, 6);   /* right padding */

    /* Checkbox style */
    lv_style_init(&g_menu_style_checkbox);
    lv_style_set_text_color(&g_menu_style_checkbox, lv_color_white());

    /* Button style */
    lv_style_init(&g_menu_style_button);
    lv_style_set_bg_opa(&g_menu_style_button, LV_OPA_80);
    lv_style_set_bg_color(&g_menu_style_button, lv_color_make(40, 120, 40));
    lv_style_set_radius(&g_menu_style_button, 6);
}

/* Animation executor: updates X position of panel */
static void menu_right_anim_exec_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_x(obj, v);
}

/* Animation ready callback: adjust visible flag (info only) */
static void menu_right_anim_ready_cb(lv_anim_t *a)
{
    (void)a;
    if (!g_menu_panel)
        return;

    int32_t x = lv_obj_get_x(g_menu_panel);

    if (x <= MENU_RIGHT_X_VISIBLE) {
        g_menu_visible = true;
    } else if (x >= MENU_RIGHT_X_HIDDEN) {
        g_menu_visible = false;
    }
}

/* Start slide animation to target X */
static void menu_right_animate_to(int32_t target_x)
{
    if (!g_menu_panel)
        return;

    int32_t start_x = lv_obj_get_x(g_menu_panel);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_menu_panel);
    lv_anim_set_values(&a, start_x, target_x);
    lv_anim_set_duration(&a, MENU_RIGHT_ANIM_TIME_MS);
    lv_anim_set_exec_cb(&a, menu_right_anim_exec_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, menu_right_anim_ready_cb);
    lv_anim_start(&a);
}

/* Push data from UI fields to connection API */
static void menu_right_push_to_api(void)
{
    if (!g_ta_ip || !g_ta_login || !g_ta_pass || !g_cb_autoconnect)
        return;

    const char *ip    = lv_textarea_get_text(g_ta_ip);
    const char *login = lv_textarea_get_text(g_ta_login);
    const char *pass  = lv_textarea_get_text(g_ta_pass);
    bool autoconnect  = lv_obj_has_state(g_cb_autoconnect, LV_STATE_CHECKED);

    conn_api_set_params(ip ? ip : "",
                        login ? login : "",
                        pass ? pass : "",
                        autoconnect);
}

/* Update button / status label according to connection status */
static void menu_right_update_connection_state_ui(void)
{
    if (!g_btn_connect || !g_label_status)
        return;

    conn_status_e st = conn_api_get_status();

    switch (st) {
    case CONN_STATUS_CONNECTED:
        lv_obj_add_flag(g_btn_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_label_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_label_status, lang_get_str(STR_MENU_CONN_CONNECTED));
        break;

    case CONN_STATUS_CONNECTING:
        lv_obj_add_flag(g_btn_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_label_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_label_status, lang_get_str(STR_MENU_CONN_CONNECTING));
        break;

    case CONN_STATUS_ERROR:
        lv_obj_clear_flag(g_btn_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_label_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_label_status, lang_get_str(STR_MENU_CONN_ERROR));
        break;

    case CONN_STATUS_DISCONNECTED:
    default:
        lv_obj_clear_flag(g_btn_connect, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_label_status, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

/* Load saved params from API into UI fields */
static void menu_right_load_from_api(void)
{
    if (!g_ta_ip || !g_ta_login || !g_ta_pass || !g_cb_autoconnect)
        return;

    char ip[MENU_RIGHT_TA_IP_MAXLEN]       = {0};
    char login[MENU_RIGHT_TA_LOGIN_MAXLEN] = {0};
    char pass[MENU_RIGHT_TA_PASS_MAXLEN]   = {0};
    bool autoconnect                       = false;

    conn_api_get_params(ip, sizeof(ip),
                        login, sizeof(login),
                        pass, sizeof(pass),
                        &autoconnect);

    lv_textarea_set_text(g_ta_ip, ip);
    lv_textarea_set_text(g_ta_login, login);
    lv_textarea_set_text(g_ta_pass, pass);

    if (autoconnect)
        lv_obj_add_state(g_cb_autoconnect, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(g_cb_autoconnect, LV_STATE_CHECKED);

    menu_right_update_connection_state_ui();
}

/* Timer callback: hover activation + auto-hide logic */
static void menu_right_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (!g_menu_panel)
        return;

    bool visible = menu_right_is_visible();

    /* Try to get pointer device and its coordinates */
    lv_indev_t *indev    = NULL;
    lv_indev_t *pointer  = NULL;
    for (indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            pointer = indev;
            break;
        }
    }

    if (!visible && pointer) {
        lv_point_t p;
        lv_indev_get_point(pointer, &p);

        /* Mouse near right edge while menu is hidden → show */
        if (p.x >= (UI_SCREEN_WIDTH - MENU_RIGHT_ACTIVATION_WIDTH) &&
            p.x < UI_SCREEN_WIDTH &&
            p.y >= 0 && p.y < UI_SCREEN_HEIGHT) {

            menu_right_touch();
            menu_right_show();
            return;
        }
    }

    /* If menu is not visible – nothing more to do */
    if (!visible)
        return;

    /* Auto-hide visible menu after inactivity */
    uint32_t now  = lv_tick_get();
    uint32_t diff = now - g_menu_last_interaction_ms;

    if (diff > MENU_RIGHT_AUTOHIDE_MS) {
        menu_right_hide();
    }
}

/* --------------------------------------------------------------------------
 * LVGL event callbacks
 * -------------------------------------------------------------------------- */

/* Any press in the panel updates last interaction time */
static void menu_right_event_touch_cb(lv_event_t *e)
{
    (void)e;
    menu_right_touch();
}

/* Focus handler for textareas: tells SDL→LVGL driver where to send keyboard input. */
static void menu_right_event_focus_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_FOCUSED)
        return;

#if defined(PLATFORM_DESKTOP)
    lv_obj_t *obj = lv_event_get_target(e);
    sdl2_lvgl_input_set_focus_obj(obj);
#endif
}

/* Textarea changed (IP/login/pass) */
static void menu_right_event_ta_changed_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_VALUE_CHANGED)
        return;

    menu_right_touch();
    // menu_right_push_to_api();
}

/* "Show password" checkbox handler */
static void menu_right_event_show_pass_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_VALUE_CHANGED)
        return;

    menu_right_touch();

    if (!g_ta_pass || !g_cb_show_pass)
        return;

    bool checked = lv_obj_has_state(g_cb_show_pass, LV_STATE_CHECKED);
    lv_textarea_set_password_mode(g_ta_pass, !checked);
}

/* "Autoconnect" checkbox handler */
static void menu_right_event_autoconnect_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_VALUE_CHANGED)
        return;

    menu_right_touch();
    //menu_right_push_to_api();

    conn_status_e st  = conn_api_get_status();
    bool autoconnect  = lv_obj_has_state(g_cb_autoconnect, LV_STATE_CHECKED);

    if (autoconnect && st == CONN_STATUS_DISCONNECTED) {
        conn_api_request_connect();
    }

    menu_right_update_connection_state_ui();
}

/* "Connect" button handler */
static void menu_right_event_connect_btn_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_CLICKED)
        return;

    menu_right_touch();

    menu_right_push_to_api();
    conn_api_request_connect();
    menu_right_update_connection_state_ui();
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void menu_right_init(void)
{
    menu_right_init_styles();

    lv_obj_t *root = lv_scr_act();
    if (!root)
        return;

    /* Create main panel */
    g_menu_panel = lv_obj_create(root);
    lv_obj_remove_style_all(g_menu_panel);
    lv_obj_add_style(g_menu_panel, &g_menu_style_panel, LV_PART_MAIN);

    lv_obj_set_size(g_menu_panel, MENU_RIGHT_WIDTH, MENU_RIGHT_HEIGHT);

    /* Disable scrollbars for the panel; layout is handled by flex only */
    lv_obj_clear_flag(g_menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_menu_panel, LV_SCROLLBAR_MODE_OFF);

    /* Start hidden outside the right edge */
    lv_obj_set_pos(g_menu_panel, MENU_RIGHT_X_HIDDEN, MENU_RIGHT_MARGIN_Y);
    g_menu_visible = false;

    /* Panel receives press events to update interaction time */
    lv_obj_add_event_cb(g_menu_panel, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);

    /* Flex layout: vertical column, full-width children */
    lv_obj_set_flex_flow(g_menu_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_menu_panel,
                          LV_FLEX_ALIGN_START,   /* main axis alignment */
                          LV_FLEX_ALIGN_START,   /* cross axis alignment */
                          LV_FLEX_ALIGN_START);  /* track cross axis alignment */
    lv_obj_set_style_pad_row(g_menu_panel, MENU_RIGHT_ROW_GAP, 0);

    /* --- IP --- */
    g_label_ip = lv_label_create(g_menu_panel);
    lv_obj_add_style(g_label_ip, &g_menu_style_label, LV_PART_MAIN);
    lv_obj_set_width(g_label_ip, LV_PCT(100));
    lv_label_set_text(g_label_ip, lang_get_str(STR_MENU_SERVER_IP));

    g_ta_ip = lv_textarea_create(g_menu_panel);
    lv_obj_add_style(g_ta_ip, &g_menu_style_textarea, LV_PART_MAIN);
    lv_textarea_set_one_line(g_ta_ip, true);
    lv_textarea_set_max_length(g_ta_ip, MENU_RIGHT_TA_IP_MAXLEN);
    lv_obj_set_width(g_ta_ip, LV_PCT(100));
    lv_obj_set_height(g_ta_ip, MENU_RIGHT_TA_HEIGHT);
    lv_obj_add_event_cb(g_ta_ip, menu_right_event_ta_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ta_ip, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_ta_ip, menu_right_event_focus_cb, LV_EVENT_FOCUSED, NULL);

    /* --- Login --- */
    g_label_login = lv_label_create(g_menu_panel);
    lv_obj_add_style(g_label_login, &g_menu_style_label, LV_PART_MAIN);
    lv_obj_set_width(g_label_login, LV_PCT(100));
    lv_label_set_text(g_label_login, lang_get_str(STR_MENU_LOGIN));

    g_ta_login = lv_textarea_create(g_menu_panel);
    lv_obj_add_style(g_ta_login, &g_menu_style_textarea, LV_PART_MAIN);
    lv_textarea_set_one_line(g_ta_login, true);
    lv_textarea_set_max_length(g_ta_login, MENU_RIGHT_TA_LOGIN_MAXLEN);
    lv_obj_set_width(g_ta_login, LV_PCT(100));
    lv_obj_set_height(g_ta_login, MENU_RIGHT_TA_HEIGHT);
    lv_obj_add_event_cb(g_ta_login, menu_right_event_ta_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ta_login, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_ta_login, menu_right_event_focus_cb, LV_EVENT_FOCUSED, NULL);

    /* --- Password --- */
    g_label_pass = lv_label_create(g_menu_panel);
    lv_obj_add_style(g_label_pass, &g_menu_style_label, LV_PART_MAIN);
    lv_obj_set_width(g_label_pass, LV_PCT(100));
    lv_label_set_text(g_label_pass, lang_get_str(STR_MENU_PASSWORD));

    g_ta_pass = lv_textarea_create(g_menu_panel);
    lv_obj_add_style(g_ta_pass, &g_menu_style_textarea, LV_PART_MAIN);
    lv_textarea_set_one_line(g_ta_pass, true);
    lv_textarea_set_max_length(g_ta_pass, MENU_RIGHT_TA_PASS_MAXLEN);
    lv_textarea_set_password_mode(g_ta_pass, true);  /* default hidden */
    lv_obj_set_width(g_ta_pass, LV_PCT(100));
    lv_obj_set_height(g_ta_pass, MENU_RIGHT_TA_HEIGHT);
    lv_obj_add_event_cb(g_ta_pass, menu_right_event_ta_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_ta_pass, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_ta_pass, menu_right_event_focus_cb, LV_EVENT_FOCUSED, NULL);

    /* Show password checkbox */
    g_cb_show_pass = lv_checkbox_create(g_menu_panel);
    lv_obj_add_style(g_cb_show_pass, &g_menu_style_checkbox, LV_PART_MAIN);
    lv_checkbox_set_text(g_cb_show_pass, lang_get_str(STR_MENU_SHOW_PASSWORD));
    lv_obj_set_width(g_cb_show_pass, LV_PCT(100));
    lv_obj_add_event_cb(g_cb_show_pass, menu_right_event_show_pass_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_cb_show_pass, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);

    /* Autoconnect checkbox */
    g_cb_autoconnect = lv_checkbox_create(g_menu_panel);
    lv_obj_add_style(g_cb_autoconnect, &g_menu_style_checkbox, LV_PART_MAIN);
    lv_checkbox_set_text(g_cb_autoconnect, lang_get_str(STR_MENU_AUTOCONNECT));
    lv_obj_set_width(g_cb_autoconnect, LV_PCT(100));
    lv_obj_add_event_cb(g_cb_autoconnect, menu_right_event_autoconnect_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_cb_autoconnect, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);

    /* Connect button */
    g_btn_connect = lv_btn_create(g_menu_panel);
    lv_obj_add_style(g_btn_connect, &g_menu_style_button, LV_PART_MAIN);
    lv_obj_set_width(g_btn_connect, LV_PCT(100));
    lv_obj_set_height(g_btn_connect, MENU_RIGHT_CONNECT_BTN_H);
    lv_obj_add_event_cb(g_btn_connect, menu_right_event_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_btn_connect, menu_right_event_touch_cb, LV_EVENT_PRESSED, NULL);

    g_label_btn_connect = lv_label_create(g_btn_connect);
    lv_label_set_text(g_label_btn_connect, lang_get_str(STR_MENU_CONNECT));
    lv_obj_center(g_label_btn_connect);

    /* Status label (shown when connected/connecting/error) */
    g_label_status = lv_label_create(g_menu_panel);
    lv_obj_add_style(g_label_status, &g_menu_style_label, LV_PART_MAIN);
    lv_obj_set_width(g_label_status, LV_PCT(100));
    lv_label_set_text(g_label_status, "");
    lv_obj_add_flag(g_label_status, LV_OBJ_FLAG_HIDDEN);

    /* Disable scrollbars for textarea (no need to scroll) */
    lv_obj_clear_flag(g_ta_ip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_ta_login, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_ta_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_ta_ip, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(g_ta_login, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(g_ta_pass, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(g_menu_panel, LV_SCROLLBAR_MODE_OFF);

    /* Set font for all textareas (labels use theme font) */
    lv_obj_set_style_text_font(g_ta_ip, &lv_font_montserrat_16, LV_STYLE_STATE_CMP_SAME);
    lv_obj_set_style_text_font(g_ta_login, &lv_font_montserrat_16, LV_STYLE_STATE_CMP_SAME);
    lv_obj_set_style_text_font(g_ta_pass, &lv_font_montserrat_16, LV_STYLE_STATE_CMP_SAME);

    /* Timer for auto-hide and hover activation */
    g_menu_timer = lv_timer_create(menu_right_timer_cb, MENU_RIGHT_TIMER_PERIOD_MS, NULL);

    /* Load saved values from connection API */
    menu_right_load_from_api();

    /* Initial interaction timestamp */
    g_menu_last_interaction_ms = lv_tick_get();

    menu_right_show();
}

void menu_right_show(void)
{
    if (!g_menu_panel)
        return;
    if (menu_right_is_visible())
        return;

    menu_right_load_from_api();
    menu_right_update_connection_state_ui();

    lv_obj_set_pos(g_menu_panel, MENU_RIGHT_X_HIDDEN, MENU_RIGHT_MARGIN_Y);

    menu_right_touch();
    menu_right_animate_to(MENU_RIGHT_X_VISIBLE);
}

void menu_right_hide(void)
{
    if (!g_menu_panel)
        return;
    if (!menu_right_is_visible())
        return;

#if defined(PLATFORM_DESKTOP)
    /* Clear keyboard focus when menu is hidden. */
    sdl2_lvgl_input_set_focus_obj(NULL);
#endif

    menu_right_animate_to(MENU_RIGHT_X_HIDDEN);
}

void menu_right_toggle(void)
{
    if (menu_right_is_visible())
        menu_right_hide();
    else
        menu_right_show();
}

void menu_right_refresh_from_api(void)
{
    menu_right_load_from_api();
}

void menu_right_on_connection_status_changed(void)
{
    menu_right_update_connection_state_ui();
}
