/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "menu_left.h"

#include "lvgl/lvgl.h"
#include "ui/ui.h"
#include "ui/lang/lang.h"
#include "system/drone_api.h"

#if defined(PLATFORM_DESKTOP)
#include "sdl2_lvgl_input.h"
#endif

#include "unistd.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Layout defines
 * -------------------------------------------------------------------------- */
#define UI_SCREEN_WIDTH              LVGL_BUFF_WIDTH
#define UI_SCREEN_HEIGHT             LVGL_BUFF_HEIGHT

/* Panel geometry */
#define MENU_LEFT_MARGIN_X           20
#define MENU_LEFT_MARGIN_Y           70

#define MENU_LEFT_WIDTH              (UI_SCREEN_WIDTH / 2)
#define MENU_LEFT_HEIGHT             (UI_SCREEN_HEIGHT - 2 * MENU_LEFT_MARGIN_Y)

/* X positions for slide animation */
#define MENU_LEFT_X_VISIBLE          (MENU_LEFT_MARGIN_X)
#define MENU_LEFT_X_HIDDEN           (-(MENU_LEFT_MARGIN_X + MENU_LEFT_WIDTH))

/* Mouse activation area on the left edge (in LVGL logical pixels) */
#define MENU_LEFT_ACTIVATION_WIDTH   40

/* Animation parameters */
#define MENU_LEFT_ANIM_TIME_MS       500

/* Timer: auto-hide + hover */
#define MENU_LEFT_TIMER_PERIOD_MS    200
#define MENU_LEFT_AUTOHIDE_MS        8000

/* Timer: periodic refresh of drone list */
#define MENU_LEFT_REFRESH_PERIOD_MS  200

/* Panel style */
#define MENU_LEFT_BG_OPA             LV_OPA_30
#define MENU_LEFT_BG_RADIUS          10
#define MENU_LEFT_BORDER_WIDTH       0
#define MENU_LEFT_BORDER_OPA         MENU_LEFT_BG_OPA

/* Panel paddings */
#define MENU_LEFT_PAD_LEFT           16
#define MENU_LEFT_PAD_RIGHT          16
#define MENU_LEFT_PAD_TOP            16
#define MENU_LEFT_PAD_BOTTOM         16

/* Flex layout */
#define MENU_LEFT_ROW_GAP            8    /* vertical gap between rows */
#define MENU_LEFT_LIST_ROW_GAP       4    /* gap between drone rows */

/* Row layout */
#define MENU_LEFT_ROW_HEIGHT         32
#define MENU_LEFT_BOTTOM_H           32   /* bottom control row */

/* Column widths in drone list rows */
#define MENU_LEFT_COL_ID_WIDTH       190
#define MENU_LEFT_COL_STATUS_WIDTH   100
#define MENU_LEFT_COL_RC_WIDTH       100
#define MENU_LEFT_COL_BTN_WIDTH      175

/* Gap between columns */
#define MENU_LEFT_COL_GAP            12

/* --------------------------------------------------------------------------
 * Button styling (Connect / Disconnect)
 * -------------------------------------------------------------------------- */
#define MENU_LEFT_BTN_RADIUS              8
#define MENU_LEFT_BTN_OPA                 LV_OPA_80

/* Connect (normal) */
#define MENU_LEFT_BTN_CONNECT_R           40
#define MENU_LEFT_BTN_CONNECT_G           120
#define MENU_LEFT_BTN_CONNECT_B           40

/* Disconnect (active) */
#define MENU_LEFT_BTN_DISCONNECT_R        140
#define MENU_LEFT_BTN_DISCONNECT_G        40
#define MENU_LEFT_BTN_DISCONNECT_B        40

/* Text on buttons */
#define MENU_LEFT_BTN_TEXT_COLOR          lv_color_white()


#define MENU_LEFT_SCROLL_STEP        (MENU_LEFT_ROW_HEIGHT * 4)

/* --------------------------------------------------------------------------
 * Local objects
 * -------------------------------------------------------------------------- */

static lv_obj_t *g_menu_panel        = NULL;
static lv_obj_t *g_label_title       = NULL;
static lv_obj_t *g_list_container    = NULL;  /* scrollable list */
static lv_obj_t *g_bottom_container  = NULL;  /* bottom RC control */


/* scroll buttons */
static lv_obj_t *g_btn_scroll_up     = NULL;
static lv_obj_t *g_btn_scroll_down   = NULL;

static lv_obj_t *g_label_rc_global   = NULL;
static lv_obj_t *g_switch_rc_global  = NULL;

/* Styles */
static lv_style_t g_menu_style_panel;
static lv_style_t g_menu_style_label;
static lv_style_t g_menu_style_row;
static lv_style_t g_menu_style_row_active;
static lv_style_t g_menu_style_button;
static lv_style_t g_menu_style_button_active;
static lv_style_t g_menu_style_switch_label;

static bool      g_menu_styles_inited         = false;
static bool      g_menu_visible_flag          = false;

/* Timers */
static lv_timer_t *g_menu_timer               = NULL;
static lv_timer_t *g_refresh_timer            = NULL;
static uint32_t    g_menu_last_interaction_ms = 0;

static lv_obj_t   *g_active_row               = NULL;

static void menu_left_update_from_api(void);

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static void menu_left_touch(void)
{
    g_menu_last_interaction_ms = lv_tick_get();
}

static bool menu_left_is_visible(void)
{
    if (!g_menu_panel)
        return false;

    int32_t x = lv_obj_get_x(g_menu_panel);
    /* Panel is visible if it intersects the screen horizontally */
    return (x + MENU_LEFT_WIDTH) > 0 && x < UI_SCREEN_WIDTH;
}

static void menu_left_init_styles(void)
{
    if (g_menu_styles_inited)
        return;

    g_menu_styles_inited = true;

    /* Panel background */
    lv_style_init(&g_menu_style_panel);
    lv_style_set_bg_opa(&g_menu_style_panel, MENU_LEFT_BG_OPA);
    lv_style_set_bg_color(&g_menu_style_panel, lv_color_black());
    lv_style_set_radius(&g_menu_style_panel, MENU_LEFT_BG_RADIUS);
    lv_style_set_border_width(&g_menu_style_panel, MENU_LEFT_BORDER_WIDTH);
    lv_style_set_border_opa(&g_menu_style_panel, MENU_LEFT_BORDER_OPA);
    lv_style_set_border_color(&g_menu_style_panel, lv_color_make(80, 80, 80));
    lv_style_set_pad_left(&g_menu_style_panel, MENU_LEFT_PAD_LEFT);
    lv_style_set_pad_right(&g_menu_style_panel, MENU_LEFT_PAD_RIGHT);
    lv_style_set_pad_top(&g_menu_style_panel, MENU_LEFT_PAD_TOP);
    lv_style_set_pad_bottom(&g_menu_style_panel, MENU_LEFT_PAD_BOTTOM);

    /* Common label */
    lv_style_init(&g_menu_style_label);
    lv_style_set_text_color(&g_menu_style_label, lv_color_white());

    /* Row style (normal) */
    lv_style_init(&g_menu_style_row);
    lv_style_set_bg_opa(&g_menu_style_row, LV_OPA_20);
    lv_style_set_bg_color(&g_menu_style_row, lv_color_make(40, 40, 40));
    lv_style_set_radius(&g_menu_style_row, 6);
    lv_style_set_pad_left(&g_menu_style_row, 6);
    lv_style_set_pad_right(&g_menu_style_row, 6);
    lv_style_set_pad_top(&g_menu_style_row, 4);
    lv_style_set_pad_bottom(&g_menu_style_row, 4);
    lv_style_set_pad_column(&g_menu_style_row, MENU_LEFT_COL_GAP);

    /* Row style (active drone) */
    lv_style_init(&g_menu_style_row_active);
    lv_style_set_bg_opa(&g_menu_style_row_active, LV_OPA_60);
    lv_style_set_bg_color(&g_menu_style_row_active, lv_color_make(60, 100, 60));

    /* Connect button (normal) */
    lv_style_init(&g_menu_style_button);
    lv_style_set_bg_opa(&g_menu_style_button, LV_OPA_80);
    lv_style_set_bg_color(&g_menu_style_button, lv_color_make(40, 120, 40));
    lv_style_set_radius(&g_menu_style_button, 6);
    lv_style_set_pad_left(&g_menu_style_button, 6);
    lv_style_set_pad_right(&g_menu_style_button, 6);
    lv_style_set_pad_top(&g_menu_style_button, 4);
    lv_style_set_pad_bottom(&g_menu_style_button, 4);

    /* Connect button (active = "Disconnect") */
    lv_style_init(&g_menu_style_button_active);
    lv_style_set_bg_opa(&g_menu_style_button_active, LV_OPA_80);
    lv_style_set_bg_color(&g_menu_style_button_active, lv_color_make(140, 40, 40));
    lv_style_set_radius(&g_menu_style_button_active, 6);
    lv_style_set_pad_left(&g_menu_style_button_active, 6);
    lv_style_set_pad_right(&g_menu_style_button_active, 6);
    lv_style_set_pad_top(&g_menu_style_button_active, 4);
    lv_style_set_pad_bottom(&g_menu_style_button_active, 4);

    /* Switch label style */
    lv_style_init(&g_menu_style_switch_label);
    lv_style_set_text_color(&g_menu_style_switch_label, lv_color_white());
}

/* Animation executor */
static void menu_left_anim_exec_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_x(obj, v);
}

/* Animation ready callback: set visible flag */
static void menu_left_anim_ready_cb(lv_anim_t *a)
{
    (void)a;
    if (!g_menu_panel)
        return;

    g_menu_visible_flag = menu_left_is_visible();
}

/* Start slide animation */
static void menu_left_animate_to(int32_t target_x)
{
    if (!g_menu_panel)
        return;

    int32_t start_x = lv_obj_get_x(g_menu_panel);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_menu_panel);
    lv_anim_set_values(&a, start_x, target_x);
    lv_anim_set_duration(&a, MENU_LEFT_ANIM_TIME_MS);
    lv_anim_set_exec_cb(&a, menu_left_anim_exec_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, menu_left_anim_ready_cb);
    lv_anim_start(&a);
}

/* --------------------------------------------------------------------------
 * Row helpers
 * -------------------------------------------------------------------------- */

typedef struct {
    char id[32];
} row_user_data_t;

static void menu_left_row_set_active(lv_obj_t *row, bool active)
{
    if (!row) return;

    if (active) {
        lv_obj_add_style(row, &g_menu_style_row_active, LV_PART_MAIN);
    } else {
        lv_obj_remove_style(row, &g_menu_style_row_active, LV_PART_MAIN);
    }
}

/* Connect/Disconnect button handler */
static void menu_left_event_connect_btn_cb(lv_event_t *e)
{
    printf("[ UI ] menu_left_event_connect_btn_cb() called: event %d\n", e->code);
    if ((e->code != LV_EVENT_CLICKED) && (e->code != LV_EVENT_PRESSED) )
        return;

    menu_left_touch();
    printf("Connect/Disconnect button clicked\n");

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *row = lv_obj_get_parent(btn);  /* button inside row container */
    if (!row) {
        printf("[ UI ] No parent row for button!\n");
        return;
    }

    /* Get per-row user data from the event */
    row_user_data_t *ud = (row_user_data_t *)lv_event_get_user_data(e);
    if (!ud) {
        printf("[ UI ] No user_data in event!\n");
        return;
    }

    const char *id = ud->id;
    printf("[ UI ] Row drone ID='%s'\n", id);

    /* If this row is already active -> disconnect. */
    if (g_active_row == row) {
        printf("[ UI ] Disconnecting from drone ID='%s'\n", id);
        drone_api_set_rc_enabled(false);  /* disable RC on new connection */
        menu_left_row_set_active(row, false);

        /* Send event to update global RC switch */
        lv_obj_clear_state(g_switch_rc_global, LV_STATE_CHECKED);
        lv_obj_send_event(g_switch_rc_global, LV_EVENT_VALUE_CHANGED, NULL);
        drone_api_clear_active();

        /* Update button text/style */
        lv_obj_remove_style(btn, &g_menu_style_button_active, LV_PART_MAIN);
        lv_obj_add_style(btn, &g_menu_style_button, LV_PART_MAIN);

        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label)
            lv_label_set_text(label, lang_get_str(STR_MENU_CONNECT));

        return;
    }

    /* Set this drone active, clear previous. */
    /* Disconnect RC on previous active drone */
    const char *prev = drone_api_get_active_id();
    if (prev) {
        printf("[ UI ] Disconnecting RC on previous active drone ID='%s'\n", prev);
        drone_api_set_rc_enabled(false);  /* disable RC on new connection */

        /* Send event to update global RC switch */
        lv_obj_clear_state(g_switch_rc_global, LV_STATE_CHECKED);
        lv_obj_send_event(g_switch_rc_global, LV_EVENT_VALUE_CHANGED, NULL);
        drone_api_clear_active();

        drone_api_clear_active();
        menu_left_row_set_active(row, false);

        /* Update button text/style */
        lv_obj_remove_style(btn, &g_menu_style_button_active, LV_PART_MAIN);
        lv_obj_add_style(btn, &g_menu_style_button, LV_PART_MAIN);

        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label)
            lv_label_set_text(label, lang_get_str(STR_MENU_CONNECT));
    }

    printf("[ UI ] Connecting to drone ID='%s'\n", id);
    drone_api_set_active(id);

    if (g_active_row && g_active_row != row) {
        menu_left_row_set_active(g_active_row, false);

        /* Previous button style/text reset */
        lv_obj_t *old_btn = lv_obj_get_child(g_active_row, 3); /* known index order */
        if (old_btn) {
            lv_obj_remove_style(old_btn, &g_menu_style_button_active, LV_PART_MAIN);
            lv_obj_add_style(old_btn, &g_menu_style_button, LV_PART_MAIN);
            lv_obj_t *lbl = lv_obj_get_child(old_btn, 0);
            if (lbl)
                lv_label_set_text(lbl, lang_get_str(STR_MENU_CONNECT));
        }
    }

    g_active_row = row;
    menu_left_row_set_active(row, true);

    /* Set this button to "Disconnect". */
    lv_obj_remove_style(btn, &g_menu_style_button, LV_PART_MAIN);
    lv_obj_add_style(btn, &g_menu_style_button_active, LV_PART_MAIN);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, lang_get_str(STR_MENU_DISCONNECT));
    }
}

/* --------------------------------------------------------------------------
 * Timers
 * -------------------------------------------------------------------------- */

/* Auto-hide + hover activation (left side) */
static void menu_left_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (!g_menu_panel)
        return;

    bool visible = menu_left_is_visible();

    /* Find pointer indev */
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

        /* Mouse near left edge while menu is hidden → show */
        if (p.x >= 0 &&
            p.x <= MENU_LEFT_ACTIVATION_WIDTH &&
            p.y >= 0 && p.y < UI_SCREEN_HEIGHT) {

            menu_left_touch();
            menu_left_show();
            return;
        }
    }

    if (!visible)
        return;

    /* Auto-hide after inactivity */
    uint32_t now  = lv_tick_get();
    uint32_t diff = now - g_menu_last_interaction_ms;

    if (diff > MENU_LEFT_AUTOHIDE_MS) {
        menu_left_hide();
    }
}

/* Periodic refresh of drones list */
static void menu_left_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    menu_left_update_from_api();
}

/* --------------------------------------------------------------------------
 * Events
 * -------------------------------------------------------------------------- */

/* Any press inside panel keeps it alive */
static void menu_left_event_touch_cb(lv_event_t *e)
{
    (void)e;
    menu_left_touch();
}

/* Global RC switch */
static void menu_left_event_rc_switch_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_VALUE_CHANGED)
        return;

    printf("[ UI ] menu_left_event_rc_switch_cb() called: event %d\n", e->code);

    menu_left_touch();

    bool checked = lv_obj_has_state(g_switch_rc_global, LV_STATE_CHECKED);
    printf("[ UI ] RC Switch on %s\n", checked ? "on" : "off");
    drone_api_set_rc_enabled(checked);
}

static void menu_left_scroll_btn_event_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_CLICKED && e->code != LV_EVENT_PRESSED)
        return;

    menu_left_touch();

    if (!g_list_container)
        return;

    lv_obj_t *btn = lv_event_get_target(e);
    int32_t top = lv_obj_get_scroll_top(g_list_container);
    int32_t bottom = lv_obj_get_scroll_bottom(g_list_container);
    //printf("[ UI ] Scroll button clicked, top=%d, bottom=%d\n", top, bottom);

    if (btn == g_btn_scroll_up) {
        lv_obj_scroll_by(g_list_container, 0, -MENU_LEFT_SCROLL_STEP, LV_ANIM_ON);
    } else if (btn == g_btn_scroll_down) {
        if (top > 0) {
            lv_obj_scroll_by(g_list_container, 0, MENU_LEFT_SCROLL_STEP, LV_ANIM_ON);
        }
        if (top < 0) {
            lv_obj_scroll_to_y(g_list_container, 0, LV_ANIM_ON);
            lv_obj_scroll_by(g_list_container, 0, 0, LV_ANIM_ON);
        }
    }
}

/* --------------------------------------------------------------------------
 * Drone list building
 * -------------------------------------------------------------------------- */

static void menu_left_clear_list(void)
{
    if (!g_list_container)
        return;

    /* Just delete all children; user_data is managed via event callbacks */
    while (lv_obj_get_child_cnt(g_list_container) > 0) {
        lv_obj_t *child = lv_obj_get_child(g_list_container, 0);
        if (child)
            lv_obj_del(child);
    }
    g_active_row = NULL;
}

static void menu_left_update_from_api(void)
{
    if (!g_list_container)
        return;

    drone_info_t *drones = malloc(sizeof(drone_info_t) * drone_api_get_count());
    if (!drones) {
        printf(" [ UI ] Failed to allocate memory for list of drones\n");
        return;
    }

    int count = drone_api_get_list(drones, drone_api_get_count());
    if (count < 0) count = 0;

    menu_left_clear_list();

    const char *active_id = drone_api_get_active_id();

    for (int i = 0; i < count; ++i) {
        drone_info_t *d = &drones[i];

        /* Row container */
        lv_obj_t *row = lv_obj_create(g_list_container);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &g_menu_style_row, LV_PART_MAIN);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, MENU_LEFT_ROW_HEIGHT);

        /* Horizontal row layout */
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        /* Per-row user data with drone ID */
        row_user_data_t *ud = malloc(sizeof(row_user_data_t));
        if (ud) {
            memset(ud, 0, sizeof(*ud));
            lv_strlcpy(ud->id, d->id, sizeof(ud->id));
        }

        /* DroneID label */
        lv_obj_t *lbl_id = lv_label_create(row);
        lv_obj_add_style(lbl_id, &g_menu_style_label, LV_PART_MAIN);
        lv_obj_set_width(lbl_id, MENU_LEFT_COL_ID_WIDTH);
        lv_label_set_text(lbl_id, d->id);

        /* Status label */
        lv_obj_t *lbl_status = lv_label_create(row);
        lv_obj_add_style(lbl_status, &g_menu_style_label, LV_PART_MAIN);
        lv_obj_set_width(lbl_status, MENU_LEFT_COL_STATUS_WIDTH);
        lv_label_set_text(lbl_status, (d->status == DRONE_STATUS_ONLINE) ? "online" : "offline");

        /* RC state label */
        lv_obj_t *lbl_rc = lv_label_create(row);
        lv_obj_add_style(lbl_rc, &g_menu_style_label, LV_PART_MAIN);
        lv_obj_set_width(lbl_rc, MENU_LEFT_COL_RC_WIDTH);
        lv_label_set_text(lbl_rc, d->rc_on ? "RC On" : "RC Off");

        /* Connect / Disconnect button */
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_add_style(btn, &g_menu_style_button, LV_PART_MAIN);
        lv_obj_set_width(btn, MENU_LEFT_COL_BTN_WIDTH);

        /* Attach handler with per-row user data */
        lv_obj_add_event_cb(btn, menu_left_event_connect_btn_cb, LV_EVENT_PRESSED, ud);

        lv_obj_t *lbl_btn = lv_label_create(btn);
        lv_label_set_text(lbl_btn, lang_get_str(STR_MENU_CONNECT));
        lv_obj_center(lbl_btn);

        /* Active row / button state */
        bool is_active = false;
        if (active_id && active_id[0] != '\0') {
            is_active = (strcmp(active_id, d->id) == 0);
        } else if (d->is_active) {
            is_active = true;
        }

        if (is_active) {
            g_active_row = row;
            menu_left_row_set_active(row, true);
            lv_obj_remove_style(btn, &g_menu_style_button, LV_PART_MAIN);
            lv_obj_add_style(btn, &g_menu_style_button_active, LV_PART_MAIN);
            lv_label_set_text(lbl_btn, lang_get_str(STR_MENU_DISCONNECT));
        }
    }

    free(drones);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void menu_left_init(void)
{
    menu_left_init_styles();

    lv_obj_t *root = lv_scr_act();
    if (!root)
        return;

    /* Create main panel */
    g_menu_panel = lv_obj_create(root);
    lv_obj_remove_style_all(g_menu_panel);
    lv_obj_add_style(g_menu_panel, &g_menu_style_panel, LV_PART_MAIN);

    lv_obj_set_size(g_menu_panel, MENU_LEFT_WIDTH, MENU_LEFT_HEIGHT);

    lv_obj_clear_flag(g_menu_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_menu_panel, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_pos(g_menu_panel, MENU_LEFT_X_HIDDEN, MENU_LEFT_MARGIN_Y);
    g_menu_visible_flag = false;

    lv_obj_add_event_cb(g_menu_panel, menu_left_event_touch_cb, LV_EVENT_PRESSED, NULL);

    lv_obj_set_flex_flow(g_menu_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_menu_panel,LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(g_menu_panel, MENU_LEFT_ROW_GAP, 0);

    /* Title */
    g_label_title = lv_label_create(g_menu_panel);
    lv_obj_add_style(g_label_title, &g_menu_style_label, LV_PART_MAIN);
    lv_obj_set_width(g_label_title, LV_PCT(100));
    lv_label_set_text(g_label_title, lang_get_str(STR_DRONES));


    /* scroll up/down buttons */
    g_btn_scroll_up = lv_btn_create(g_menu_panel);
    lv_obj_add_flag(g_btn_scroll_up, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(g_btn_scroll_up, 24, 24);
    lv_obj_align_to(g_btn_scroll_up, g_menu_panel, LV_ALIGN_TOP_RIGHT, -28, 0);
    lv_obj_add_event_cb(g_btn_scroll_up, menu_left_scroll_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_up = lv_label_create(g_btn_scroll_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP);
    lv_obj_center(lbl_up);

    g_btn_scroll_down = lv_btn_create(g_menu_panel);
    lv_obj_add_flag(g_btn_scroll_down, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(g_btn_scroll_down, 24, 24);
    lv_obj_align_to(g_btn_scroll_down, g_menu_panel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(g_btn_scroll_down, menu_left_scroll_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_down = lv_label_create(g_btn_scroll_down);
    lv_label_set_text(lbl_down, LV_SYMBOL_DOWN);
    lv_obj_center(lbl_down);
    lv_obj_set_style_text_font(lbl_up, &lv_font_montserrat_16, LV_STYLE_STATE_CMP_SAME);
    lv_obj_set_style_text_font(lbl_down, &lv_font_montserrat_16, LV_STYLE_STATE_CMP_SAME);

    /* Scrollable list */
    g_list_container = lv_obj_create(g_menu_panel);
    lv_obj_remove_style_all(g_list_container);
    lv_obj_set_width(g_list_container, LV_PCT(100));
    //lv_obj_set_height(g_list_container, LV_SIZE_CONTENT);
    lv_obj_set_height(g_list_container, LV_PCT(100));
    lv_obj_set_flex_grow(g_list_container, 1);

    lv_obj_set_flex_flow(g_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list_container,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(g_list_container, MENU_LEFT_LIST_ROW_GAP, 0);

    /* Bottom RC control */
    g_bottom_container = lv_obj_create(g_menu_panel);
    lv_obj_remove_style_all(g_bottom_container);
    lv_obj_set_width(g_bottom_container, LV_PCT(100));
    lv_obj_set_height(g_bottom_container, MENU_LEFT_BOTTOM_H);

    lv_obj_set_flex_flow(g_bottom_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bottom_container,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    g_label_rc_global = lv_label_create(g_bottom_container);
    lv_obj_add_style(g_label_rc_global, &g_menu_style_switch_label, LV_PART_MAIN);
    lv_label_set_text(g_label_rc_global, lang_get_str(STR_ACTIVATE_DRONE_CONTROL));

    g_switch_rc_global = lv_switch_create(g_bottom_container);
    lv_obj_add_event_cb(g_switch_rc_global, menu_left_event_rc_switch_cb,LV_EVENT_VALUE_CHANGED, NULL);

    if (drone_api_get_rc_enabled())
        lv_obj_add_state(g_switch_rc_global, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(g_switch_rc_global, LV_STATE_CHECKED);

    /* Timers */
    g_menu_timer = lv_timer_create(menu_left_timer_cb, MENU_LEFT_TIMER_PERIOD_MS, NULL);
    g_refresh_timer = lv_timer_create(menu_left_refresh_timer_cb,
                                      MENU_LEFT_REFRESH_PERIOD_MS, NULL);

    g_menu_last_interaction_ms = lv_tick_get();

    menu_left_update_from_api();

    menu_left_show();
}

void menu_left_show(void)
{
    if (!g_menu_panel)
        return;

    menu_left_update_from_api();

    lv_obj_set_pos(g_menu_panel, MENU_LEFT_X_HIDDEN, MENU_LEFT_MARGIN_Y);

    menu_left_touch();
    menu_left_animate_to(MENU_LEFT_X_VISIBLE);
}

void menu_left_hide(void)
{
    if (!g_menu_panel)
        return;

    menu_left_animate_to(MENU_LEFT_X_HIDDEN);
}

void menu_left_toggle(void)
{
    if (menu_left_is_visible())
        menu_left_hide();
    else
        menu_left_show();
}
