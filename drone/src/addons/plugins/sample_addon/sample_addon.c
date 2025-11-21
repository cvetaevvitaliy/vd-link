/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <time.h>

#include "addons/subsystem_api.h"

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

    subsystem_log(ctx, SUBSYS_LOG_INFO, "sample_addon", message);
    return 0;
}

static void sample_addon_shutdown(void)
{
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
