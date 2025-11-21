/* SPDX-License-Identifier: GPL-2.0-only */

#include "addons.h"
#include "log.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef VDLINK_DEFAULT_PLUGIN_DIR
// #define VDLINK_DEFAULT_PLUGIN_DIR "/var/lib/vd-link/plugins"
#define VDLINK_DEFAULT_PLUGIN_DIR "/root"
#endif

typedef const subsystem_descriptor_t *(*descriptor_fn_t)(void);

typedef struct {
	char path[PATH_MAX];
	time_t mtime;
	void *handle;
	subsystem_shutdown_fn shutdown;
	const subsystem_descriptor_t *descriptor;
	bool seen;
} loaded_plugin_t;

extern const subsystem_host_api_t g_host_api;
static const char *module_name_str = "ADDONS";
static pthread_mutex_t g_plugins_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_scan_lock = PTHREAD_MUTEX_INITIALIZER;
static loaded_plugin_t *g_plugins = NULL;
static size_t g_plugin_count = 0;
static pthread_t g_watcher_thread;
static bool g_watcher_running = false;
static bool g_initialized = false;

static struct {
	char plugin_dir[PATH_MAX];
	char conf_file_path[PATH_MAX];
	bool is_debug_build;
	uint32_t scan_interval_ms;
	bool autocreate_directory;
} g_settings;

static subsystem_context_t g_subsystem_context;

static void addon_logger(subsystem_log_severity_t severity,
			const char *component,
			const char *message,
			void *user_data)
{
	(void)user_data;
	const char *tag = (component && component[0]) ? component : "addon";
	const char *text = message ? message : "";
	switch (severity) {
	case SUBSYS_LOG_ERROR:
		ERROR_M("%s", tag, text);
		break;
	case SUBSYS_LOG_WARN:
		WARN_M("%s", tag, text);
		break;
	case SUBSYS_LOG_INFO:
		INFO_M("%s", tag, text);
		break;
	case SUBSYS_LOG_DEBUG:
	default:
		DEBUG_M("%s", tag, text);
		break;
	}
}

static bool ensure_directory_exists(const char *path)
{
	struct stat st = {0};
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);
	}

	if (errno != ENOENT) {
		return false;
	}

	if (!g_settings.autocreate_directory) {
		return false;
	}

	char tmp[PATH_MAX];
	snprintf(tmp, sizeof(tmp), "%s", path);

	size_t len = strlen(tmp);
	if (len == 0) {
		return false;
	}

	if (tmp[len - 1] == '/') {
		tmp[len - 1] = '\0';
	}

	for (char *p = tmp + 1; *p; ++p) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
				return false;
			}
			*p = '/';
		}
	}

	if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
		return false;
	}

	return true;
}

static bool has_shared_object_extension(const char *name)
{
	size_t len = strlen(name);
	return (len > 3 && strcmp(name + len - 3, ".so") == 0);
}

static size_t find_plugin_index_locked(const char *path)
{
	for (size_t i = 0; i < g_plugin_count; ++i) {
		if (strncmp(g_plugins[i].path, path, PATH_MAX) == 0) {
			return i;
		}
	}
	return SIZE_MAX;
}

static loaded_plugin_t detach_plugin_locked(size_t idx)
{
	loaded_plugin_t plugin = g_plugins[idx];
	g_plugins[idx] = g_plugins[g_plugin_count - 1];
	g_plugin_count--;

	if (g_plugin_count == 0) {
		free(g_plugins);
		g_plugins = NULL;
	} else {
		loaded_plugin_t *resized = realloc(g_plugins, g_plugin_count * sizeof(*g_plugins));
		if (resized) {
			g_plugins = resized;
		}
	}

	return plugin;
}

static void unload_plugin_at(size_t idx)
{
	pthread_mutex_lock(&g_plugins_lock);
	if (idx >= g_plugin_count) {
		pthread_mutex_unlock(&g_plugins_lock);
		return;
	}
	loaded_plugin_t plugin = detach_plugin_locked(idx);
	pthread_mutex_unlock(&g_plugins_lock);

	char name_buf[128] = {0};
	const char *src_name = (plugin.descriptor && plugin.descriptor->name) ?
					 plugin.descriptor->name : NULL;
	if (src_name) {
		snprintf(name_buf, sizeof(name_buf), "%s", src_name);
	}

	if (plugin.shutdown) {
		plugin.shutdown();
	}
	if (plugin.handle) {
		dlclose(plugin.handle);
	}

	INFO("Unloaded plugin %s", name_buf[0] ? name_buf : "<unknown>");
}

static int load_plugin(const char *path, time_t mtime)
{
	void *handle = dlopen(path, RTLD_NOW);
	if (!handle) {
		ERROR("dlopen failed for %s: %s", path, dlerror());
		return -1;
	}

	descriptor_fn_t descriptor_fn = (descriptor_fn_t)dlsym(handle, VDLINK_SUBSYSTEM_DESCRIPTOR_FN);
	if (!descriptor_fn) {
		ERROR("%s missing subsystem descriptor", path);
		dlclose(handle);
		return -1;
	}

	const subsystem_descriptor_t *descriptor = descriptor_fn();
	if (!descriptor) {
		ERROR("%s returned NULL descriptor", path);
		dlclose(handle);
		return -1;
	}
	if (descriptor->api_version != VDLINK_SUBSYSTEM_API_VERSION) {
		ERROR("%s API version mismatch (plugin=%u, expected=%u)",
		      path, descriptor->api_version, VDLINK_SUBSYSTEM_API_VERSION);
		dlclose(handle);
		return -1;
	}
	if (!descriptor->init) {
		ERROR("%s descriptor missing init callback", path);
		dlclose(handle);
		return -1;
	}

	subsystem_shutdown_fn shutdown_fn = descriptor->shutdown;
	int init_rc = descriptor->init(&g_subsystem_context);

	if (init_rc != 0) {
		ERROR("%s init callback failed (%d)", path, init_rc);
		dlclose(handle);
		return -1;
	}

	loaded_plugin_t plugin = {0};
	snprintf(plugin.path, sizeof(plugin.path), "%s", path);
	plugin.mtime = mtime;
	plugin.handle = handle;
	plugin.shutdown = shutdown_fn;
	plugin.descriptor = descriptor;
	plugin.seen = true;

	pthread_mutex_lock(&g_plugins_lock);
	loaded_plugin_t *resized = realloc(g_plugins, sizeof(*g_plugins) * (g_plugin_count + 1));
	if (!resized) {
		pthread_mutex_unlock(&g_plugins_lock);
		if (plugin.shutdown) {
			plugin.shutdown();
		}
		dlclose(handle);
		ERROR("Out of memory while tracking %s", path);
		return -1;
	}
	g_plugins = resized;
	g_plugins[g_plugin_count++] = plugin;
	pthread_mutex_unlock(&g_plugins_lock);

	INFO("Loaded plugin %s", descriptor && descriptor->name ? descriptor->name : "<unnamed>");
	return 0;
}

static void handle_candidate(const char *path, time_t mtime)
{
	pthread_mutex_lock(&g_plugins_lock);
	size_t idx = find_plugin_index_locked(path);
	if (idx != SIZE_MAX) {
		g_plugins[idx].seen = true;
		bool needs_reload = (g_plugins[idx].mtime != mtime);
		pthread_mutex_unlock(&g_plugins_lock);
		if (needs_reload) {
			INFO("Reloading plugin %s", path);
			unload_plugin_at(idx);
			load_plugin(path, mtime);
		}
		return;
	}
	pthread_mutex_unlock(&g_plugins_lock);

	load_plugin(path, mtime);
}

static void scan_directory_once(void)
{
	pthread_mutex_lock(&g_scan_lock);

	if (!ensure_directory_exists(g_settings.plugin_dir)) {
		WARN("Plugin directory %s is unavailable", g_settings.plugin_dir);
		pthread_mutex_unlock(&g_scan_lock);
		return;
	}

	DIR *dir = opendir(g_settings.plugin_dir);
	if (!dir) {
		ERROR("Failed to open plugin directory %s: %s",
		       g_settings.plugin_dir, strerror(errno));
		pthread_mutex_unlock(&g_scan_lock);
		return;
	}

	pthread_mutex_lock(&g_plugins_lock);
	for (size_t i = 0; i < g_plugin_count; ++i) {
		g_plugins[i].seen = false;
	}
	pthread_mutex_unlock(&g_plugins_lock);

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		if (!has_shared_object_extension(entry->d_name)) {
			continue;
		}

		char full_path[PATH_MAX];
		snprintf(full_path, sizeof(full_path), "%s/%s", g_settings.plugin_dir, entry->d_name);

		struct stat sb = {0};
		if (stat(full_path, &sb) != 0) {
			continue;
		}
		if (!S_ISREG(sb.st_mode)) {
			continue;
		}

		handle_candidate(full_path, sb.st_mtime);
	}

	closedir(dir);

	pthread_mutex_lock(&g_plugins_lock);
	for (size_t i = 0; i < g_plugin_count;) {
		if (!g_plugins[i].seen) {
			size_t idx = i;
			pthread_mutex_unlock(&g_plugins_lock);
			unload_plugin_at(idx);
			pthread_mutex_lock(&g_plugins_lock);
			i = 0; // restart because list changed
			continue;
		}
		++i;
	}
	pthread_mutex_unlock(&g_plugins_lock);
	pthread_mutex_unlock(&g_scan_lock);
}

static void *watcher_thread_main(void *arg)
{
	(void)arg;
	while (g_watcher_running) {
		scan_directory_once();

		struct timespec ts = {0};
		ts.tv_sec = g_settings.scan_interval_ms / 1000;
		ts.tv_nsec = (g_settings.scan_interval_ms % 1000) * 1000000L;
		nanosleep(&ts, NULL);
	}
	return NULL;
}

int addons_manager_init(const addons_config_t *config)
{
	if (!config) {
		return -EINVAL;
	}
	if (g_initialized) {
		return 0;
	}

	memset(&g_settings, 0, sizeof(g_settings));
	const char *preferred_dir = (config->plugin_directory && config->plugin_directory[0]) ?
								 config->plugin_directory : VDLINK_DEFAULT_PLUGIN_DIR;
	const char *override_dir = getenv("VDLINK_PLUGIN_DIR");
	const char *dir_to_use = (override_dir && override_dir[0]) ? override_dir : preferred_dir;
	snprintf(g_settings.plugin_dir, sizeof(g_settings.plugin_dir), "%s", dir_to_use);
	if (config->conf_file_path) {
		snprintf(g_settings.conf_file_path, sizeof(g_settings.conf_file_path), "%s",
				 config->conf_file_path);
	}
	g_settings.is_debug_build = config->is_debug_build;
	g_settings.scan_interval_ms = config->scan_interval_ms ? config->scan_interval_ms : 5000u;
	g_settings.autocreate_directory = config->autocreate_directory;

	g_subsystem_context.is_debug_build = g_settings.is_debug_build;
	g_subsystem_context.conf_file_path = g_settings.conf_file_path[0] ? g_settings.conf_file_path : NULL;
	g_subsystem_context.logger = addon_logger;
	g_subsystem_context.logger_user_data = NULL;
	g_subsystem_context.host_api = &g_host_api;

	if (!ensure_directory_exists(g_settings.plugin_dir)) {
		WARN("Plugin directory %s is not available", g_settings.plugin_dir);
	}

	g_watcher_running = true;
	int rc = pthread_create(&g_watcher_thread, NULL, watcher_thread_main, NULL);
	if (rc != 0) {
		g_watcher_running = false;
		ERROR("Failed to start addon watcher thread");
		return -rc;
	}

	g_initialized = true;
	INFO("Plugin manager watching %s", g_settings.plugin_dir);
	return 0;
}

void addons_manager_shutdown(void)
{
	if (!g_initialized) {
		return;
	}

	g_watcher_running = false;
	pthread_join(g_watcher_thread, NULL);

	while (true) {
		pthread_mutex_lock(&g_plugins_lock);
		if (g_plugin_count == 0) {
			pthread_mutex_unlock(&g_plugins_lock);
			break;
		}
		loaded_plugin_t plugin = detach_plugin_locked(g_plugin_count - 1);
		pthread_mutex_unlock(&g_plugins_lock);

		if (plugin.shutdown) {
			plugin.shutdown();
		}
		if (plugin.handle) {
			dlclose(plugin.handle);
		}
	}

	g_initialized = false;
	INFO("Plugin manager stopped");
}

void addons_manager_force_rescan(void)
{
	if (!g_initialized) {
		return;
	}
	scan_directory_once();
}
