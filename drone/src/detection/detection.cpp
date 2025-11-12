/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "detection.h"
#include "detection_types.h"
#include "rknn_npu.h"

static RknnNpu rknn_npu;


bool detection_init_cpp()
{
    rknn_npu.RknnNpuInit((char*)"/etc/ai_model/yolov7_tiny.rknn", (char*)"/etc/ai_model/default_anchors.txt", OBJ_CLASS_NUM);

    return true;
}

static int rknn_process_cpp(void *buffer, uint32_t width, uint32_t height, detection_result_group_t *results)
{
    if (buffer == nullptr || results == nullptr) {
        return -1;
    }
    
    // Process the image with the NPU
    int ret = rknn_npu.RknnNpuProcess(buffer, width, height, YOLOV7, results);
    if (ret < 0) {
        return -1;
    }
    
    return 0;
}

extern "C" int detection_init()
{
    return detection_init_cpp() ? 0 : -1;
}

extern "C" int detection_get_nn_model_height()
{
    return rknn_npu.GetModelHeight();
}

extern "C" int detection_get_nn_model_width()
{
    return rknn_npu.GetModelWidth();
}

extern "C" const char* detection_get_class_name(int class_id)
{
    switch (class_id) {
    case 0:
        return "person";
    case 1:
        return "vehicle";
    case 2: 
        return "box";
    default:
        return "unknown";
    }
}

static void normalize_box(detection_box_t& box, normalized_box_t& out)
{
    // Normalize box coordinates to the model input size
    out.x = (box.left / (float)rknn_npu.GetModelWidth());
    out.y = (box.top / (float)rknn_npu.GetModelHeight());
    out.width = (box.right - box.left) / (float)rknn_npu.GetModelWidth();
    out.height = (box.bottom - box.top) / (float)rknn_npu.GetModelHeight();
}

extern "C" void normalize_detection_results(detection_result_group_t *results)
{
    if (results == nullptr) {
        return;
    }

    for (int i = 0; i < results->count; i++) {
        normalize_box(results->results[i].box, results->results[i].norm_box);
    }
}

extern "C" int detection_process_frame(void *buffer, uint32_t width, uint32_t height, detection_result_group_t *results)
{
    if (buffer == nullptr || results == nullptr) {
        return -1;
    }

    // Process the image with the NPU
    int ret = rknn_process_cpp(buffer, width, height, results);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

extern "C" void detection_deinit()
{
    rknn_npu.~RknnNpu();
}