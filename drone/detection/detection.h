/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#ifndef DETECTION_H
#define DETECTION_H

#include "detection_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int detection_init();

int detection_get_nn_model_height();
int detection_get_nn_model_width();
const char* detection_get_class_name(int class_id);
void normalize_detection_results(detection_result_group_t *results);

int detection_process_frame(void *buffer, uint32_t width, uint32_t height, detection_result_group_t *results);

void detection_deinit();

#ifdef __cplusplus
}
#endif


#endif // DETECTION_H