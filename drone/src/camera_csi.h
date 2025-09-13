/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CAMERA_CSI_H
#define CAMERA_CSI_H
#include <stdbool.h>
#include <stdint.h>

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

int camera_csi_init(camera_csi_config_t *cfg);
int camera_csi_bind_encoder(int cam_id, int enc_id);
int camera_csi_unbind_encoder(int cam_id, int enc_id);
int camera_csi_deinit(camera_csi_config_t *cfg);

#endif //CAMERA_CSI_H
