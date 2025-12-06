/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * SDL2 → LVGL pointer + keyboard input driver (desktop, LVGL v9.x).
 *
 * Pointer:
 *   - SDL thread only записує координати/стан кнопки в g_mouse (під mutex).
 *   - LVGL thread читає їх у lv_indev_read() callback.
 *
 * Keyboard:
 *   - SDL_TEXTINPUT / SDL_KEYDOWN → символи кладуться в кільцевий буфер.
 *   - LVGL timer періодично читає буфер і додає символи в поточний textarea.
 *
 * Все виклики LVGL API виконуються тільки з LVGL-потоку (timer + indev_read),
 * тому зависань від race-condition більше не буде.
 *
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "sdl2_lvgl_input.h"

#if defined(PLATFORM_DESKTOP)

#include <stdbool.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "ui/ui.h"

/* Fallbacks, in case they are not already defined somewhere. */
#ifndef LVGL_BUFF_WIDTH
#define LVGL_BUFF_WIDTH   1280
#endif

#ifndef LVGL_BUFF_HEIGHT
#define LVGL_BUFF_HEIGHT  720
#endif

/* --------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------- */

/* LVGL pointer input device handle. */
static lv_indev_t *g_indev = NULL;

/* Viewport where LVGL scene (1280x720) is drawn inside SDL window.
 * Must match dst rect in sdl2_display.c. */
static int g_vp_x = 0;
static int g_vp_y = 0;
static int g_vp_w = LVGL_BUFF_WIDTH;
static int g_vp_h = LVGL_BUFF_HEIGHT;

/* Shared mouse state (updated from SDL thread, read from LVGL thread). */
typedef struct {
    int32_t x;
    int32_t y;
    bool    pressed;
} sdl_mouse_state_t;

static sdl_mouse_state_t g_mouse = { 0, 0, false };

/* Keyboard ring buffer (ASCII / UTF-8 bytes). */
#define SDL2_LVGL_KEYBUF_SIZE 256
static char     g_keybuf[SDL2_LVGL_KEYBUF_SIZE];
static uint16_t g_key_head = 0;
static uint16_t g_key_tail = 0;

/* LVGL timer that flushes keyboard buffer into focused textarea. */
#define SDL2_LVGL_KEY_TIMER_PERIOD_MS  10
static lv_timer_t *g_key_timer = NULL;

/* Currently focused LVGL object for text input (usually textarea). */
static lv_obj_t *g_focus_obj = NULL;

/* Single mutex to protect mouse state, keybuf and focus object. */
static SDL_mutex *g_input_lock = NULL;

/* --------------------------------------------------------------------------
 * Helpers: viewport + coord mapping
 * -------------------------------------------------------------------------- */

/* Map SDL window coordinates to LVGL 1280x720 logical coordinates. */
static bool sdl2_to_lvgl_coords(int sdl_x, int sdl_y, int32_t *lv_x, int32_t *lv_y)
{
    /* Ignore if outside viewport. */
    if (sdl_x < g_vp_x || sdl_y < g_vp_y ||
        sdl_x >= g_vp_x + g_vp_w ||
        sdl_y >= g_vp_y + g_vp_h) {
        return false;
    }

    int rel_x = sdl_x - g_vp_x;
    int rel_y = sdl_y - g_vp_y;

    /* Scale to LVGL logical resolution. */
    *lv_x = (int32_t)((int64_t)rel_x * LVGL_BUFF_WIDTH  / g_vp_w);
    *lv_y = (int32_t)((int64_t)rel_y * LVGL_BUFF_HEIGHT / g_vp_h);

    return true;
}

/* --------------------------------------------------------------------------
 * Helpers: keyboard ring buffer
 * -------------------------------------------------------------------------- */

static void keybuf_push_char(char c)
{
    if (!g_input_lock)
        return;

    SDL_LockMutex(g_input_lock);

    uint16_t next_tail = (uint16_t)((g_key_tail + 1) % SDL2_LVGL_KEYBUF_SIZE);

    /* Drop oldest char if buffer is full. */
    if (next_tail == g_key_head) {
        g_key_head = (uint16_t)((g_key_head + 1) % SDL2_LVGL_KEYBUF_SIZE);
    }

    g_keybuf[g_key_tail] = c;
    g_key_tail = next_tail;

    SDL_UnlockMutex(g_input_lock);
}

static bool keybuf_pop_char(char *out)
{
    if (!out || !g_input_lock)
        return false;

    SDL_LockMutex(g_input_lock);

    if (g_key_head == g_key_tail) {
        SDL_UnlockMutex(g_input_lock);
        return false; /* empty */
    }

    *out = g_keybuf[g_key_head];
    g_key_head = (uint16_t)((g_key_head + 1) % SDL2_LVGL_KEYBUF_SIZE);

    SDL_UnlockMutex(g_input_lock);
    return true;
}

/* --------------------------------------------------------------------------
 * LVGL callbacks (run in LVGL thread)
 * -------------------------------------------------------------------------- */

/* Pointer read callback: LVGL polls current mouse state. */
static void sdl2_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    if (!data || !g_input_lock)
        return;

    int32_t x, y;
    bool    pressed;

    /* Copy shared mouse state under mutex. */
    SDL_LockMutex(g_input_lock);
    x       = g_mouse.x;
    y       = g_mouse.y;
    pressed = g_mouse.pressed;
    SDL_UnlockMutex(g_input_lock);

    data->point.x = x;
    data->point.y = y;
    data->state   = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

/* Keyboard flush timer: apply queued chars to focused textarea. */
static void sdl2_lvgl_key_timer_cb(lv_timer_t *t)
{
    (void)t;

    /* Snapshot focused object under lock. */
    lv_obj_t *focus = NULL;

    if (g_input_lock)
        SDL_LockMutex(g_input_lock);

    focus = g_focus_obj;

    if (g_input_lock)
        SDL_UnlockMutex(g_input_lock);

    if (!focus)
        return;

    char ch;
    while (keybuf_pop_char(&ch)) {
        /* Backspace. */
        if (ch == '\b') {
            /* Depending on LVGL version this might be
             * lv_textarea_delete_char() or lv_textarea_del_char(). */
#if LVGL_VERSION_MAJOR >= 9
            lv_textarea_delete_char(focus);
#else
            lv_textarea_del_char(focus);
#endif
            continue;
        }

        /* Enter / Return: send READY event to textarea. */
        if (ch == '\r' || ch == '\n') {
            lv_obj_send_event(focus, LV_EVENT_READY, NULL);
            continue;
        }

        /* Normal character. */
        char txt[2];
        txt[0] = ch;
        txt[1] = '\0';

        lv_textarea_add_text(focus, txt);
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int sdl2_lvgl_input_init(void)
{
    g_input_lock = SDL_CreateMutex();
    if (!g_input_lock) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                     "SDL_CreateMutex for LVGL input failed: %s", SDL_GetError());
        return -1;
    }

    memset(&g_mouse, 0, sizeof(g_mouse));
    g_mouse.pressed = false;

    g_key_head = 0;
    g_key_tail = 0;

    g_vp_x = 0;
    g_vp_y = 0;
    g_vp_w = LVGL_BUFF_WIDTH;
    g_vp_h = LVGL_BUFF_HEIGHT;

    /* Create pointer input device in standard (polled) mode. */
    g_indev = lv_indev_create();
    if (!g_indev) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "lv_indev_create() failed");
        SDL_DestroyMutex(g_input_lock);
        g_input_lock = NULL;
        return -1;
    }

    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, sdl2_lvgl_read_cb);

    /* Create LVGL timer that will flush keyboard buffer to focused textarea. */
    g_key_timer = lv_timer_create(sdl2_lvgl_key_timer_cb,
                                  SDL2_LVGL_KEY_TIMER_PERIOD_MS,
                                  NULL);

    return 0;
}

void sdl2_lvgl_input_deinit(void)
{
    if (g_key_timer) {
        lv_timer_del(g_key_timer);
        g_key_timer = NULL;
    }

    if (g_indev) {
#if LVGL_VERSION_MAJOR >= 9
        lv_indev_delete(g_indev);
#endif
        g_indev = NULL;
    }

    if (g_input_lock) {
        SDL_DestroyMutex(g_input_lock);
        g_input_lock = NULL;
    }

    g_focus_obj = NULL;
}

/* Viewport update from renderer (call з sdl2_display.c). */
void sdl2_lvgl_input_set_viewport(int x, int y, int w, int h)
{
    if (!g_input_lock)
        return;

    SDL_LockMutex(g_input_lock);

    g_vp_x = x;
    g_vp_y = y;
    g_vp_w = (w > 0) ? w : 1;
    g_vp_h = (h > 0) ? h : 1;

    SDL_UnlockMutex(g_input_lock);
}

/* Set focused LVGL object for text input (textarea). */
void sdl2_lvgl_input_set_focus_obj(lv_obj_t *obj)
{
    if (!g_input_lock)
        return;

    SDL_LockMutex(g_input_lock);
    g_focus_obj = obj;
    SDL_UnlockMutex(g_input_lock);
}

/* SDL event handler (mouse + keyboard).
 * Call this from your main SDL event loop. */
void sdl2_lvgl_input_process_event(const SDL_Event *e)
{
    if (!e || !g_input_lock)
        return;

    switch (e->type) {
    /* ---------------- Pointer ---------------- */
    case SDL_MOUSEBUTTONDOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int32_t lx, ly;
            if (!sdl2_to_lvgl_coords(e->button.x, e->button.y, &lx, &ly))
                return;

            SDL_LockMutex(g_input_lock);
            g_mouse.x       = lx;
            g_mouse.y       = ly;
            g_mouse.pressed = true;
            SDL_UnlockMutex(g_input_lock);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            int32_t lx, ly;
            if (!sdl2_to_lvgl_coords(e->button.x, e->button.y, &lx, &ly))
                return;

            SDL_LockMutex(g_input_lock);
            g_mouse.x       = lx;
            g_mouse.y       = ly;
            g_mouse.pressed = false;
            SDL_UnlockMutex(g_input_lock);
        }
        break;

    case SDL_MOUSEMOTION: {
        int32_t lx, ly;
        if (!sdl2_to_lvgl_coords(e->motion.x, e->motion.y, &lx, &ly))
            return;

        SDL_LockMutex(g_input_lock);
        g_mouse.x = lx;
        g_mouse.y = ly;
        /* pressed flag remains unchanged. */
        SDL_UnlockMutex(g_input_lock);
        break;
    }

    /* ---------------- Keyboard: text input ---------------- */
    case SDL_TEXTINPUT: {
        /* e->text.text is UTF-8, LVGL textarea also expects UTF-8.
         * Here we simply push bytes; LVGL will render them correctly. */
        const char *txt = e->text.text;
        if (!txt)
            break;

        // SDL_Log("SDL_TEXTINPUT: '%s'\n", txt);

        while (*txt) {
            keybuf_push_char(*txt);
            ++txt;
        }
        break;
    }

    /* ---------------- Keyboard: control keys ---------------- */
    case SDL_KEYDOWN: {
        SDL_Keycode sym = e->key.keysym.sym;

        switch (sym) {
        case SDLK_BACKSPACE:
            keybuf_push_char('\b');     /* backspace symbol */
            break;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            keybuf_push_char('\n');     /* treat Enter as newline / READY */
            break;

        default:
            /* Other keys are handled by SDL_TEXTINPUT (when text input is enabled). */
            break;
        }
        break;
    }

    default:
        break;
    }
}

#endif /* PLATFORM_DESKTOP */