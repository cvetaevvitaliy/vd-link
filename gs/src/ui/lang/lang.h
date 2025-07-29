/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef LANG_H
#define LANG_H
#include "lvgl/lvgl.h"

typedef enum {
    STR_HELLO,
    STR_GOODBYE,
    STR_START,
    STR_STOP,
    STR_COUNT
} lang_key_t;

// Ukrainian
static const char *lang_ua[STR_COUNT] = {
    [STR_HELLO]     = "Привіт",
    [STR_GOODBYE]   = "До побачення",
    [STR_START]     = "Пуск",
    [STR_STOP]      = "Стоп"
};

// English
static const char *lang_en[STR_COUNT] = {
    [STR_HELLO]     = "Hello",
    [STR_GOODBYE]   = "Goodbye",
    [STR_START]     = "Start",
    [STR_STOP]      = "Stop"
};

void lang_set_ukrainian(void);
void lang_set_english(void);
const char *lang_get_str(lang_key_t key);
void lang_switch_cb(lv_event_t *e);

#endif //LANG_H
