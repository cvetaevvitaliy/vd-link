/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef UI_H
#define UI_H
#include "lvgl/lvgl.h"

#define LVGL_BUFF_WIDTH 1280
#define LVGL_BUFF_HEIGHT 720

LV_FONT_DECLARE(montserrat_cyrillic_14);
LV_FONT_DECLARE(montserrat_cyrillic_16);
LV_FONT_DECLARE(montserrat_cyrillic_medium_16);
LV_FONT_DECLARE(montserrat_cyrillic_18);
LV_FONT_DECLARE(montserrat_cyrillic_medium_18);
LV_FONT_DECLARE(montserrat_cyrillic_20);
LV_FONT_DECLARE(montserrat_cyrillic_medium_20);
LV_FONT_DECLARE(montserrat_cyrillic_22);
LV_FONT_DECLARE(montserrat_cyrillic_medium_22);
LV_FONT_DECLARE(montserrat_cyrillic_24);
LV_FONT_DECLARE(montserrat_cyrillic_26);
LV_FONT_DECLARE(montserrat_cyrillic_28);
LV_FONT_DECLARE(montserrat_cyrillic_30);
LV_FONT_DECLARE(montserrat_cyrillic_32);
LV_FONT_DECLARE(montserrat_cyrillic_34);
LV_FONT_DECLARE(montserrat_cyrillic_36);
LV_FONT_DECLARE(montserrat_cyrillic_38);
LV_FONT_DECLARE(montserrat_cyrillic_40);
LV_FONT_DECLARE(montserrat_cyrillic_42);
LV_FONT_DECLARE(montserrat_cyrillic_48);

int ui_init(void);
void ui_set_fps(float fps);
void ui_set_server_ping(uint32_t data);
void ui_deinit(void);

#endif //UI_H
