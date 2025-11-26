/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef RKNN_NPU_H
#define RKNN_NPU_H

#include <rknn/rknn_api.h>
#include "postprocess.h"

struct RknnNpuCtx {
    rknn_context            ctx;
    rknn_input_output_num   io_num;
    rknn_tensor_attr       *output_attrs;   // m_rknn_tensor_attr

    rknn_input              input;          // m_inputs

    int                     model_width;
    int                     model_height;
    int                     in_channel;
};

typedef struct RknnNpuCtx RknnNpuCtx;

/* Create / destroy */
RknnNpuCtx *rknn_npu_create(void);
void rknn_npu_destroy(RknnNpuCtx *ctx);

/* Init NPU with model and number of classes */
int rknn_npu_init(RknnNpuCtx *ctx, const char *path_to_rknn_model, int obj_class_num);

/* Run inference on RGB888 frame (already resized to model size) */
int rknn_npu_process(RknnNpuCtx *ctx, void *rgb_frame, int img_width, int img_height,
                         detect_result_group_t *results,
                         float nms_threshold,
                         float box_conf_threshold,
                         float vis_threshold);


#endif //RKNN_NPU_H
