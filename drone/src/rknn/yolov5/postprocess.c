// postprocess.c - pure C port of the old YOLOv5 postprocess from C++
//
// Copyright (c) 2021 by Rockchip Electronics Co., Ltd.
// Licensed under the Apache License, Version 2.0

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>

#include "postprocess.h"

// anchors
static const int anchor0[6] = {10, 13, 16, 30, 33, 23};
static const int anchor1[6] = {30, 61, 62, 45, 59, 119};
static const int anchor2[6] = {116, 90, 156, 198, 373, 326};

static volatile int prop_box_size = (5 + OBJ_CLASS_NUM);
static volatile int obj_class_num = OBJ_CLASS_NUM;

/* ----------------- simple dynamic arrays (vector-like) ----------------- */

typedef struct {
    float *data;
    int    size;
    int    capacity;
} FloatVec;

typedef struct {
    int  *data;
    int   size;
    int   capacity;
} IntVec;

static void float_vec_init(FloatVec *v)
{
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

static void int_vec_init(IntVec *v)
{
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

static void float_vec_free(FloatVec *v)
{
    if (v->data) {
        free(v->data);
        v->data = NULL;
    }
    v->size = 0;
    v->capacity = 0;
}

static void int_vec_free(IntVec *v)
{
    if (v->data) {
        free(v->data);
        v->data = NULL;
    }
    v->size = 0;
    v->capacity = 0;
}

static int float_vec_reserve(FloatVec *v, int new_cap)
{
    if (new_cap <= v->capacity)
        return 0;
    float *p = (float *)realloc(v->data, new_cap * sizeof(float));
    if (!p)
        return -1;
    v->data = p;
    v->capacity = new_cap;
    return 0;
}

static int int_vec_reserve(IntVec *v, int new_cap)
{
    if (new_cap <= v->capacity)
        return 0;
    int *p = (int *)realloc(v->data, new_cap * sizeof(int));
    if (!p)
        return -1;
    v->data = p;
    v->capacity = new_cap;
    return 0;
}

static int float_vec_push(FloatVec *v, float val)
{
    if (v->size >= v->capacity) {
        int new_cap = (v->capacity == 0) ? 64 : (v->capacity * 2);
        if (float_vec_reserve(v, new_cap) < 0)
            return -1;
    }
    v->data[v->size++] = val;
    return 0;
}

static int int_vec_push(IntVec *v, int val)
{
    if (v->size >= v->capacity) {
        int new_cap = (v->capacity == 0) ? 64 : (v->capacity * 2);
        if (int_vec_reserve(v, new_cap) < 0)
            return -1;
    }
    v->data[v->size++] = val;
    return 0;
}

/* ------------------------ helpers ------------------------ */

inline static int clamp(float val, int min, int max)
{
    return val > min ? (val < max ? (int)val : max) : min;
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0,
                              float xmin1, float ymin1, float xmax1, float ymax1)
{
    float w = fmaxf(0.f, fminf(xmax0, xmax1) - fmaxf(xmin0, xmin1) + 1.0f);
    float h = fmaxf(0.f, fminf(ymax0, ymax1) - fmaxf(ymin0, ymin1) + 1.0f);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0f) * (ymax0 - ymin0 + 1.0f) +
              (xmax1 - xmin1 + 1.0f) * (ymax1 - ymin1 + 1.0f) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount,
               FloatVec *outputLocations,
               IntVec *order,
               float threshold)
{
    int i, j;
    for (i = 0; i < validCount; ++i)
    {
        int n = order->data[i];
        if (n == -1)
            continue;

        for (j = i + 1; j < validCount; ++j)
        {
            int m = order->data[j];
            if (m == -1)
                continue;

            float xmin0 = outputLocations->data[n * 4 + 0];
            float ymin0 = outputLocations->data[n * 4 + 1];
            float xmax0 = outputLocations->data[n * 4 + 0] + outputLocations->data[n * 4 + 2];
            float ymax0 = outputLocations->data[n * 4 + 1] + outputLocations->data[n * 4 + 3];

            float xmin1 = outputLocations->data[m * 4 + 0];
            float ymin1 = outputLocations->data[m * 4 + 1];
            float xmax1 = outputLocations->data[m * 4 + 0] + outputLocations->data[m * 4 + 2];
            float ymax1 = outputLocations->data[m * 4 + 1] + outputLocations->data[m * 4 + 3];

            float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0,
                                         xmin1, ymin1, xmax1, ymax1);

            if (iou > threshold)
            {
                order->data[j] = -1;
            }
        }
    }
    return 0;
}

static int quick_sort_indice_inverse(FloatVec *input, int left, int right, IntVec *indices)
{
    float key;
    int key_index;
    int low = left;
    int high = right;

    if (left < right)
    {
        key_index = indices->data[left];
        key = input->data[left];
        while (low < high)
        {
            while (low < high && input->data[high] <= key)
            {
                high--;
            }
            input->data[low] = input->data[high];
            indices->data[low] = indices->data[high];
            while (low < high && input->data[low] >= key)
            {
                low++;
            }
            input->data[high] = input->data[low];
            indices->data[high] = indices->data[low];
        }
        input->data[low] = key;
        indices->data[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

static float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static float unsigmoid(float y)
{
    return -1.0f * logf((1.0f / y) - 1.0f);
}

inline static int32_t __clip(float val, float min, float max)
{
    float f = val <= min ? min : (val >= max ? max : val);
    return (int32_t)f;
}

static uint8_t qnt_f32_to_affine(float f32, uint8_t zp, float scale)
{
    float dst_val = (f32 / scale) + (float)zp;
    uint8_t res = (uint8_t)__clip(dst_val, 0.0f, 255.0f);
    return res;
}

static float deqnt_affine_to_f32(uint8_t qnt, uint8_t zp, float scale)
{
    return ((float)qnt - (float)zp) * scale;
}

/* ------------------------ core process() ------------------------ */

static int process(uint8_t *input, const int *anchor,
                   int grid_h, int grid_w, int stride,
                   FloatVec *boxes, FloatVec *boxScores, IntVec *classId,
                   float threshold, uint8_t zp, float scale)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    float thres = unsigmoid(threshold);
    uint8_t thres_u8 = qnt_f32_to_affine(thres, zp, scale);

    for (int a = 0; a < 3; a++)
    {
        for (int i = 0; i < grid_h; i++)
        {
            for (int j = 0; j < grid_w; j++)
            {
                uint8_t box_confidence =
                    input[(prop_box_size * a + 4) * grid_len + i * grid_w + j];

                if (box_confidence >= thres_u8)
                {
                    int offset = (prop_box_size * a) * grid_len + i * grid_w + j;
                    uint8_t *in_ptr = input + offset;

                    float box_x = sigmoid(deqnt_affine_to_f32(*in_ptr, zp, scale)) * 2.0f - 0.5f;
                    float box_y = sigmoid(deqnt_affine_to_f32(in_ptr[grid_len], zp, scale)) * 2.0f - 0.5f;
                    float box_w = sigmoid(deqnt_affine_to_f32(in_ptr[2 * grid_len], zp, scale)) * 2.0f;
                    float box_h = sigmoid(deqnt_affine_to_f32(in_ptr[3 * grid_len], zp, scale)) * 2.0f;

                    box_x = (box_x + (float)j) * (float)stride;
                    box_y = (box_y + (float)i) * (float)stride;
                    box_w = box_w * box_w * (float)anchor[a * 2];
                    box_h = box_h * box_h * (float)anchor[a * 2 + 1];
                    box_x -= (box_w / 2.0f);
                    box_y -= (box_h / 2.0f);

                    // push box
                    float_vec_push(boxes, box_x);
                    float_vec_push(boxes, box_y);
                    float_vec_push(boxes, box_w);
                    float_vec_push(boxes, box_h);

                    // box confidence in [0,1]
                    float box_conf_f32 = sigmoid(deqnt_affine_to_f32(box_confidence, zp, scale));
                    float_vec_push(boxScores, box_conf_f32);

                    // class id (max over classes)
                    uint8_t maxClassProbs = in_ptr[5 * grid_len];
                    int maxClassId = 0;
                    for (int k = 1; k < obj_class_num; ++k)
                    {
                        uint8_t prob = in_ptr[(5 + k) * grid_len];
                        if (prob > maxClassProbs)
                        {
                            maxClassId = k;
                            maxClassProbs = prob;
                        }
                    }
                    int_vec_push(classId, maxClassId);
                    validCount++;
                }
            }
        }
    }

    return validCount;
}

/* ------------------------ public API ------------------------ */

int init_post_process(int class_num)
{
    prop_box_size = class_num + 5;
    obj_class_num = class_num;

    return 0;
}

int post_process(uint8_t *input0, uint8_t *input1, uint8_t *input2,
                 int model_in_h, int model_in_w,
                 float conf_threshold, float nms_threshold, float vis_threshold,
                 float scale_w, float scale_h,
                 uint8_t *qnt_zps, float *qnt_scales,
                 detect_result_group_t *group)
{
    FloatVec filterBoxes;   // [x, y, w, h] * N
    FloatVec boxesScore;    // box confidence (0..1)
    IntVec   classId;       // class id per box
    IntVec   indexArray;    // indices for NMS

    float_vec_init(&filterBoxes);
    float_vec_init(&boxesScore);
    int_vec_init(&classId);
    int_vec_init(&indexArray);

    memset(group, 0, sizeof(detect_result_group_t));

    int stride0 = 8;
    int grid_h0 = model_in_h / stride0;
    int grid_w0 = model_in_w / stride0;

    int stride1 = 16;
    int grid_h1 = model_in_h / stride1;
    int grid_w1 = model_in_w / stride1;

    int stride2 = 32;
    int grid_h2 = model_in_h / stride2;
    int grid_w2 = model_in_w / stride2;

    int validCount0 = 0;
    int validCount1 = 0;
    int validCount2 = 0;

    // head 0
    validCount0 = process(input0, anchor0, grid_h0, grid_w0, stride0,
                          &filterBoxes, &boxesScore, &classId,
                          conf_threshold, qnt_zps[0], qnt_scales[0]);

    // head 1
    validCount1 = process(input1, anchor1, grid_h1, grid_w1, stride1,
                          &filterBoxes, &boxesScore, &classId,
                          conf_threshold, qnt_zps[1], qnt_scales[1]);

    // head 2
    validCount2 = process(input2, anchor2, grid_h2, grid_w2, stride2,
                          &filterBoxes, &boxesScore, &classId,
                          conf_threshold, qnt_zps[2], qnt_scales[2]);

    int validCount = validCount0 + validCount1 + validCount2;

    // no object detect
    if (validCount <= 0)
    {
        float_vec_free(&filterBoxes);
        float_vec_free(&boxesScore);
        int_vec_free(&classId);
        int_vec_free(&indexArray);
        return 0;
    }

    // build indexArray (0..validCount-1)
    for (int i = 0; i < validCount; ++i)
    {
        int_vec_push(&indexArray, i);
    }

    // in original code quick_sort_indice_inverse(...) is commented out
    // so we keep the same behavior: no sorting, just NMS on original order

    //quick_sort_indice_inverse(&boxesScore, 0, validCount - 1, &indexArray);

    // NMS over all boxes (no per-class separation)
    nms(validCount, &filterBoxes, &indexArray, nms_threshold);

    int last_count = 0;
    group->count = 0;

    // box valid detect target
    for (int i = 0; i < validCount; ++i)
    {
        if (indexArray.data[i] == -1 ||
            boxesScore.data[i] < vis_threshold ||
            i >= OBJ_NUMB_MAX_SIZE)
        {
            continue;
        }

        int n = indexArray.data[i];

        float x1 = filterBoxes.data[n * 4 + 0];
        float y1 = filterBoxes.data[n * 4 + 1];
        float x2 = x1 + filterBoxes.data[n * 4 + 2];
        float y2 = y1 + filterBoxes.data[n * 4 + 3];
        int   id = classId.data[n];

        group->results[last_count].box.left   =
            (int)(clamp(x1, 0, model_in_w) / scale_w);
        group->results[last_count].box.top    =
            (int)(clamp(y1, 0, model_in_h) / scale_h);
        group->results[last_count].box.right  =
            (int)(clamp(x2, 0, model_in_w) / scale_w);
        group->results[last_count].box.bottom =
            (int)(clamp(y2, 0, model_in_h) / scale_h);

        // in original code: confidence = boxesScore[n] * 100.f
        group->results[last_count].confidence = boxesScore.data[n] * 100.0f;
        group->results[last_count].id = id;

        // label copy was commented out in original code
        // char *label = labels[id];
        // strncpy(group->results[last_count].name, label, OBJ_NAME_MAX_SIZE);

        last_count++;
    }

    group->count = last_count;

    float_vec_free(&filterBoxes);
    float_vec_free(&boxesScore);
    int_vec_free(&classId);
    int_vec_free(&indexArray);

    return 0;
}