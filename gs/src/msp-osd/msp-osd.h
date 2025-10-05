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

/**
 * Process displayport data packet and render it
 * @param data Raw MSP data packet
 * @param size Size of the data packet
 */
void msp_process_data_pack(const uint8_t *data, size_t size);

/**
 * Clear all OSD data and reset to waiting state
 * Useful when connection is lost
 */
void msp_osd_clear_and_reset(void);

#endif //VD_LINK_MSP_OSD_H
