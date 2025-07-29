/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "lang.h"
#include <stdbool.h>

static const char **current_lang = lang_en;

void lang_set_ukrainian(void)
{
    current_lang = lang_ua;
}

void lang_set_english(void)
{
    current_lang = lang_en;
}

const char *lang_get_str(lang_key_t key)
{
    if (key >= STR_COUNT) return "?";
    return current_lang[key];
}

void lang_switch_cb(lv_event_t *e)
{
    // Check if the event is a click event
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    static bool is_ukr = false;
    is_ukr = !is_ukr;

    if (is_ukr)
        lang_set_ukrainian();
    else
        lang_set_english();

    //ui_update_texts(); // TODO: add a function to update user interface texts or implement updating all texts in ui.c
}