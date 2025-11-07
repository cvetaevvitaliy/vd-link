/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef ENCODER_H
#define ENCODER_H
#include "common.h"

int encoder_init(encoder_config_t *cfg);
void encoder_focus_mode(encoder_config_t *cfg);
int encoder_manual_push_frame(encoder_config_t *cfg, void *data, int size);
void encoder_clean(void);
int encoder_set_bitrate(int bitrate);
int encoder_set_fps(int fps);
int encoder_set_gop(int gop);
int encoder_set_rate_control(rate_control_mode_t mode);
int encoder_set_codec(codec_type_t codec);


#endif //ENCODER_H
