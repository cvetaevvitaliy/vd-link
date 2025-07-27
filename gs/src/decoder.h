/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */

#ifndef VRX_DECODER_H
#define VRX_DECODER_H
#include "common.h"

int decoder_start(struct config_t *cfg);
int decoder_put_frame(struct config_t *cfg, void *data, int size);
int decoder_stop(void);

#endif //VRX_DECODER_H
