/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include "detection_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int bytetrack_init(int frame_rate, int track_buffer);
int bytetrack_update(detection_result_group_t* input);

#ifdef __cplusplus
}
#endif
