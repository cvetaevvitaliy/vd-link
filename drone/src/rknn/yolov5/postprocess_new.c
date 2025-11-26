// postprocess.c - pure C port of Rockchip yolov5 postprocess.cc

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "postprocess.h"

#define LABEL_NALE_TXT_PATH "./model/coco_80_labels_list.txt"

static char *labels[OBJ_CLASS_NUM];
static const char *default_coco_labels[80] = {
    "person", "bicycle", "car", "motorbike", "aeroplane", "bus",
    "train", "truck", "boat", "traffic light", "fire hydrant",
    "stop sign", "parking meter", "bench", "bird", "cat", "dog",
    "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie",
    "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard",
    "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog",
    "pizza", "donut", "cake", "chair", "sofa", "pottedplant",
    "bed", "diningtable", "toilet", "tvmonitor", "laptop",
    "mouse", "remote", "keyboard", "cell phone", "microwave",
    "oven", "toaster", "sink", "refrigerator", "book", "clock",
    "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

/* ----------------- simple dynamic arrays (vector-like) ----------------- */

typedef struct {
    float *data;
    int   size;
    int   capacity;
} FloatVec;

typedef struct {
    int *data;
    int  size;
    int  capacity;
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

/* ---------------------- helpers ---------------------- */

inline static int clamp(float val, int min, int max)
{
    return (val > min) ? ((val < max) ? (int)val : max) : min;
}

static char *readLine(FILE *fp, char *buffer, int *len)
{
    int ch;
    int i = 0;
    size_t buff_len = 0;

    (void)buffer;

    buffer = (char *)malloc(1);
    if (!buffer)
        return NULL;

    while ((ch = fgetc(fp)) != '\n' && ch != EOF)
    {
        buff_len++;
        void *tmp = realloc(buffer, buff_len + 1);
        if (tmp == NULL)
        {
            free(buffer);
            return NULL;
        }
        buffer = (char *)tmp;
        buffer[i++] = (char)ch;
    }
    buffer[i] = '\0';
    *len = (int)buff_len;

    if (ch == EOF && (i == 0 || ferror(fp)))
    {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int readLines(const char *fileName, char *lines[], int max_line)
{
    FILE *file = fopen(fileName, "r");
    char *s = NULL;
    int i = 0;
    int n = 0;

    if (file == NULL)
    {
        printf("Open %s fail!\n", fileName);
        return -1;
    }

    while ((s = readLine(file, NULL, &n)) != NULL)
    {
        lines[i++] = s;
        if (i >= max_line)
            break;
    }
    fclose(file);
    return i;
}

static int loadLabelName(const char *locationFilename, char *label[])
{
    int lines;
    printf("load lable %s\n", locationFilename);
    lines = readLines(locationFilename, label, OBJ_CLASS_NUM);
    if (lines <= 0) {
        printf("readLines(%s) failed: %d\n", locationFilename, lines);
        return -1;
    }
    return 0;
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
               IntVec *classIds,
               IntVec *order,
               int filterId,
               float threshold)
{
    int i, j;
    for (i = 0; i < validCount; ++i)
    {
        int n = order->data[i];
        if (n == -1 || classIds->data[n] != filterId)
            continue;

        for (j = i + 1; j < validCount; ++j)
        {
            int m = order->data[j];
            if (m == -1 || classIds->data[m] != filterId)
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

inline static int32_t __clip(float val, float min, float max)
{
    float f = val <= min ? min : (val >= max ? max : val);
    return (int32_t)f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + (float)zp;
    int8_t res = (int8_t)__clip(dst_val, -128.0f, 127.0f);
    return res;
}

static uint8_t qnt_f32_to_affine_u8(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + (float)zp;
    uint8_t res = (uint8_t)__clip(dst_val, 0.0f, 255.0f);
    return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale)
{
    return ((float)qnt - (float)zp) * scale;
}

static float deqnt_affine_u8_to_f32(uint8_t qnt, int32_t zp, float scale)
{
    return ((float)qnt - (float)zp) * scale;
}

/* ------------------------ process_* (C port of C++) ------------------------ */

static int process_u8(uint8_t *input, int *anchor_arr,
                      int grid_h, int grid_w,
                      int height, int width, int stride,
                      FloatVec *boxes, FloatVec *objProbs, IntVec *classId,
                      float threshold, int32_t zp, float scale)
{
    (void)height;
    (void)width;

    int validCount = 0;
    int grid_len = grid_h * grid_w;
    uint8_t thres_u8 = qnt_f32_to_affine_u8(threshold, zp, scale);

    for (int a = 0; a < 3; a++)
    {
        for (int i = 0; i < grid_h; i++)
        {
            for (int j = 0; j < grid_w; j++)
            {
                uint8_t box_confidence =
                    input[(PROP_BOX_SIZE * a + 4) * grid_len + i * grid_w + j];

                if (box_confidence >= thres_u8)
                {
                    int offset = (PROP_BOX_SIZE * a) * grid_len + i * grid_w + j;
                    uint8_t *in_ptr = input + offset;

                    float box_x = (deqnt_affine_u8_to_f32(*in_ptr, zp, scale)) * 2.0f - 0.5f;
                    float box_y = (deqnt_affine_u8_to_f32(in_ptr[grid_len], zp, scale)) * 2.0f - 0.5f;
                    float box_w = (deqnt_affine_u8_to_f32(in_ptr[2 * grid_len], zp, scale)) * 2.0f;
                    float box_h = (deqnt_affine_u8_to_f32(in_ptr[3 * grid_len], zp, scale)) * 2.0f;

                    box_x = (box_x + (float)j) * (float)stride;
                    box_y = (box_y + (float)i) * (float)stride;
                    box_w = box_w * box_w * (float)anchor_arr[a * 2];
                    box_h = box_h * box_h * (float)anchor_arr[a * 2 + 1];
                    box_x -= (box_w / 2.0f);
                    box_y -= (box_h / 2.0f);

                    uint8_t maxClassProbs = in_ptr[5 * grid_len];
                    int maxClassId = 0;
                    for (int k = 1; k < OBJ_CLASS_NUM; ++k)
                    {
                        uint8_t prob = in_ptr[(5 + k) * grid_len];
                        if (prob > maxClassProbs)
                        {
                            maxClassId = k;
                            maxClassProbs = prob;
                        }
                    }
                    float limit_score =
                        deqnt_affine_u8_to_f32(maxClassProbs, zp, scale) *
                        deqnt_affine_u8_to_f32(box_confidence, zp, scale);

                    if (limit_score >= threshold)
                    {
                        float_vec_push(objProbs, limit_score);
                        int_vec_push(classId, maxClassId);
                        validCount++;

                        float_vec_push(boxes, box_x);
                        float_vec_push(boxes, box_y);
                        float_vec_push(boxes, box_w);
                        float_vec_push(boxes, box_h);
                    }
                }
            }
        }
    }
    return validCount;
}

static int process_i8(int8_t *input, int *anchor_arr,
                      int grid_h, int grid_w,
                      int height, int width, int stride,
                      FloatVec *boxes, FloatVec *objProbs, IntVec *classId,
                      float threshold, int32_t zp, float scale)
{
    (void)height;
    (void)width;

    int validCount = 0;
    int grid_len = grid_h * grid_w;
    int8_t thres_i8 = qnt_f32_to_affine(threshold, zp, scale);

    for (int a = 0; a < 3; a++)
    {
        for (int i = 0; i < grid_h; i++)
        {
            for (int j = 0; j < grid_w; j++)
            {
                int8_t box_confidence =
                    input[(PROP_BOX_SIZE * a + 4) * grid_len + i * grid_w + j];

                if (box_confidence >= thres_i8)
                {
                    int offset = (PROP_BOX_SIZE * a) * grid_len + i * grid_w + j;
                    int8_t *in_ptr = input + offset;

                    float box_x = (deqnt_affine_to_f32(*in_ptr, zp, scale)) * 2.0f - 0.5f;
                    float box_y = (deqnt_affine_to_f32(in_ptr[grid_len], zp, scale)) * 2.0f - 0.5f;
                    float box_w = (deqnt_affine_to_f32(in_ptr[2 * grid_len], zp, scale)) * 2.0f;
                    float box_h = (deqnt_affine_to_f32(in_ptr[3 * grid_len], zp, scale)) * 2.0f;

                    box_x = (box_x + (float)j) * (float)stride;
                    box_y = (box_y + (float)i) * (float)stride;
                    box_w = box_w * box_w * (float)anchor_arr[a * 2];
                    box_h = box_h * box_h * (float)anchor_arr[a * 2 + 1];
                    box_x -= (box_w / 2.0f);
                    box_y -= (box_h / 2.0f);

                    int8_t maxClassProbs = in_ptr[5 * grid_len];
                    int maxClassId = 0;
                    for (int k = 1; k < OBJ_CLASS_NUM; ++k)
                    {
                        int8_t prob = in_ptr[(5 + k) * grid_len];
                        if (prob > maxClassProbs)
                        {
                            maxClassId = k;
                            maxClassProbs = prob;
                        }
                    }
                    float limit_score =
                        deqnt_affine_to_f32(maxClassProbs, zp, scale) *
                        deqnt_affine_to_f32(box_confidence, zp, scale);

                    if (limit_score >= threshold)
                    {
                        float_vec_push(objProbs, limit_score);
                        int_vec_push(classId, maxClassId);
                        validCount++;

                        float_vec_push(boxes, box_x);
                        float_vec_push(boxes, box_y);
                        float_vec_push(boxes, box_w);
                        float_vec_push(boxes, box_h);
                    }
                }
            }
        }
    }
    return validCount;
}

static int process_fp32(float *input, int *anchor_arr,
                        int grid_h, int grid_w,
                        int height, int width, int stride,
                        FloatVec *boxes, FloatVec *objProbs, IntVec *classId,
                        float threshold)
{
    (void)height;
    (void)width;

    int validCount = 0;
    int grid_len = grid_h * grid_w;

    for (int a = 0; a < 3; a++)
    {
        for (int i = 0; i < grid_h; i++)
        {
            for (int j = 0; j < grid_w; j++)
            {
                float box_confidence =
                    input[(PROP_BOX_SIZE * a + 4) * grid_len + i * grid_w + j];

                if (box_confidence >= threshold)
                {
                    int offset = (PROP_BOX_SIZE * a) * grid_len + i * grid_w + j;
                    float *in_ptr = input + offset;

                    float box_x = in_ptr[0] * 2.0f - 0.5f;
                    float box_y = in_ptr[grid_len] * 2.0f - 0.5f;
                    float box_w = in_ptr[2 * grid_len] * 2.0f;
                    float box_h = in_ptr[3 * grid_len] * 2.0f;

                    box_x = (box_x + (float)j) * (float)stride;
                    box_y = (box_y + (float)i) * (float)stride;
                    box_w = box_w * box_w * (float)anchor_arr[a * 2];
                    box_h = box_h * box_h * (float)anchor_arr[a * 2 + 1];
                    box_x -= (box_w / 2.0f);
                    box_y -= (box_h / 2.0f);

                    float maxClassProbs = in_ptr[5 * grid_len];
                    int maxClassId = 0;
                    for (int k = 1; k < OBJ_CLASS_NUM; ++k)
                    {
                        float prob = in_ptr[(5 + k) * grid_len];
                        if (prob > maxClassProbs)
                        {
                            maxClassId = k;
                            maxClassProbs = prob;
                        }
                    }
                    if (maxClassProbs > threshold)
                    {
                        float_vec_push(objProbs, maxClassProbs * box_confidence);
                        int_vec_push(classId, maxClassId);
                        validCount++;

                        float_vec_push(boxes, box_x);
                        float_vec_push(boxes, box_y);
                        float_vec_push(boxes, box_w);
                        float_vec_push(boxes, box_h);
                    }
                }
            }
        }
    }
    return validCount;
}

/* ------------------------ anchors (same as C++) ------------------------ */

const int anchor[3][6] = {
    {10, 13, 16, 30, 33, 23},
    {30, 61, 62, 45, 59, 119},
    {116, 90, 156, 198, 373, 326}
};

/* ------------------------ main post_process ------------------------ */

int post_process(rknn_app_context_t *app_ctx,
                 void *outputs,
                 letterbox_t *letter_box,
                 float conf_threshold,
                 float nms_threshold,
                 object_detect_result_list *od_results)
{
#if defined(RV1106_1103)
    rknn_tensor_mem **_outputs = (rknn_tensor_mem **)outputs;
#else
    rknn_output *_outputs = (rknn_output *)outputs;
#endif

    FloatVec filterBoxes;
    FloatVec objProbs;
    IntVec   classId;
    IntVec   indexArray;

    float_vec_init(&filterBoxes);
    float_vec_init(&objProbs);
    int_vec_init(&classId);
    int_vec_init(&indexArray);

    int validCount = 0;
    int stride = 0;
    int grid_h = 0;
    int grid_w = 0;
    int model_in_w = app_ctx->model_width;
    int model_in_h = app_ctx->model_height;

    memset(od_results, 0, sizeof(object_detect_result_list));

    for (int i = 0; i < 3; i++)
    {
#if defined(RV1106_1103)
        grid_h = app_ctx->output_attrs[i].dims[2];
        grid_w = app_ctx->output_attrs[i].dims[3];
        stride = model_in_h / grid_h;

        if (app_ctx->is_quant)
        {
            validCount += process_i8((int8_t *)(_outputs[i]->virt_addr),
                                     (int *)anchor[i],
                                     grid_h, grid_w,
                                     model_in_h, model_in_w, stride,
                                     &filterBoxes, &objProbs, &classId,
                                     conf_threshold,
                                     app_ctx->output_attrs[i].zp,
                                     app_ctx->output_attrs[i].scale);
        }
#elif defined(RKNPU1)
        // NCHW reversed: WHCN
        grid_h = app_ctx->output_attrs[i].dims[1];
        grid_w = app_ctx->output_attrs[i].dims[0];
        stride = model_in_h / grid_h;

        if (app_ctx->is_quant)
        {
            validCount += process_u8((uint8_t *)_outputs[i].buf,
                                     (int *)anchor[i],
                                     grid_h, grid_w,
                                     model_in_h, model_in_w, stride,
                                     &filterBoxes, &objProbs, &classId,
                                     conf_threshold,
                                     app_ctx->output_attrs[i].zp,
                                     app_ctx->output_attrs[i].scale);
        }
        else
        {
            validCount += process_fp32((float *)_outputs[i].buf,
                                       (int *)anchor[i],
                                       grid_h, grid_w,
                                       model_in_h, model_in_w, stride,
                                       &filterBoxes, &objProbs, &classId,
                                       conf_threshold);
        }
#else
        grid_h = app_ctx->output_attrs[i].dims[2];
        grid_w = app_ctx->output_attrs[i].dims[3];
        stride = model_in_h / grid_h;

        if (app_ctx->is_quant)
        {
            validCount += process_i8((int8_t *)_outputs[i].buf,
                                     (int *)anchor[i],
                                     grid_h, grid_w,
                                     model_in_h, model_in_w, stride,
                                     &filterBoxes, &objProbs, &classId,
                                     conf_threshold,
                                     app_ctx->output_attrs[i].zp,
                                     app_ctx->output_attrs[i].scale);
        }
        else
        {
            validCount += process_fp32((float *)_outputs[i].buf,
                                       (int *)anchor[i],
                                       grid_h, grid_w,
                                       model_in_h, model_in_w, stride,
                                       &filterBoxes, &objProbs, &classId,
                                       conf_threshold);
        }
#endif
    }

    if (validCount <= 0)
    {
        float_vec_free(&filterBoxes);
        float_vec_free(&objProbs);
        int_vec_free(&classId);
        int_vec_free(&indexArray);
        return 0;
    }

    // build indexArray
    for (int i = 0; i < validCount; ++i)
        int_vec_push(&indexArray, i);

    // sort by objProbs (desc) – exactly як в C++
    quick_sort_indice_inverse(&objProbs, 0, validCount - 1, &indexArray);

    // emulate std::set<int> over classId
    bool class_present[OBJ_CLASS_NUM];
    memset(class_present, 0, sizeof(class_present));
    for (int i = 0; i < classId.size; ++i)
    {
        int cid = classId.data[i];
        if (cid >= 0 && cid < OBJ_CLASS_NUM)
            class_present[cid] = true;
    }

    for (int c = 0; c < OBJ_CLASS_NUM; ++c)
    {
        if (class_present[c])
        {
            nms(validCount, &filterBoxes, &classId, &indexArray, c, nms_threshold);
        }
    }

    int last_count = 0;
    od_results->count = 0;

    // box valid detect target (як в оригіналі)
    for (int i = 0; i < validCount; ++i)
    {
        if (indexArray.data[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE)
            continue;

        int n = indexArray.data[i];

        float x1 = filterBoxes.data[n * 4 + 0] - letter_box->x_pad;
        float y1 = filterBoxes.data[n * 4 + 1] - letter_box->y_pad;
        float x2 = x1 + filterBoxes.data[n * 4 + 2];
        float y2 = y1 + filterBoxes.data[n * 4 + 3];

        int id = classId.data[n];
        float obj_conf = objProbs.data[i];

        // захист від некоректної scale
        float scale = (letter_box->scale > 0.0f) ? letter_box->scale : 1.0f;

        od_results->results[last_count].box.left =
            (int)(clamp(x1, 0, model_in_w) / scale);
        od_results->results[last_count].box.top =
            (int)(clamp(y1, 0, model_in_h) / scale);
        od_results->results[last_count].box.right =
            (int)(clamp(x2, 0, model_in_w) / scale);
        od_results->results[last_count].box.bottom =
            (int)(clamp(y2, 0, model_in_h) / scale);
        od_results->results[last_count].prop = obj_conf;
        od_results->results[last_count].cls_id = id;
        last_count++;
    }

    od_results->count = last_count;

    float_vec_free(&filterBoxes);
    float_vec_free(&objProbs);
    int_vec_free(&classId);
    int_vec_free(&indexArray);

    return 0;
}

/* ------------------------ labels API ------------------------ */

int init_post_process(int obj_class_num)
{
    FILE *f = fopen(LABEL_NALE_TXT_PATH, "r");
    if (f) {
        fclose(f);
        if (loadLabelName(LABEL_NALE_TXT_PATH, labels) < 0)
        {
            printf("Load %s failed, using built-in COCO labels!\n", LABEL_NALE_TXT_PATH);
        } else {
            printf("Loaded labels from %s\n", LABEL_NALE_TXT_PATH);
            return 0;
        }
    } else {
        printf("%s not found, using built-in COCO labels!\n", LABEL_NALE_TXT_PATH);
    }

    // Fallback: built-in COCO labels
    for (int i = 0; i < OBJ_CLASS_NUM; i++) {
        labels[i] = strdup(default_coco_labels[i]);
    }

    return 0;
}

char *coco_cls_to_name(int cls_id)
{
    if (cls_id < 0 || cls_id >= OBJ_CLASS_NUM)
        return (char *)"null";

    if (labels[cls_id])
        return labels[cls_id];

    return (char *)"null";
}

void deinit_post_process()
{
    for (int i = 0; i < OBJ_CLASS_NUM; i++)
    {
        if (labels[i] != NULL)
        {
            free(labels[i]);
            labels[i] = NULL;
        }
    }
}