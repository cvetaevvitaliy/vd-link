/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VD_LINK_MSP_OSD_H
#define VD_LINK_MSP_OSD_H
#include "common.h"
#include "wfb_status_link.h"

int msp_osd_init(struct config_t *cfg);

void msp_osd_stop(void);

void *msp_osd_get_fb_addr(void);

void osd_wfb_status_link_callback(const wfb_rx_status *st);

#endif //VD_LINK_MSP_OSD_H
