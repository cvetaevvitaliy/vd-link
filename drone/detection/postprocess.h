/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#ifndef _RKNN_ZERO_COPY_DEMO_POSTPROCESS_H_
#define _RKNN_ZERO_COPY_DEMO_POSTPROCESS_H_

#include <cstdint>
#include <vector>
#include <rknn/rknn_api.h>
#include "detection_types.h"

typedef enum { YOLOV5 = 0, YOLOV7, MODEL_TYPE_UNDEFINED } model_type_e;

void init_post_process(int class_num);
int post_process(std::vector<std::vector<int> > anchors, void* outputs, rknn_tensor_attr* output_attrs,
                 model_type_e model_type, int model_in_h, int model_in_w, float conf_threshold, float nms_threshold,
                 float vis_threshold, float scale_w, float scale_h, detection_result_group_t* group);

#endif //_RKNN_ZERO_COPY_DEMO_POSTPROCESS_H_
