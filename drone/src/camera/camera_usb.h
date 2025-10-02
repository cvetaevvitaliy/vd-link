/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CAMERA_USB_H
#define CAMERA_USB_H

#include "common.h"
#include "camera.h"

int camera_usb_init(camera_info_t* config, common_config_t* common_config);
void camera_usb_deinit(void);
int camera_usb_bind_encoder(int cam_id, int enc_id);
int camera_usb_unbind_encoder(int cam_id, int enc_id);

#endif //CAMERA_USB_H
