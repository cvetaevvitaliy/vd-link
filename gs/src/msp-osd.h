/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VD_LINK_MSP_OSD_H
#define VD_LINK_MSP_OSD_H
#include "common.h"

int msp_osd_init(struct config_t *cfg);

void msp_osd_stop(void);

void *msp_osd_get_fb_addr(void);

#endif //VD_LINK_MSP_OSD_H
