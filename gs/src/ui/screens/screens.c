/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "screens.h"

#include "unistd.h"
#include "lvgl/lvgl.h"

#include <src/misc/lv_event_private.h>

/* central tap area to hide left/right menus  */
static lv_obj_t *g_tap_area = NULL;

static void screens_tap_area_event_cb(lv_event_t *e)
{
    if (e->code != LV_EVENT_CLICKED)
        return;

    menu_left_hide();
    menu_right_hide();
}

static void screens_tap_area(void)
{
    lv_obj_t *root = lv_scr_act();
    if (!root)
        return;

    if (g_tap_area)
        return;

    g_tap_area = lv_obj_create(root);
    lv_obj_remove_style_all(g_tap_area);

    lv_obj_set_size(g_tap_area, LV_PCT(100), LV_PCT(100));

    lv_obj_clear_flag(g_tap_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_tap_area, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(g_tap_area, screens_tap_area_event_cb,LV_EVENT_CLICKED, NULL);

}

void screens_init(void)
{
    /* Create central tap area to hide menus */
    screens_tap_area();

    /* Initialize individual screens */
    screen_status_init();
    screen_settings_init();
    menu_right_init();
    menu_left_init();
}