/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef RTP_STREAMER_H
#define RTP_STREAMER_H
#include "common.h"

int rtp_streamer_init(struct common_config_t *cfg);
int rtp_streamer_push_frame(void *data, int size, uint32_t timestamp);
void rtp_streamer_deinit(void);

#endif //RTP_STREAMER_H
