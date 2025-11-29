/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025
 * Author: Vitaliy N <vitaliy.nimych@gmail.com>
 */

#ifndef VD_LINK_DECODER_PC_H
#define VD_LINK_DECODER_PC_H
#include "common.h"

int decoder_start(struct config_t *cfg);
int decoder_put_frame(struct config_t *cfg, void *data, int size);
int decoder_stop(void);

#endif //VD_LINK_DECODER_PC_H
