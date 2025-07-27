/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VRX_RTP_RECEIVER_H
#define VRX_RTP_RECEIVER_H
#include "common.h"

int rtp_receiver_start(struct config_t *cfg);
void rtp_receiver_stop(void);

#endif //VRX_RTP_RECEIVER_H
