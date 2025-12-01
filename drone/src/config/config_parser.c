/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "config/config_parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <arpa/inet.h>

/**
 * Example config file
 *
 *    [protocol]               # Protocol configuration
 *    version=6                # IPv6
 *
 *    [user]
 *    name = Bob Smith         # Spaces around '=' are stripped
 *    email = bob@smith.com    # And comments (like this) ignored
 *    active = true            # Test a boolean
 *    pi = 3.14159             # Test a floating point number
 *    trillion = 1000000000000 # Test 64-bit integers
 *
 * example https://github.com/benhoyt/inih/blob/master/examples/ini_example.c
 */

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

const resolution_preset_t RESOLUTIONS[] = {
    { "FullHD", 1920, 1080 },
    { "HD",     1280,  720 },
    { "qHD",     960,  540 },
    { "SD",      720,  480 },
    { "XGA",    1024,  768 },
    { "VGA",     640,  480 },
};
#define NUM_RESOLUTIONS (sizeof(RESOLUTIONS)/sizeof(RESOLUTIONS[0]))

int config_parser_dumper(void* user, const char* section, const char* name, const char* value)
{
    static char prev_section[50] = "";

    if (strcmp(section, prev_section)) {
        printf("%s[%s]\n", (prev_section[0] ? "\n" : ""), section);
        strncpy(prev_section, section, sizeof(prev_section));
        prev_section[sizeof(prev_section) - 1] = '\0';
    }
    printf("%s = %s\n", name, value);
    return 0;
}

static int config_file_dumper(void* user, const char* section, const char* name, const char* value)
{
    FILE *file = (FILE*)user;
    static char prev_section[50] = "";

    if (strcmp(section, prev_section)) {
        fprintf(file, "%s[%s]\n", (prev_section[0] ? "\n" : ""), section);
        strncpy(prev_section, section, sizeof(prev_section));
        prev_section[sizeof(prev_section) - 1] = '\0';
    }
    fprintf(file, "%s = %s\n", name, value);
    return 0;
}


// case-insensitive equality
static int str_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

// trim whitespace (returns pointer into same buffer)
// Safe in-place trim: works for empty strings, modifies buffer
static char* trim(char *s)
{
    if (!s) return s;

    // left trim
    while (isspace((unsigned char)*s)) s++;

    // if now empty -> return as-is
    if (*s == '\0') return s;

    // right trim (end points to last char)
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) --end;
    end[1] = '\0';
    return s;
}

// parse int with range and full validation
static int parse_int(const char *txt, long minv, long maxv, int *out) {
    if (!txt || !*txt || !out) return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(txt, &end, 10);
    if (errno == ERANGE || end == txt) return -1;
    if (*trim(end) != '\0') return -1;
    if (v < minv || v > maxv) return -1;
    *out = (int)v;
    return 0;
}

// parse uint32 with range and validation
static int parse_u32(const char *txt, unsigned long minv, unsigned long maxv, uint32_t *out) {
    if (!txt || !*txt || !out) return -1;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(txt, &end, 10);
    if (errno == ERANGE || end == txt) return -1;
    if (*trim(end) != '\0') return -1;
    if (v < minv || v > maxv) return -1;
    *out = (uint32_t)v;
    return 0;
}

// parse float with range and validation
static int parse_float(const char *txt, float minv, float maxv, float *out) {
    if (!txt || !*txt || !out) return -1;
    errno = 0;
    char *end = NULL;
    float v = strtof(txt, &end);
    if (errno == ERANGE || end == txt) return -1;
    if (*trim(end) != '\0') return -1;
    if (!(v >= minv && v <= maxv)) return -1; // filters NaN/Inf too
    *out = v;
    return 0;
}

// parse bool from common variants
static int parse_bool(const char *txt, bool *out) {
    if (!txt || !out) return -1;
    if (str_ieq(txt, "1") || str_ieq(txt, "true") || str_ieq(txt, "yes") || str_ieq(txt, "on"))  { *out = true;  return 0; }
    if (str_ieq(txt, "0") || str_ieq(txt, "false")|| str_ieq(txt, "no")  || str_ieq(txt, "off")) { *out = false; return 0; }
    return -1;
}

// validate IPv4/IPv6 literal
static int validate_ip_literal(const char *txt) {
    if (!txt || !*txt) return -1;
    struct in_addr a4; struct in6_addr a6;
    if (inet_pton(AF_INET, txt, &a4) == 1) return 0;
    if (inet_pton(AF_INET6, txt, &a6) == 1) return 0;
    return -1;
}

// assign strdup with previous free
static int assign_dup(char **dst, const char *src) {
    if (!dst) return -1;
    char *tmp = strdup(src ? src : "");
    if (!tmp) return -1;
    free(*dst);
    *dst = tmp;
    return 0;
}

// enums
static int parse_codec(const char *txt, codec_type_t *out) {
    if (!txt || !out) return -1;
    if (str_ieq(txt, "h264")) { *out = CODEC_H264; return 0; }
    if (str_ieq(txt, "h265")) { *out = CODEC_H265; return 0; }
    return -1;
}

static int parse_rate_mode(const char *txt, rate_control_mode_t *out) {
    if (!txt || !out) return -1;
    if (str_ieq(txt, "cbr"))   { *out = RATE_CONTROL_CBR;   return 0; }
    if (str_ieq(txt, "vbr"))   { *out = RATE_CONTROL_VBR;   return 0; }
    if (str_ieq(txt, "avbr"))  { *out = RATE_CONTROL_AVBR;  return 0; }
    if (str_ieq(txt, "fixqp")) { *out = RATE_CONTROL_FIXQP; return 0; }
    return -1;
}

static int parse_resolution(const char *txt, int *w, int *h) {
    for (size_t i = 0; i < NUM_RESOLUTIONS; i++) {
        if (str_ieq(txt, RESOLUTIONS[i].name)) {
            *w = RESOLUTIONS[i].width;
            *h = RESOLUTIONS[i].height;
            return 0;
        }
    }
    return -1;
}

/* ========================== Macros: setter generators ========================== */

typedef int (*config_setter)(common_config_t *cfg, const char *value);

#define DEF_SETTER_INT(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)            \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    int iv;                                                              \
    if (parse_int(val, (MINV), (MAXV), &iv) != 0) {                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = iv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_U32(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)            \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    uint32_t uv;                                                         \
    if (parse_u32(val, (MINV), (MAXV), &uv) != 0) {                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = uv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_FLOAT(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)           \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    float fv;                                                            \
    if (parse_float(val, (MINV), (MAXV), &fv) != 0) {                    \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = fv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_BOOL(FUNC, FIELD_LVAL, KEYSTR)                        \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    bool bv;                                                             \
    if (parse_bool(val, &bv) != 0) {                                     \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = bv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_STR(FUNC, FIELD_LVAL, MAXLEN, KEYSTR)                 \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    if (!val || strlen(val) >= (MAXLEN)) {                               \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    strncpy((FIELD_LVAL), val, (MAXLEN) - 1);                           \
    (FIELD_LVAL)[(MAXLEN) - 1] = '\0';                                   \
    return 0;                                                            \
}

#define DEF_SETTER_ENUM(FUNC, FIELD_LVAL, PARSE_FN, KEYSTR)              \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    __typeof__(FIELD_LVAL) tmp;                                          \
    if (PARSE_FN(val, &tmp) != 0) {                                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = tmp;                                                  \
    return 0;                                                            \
}

#define DEF_SETTER_IP(FUNC, FIELD_LVAL, KEYSTR)                          \
static int FUNC(common_config_t *cfg, const char *val) {                 \
    if (validate_ip_literal(val) != 0) {                                 \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    if (assign_dup(&(FIELD_LVAL), val) != 0) {                           \
        fprintf(stderr, "config: OOM on %s\n", (KEYSTR));                \
        return -1;                                                       \
    }                                                                    \
    return 0;                                                            \
}

#define MAP(SEC, KEY, FN) { (SEC), (KEY), (FN) }

/* ========================== Setters via macros ========================== */
// TODO: need to check all value ranges and enums (generated using Copilot)

// rtp-streamer
DEF_SETTER_IP   (set_rtp_ip,   cfg->rtp_streamer_config.ip,    "rtp-streamer.ip")
DEF_SETTER_INT  (set_rtp_port, cfg->rtp_streamer_config.port,  1, 65535, "rtp-streamer.port")

// encoder (flat, width/height now driven by [video].resolution)
DEF_SETTER_ENUM (set_encoder_codec,     cfg->encoder_config.codec,     parse_codec,     "encoder.codec")
DEF_SETTER_ENUM (set_encoder_rate,      cfg->encoder_config.rate_mode, parse_rate_mode, "encoder.rate_mode")
DEF_SETTER_INT  (set_encoder_fps,       cfg->encoder_config.fps,       1, 60, "encoder.fps")
DEF_SETTER_INT  (set_encoder_gop,       cfg->encoder_config.gop,       1, 60, "encoder.gop")

// encoder.osd
DEF_SETTER_INT  (set_osd_width,  cfg->encoder_config.osd_config.width,   0, 16384, "encoder.osd.width")
DEF_SETTER_INT  (set_osd_height, cfg->encoder_config.osd_config.height,  0, 16384, "encoder.osd.height")
DEF_SETTER_INT  (set_osd_pos_x,  cfg->encoder_config.osd_config.pos_x,  0, 1920, "encoder.osd.pos_x")
DEF_SETTER_INT  (set_osd_pos_y,  cfg->encoder_config.osd_config.pos_y,  0, 1080, "encoder.osd.pos_y")

// encoder.focus
DEF_SETTER_INT  (set_focus_quality,    cfg->encoder_config.encoder_focus_mode.focus_quality, -51, 51, "encoder.focus.focus_quality")
DEF_SETTER_INT  (set_focus_frame_size, cfg->encoder_config.encoder_focus_mode.frame_size,    1, 100, "encoder.focus.frame_size")

// camera-csi (width/height now driven by [video].resolution)
DEF_SETTER_INT  (set_cam_id,         cfg->camera_csi_config.cam_id,         0, 2, "camera-csi.cam_id")
DEF_SETTER_INT  (set_cam_flip,       cfg->camera_csi_config.flip,           0, 1, "camera-csi.flip")
DEF_SETTER_INT  (set_cam_mirror,     cfg->camera_csi_config.mirror,         0, 1, "camera-csi.mirror")
DEF_SETTER_INT  (set_cam_brightness, cfg->camera_csi_config.brightness,    0, 255, "camera-csi.brightness")
DEF_SETTER_INT  (set_cam_contrast,   cfg->camera_csi_config.contrast,      0, 255, "camera-csi.contrast")
DEF_SETTER_INT  (set_cam_saturation, cfg->camera_csi_config.saturation,    0, 255, "camera-csi.saturation")
DEF_SETTER_INT  (set_cam_sharpness,  cfg->camera_csi_config.sharpness,     0, 255, "camera-csi.sharpness")
DEF_SETTER_BOOL (set_cam_awb,        cfg->camera_csi_config.auto_white_balance, "camera-csi.auto_white_balance")
DEF_SETTER_INT  (set_cam_correction, cfg->camera_csi_config.correction,    0, 255, "camera-csi.correction")
DEF_SETTER_FLOAT(set_cam_fast_ae_min_time, cfg->camera_csi_config.fast_ae_min_time, 0.0f, 10.0f, "camera-csi.fast_ae_min_time")
DEF_SETTER_FLOAT(set_cam_fast_ae_max_time, cfg->camera_csi_config.fast_ae_max_time, 0.0f, 10.0f, "camera-csi.fast_ae_max_time")
DEF_SETTER_FLOAT(set_cam_fast_ae_max_gain, cfg->camera_csi_config.fast_ae_max_gain, 0.0f, 256.0f, "camera-csi.fast_ae_max_gain")
DEF_SETTER_BOOL (set_cam_li_enable,         cfg->camera_csi_config.light_inhibition_enable, "camera-csi.light_inhibition_enable")
DEF_SETTER_INT  (set_cam_li_strength,       cfg->camera_csi_config.light_inhibition_strength, 0, 255, "camera-csi.light_inhibition_strength")
DEF_SETTER_INT  (set_cam_li_level,          cfg->camera_csi_config.light_inhibition_level,    0, 255, "camera-csi.light_inhibition_level")
DEF_SETTER_BOOL (set_cam_backlight_enable,  cfg->camera_csi_config.backlight_enable, "camera-csi.backlight_enable")
DEF_SETTER_INT  (set_cam_backlight_strength,cfg->camera_csi_config.backlight_strength, 0, 255, "camera-csi.backlight_strength")

// server
DEF_SETTER_BOOL (set_server_enabled,    cfg->server_config.enabled, "server.enabled")
DEF_SETTER_STR  (set_server_host,       cfg->server_config.server_host, sizeof(cfg->server_config.server_host), "server.host")
DEF_SETTER_INT  (set_server_port,       cfg->server_config.server_port, 1, 65535, "server.port")
DEF_SETTER_STR  (set_server_drone_id,   cfg->server_config.drone_id, sizeof(cfg->server_config.drone_id), "server.drone_id")
DEF_SETTER_INT  (set_server_heartbeat,  cfg->server_config.heartbeat_interval, 5, 300, "server.heartbeat_interval")
DEF_SETTER_STR  (set_server_owner_id,   cfg->server_config.owner_id, sizeof(cfg->server_config.owner_id), "server.owner_id")
DEF_SETTER_INT  (set_server_max_retries, cfg->server_config.server_connect_max_retries, 0, 50, "server.max_connect_retries")
DEF_SETTER_INT  (set_server_retry_delay, cfg->server_config.server_connect_retry_delay, 1, 60, "server.initial_retry_delay")


// video (common resolution for camera, encoder, stream)
static int set_common_resolution(common_config_t *cfg, const char *val) {
    int w, h;
    if (parse_resolution(val, &w, &h) != 0) {
        fprintf(stderr, "config: invalid video.resolution '%s' (allowed: FullHD, HD, qHD, SD, XGA, VGA)\n", val);
        return -1;
    }
    // Apply everywhere:
    cfg->encoder_config.width       = w;
    cfg->encoder_config.height      = h;
    cfg->stream_width               = w;
    cfg->stream_height              = h;
    cfg->camera_csi_config.width    = w;
    cfg->camera_csi_config.height   = h;
    return 0;
}

static int set_common_bitrate(common_config_t *cfg, const char *val)
{
    int br;
    if (parse_int(val, 1000, (1<<30), &br) != 0) {
        fprintf(stderr, "config: invalid video.bitrate '%s'\n", val);
        return -1;
    }

    cfg->encoder_config.bitrate = br;
    cfg->stream_bitrate         = br;
    return 0;
}

/* ========================== Mapping table ========================== */
typedef struct {
    const char *section;
    const char *key;
    config_setter setter;
} config_entry_t;

static const config_entry_t CONFIG_TABLE[] = {
    // video (single source of truth for resolution)
    MAP("video", "resolution",                      set_common_resolution),
    MAP("video", "bitrate",                         set_common_bitrate),

    // rtp-streamer
    MAP("rtp-streamer", "ip",                       set_rtp_ip),
    MAP("rtp-streamer", "port",                     set_rtp_port),

    // encoder (no width/height keys anymore)
    MAP("encoder", "codec",                         set_encoder_codec),
    MAP("encoder", "rate_mode",                     set_encoder_rate),
    MAP("encoder", "fps",                           set_encoder_fps),
    MAP("encoder", "gop",                           set_encoder_gop),

    // encoder.osd
    MAP("encoder.osd", "width",                     set_osd_width),
    MAP("encoder.osd", "height",                    set_osd_height),
    MAP("encoder.osd", "pos_x",                     set_osd_pos_x),
    MAP("encoder.osd", "pos_y",                     set_osd_pos_y),

    // encoder.focus
    MAP("encoder.focus", "focus_quality",           set_focus_quality),
    MAP("encoder.focus", "frame_size",              set_focus_frame_size),

    // camera-csi (no width/height keys anymore)
    MAP("camera-csi", "cam_id",                     set_cam_id),
    MAP("camera-csi", "flip",                       set_cam_flip),
    MAP("camera-csi", "mirror",                     set_cam_mirror),
    MAP("camera-csi", "brightness",                 set_cam_brightness),
    MAP("camera-csi", "contrast",                   set_cam_contrast),
    MAP("camera-csi", "saturation",                 set_cam_saturation),
    MAP("camera-csi", "sharpness",                  set_cam_sharpness),
    MAP("camera-csi", "auto_white_balance",         set_cam_awb),
    MAP("camera-csi", "correction",                 set_cam_correction),
    MAP("camera-csi", "fast_ae_min_time",           set_cam_fast_ae_min_time),
    MAP("camera-csi", "fast_ae_max_time",           set_cam_fast_ae_max_time),
    MAP("camera-csi", "fast_ae_max_gain",           set_cam_fast_ae_max_gain),
    MAP("camera-csi", "light_inhibition_enable",    set_cam_li_enable),
    MAP("camera-csi", "light_inhibition_strength",  set_cam_li_strength),
    MAP("camera-csi", "light_inhibition_level",     set_cam_li_level),
    MAP("camera-csi", "backlight_enable",           set_cam_backlight_enable),
    MAP("camera-csi", "backlight_strength",         set_cam_backlight_strength),

    // server
    MAP("server", "enabled",                        set_server_enabled),
    MAP("server", "host",                           set_server_host),
    MAP("server", "port",                           set_server_port),
    MAP("server", "drone_id",                       set_server_drone_id),
    MAP("server", "owner_id",                       set_server_owner_id),
    MAP("server", "heartbeat_interval",             set_server_heartbeat),
    MAP("server", "max_connect_retries",            set_server_max_retries),
    MAP("server", "initial_retry_delay",            set_server_retry_delay),
};

#define CONFIG_TABLE_LEN (sizeof(CONFIG_TABLE)/sizeof(CONFIG_TABLE[0]))

static int ini_dispatch(void* user, const char* section, const char* name, const char* value)
{
    // Debug log for every parsed key
    //fprintf(stderr, "DEBUG: section='%s' key='%s' val='%s'\n", section ? section : "(null)", name ? name : "(null)", value ? value : "(null)");

    common_config_t *cfg = (common_config_t*)user;
    if (!cfg || !section || !name || !value) {
        fprintf(stderr, "FAIL: ini_dispatch received null arguments\n");
        return 0; // do not abort parsing, just log
    }

    // Make a writable trimmed copy of the value
    char *copy = strdup(value);
    if (!copy) {
        fprintf(stderr, "config: OOM duplicating value for %s.%s\n", section, name);
        return 0; // continue parsing other keys
    }
    char *val = trim(copy);

    // Lookup in the config mapping table
    for (size_t i = 0; i < CONFIG_TABLE_LEN; i++) {
        if (str_ieq(section, CONFIG_TABLE[i].section) &&
            str_ieq(name, CONFIG_TABLE[i].key)) {

            int rc = CONFIG_TABLE[i].setter(cfg, val);
            if (rc != 0) {
                //fprintf(stderr, "Parse value FAIL: setter %s.%s('%s') rc=%d\n", section, name, val, rc);
                fprintf(stderr, "Parse '%s.%s' = '%s' - Fail!\n", section, name, val);
            } else {
                //fprintf(stderr, "Parse value OK  : %s.%s('%s')\n", section, name, val);
                fprintf(stderr, "Parse '%s.%s' = '%s' - Ok!\n", section, name, val);

            }
            free(copy);

            return 0;
            }
    }

    // Unknown key fallback
    fprintf(stderr, "config: unknown key %s.%s (ignored)\n", section, name);
    free(copy);

    // NOTE: temporary: ignore unknown keys to find all issues
    return 0;
}

int config_load(const char *path, common_config_t *cfg)
{
    if (!path || !cfg)
        return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "config: cannot open '%s'\n", path);
        return -1;
    }

    // Parse config file
    int rc = ini_parse_file(f, ini_dispatch, cfg);
    fclose(f);

    if (rc < 0) {
        // Negative return codes usually mean I/O or internal parser errors
        fprintf(stderr, "config: parse I/O/internal error (rc=%d)\n", rc);
        return -1;
    }

    return 0;
}

// Save configuration to file using existing CONFIG_TABLE structure
int config_save(const char *path, common_config_t *cfg)
{
    if (!path || !cfg) {
        fprintf(stderr, "config_save: invalid arguments\n");
        return -1;
    }

    // Create backup of existing config file before overwriting
    char backup_path[512];
    snprintf(backup_path, sizeof(backup_path), "%s.backup", path);
    
    // Check if original file exists
    FILE *original = fopen(path, "r");
    if (original) {
        fclose(original);
        
        // Copy original to backup
        FILE *backup = fopen(backup_path, "w");
        original = fopen(path, "r");
        if (backup && original) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), original)) > 0) {
                fwrite(buffer, 1, bytes, backup);
            }
            fclose(backup);
            fclose(original);
            fprintf(stderr, "config_save: backup created at '%s'\n", backup_path);
        } else {
            if (backup) fclose(backup);
            if (original) fclose(original);
            fprintf(stderr, "config_save: warning - could not create backup\n");
        }
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "config_save: cannot create file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(file, "# VD-Link Configuration File\n");
    fprintf(file, "# Generated automatically\n\n");

    // Track current section for grouping
    const char *current_section = NULL;

    // Iterate through CONFIG_TABLE to save all configured values
    for (size_t i = 0; i < CONFIG_TABLE_LEN; i++) {
        const char *section = CONFIG_TABLE[i].section;
        const char *key = CONFIG_TABLE[i].key;
        
        // Write section header when section changes
        if (!current_section || strcmp(current_section, section) != 0) {
            if (current_section) fprintf(file, "\n"); // Add blank line between sections
            fprintf(file, "[%s]\n", section);
            current_section = section;
        }

        // Generate value string based on the key
        char value_str[256] = {0};
        
        // Special handling for resolution (derived from width/height)
        if (str_ieq(key, "resolution")) {
            const char *resolution_name = "HD"; // default fallback
            for (size_t j = 0; j < NUM_RESOLUTIONS; j++) {
                if (RESOLUTIONS[j].width == cfg->encoder_config.width &&
                    RESOLUTIONS[j].height == cfg->encoder_config.height) {
                    resolution_name = RESOLUTIONS[j].name;
                    break;
                }
            }
            strncpy(value_str, resolution_name, sizeof(value_str) - 1);
        }
        // Special handling for bitrate
        else if (str_ieq(key, "bitrate")) {
            snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.bitrate);
        }
        // RTP streamer values
        else if (str_ieq(section, "rtp-streamer")) {
            if (str_ieq(key, "ip")) {
                strncpy(value_str, cfg->rtp_streamer_config.ip ? cfg->rtp_streamer_config.ip : "127.0.0.1", sizeof(value_str) - 1);
            } else if (str_ieq(key, "port")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->rtp_streamer_config.port);
            }
        }
        // Encoder values
        else if (str_ieq(section, "encoder")) {
            if (str_ieq(key, "codec")) {
                strncpy(value_str, (cfg->encoder_config.codec == CODEC_H264) ? "h264" : "h265", sizeof(value_str) - 1);
            } else if (str_ieq(key, "rate_mode")) {
                const char *rate_str = "cbr";
                switch(cfg->encoder_config.rate_mode) {
                    case RATE_CONTROL_CBR: rate_str = "cbr"; break;
                    case RATE_CONTROL_VBR: rate_str = "vbr"; break;
                    case RATE_CONTROL_AVBR: rate_str = "avbr"; break;
                    case RATE_CONTROL_FIXQP: rate_str = "fixqp"; break;
                }
                strncpy(value_str, rate_str, sizeof(value_str) - 1);
            } else if (str_ieq(key, "fps")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.fps);
            } else if (str_ieq(key, "gop")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.gop);
            }
        }
        // Encoder OSD values
        else if (str_ieq(section, "encoder.osd")) {
            if (str_ieq(key, "width")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.osd_config.width);
            } else if (str_ieq(key, "height")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.osd_config.height);
            } else if (str_ieq(key, "pos_x")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.osd_config.pos_x);
            } else if (str_ieq(key, "pos_y")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.osd_config.pos_y);
            }
        }
        // Encoder focus values
        else if (str_ieq(section, "encoder.focus")) {
            if (str_ieq(key, "focus_quality")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.encoder_focus_mode.focus_quality);
            } else if (str_ieq(key, "frame_size")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->encoder_config.encoder_focus_mode.frame_size);
            }
        }
        // Camera CSI values
        else if (str_ieq(section, "camera-csi")) {
            if (str_ieq(key, "cam_id")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.cam_id);
            } else if (str_ieq(key, "flip")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.flip);
            } else if (str_ieq(key, "mirror")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.mirror);
            } else if (str_ieq(key, "brightness")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.brightness);
            } else if (str_ieq(key, "contrast")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.contrast);
            } else if (str_ieq(key, "saturation")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.saturation);
            } else if (str_ieq(key, "sharpness")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.sharpness);
            } else if (str_ieq(key, "auto_white_balance")) {
                strncpy(value_str, cfg->camera_csi_config.auto_white_balance ? "true" : "false", sizeof(value_str) - 1);
            } else if (str_ieq(key, "correction")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.correction);
            } else if (str_ieq(key, "fast_ae_min_time")) {
                snprintf(value_str, sizeof(value_str), "%.6f", cfg->camera_csi_config.fast_ae_min_time);
            } else if (str_ieq(key, "fast_ae_max_time")) {
                snprintf(value_str, sizeof(value_str), "%.6f", cfg->camera_csi_config.fast_ae_max_time);
            } else if (str_ieq(key, "fast_ae_max_gain")) {
                snprintf(value_str, sizeof(value_str), "%.1f", cfg->camera_csi_config.fast_ae_max_gain);
            } else if (str_ieq(key, "light_inhibition_enable")) {
                strncpy(value_str, cfg->camera_csi_config.light_inhibition_enable ? "true" : "false", sizeof(value_str) - 1);
            } else if (str_ieq(key, "light_inhibition_strength")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.light_inhibition_strength);
            } else if (str_ieq(key, "light_inhibition_level")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.light_inhibition_level);
            } else if (str_ieq(key, "backlight_enable")) {
                strncpy(value_str, cfg->camera_csi_config.backlight_enable ? "true" : "false", sizeof(value_str) - 1);
            } else if (str_ieq(key, "backlight_strength")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->camera_csi_config.backlight_strength);
            }
        }
        // Server values
        else if (str_ieq(section, "server")) {
            if (str_ieq(key, "enabled")) {
                strncpy(value_str, cfg->server_config.enabled ? "true" : "false", sizeof(value_str) - 1);
            } else if (str_ieq(key, "host")) {
                strncpy(value_str, cfg->server_config.server_host, sizeof(value_str) - 1);
            } else if (str_ieq(key, "port")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->server_config.server_port);
            } else if (str_ieq(key, "drone_id")) {
                strncpy(value_str, cfg->server_config.drone_id, sizeof(value_str) - 1);
            } else if (str_ieq(key, "owner_id")) {
                strncpy(value_str, cfg->server_config.owner_id, sizeof(value_str) - 1);
            } else if (str_ieq(key, "heartbeat_interval")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->server_config.heartbeat_interval);
            } else if (str_ieq(key, "max_connect_retries")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->server_config.server_connect_max_retries);
            } else if (str_ieq(key, "initial_retry_delay")) {
                snprintf(value_str, sizeof(value_str), "%d", cfg->server_config.server_connect_retry_delay);
            }
        }

        // Write the key-value pair
        if (value_str[0] != '\0') {
            fprintf(file, "%s = %s\n", key, value_str);
        }
    }

    fclose(file);
    return 0;
}

void config_init_defaults(common_config_t *cfg)
{
    memset(cfg, 0, sizeof(common_config_t));

    // Resolution preset -> applies to encoder/stream/camera
    set_common_resolution(cfg, "HD");
    // Bitrate preset -> applies to encoder/stream
    set_common_bitrate(cfg, "4000000");

    // RTP defaults
    assign_dup(&cfg->rtp_streamer_config.ip, "127.0.0.1");
    cfg->rtp_streamer_config.port = 5602;

    // Encoder defaults
    cfg->encoder_config.codec     = CODEC_H265;           // H.265 for better compression
    cfg->encoder_config.rate_mode = RATE_CONTROL_CBR;     // constant bitrate for stable channel
    cfg->encoder_config.fps       = 60;                   // framerate
    cfg->encoder_config.gop       = 2;                    // low GOP low latency

    // Encoder focus mode off by default
    cfg->encoder_config.encoder_focus_mode.focus_quality = -51;
    cfg->encoder_config.encoder_focus_mode.frame_size    = 65;

    // Encoder OSD off by default
    cfg->encoder_config.osd_config.width  = 256;
    cfg->encoder_config.osd_config.height = 128;
    cfg->encoder_config.osd_config.pos_x  = 50;
    cfg->encoder_config.osd_config.pos_y  = 100;


    // Camera defaults
    cfg->camera_csi_config.cam_id = 0;
    cfg->camera_csi_config.auto_white_balance = true;
    cfg->camera_csi_config.flip = 0;
    cfg->camera_csi_config.mirror = 0;
    cfg->camera_csi_config.brightness = 128;
    cfg->camera_csi_config.contrast = 128;
    cfg->camera_csi_config.saturation = 128;
    cfg->camera_csi_config.sharpness = 128;
    cfg->camera_csi_config.correction = 128;
    cfg->camera_csi_config.fast_ae_min_time =  0.001f;  // 1ms
    cfg->camera_csi_config.fast_ae_max_time =  0.033f;  // 33ms
    cfg->camera_csi_config.fast_ae_max_gain =  8.0f;    // 8x
    cfg->camera_csi_config.light_inhibition_enable = false;
    cfg->camera_csi_config.light_inhibition_strength = 50;
    cfg->camera_csi_config.light_inhibition_level = 128;
    cfg->camera_csi_config.backlight_enable = false;
    cfg->camera_csi_config.backlight_strength = 50;

    // Server connection defaults
    cfg->server_config.enabled = false;  // Disabled by default
    strncpy(cfg->server_config.server_host, "stream.hard-tech.org.ua", sizeof(cfg->server_config.server_host) - 1);
    cfg->server_config.server_port = 8000;
    strncpy(cfg->server_config.drone_id, "Drone-<cpu_serial>", sizeof(cfg->server_config.drone_id) - 1);
    cfg->server_config.heartbeat_interval = 30;
    strncpy(cfg->server_config.owner_id, "default", sizeof(cfg->server_config.owner_id) - 1);
    cfg->server_config.owner_id[sizeof(cfg->server_config.owner_id) - 1] = '\0';
    
    // Connection retry defaults
    cfg->server_config.server_connect_max_retries = 10;   // Try 10 times by default
    cfg->server_config.server_connect_retry_delay = 2;    // Retry with 2 seconds delay

}

void config_cleanup(common_config_t *cfg)
{
    if (!cfg) return;

    if (cfg->rtp_streamer_config.ip) {
        free(cfg->rtp_streamer_config.ip);
        cfg->rtp_streamer_config.ip = NULL;
    }
}
