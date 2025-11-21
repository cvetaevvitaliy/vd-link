/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Public runtime interface for dynamically loaded VD-Link subsystems.
 * Every plugin must export vdlink_get_subsystem_descriptor() returning a
 * fully populated descriptor struct.
 */

#ifndef VDLINK_SUBSYSTEM_API_H
#define VDLINK_SUBSYSTEM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VDLINK_SUBSYSTEM_API_VERSION 1u

#define VDLINK_SUBSYSTEM_DESCRIPTOR_FN "vdlink_get_subsystem_descriptor"

typedef enum {
	SUBSYS_LOG_ERROR = 0,
	SUBSYS_LOG_WARN,
	SUBSYS_LOG_INFO,
	SUBSYS_LOG_DEBUG
} subsystem_log_severity_t;

typedef void (*subsystem_log_fn)(subsystem_log_severity_t severity,
								 const char *component,
								 const char *message,
								 void *user_data);

typedef int (*subsystem_enable_rc_override_fn)(const uint16_t *channels,
												 size_t channel_count);

typedef struct subsystem_host_api_s {
	subsystem_enable_rc_override_fn enable_rc_override;
} subsystem_host_api_t;

typedef struct subsystem_context_s {
	bool is_debug_build;
	const char *conf_file_path;
	subsystem_log_fn logger;
	void *logger_user_data;
	const subsystem_host_api_t *host_api;
} subsystem_context_t;

static inline void subsystem_log(const subsystem_context_t *ctx,
								 subsystem_log_severity_t severity,
								 const char *component,
								 const char *message)
{
	if (ctx && ctx->logger) {
		ctx->logger(severity, component, message, ctx->logger_user_data);
	}
}

typedef int (*subsystem_init_fn)(const subsystem_context_t *ctx);
typedef void (*subsystem_shutdown_fn)(void);

typedef struct {
	uint32_t api_version;
	const char *name;
	const char *version;
	subsystem_init_fn init;
	subsystem_shutdown_fn shutdown;
} subsystem_descriptor_t;

#endif // VDLINK_SUBSYSTEM_API_H
