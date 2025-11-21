/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Lightweight plugin manager that discovers and loads shared-object subsystems
 * from a watched directory.
 */

#ifndef VDLINK_ADDONS_H
#define VDLINK_ADDONS_H

#include <stdbool.h>
#include <stdint.h>

#include "subsystem_api.h"

typedef struct {
    const char *plugin_directory;     // Directory to monitor for .so plugins
    const char *conf_file_path;       // Path forwarded into subsystem_context_t
    bool is_debug_build;              // Propagate build mode to plugins
    uint32_t scan_interval_ms;        // Directory rescan interval (defaults to 5000ms)
    bool autocreate_directory;        // Create directory if missing (defaults to true)
} addons_config_t;

int addons_manager_init(const addons_config_t *config);
void addons_manager_shutdown(void);
void addons_manager_force_rescan(void);

#endif // VDLINK_ADDONS_H
