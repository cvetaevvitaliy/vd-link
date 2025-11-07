/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CONFIG_H
#define CONFIG_H
#include "../../version.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int (*encoder_callback)(void *data, int size, uint32_t timestamp);

typedef enum {
    CODEC_UNKNOWN = 0,
    CODEC_H264,
    CODEC_H265
} codec_type_t;

typedef enum {
    RATE_CONTROL_CBR = 0,
    RATE_CONTROL_VBR,
    RATE_CONTROL_AVBR,
    RATE_CONTROL_FIXQP
} rate_control_mode_t;

typedef struct {
    const char *name;
    int width;
    int height;
} resolution_preset_t;

typedef struct {
    int width;
    int height;
    int pos_x;
    int pos_y;
} encoder_osd_config_t;

typedef struct {
    int focus_quality;
    int frame_size; // percentage of frame to focus on (e.g., 65 for 65%)
} encoder_focus_mode_t;

typedef struct {
    codec_type_t codec;
    rate_control_mode_t rate_mode;
    encoder_osd_config_t osd_config;
    encoder_focus_mode_t encoder_focus_mode;
    int width;
    int height;
    int bitrate;
    int fps;
    int gop;
    encoder_callback callback;
} encoder_config_t;

typedef struct {
    int cam_id;
    int width;
    int height;
    int flip;
    int mirror;
    int brightness;
    int contrast;
    int saturation;
    int sharpness;
    bool auto_white_balance;
    int correction;

    // Fast auto exposure parameters
    float fast_ae_min_time;
    float fast_ae_max_time;
    float fast_ae_max_gain;

    // Light inhibition parameters
    bool light_inhibition_enable;
    uint8_t light_inhibition_strength;
    uint8_t light_inhibition_level;

    // Backlight parameters
    bool backlight_enable;
    uint32_t backlight_strength;
} camera_csi_config_t;

typedef struct {
    int device_index; // e.g., /dev/video0 -> 0
    int width;
    int height;
    int fps;
} camera_usb_config_t;

typedef struct {
    char *ip;    // Destination IP address
    int port;    // Destination port
} rtp_streamer_config_t;

typedef struct {
    bool enabled;
    char server_host[256];
    int server_port;
    int heartbeat_interval;

    char drone_id[32]; // UID from flight controller (e.g., 1234567890ABCDEF12345678)
    char name[64]; // Craft name from flight controller (e.g., MyDrone)
    char firmware_version[10]; // Firmware version from flight controller (1.0.0, etc.)
    char hardware_version[32]; // Hardware version from flight controller (STF4, etc.)
    char fc_variant[5]; // Flight controller variant (BTFL, etc.)
    char owner_id[32]; // Owner ID from config (e.g., default, user123)
} server_connection_config_t;

typedef struct {
    camera_csi_config_t camera_csi_config;
    camera_usb_config_t camera_usb_config;
    rtp_streamer_config_t rtp_streamer_config;
    encoder_config_t encoder_config;
    server_connection_config_t server_config;
    int stream_width;
    int stream_height;
    int stream_bitrate;
} common_config_t;

#endif //CONFIG_H
