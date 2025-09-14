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

static const resolution_preset_t RESOLUTIONS[] = {
    { "FullHD", 1920, 1080 },
    { "HD",     1280,  720 },
    { "qHD",     960,  540 },
    { "SD",      720,  480 },
    { "XGA",    1024,  768 },
    { "VGA",     640,  480 },
};
#define NUM_RESOLUTIONS (sizeof(RESOLUTIONS)/sizeof(RESOLUTIONS[0]))\

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

typedef int (*config_setter)(struct common_config_t *cfg, const char *value);

#define DEF_SETTER_INT(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)            \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
    int iv;                                                              \
    if (parse_int(val, (MINV), (MAXV), &iv) != 0) {                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = iv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_U32(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)            \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
    uint32_t uv;                                                         \
    if (parse_u32(val, (MINV), (MAXV), &uv) != 0) {                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = uv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_FLOAT(FUNC, FIELD_LVAL, MINV, MAXV, KEYSTR)           \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
    float fv;                                                            \
    if (parse_float(val, (MINV), (MAXV), &fv) != 0) {                    \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = fv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_BOOL(FUNC, FIELD_LVAL, KEYSTR)                        \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
    bool bv;                                                             \
    if (parse_bool(val, &bv) != 0) {                                     \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = bv;                                                   \
    return 0;                                                            \
}

#define DEF_SETTER_ENUM(FUNC, FIELD_LVAL, PARSE_FN, KEYSTR)              \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
    __typeof__(FIELD_LVAL) tmp;                                          \
    if (PARSE_FN(val, &tmp) != 0) {                                      \
        fprintf(stderr, "config: invalid %s '%s'\n", (KEYSTR), val);     \
        return -1;                                                       \
    }                                                                    \
    (FIELD_LVAL) = tmp;                                                  \
    return 0;                                                            \
}

#define DEF_SETTER_IP(FUNC, FIELD_LVAL, KEYSTR)                          \
static int FUNC(struct common_config_t *cfg, const char *val) {          \
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

// video (common resolution for camera, encoder, stream)
static int set_common_resolution(struct common_config_t *cfg, const char *val) {
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

static int set_common_bitrate(struct common_config_t *cfg, const char *val)
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
};

#define CONFIG_TABLE_LEN (sizeof(CONFIG_TABLE)/sizeof(CONFIG_TABLE[0]))

static int ini_dispatch(void* user, const char* section, const char* name, const char* value)
{
    // Debug log for every parsed key
    //fprintf(stderr, "DEBUG: section='%s' key='%s' val='%s'\n", section ? section : "(null)", name ? name : "(null)", value ? value : "(null)");

    struct common_config_t *cfg = (struct common_config_t*)user;
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

int config_load(const char *path, struct common_config_t *cfg)
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

void config_init_defaults(struct common_config_t *cfg)
{
    memset(cfg, 0, sizeof(struct common_config_t));

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

}

void config_cleanup(struct common_config_t *cfg)
{
    if (!cfg) return;

    if (cfg->rtp_streamer_config.ip) {
        free(cfg->rtp_streamer_config.ip);
        cfg->rtp_streamer_config.ip = NULL;
    }
}
