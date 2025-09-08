/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#ifndef DETECTION_TYPES_H
#define DETECTION_TYPES_H

#include <stdint.h>

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUM_MAX_SIZE 64

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64
#define OBJ_CLASS_NUM     3
#define PROP_BOX_SIZE     (5 + OBJ_CLASS_NUM)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int left;
    int right;
    int top;
    int bottom;
} detection_box_t;

typedef struct {
    float x;
    float y;
    float width;
    float height;
} normalized_box_t;

typedef struct {
    char name[OBJ_NAME_MAX_SIZE];
    int obj_class;
    detection_box_t box;
    normalized_box_t norm_box; // Normalized coordinates (0.0 to 1.0)

    float confidence;
    int track_id;
} detection_result_t;

typedef struct {
    int count;
    detection_result_t results[OBJ_NUM_MAX_SIZE];
    float once_npu_run; // time in seconds
} detection_result_group_t;

#ifdef __cplusplus
}
#endif
#endif // DETECTION_TYPES_H