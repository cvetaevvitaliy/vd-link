/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef SDL2_LVGL_INPUT_H
#define SDL2_LVGL_INPUT_H

#ifdef PLATFORM_DESKTOP

#include <SDL2/SDL.h>
#include "lvgl/lvgl.h"

int  sdl2_lvgl_input_init(void);
void sdl2_lvgl_input_deinit(void);

void sdl2_lvgl_input_set_viewport(int x, int y, int w, int h);

/* Feed SDL events (mouse + keyboard) main SDL loop. */
void sdl2_lvgl_input_process_event(const SDL_Event *e);

/* Inform SDL2→LVGL driver which LVGL object currently has keyboard focus */
void sdl2_lvgl_input_set_focus_obj(lv_obj_t *obj);

#endif /* PLATFORM_DESKTOP */

#endif //SDL2_LVGL_INPUT_H
