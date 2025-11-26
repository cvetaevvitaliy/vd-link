/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CAMERA_CSI_H
#define CAMERA_CSI_H
#include <stdbool.h>
#include "common.h"

int camera_csi_init(camera_csi_config_t *cfg);
int camera_csi_bind_encoder(int cam_id, int enc_id);
int camera_csi_bind_rknn(int cam_id, int cam_ch, int rga_ch, int rknn_width, int rknn_height);
int camera_csi_unbind_encoder(int cam_id, int enc_id);
int camera_csi_unbind_rknn(int cam_id, int ch_id, int rga_id, int rga_ch);
int camera_csi_deinit(camera_csi_config_t *cfg);

int camera_csi_set_hdr_mode(int cam_id, bool enable);
int set_camera_csi_brightness(int cam_id, uint32_t brightness);
int set_camera_csi_contrast(int cam_id, uint32_t contrast);
int set_camera_csi_saturation(int cam_id, uint32_t saturation);
int set_camera_csi_sharpness(int cam_id, uint32_t sharpness);
int camera_csi_set_hdr_mode(int cam_id, bool enable);
int set_camera_csi_mirror_flip(int cam_id, bool mirror, bool flip);

#endif //CAMERA_CSI_H
