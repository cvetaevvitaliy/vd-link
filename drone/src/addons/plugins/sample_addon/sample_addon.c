/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "addons/subsystem_api.h"

static const subsystem_context_t *g_ctx = NULL;
static pthread_t g_worker_thread;
static volatile bool g_thread_should_run = false;
static bool g_worker_started = false;

static void fc_properties_callback(const fc_properties_t *properties, uint64_t *timestamp_ms)
{
    (void)timestamp_ms;
    if (!properties) {
        return;
    }
    fprintf(stderr,
        "[sample_addon] fc callback: roll=%0.2f pitch=%0.2f yaw=%0.2f alt=%0.2f\n",
        properties->attitude.roll,
        properties->attitude.pitch,
        properties->attitude.yaw,
        properties->altitude_m);
}

static void demo_fc_api(const subsystem_context_t *ctx)
{
    const subsystem_host_api_t *api = ctx->host_api;
    if (!api) {
        subsystem_log(ctx, SUBSYS_LOG_DEBUG, "sample_addon",
                  "Host API missing; skipping FC helpers");
        return;
    }

    static int direction = 1;
    static uint16_t base = 1500;
    const size_t channel_count = 8;
    uint16_t demo_channels[channel_count];
    uint8_t channels_map[] = {1, 1, 1, 1, 0, 0, 0, 0};
    for (size_t i = 0; i < channel_count; ++i) {
        int16_t offset = (int16_t)(i * 25);
        int value = (int)base + direction * offset;
        if (value < 1000) {
            value = 1000;
        } else if (value > 2000) {
            value = 2000;
        }
        demo_channels[i] = (uint16_t)value;
    }

    base += (direction * 20);
    if (base > 1900) {
        direction = -1;
        base = 1900;
    } else if (base < 1100) {
        direction = 1;
        base = 1100;
    }

    if (api->fc.enable_rc_override) {
        int rc = api->fc.enable_rc_override(channels_map, channel_count);
        subsystem_log(ctx, rc == 0 ? SUBSYS_LOG_INFO : SUBSYS_LOG_WARN,
                  "sample_addon",
                  rc == 0 ? "RC override enabled" : "RC override failed");
    }

    if (api->fc.send_rc_buf_override) {
        int rc = api->fc.send_rc_buf_override(demo_channels, channel_count);
        subsystem_log(ctx, rc == 0 ? SUBSYS_LOG_INFO : SUBSYS_LOG_WARN,
                  "sample_addon",
                  rc == 0 ? "RC buffer override sent" : "RC buffer override failed");
    }

    if (api->fc.send_rc_override) {
        uint16_t throttle = demo_channels[0];
        uint16_t yaw = demo_channels[1];
        uint16_t pitch = demo_channels[2];
        uint16_t roll = demo_channels[3];
        int rc = api->fc.send_rc_override(throttle, yaw, pitch, roll,
                                 demo_channels[4], demo_channels[5],
                                 demo_channels[6], demo_channels[7]);
        subsystem_log(ctx, rc == 0 ? SUBSYS_LOG_INFO : SUBSYS_LOG_WARN,
                  "sample_addon",
                  rc == 0 ? "RC override (individual) sent" :
                  "RC override (individual) failed");
    }

    if (api->fc.register_fc_property_update_callback) {
        int rc = api->fc.register_fc_property_update_callback(fc_properties_callback, 5u);
        subsystem_log(ctx, rc == 0 ? SUBSYS_LOG_INFO : SUBSYS_LOG_WARN,
                  "sample_addon",
                  rc == 0 ? "FC property callback registered" :
                  "FC property callback registration failed");
    }
}

static void demo_overlay_api(const subsystem_context_t *ctx)
{
    const subsystem_host_api_t *api = ctx->host_api;
    if (!api) {
        return;
    }

    subsystem_overlay_point_norm_t center = {0.5f, 0.5f};
    float crosshair_step = 0.1f;
    subsystem_overlay_point_norm_t top_left = {0.2f, 0.2f};
    subsystem_overlay_point_norm_t bottom_right = {0.8f, 0.8f};
    float box_step = 0.05f;

    char osd_text[64];
    static uint32_t counter = 0;
    snprintf(osd_text, sizeof(osd_text), "Sample OSD Counter: %u", counter++);

    if (api->overlay.draw_text) {
        api->overlay.draw_text(top_left, osd_text, SUBSYS_OVERLAY_COLOR_GREEN, 2);
    }
    if (api->overlay.draw_rectangle) {
        api->overlay.draw_rectangle(top_left, bottom_right, SUBSYS_OVERLAY_COLOR_RED, 1);
    }
    if (api->overlay.draw_crosshair) {
        api->overlay.draw_crosshair(center, 0.1f, SUBSYS_OVERLAY_COLOR_WHITE, 1);
    }
    if (api->overlay.draw_screen) {
        api->overlay.draw_screen();
    }
    if (api->overlay.clear) {
        api->overlay.clear();
    }

    if (center.y > 0.9f) {
        crosshair_step = -0.1f;
    } else if (center.y < 0.1f) {
        crosshair_step = 0.1f;
    }
    center.y += crosshair_step;
    
    if (bottom_right.x > 0.9f) {
        box_step = -0.05f;
    } else if (bottom_right.x < 0.6f) {
        box_step = 0.05f;
    }
    bottom_right.x += box_step;
    
}

static void demo_video_api(const subsystem_context_t *ctx)
{
    const subsystem_host_api_t *api = ctx ? ctx->host_api : NULL;
    if (!api) {
        return;
    }

    if (api->video.get_stream_frame) {
        uint8_t frame_stub[64] = {0};
        uint32_t frame_size = 0;
        uint64_t timestamp_ms = 0;
        api->video.get_stream_frame(frame_stub, &frame_size, &timestamp_ms);
        subsystem_log(ctx, SUBSYS_LOG_INFO, "sample_addon",
                  "Polled video stream frame");
    }
}

static void *sample_addon_thread(void *arg)
{
    (void)arg;
    bool started = false;
    const subsystem_context_t *ctx = g_ctx;
    const subsystem_host_api_t *api = ctx ? ctx->host_api : NULL;

    if (!ctx || !api) {
        return NULL;
    }
    subsystem_log(ctx, SUBSYS_LOG_INFO, "sample_addon",
              "Background thread started");

    if (api->video.start_receiving_stream) {
        started = (api->video.start_receiving_stream(1280, 720) == 0);
    }

    while (g_thread_should_run) {
        demo_fc_api(ctx);
        demo_overlay_api(ctx);
        demo_video_api(ctx);

        sleep(1);
    }

    if (started && api->video.stop_receiving_stream) {
        api->video.stop_receiving_stream();
    }
    return NULL;
}

static int sample_addon_init(const subsystem_context_t *ctx)
{
    char message[256];
    time_t now = time(NULL);
    struct tm tm_now = *localtime(&now);
    snprintf(message, sizeof(message),
             "Sample addon ready (debug=%s, config=%s, time=%02d:%02d:%02d)",
             ctx->is_debug_build ? "true" : "false",
             ctx->conf_file_path ? ctx->conf_file_path : "<none>",
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    subsystem_log(ctx, SUBSYS_LOG_INFO, "sample_addon started", message);

    g_ctx = ctx;
    g_thread_should_run = true;
    if (pthread_create(&g_worker_thread, NULL, sample_addon_thread, NULL) == 0) {
        g_worker_started = true;
    } else {
        g_thread_should_run = false;
        subsystem_log(ctx, SUBSYS_LOG_WARN, "sample_addon",
                  "Failed to start background thread");
    }
    return 0;
}

static void sample_addon_shutdown(void)
{
    if (g_worker_started) {
        g_thread_should_run = false;
        pthread_join(g_worker_thread, NULL);
        g_worker_started = false;
    }
    g_ctx = NULL;
    fprintf(stderr, "[sample_addon] shutdown invoked\n");
}

static subsystem_descriptor_t sample_addon_descriptor = {
    .api_version = VDLINK_SUBSYSTEM_API_VERSION,
    .name = "sample_addon",
    .version = "1.0.0",
    .init = sample_addon_init,
    .shutdown = sample_addon_shutdown,
};

const subsystem_descriptor_t *vdlink_get_subsystem_descriptor(void)
{
    return &sample_addon_descriptor;
}
