/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef POSTPROCESS_H
#define POSTPROCESS_H


// #define OBJ_NAME_MAX_SIZE 64
// #define OBJ_NUMB_MAX_SIZE 128
// #define OBJ_CLASS_NUM 3
// #define NMS_THRESH 0.45
// #define BOX_THRESH 0.25
// #define PROP_BOX_SIZE ( 5 + OBJ_CLASS_NUM)
#include <rknn/rknn_api.h>
#include <stdbool.h>

#define OBJ_NAME_MAX_SIZE 16
#define OBJ_NUMB_MAX_SIZE 64
#define OBJ_CLASS_NUM     3
#define PROP_BOX_SIZE     (5 + OBJ_CLASS_NUM)

typedef struct {
    int left; // x1
    int right; // x2
    int top; // y1
    int bottom; // y2
} BOX_RECT;

typedef struct {
    char name[OBJ_NAME_MAX_SIZE];
    int obj_class;
    BOX_RECT box;
    float confidence;
    int id;
} detect_result_t;

typedef struct {
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
    float once_npu_run;
    int focused_box_id;
    int target_box_id;
} detect_result_group_t;


int init_post_process(int class_num);
void deinit_post_process();
char *coco_cls_to_name(int cls_id);
int post_process(uint8_t *input0, uint8_t *input1, uint8_t *input2,
                 int model_in_h, int model_in_w,
                 float conf_threshold, float nms_threshold, float vis_threshold,
                 float scale_w, float scale_h,
                 uint8_t *qnt_zps, float *qnt_scales,
                 detect_result_group_t *group);
#endif //POSTPROCESS_H
