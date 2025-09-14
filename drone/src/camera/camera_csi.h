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
int camera_csi_unbind_encoder(int cam_id, int enc_id);
int camera_csi_deinit(camera_csi_config_t *cfg);

#endif //CAMERA_CSI_H
