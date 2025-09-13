/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef ENCODER_H
#define ENCODER_H
#include "common.h"

#include <stdint.h>

typedef void (*encoder_callback)(void *data, int size, uint32_t timestamp);

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

int encoder_init(encoder_config_t *cfg);
void encoder_focus_mode(encoder_config_t *cfg);
void encoder_deinit(void);



#endif //ENCODER_H
