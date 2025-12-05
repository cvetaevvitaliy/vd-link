# Plugin Infrastructure

VD-Link Drone now supports loading optional subsystems that are packaged as shared
objects (`.so`). At runtime the daemon watches the plugin directory and loads new
files automatically without restarts.

## Runtime behaviour

- Default directory: `${CMAKE_INSTALL_PREFIX}/lib/vd-link/plugins` (override with
  `VDLINK_PLUGIN_DIR=/path/to/plugins`).
- The directory is polled every 5 seconds. New or updated `.so` files are loaded,
  removed files are unloaded, and the plugin `shutdown` callback (if provided) is
  invoked.
- Each plugin must export the descriptor API entry point
  `vdlink_get_subsystem_descriptor()`.

## C API

Public headers live under `drone/src/addons/`:

```c
#include "addons/subsystem_api.h"

static int my_init(const subsystem_context_t *ctx) {
    subsystem_log(ctx, SUBSYS_LOG_INFO, "example", "hello!");
    return 0;
}

static void my_shutdown(void) {
    /* cleanup */
}

static subsystem_descriptor_t desc = {
    .api_version = VDLINK_SUBSYSTEM_API_VERSION,
    .name = "example",
    .version = "1.0.0",
    .init = my_init,
    .shutdown = my_shutdown,
};

const subsystem_descriptor_t *vdlink_get_subsystem_descriptor(void) {
    return &desc;
}
```

The `subsystem_context_t` struct currently exposes:

| Field | Description |
| ----- | ----------- |
| `is_debug_build` | Build type of the running binary. |
| `conf_file_path` | Path to `vd-link.config`. Use this to read shared configuration. |
| `logger` / `logger_user_data` | Callback used by `subsystem_log()` helper for consistent logging. |
| `host_api` | Optional struct with function pointers provided by the host (see below). |

### Host API helpers

`subsystem_host_api_t` exposes opt-in helpers injected by the main binary. Always
check the pointer for `NULL` before use to remain compatible with older builds.

Currently available helpers:

- `enable_rc_override(const uint16_t *channels, size_t channel_count)`
  Enables RC override with the supplied channel values. The current stub simply
  returns `-ENOTSUP`, but addons can start calling it today and they will gain
  the feature automatically once the host wires in a backend implementation.

## Sample addon

`drone/src/addons/plugins/sample_logger` implements a minimal plugin that prints a
message during initialization. It is built automatically and installed into the
plugin directory along with the main binary. Use it as a template for custom
subsystems.

## Developing plugins

1. Place your sources under `drone/src/addons/plugins/<name>/` and add a shared
   library target to the nested `CMakeLists.txt` (see the sample for reference).
2. Include `addons/subsystem_api.h` for the public interfaces.
3. Keep plugins self-contained: they can parse the shared configuration file or
   communicate with the main process through existing IPC mechanisms.

At runtime you can copy new `.so` files into the watched directory (on the drone
filesystem) and the daemon will handle (re)loading automatically.

## Deployment

To deploy a built plugin to the target board, copy the shared object file to the plugin directory.

Default plugin directory: `/var/lib/vd-link/plugins`

Example `scp` command:

```bash
ssh root@192.168.55.1 "mkdir -p /var/lib/vd-link/plugins"
scp cmake-build-debug-docker-rv1126/drone/src/addons/plugins/sample_addon/sample_addon.so root@192.168.55.1:/var/lib/vd-link/plugins
```
