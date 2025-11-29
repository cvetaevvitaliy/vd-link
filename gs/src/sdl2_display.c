/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#include "sdl2_display.h"

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only for initial window/texture size; runtime logic does not depend on it */
#define INITIAL_WIDTH   1280
#define INITIAL_HEIGHT  720

typedef struct {
    SDL_mutex   *lock;
    uint8_t     *y_plane;
    uint8_t     *u_plane;
    uint8_t     *v_plane;
    int          y_stride;
    int          u_stride;
    int          v_stride;
    int          width;
    int          height;
    bool         has_frame;
} video_state_t;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;

    SDL_Texture  *video_tex;
    int           video_tex_w;
    int           video_tex_h;

    SDL_Texture  *overlay_tex;
    uint32_t     *overlay_buffer;
    int           overlay_tex_w;
    int           overlay_tex_h;
    int           overlay_buf_w;
    int           overlay_buf_h;

    int           win_w;
    int           win_h;

    SDL_mutex    *osd_lock;
    bool          osd_dirty;

    bool          quit;

    /* fullscreen toggle state */
    bool          fullscreen;
    int           prev_win_x;
    int           prev_win_y;
    int           prev_win_w;
    int           prev_win_h;

    drm_osd_frame_done_cb_t osd_done_cb;
} sdl2_display_state_t;

static video_state_t         g_video = {0};
static sdl2_display_state_t  g_sdl   = {0};

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static void sdl2_recreate_video_texture_if_needed(int width, int height)
{
    if (!g_sdl.renderer)
        return;

    if (g_sdl.video_tex && g_sdl.video_tex_w == width && g_sdl.video_tex_h == height) {
        return;
    }

    if (g_sdl.video_tex) {
        SDL_DestroyTexture(g_sdl.video_tex);
        g_sdl.video_tex = NULL;
        g_sdl.video_tex_w = 0;
        g_sdl.video_tex_h = 0;
    }

    g_sdl.video_tex = SDL_CreateTexture(g_sdl.renderer, SDL_PIXELFORMAT_IYUV, /* planar YUV420 */
                                        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!g_sdl.video_tex) {
        fprintf(stderr, "SDL_CreateTexture(video) failed: %s\n", SDL_GetError());
        return;
    }

    g_sdl.video_tex_w = width;
    g_sdl.video_tex_h = height;
}

/* Compute destination rect to fit logical scene into window, keeping aspect */
static SDL_Rect sdl2_compute_dst_rect(int logical_w, int logical_h)
{
    SDL_Rect dst = { 0, 0, g_sdl.win_w, g_sdl.win_h };

    if (logical_w <= 0 || logical_h <= 0) {
        return dst;
    }

    float win_aspect    = (float)g_sdl.win_w / (float)g_sdl.win_h;
    float logical_aspect = (float)logical_w / (float)logical_h;

    if (win_aspect > logical_aspect) {
        /* Window wider than scene → fit by height */
        dst.h = g_sdl.win_h;
        dst.w = (int)((float)dst.h * logical_aspect);
        dst.x = (g_sdl.win_w - dst.w) / 2;
        dst.y = 0;
    } else {
        /* Window taller → fit by width */
        dst.w = g_sdl.win_w;
        dst.h = (int)((float)dst.w / logical_aspect);
        dst.x = 0;
        dst.y = (g_sdl.win_h - dst.h) / 2;
    }

    return dst;
}

/* Toggle fullscreen on double click */
static void sdl2_toggle_fullscreen(void)
{
    if (!g_sdl.window)
        return;

    if (!g_sdl.fullscreen) {
        /* store previous window rect */
        SDL_GetWindowPosition(g_sdl.window, &g_sdl.prev_win_x, &g_sdl.prev_win_y);
        SDL_GetWindowSize(g_sdl.window, &g_sdl.prev_win_w, &g_sdl.prev_win_h);

        if (SDL_SetWindowFullscreen(g_sdl.window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "SDL_SetWindowFullscreen ON failed: %s\n", SDL_GetError());
            return;
        }
        g_sdl.fullscreen = true;
    } else {
        if (SDL_SetWindowFullscreen(g_sdl.window, 0) != 0) {
            fprintf(stderr, "SDL_SetWindowFullscreen OFF failed: %s\n", SDL_GetError());
            return;
        }

        /* restore previous window rect (if sane) */
        if (g_sdl.prev_win_w > 0 && g_sdl.prev_win_h > 0) {
            SDL_SetWindowPosition(g_sdl.window, g_sdl.prev_win_x, g_sdl.prev_win_y);
            SDL_SetWindowSize(g_sdl.window, g_sdl.prev_win_w, g_sdl.prev_win_h);
        }

        g_sdl.fullscreen = false;
    }

    SDL_GetWindowSize(g_sdl.window, &g_sdl.win_w, &g_sdl.win_h);
}

/* ------------------------------------------------------------------------- */
/* Public API: init / deinit                                                 */
/* ------------------------------------------------------------------------- */

int sdl2_display_init(struct config_t *cfg)
{
    (void)cfg;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    g_video.lock = SDL_CreateMutex();
    if (!g_video.lock) {
        fprintf(stderr, "SDL_CreateMutex(video.lock) failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    g_sdl.window = SDL_CreateWindow("VD-Link " GIT_TAG " (branch:" GIT_BRANCH "-" GIT_HASH ")", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, INITIAL_WIDTH, INITIAL_HEIGHT,
                                    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_sdl.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    /* No vsync for minimal latency on desktop */
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    g_sdl.renderer = SDL_CreateRenderer(g_sdl.window, -1, renderer_flags);
    if (!g_sdl.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    /* Initial video texture (default size); will be recreated on first real frame */
    g_sdl.video_tex = SDL_CreateTexture(g_sdl.renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                        INITIAL_WIDTH, INITIAL_HEIGHT);
    if (!g_sdl.video_tex) {
        fprintf(stderr, "SDL_CreateTexture(video) failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g_sdl.renderer);
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }
    g_sdl.video_tex_w = INITIAL_WIDTH;
    g_sdl.video_tex_h = INITIAL_HEIGHT;

    /* Create default black YUV frame so we have black background before real video */
    g_video.width = INITIAL_WIDTH;
    g_video.height = INITIAL_HEIGHT;
    g_video.y_stride = INITIAL_WIDTH;
    g_video.u_stride = INITIAL_WIDTH / 2;
    g_video.v_stride = INITIAL_WIDTH / 2;

    size_t y_size = (size_t)g_video.y_stride * g_video.height;
    size_t u_size = (size_t)g_video.u_stride * (g_video.height / 2);
    size_t v_size = (size_t)g_video.v_stride * (g_video.height / 2);

    g_video.y_plane = (uint8_t*)malloc(y_size);
    g_video.u_plane = (uint8_t*)malloc(u_size);
    g_video.v_plane = (uint8_t*)malloc(v_size);

    if (!g_video.y_plane || !g_video.u_plane || !g_video.v_plane) {
        fprintf(stderr, "malloc default YUV failed\n");
        free(g_video.y_plane);
        free(g_video.u_plane);
        free(g_video.v_plane);
        g_video.y_plane = g_video.u_plane = g_video.v_plane = NULL;

        SDL_DestroyTexture(g_sdl.video_tex);
        SDL_DestroyRenderer(g_sdl.renderer);
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    memset(g_video.y_plane, 0, y_size);
    memset(g_video.u_plane, 128, u_size);
    memset(g_video.v_plane, 128, v_size);
    g_video.has_frame = true;

    /* Overlay texture (ARGB8888). Logical size = LVGL buffer size (initially 1280x720). */
    g_sdl.overlay_tex_w = INITIAL_WIDTH;
    g_sdl.overlay_tex_h = INITIAL_HEIGHT;
    g_sdl.overlay_buf_w = INITIAL_WIDTH;
    g_sdl.overlay_buf_h = INITIAL_HEIGHT;

    g_sdl.overlay_tex = SDL_CreateTexture(g_sdl.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                          g_sdl.overlay_tex_w, g_sdl.overlay_tex_h);
    if (!g_sdl.overlay_tex) {
        fprintf(stderr, "SDL_CreateTexture(overlay) failed: %s\n", SDL_GetError());
        SDL_DestroyTexture(g_sdl.video_tex);
        SDL_DestroyRenderer(g_sdl.renderer);
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    SDL_SetTextureBlendMode(g_sdl.overlay_tex, SDL_BLENDMODE_BLEND);

    g_sdl.overlay_buffer = (uint32_t*)malloc((size_t)g_sdl.overlay_buf_w * g_sdl.overlay_buf_h * 4);
    if (!g_sdl.overlay_buffer) {
        fprintf(stderr, "malloc overlay_buffer failed\n");
        SDL_DestroyTexture(g_sdl.overlay_tex);
        SDL_DestroyTexture(g_sdl.video_tex);
        SDL_DestroyRenderer(g_sdl.renderer);
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    memset(g_sdl.overlay_buffer, 0, (size_t)g_sdl.overlay_buf_w * g_sdl.overlay_buf_h * 4);

    g_sdl.osd_lock = SDL_CreateMutex();
    if (!g_sdl.osd_lock) {
        fprintf(stderr, "SDL_CreateMutex(osd_lock) failed: %s\n", SDL_GetError());
        free(g_sdl.overlay_buffer);
        SDL_DestroyTexture(g_sdl.overlay_tex);
        SDL_DestroyTexture(g_sdl.video_tex);
        SDL_DestroyRenderer(g_sdl.renderer);
        SDL_DestroyWindow(g_sdl.window);
        SDL_DestroyMutex(g_video.lock);
        SDL_Quit();
        return -1;
    }

    g_sdl.osd_dirty   = true; /* force first render */
    g_sdl.quit        = false;
    g_sdl.osd_done_cb = NULL;
    g_sdl.fullscreen  = false;
    g_sdl.prev_win_x  = SDL_WINDOWPOS_CENTERED;
    g_sdl.prev_win_y  = SDL_WINDOWPOS_CENTERED;
    g_sdl.prev_win_w  = INITIAL_WIDTH;
    g_sdl.prev_win_h  = INITIAL_HEIGHT;

    SDL_GetWindowSize(g_sdl.window, &g_sdl.win_w, &g_sdl.win_h);

    return 0;
}

int sdl2_display_deinit(void)
{
    g_sdl.quit = true;

    if (g_sdl.osd_lock) {
        SDL_DestroyMutex(g_sdl.osd_lock);
        g_sdl.osd_lock = NULL;
    }

    if (g_sdl.overlay_buffer) {
        free(g_sdl.overlay_buffer);
        g_sdl.overlay_buffer = NULL;
    }

    if (g_sdl.overlay_tex) {
        SDL_DestroyTexture(g_sdl.overlay_tex);
        g_sdl.overlay_tex = NULL;
    }

    if (g_sdl.video_tex) {
        SDL_DestroyTexture(g_sdl.video_tex);
        g_sdl.video_tex = NULL;
    }

    if (g_sdl.renderer) {
        SDL_DestroyRenderer(g_sdl.renderer);
        g_sdl.renderer = NULL;
    }

    if (g_sdl.window) {
        SDL_DestroyWindow(g_sdl.window);
        g_sdl.window = NULL;
    }

    if (g_video.y_plane) {
        free(g_video.y_plane);
        g_video.y_plane = NULL;
    }
    if (g_video.u_plane) {
        free(g_video.u_plane);
        g_video.u_plane = NULL;
    }
    if (g_video.v_plane) {
        free(g_video.v_plane);
        g_video.v_plane = NULL;
    }

    if (g_video.lock) {
        SDL_DestroyMutex(g_video.lock);
        g_video.lock = NULL;
    }

    SDL_Quit();
    return 0;
}

void sdl2_set_osd_frame_done_callback(drm_osd_frame_done_cb_t cb)
{
    g_sdl.osd_done_cb = cb;
}

/* frame pushers (thread-safe)*/
int sdl2_push_new_video_frame(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                              int width, int height,
                              int y_stride, int uv_stride)
{
    if (!g_video.lock || !y || !u || !v || width <= 0 || height <= 0 || y_stride <= 0 || uv_stride <= 0)
        return -1;

    SDL_LockMutex(g_video.lock);

    bool need_realloc = (!g_video.y_plane || !g_video.u_plane || !g_video.v_plane) ||
                        g_video.width != width ||
                        g_video.height != height ||
                        g_video.y_stride != y_stride ||
                        g_video.u_stride != uv_stride ||
                        g_video.v_stride != uv_stride;

    if (need_realloc) {
        free(g_video.y_plane);
        free(g_video.u_plane);
        free(g_video.v_plane);

        g_video.width    = width;
        g_video.height   = height;
        g_video.y_stride = y_stride;
        g_video.u_stride = uv_stride;
        g_video.v_stride = uv_stride;

        size_t y_size = (size_t)g_video.y_stride * g_video.height;
        size_t u_size = (size_t)g_video.u_stride * (g_video.height / 2);
        size_t v_size = (size_t)g_video.v_stride * (g_video.height / 2);

        g_video.y_plane = (uint8_t*)malloc(y_size);
        g_video.u_plane = (uint8_t*)malloc(u_size);
        g_video.v_plane = (uint8_t*)malloc(v_size);

        if (!g_video.y_plane || !g_video.u_plane || !g_video.v_plane) {
            fprintf(stderr, "malloc failed in sdl2_push_new_video_frame\n");
            free(g_video.y_plane);
            free(g_video.u_plane);
            free(g_video.v_plane);
            g_video.y_plane = g_video.u_plane = g_video.v_plane = NULL;
            SDL_UnlockMutex(g_video.lock);
            return -1;
        }
    }

    memcpy(g_video.y_plane, y, (size_t)g_video.y_stride * g_video.height);
    memcpy(g_video.u_plane, u, (size_t)g_video.u_stride * (g_video.height / 2));
    memcpy(g_video.v_plane, v, (size_t)g_video.v_stride * (g_video.height / 2));
    g_video.has_frame = true;

    SDL_UnlockMutex(g_video.lock);
    return 0;
}

/*
 * Push new OSD frame (ARGB8888).
 * Can be called from ANY thread (e.g. OSD/overlay thread).
 */
int sdl2_push_new_osd_frame(const void *src_addr, int width, int height)
{
    if (!g_sdl.overlay_buffer || !g_sdl.osd_lock)
        return -1;

    if (!src_addr || width <= 0 || height <= 0)
        return -1;

    const uint32_t *src = (const uint32_t *)src_addr;
    uint32_t       *dst = g_sdl.overlay_buffer;

    SDL_LockMutex(g_sdl.osd_lock);

    int copy_w = width;
    int copy_h = height;
    if (copy_w > g_sdl.overlay_buf_w)
        copy_w = g_sdl.overlay_buf_w;
    if (copy_h > g_sdl.overlay_buf_h)
        copy_h = g_sdl.overlay_buf_h;

    for (int y = 0; y < copy_h; ++y) {
        memcpy(dst + y * g_sdl.overlay_buf_w,
               src + y * width,
               (size_t)copy_w * 4);
    }

    g_sdl.osd_dirty = true;

    SDL_UnlockMutex(g_sdl.osd_lock);

    return 0;
}


/* poll/render (must be called from main thread) */
int sdl2_display_poll(void)
{
    if (!g_sdl.window || !g_sdl.renderer)
        return -1;

    /* Handle events (close window, resize, ESC, double click) */
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            g_sdl.quit = true;
        } else if (ev.type == SDL_WINDOWEVENT &&
                   ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            g_sdl.win_w = ev.window.data1;
            g_sdl.win_h = ev.window.data2;
        } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            //g_sdl.quit = true;
            //g_sdl.fullscreen = false;
            //sdl2_toggle_fullscreen();
        } else if (ev.type == SDL_MOUSEBUTTONDOWN &&
                   ev.button.button == SDL_BUTTON_LEFT &&
                   ev.button.clicks == 2) {
            /* double left click -> toggle fullscreen */
            sdl2_toggle_fullscreen();
        }
    }

    if (g_sdl.quit)
        return -1;

    SDL_GetWindowSize(g_sdl.window, &g_sdl.win_w, &g_sdl.win_h);

    /* Update video texture from latest YUV frame (or keep last) */
    SDL_LockMutex(g_video.lock);
    bool have_video = g_video.has_frame &&
                      g_video.y_plane && g_video.u_plane && g_video.v_plane;
    int v_w = have_video ? g_video.width  : 0;
    int v_h = have_video ? g_video.height : 0;

    if (have_video) {
        sdl2_recreate_video_texture_if_needed(v_w, v_h);
        if (g_sdl.video_tex) {
            SDL_Rect rect = {0, 0, v_w, v_h};
            SDL_UpdateYUVTexture(
                g_sdl.video_tex,
                &rect,
                g_video.y_plane, g_video.y_stride,
                g_video.u_plane, g_video.u_stride,
                g_video.v_plane, g_video.v_stride
            );
        }
    }
    SDL_UnlockMutex(g_video.lock);

    /* Upload overlay buffer to overlay texture if dirty */
    SDL_LockMutex(g_sdl.osd_lock);
    bool osd_dirty = g_sdl.osd_dirty;
    g_sdl.osd_dirty = false;
    SDL_UnlockMutex(g_sdl.osd_lock);

    if (g_sdl.overlay_tex && g_sdl.overlay_buffer && osd_dirty) {
        void *tex_pixels;
        int tex_pitch;
        if (SDL_LockTexture(g_sdl.overlay_tex, NULL, &tex_pixels, &tex_pitch) == 0) {
            uint8_t       *dst = (uint8_t *)tex_pixels;
            const uint8_t *src = (const uint8_t *)g_sdl.overlay_buffer;
            int row_bytes = g_sdl.overlay_buf_w * 4;

            for (int y = 0; y < g_sdl.overlay_tex_h; ++y) {
                memcpy(dst + y * tex_pitch,
                       src + y * row_bytes,
                       (size_t)row_bytes);
            }
            SDL_UnlockTexture(g_sdl.overlay_tex);
        }
    }

    int logical_w = g_sdl.overlay_tex_w;
    int logical_h = g_sdl.overlay_tex_h;
    if (logical_w <= 0 || logical_h <= 0) {
        /* fallback to video size if overlay is not ready */
        logical_w = (v_w > 0) ? v_w : INITIAL_WIDTH;
        logical_h = (v_h > 0) ? v_h : INITIAL_HEIGHT;
    }

    SDL_Rect dst = sdl2_compute_dst_rect(logical_w, logical_h);

    /* Render video + overlay */
    SDL_SetRenderDrawColor(g_sdl.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_sdl.renderer);

    if (g_sdl.video_tex && have_video) {
        SDL_RenderCopy(g_sdl.renderer, g_sdl.video_tex, NULL, &dst);
    }

    if (g_sdl.overlay_tex) {
        SDL_RenderCopy(g_sdl.renderer, g_sdl.overlay_tex, NULL, &dst);
    }

    SDL_RenderPresent(g_sdl.renderer);

    if (g_sdl.osd_done_cb) {
        g_sdl.osd_done_cb();
    }

    SDL_Delay(4); /* prevent 100% CPU */

    return 0;
}
