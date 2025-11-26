/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "rknn_yolov5.h"

// rknn_npu.c - C port of RknnNpu C++ class (without RGA resize)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <rknn/rknn_api.h>
#include <rga/im2d.h>
#include <rga/rga.h>


#include "postprocess.h"
#include "rknn_yolov5.h"

static void *m_resize_buf = NULL;
static rga_buffer_t m_img_src;
static rga_buffer_t m_img_dst;

/* ------------ helpers: load model, dump attr ------------ */
static double GetUs(struct timeval t) { return ((double)t.tv_sec * 1000000 + (double)t.tv_usec); }
inline static const char* getFormatString(rknn_tensor_format fmt)
{
    switch(fmt) {
    case RKNN_TENSOR_NCHW: return "NCHW";
    case RKNN_TENSOR_NHWC: return "NHWC";
    default: return "UNKNOW";
    }
}

inline static const char* getTypeString(rknn_tensor_type type)
{
    switch(type) {
    case RKNN_TENSOR_FLOAT32: return "FP32";
    case RKNN_TENSOR_FLOAT16: return "FP16";
    case RKNN_TENSOR_INT8: return "INT8";
    case RKNN_TENSOR_UINT8: return "UINT8";
    case RKNN_TENSOR_INT16: return "INT16";
    default: return "UNKNOW";
    }
}

inline static const char* getQntTypeString(rknn_tensor_qnt_type type)
{
    switch(type) {
    case RKNN_TENSOR_QNT_NONE: return "NONE";
    case RKNN_TENSOR_QNT_DFP: return "DFP";
    case RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC: return "AFFINE";
    default: return "UNKNOW";
    }
}

static unsigned char *rknn_npu_load_data(FILE *fp, size_t ofst, size_t sz)
{
    if (fseek(fp, (long)ofst, SEEK_SET) != 0) {
        printf(" [RKNN] blob seek failure.\n");
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc(sz);
    if (!data) {
        printf(" [RKNN] buffer malloc failure.\n");
        return NULL;
    }

    size_t rb = fread(data, 1, sz, fp);
    if (rb > 0)
        return data;

    free(data);
    return NULL;
}

static unsigned char *rknn_npu_load_model(const char *filename, int *model_size)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf(" [RKNN] Open file %s failed.\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size <= 0) {
        printf(" [RKNN] ftell failed or size <= 0\n");
        fclose(fp);
        return NULL;
    }

    unsigned char *data = rknn_npu_load_data(fp, 0, (size_t)size);
    fclose(fp);

    if (!data)
        return NULL;

    *model_size = (int)size;
    return data;
}

static void rknn_npu_dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf(
        "  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, "
        "fmt=%s, type=%s, qnt_type=%s, zp=%d, scale=%f\n",
        attr->index,
        attr->name,
        attr->n_dims,
        attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
        attr->n_elems,
        attr->size,
        getFormatString(attr->fmt),
        getTypeString(attr->type),
        getQntTypeString(attr->qnt_type),
        attr->zp,
        attr->scale
    );
}



/* ----------------- public API ----------------- */

void ReleaseResizeBuff(void)
{
    if (m_resize_buf) {
        free(m_resize_buf);
    }
}

RknnNpuCtx *rknn_npu_create(void)
{
    RknnNpuCtx *ctx = (RknnNpuCtx *)calloc(1, sizeof(RknnNpuCtx));
    if (!ctx)
        return NULL;

    printf(" [RKNN] NPU: rknn_npu_create\n");
    return ctx;
}

void rknn_npu_destroy(RknnNpuCtx *ctx)
{
    if (!ctx)
        return;

    printf(" [RKNN] Destroy NPU: rknn_npu_destroy\n");

    if (ctx->output_attrs) {
        free(ctx->output_attrs);
        ctx->output_attrs = NULL;
    }

    if (ctx->ctx) {
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
    }

    free(ctx);

    ReleaseResizeBuff();
}

/* Init NPU with model */
int rknn_npu_init(RknnNpuCtx *ctx, const char *path_to_rknn_model, int obj_class_num)
{
    if (!ctx || !path_to_rknn_model || obj_class_num <= 0) {
        printf(" [RKNN] NULL pointer or invalid params\n");
        return -1;
    }

    int ret;
    int model_data_size = 0;

    printf(" [RKNN] Loading model: %s\n", path_to_rknn_model);
    unsigned char *model_data = rknn_npu_load_model(path_to_rknn_model, &model_data_size);
    if (!model_data) {
        printf(" [RKNN] Load model failed\n");
        return -1;
    }

    ret = rknn_init(&ctx->ctx, model_data, model_data_size, 0);
    free(model_data);  // rknn_init копіює всередину, оригінал можна вільнити
    if (ret < 0) {
        printf(" [RKNN] rknn_init error ret=%d\n", ret);
        return -1;
    }

    /* SDK version */
    rknn_sdk_version version;
    ret = rknn_query(ctx->ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(version));
    if (ret < 0) {
        printf(" [RKNN] RKNN_QUERY_SDK_VERSION error ret=%d\n", ret);
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
        return -1;
    }
    printf(" [RKNN] sdk version: %s driver version: %s\n",
                 version.api_version, version.drv_version);

    /* in/out num */
    ret = rknn_query(ctx->ctx, RKNN_QUERY_IN_OUT_NUM, &ctx->io_num, sizeof(ctx->io_num));
    if (ret < 0) {
        printf(" [RKNN] RKNN_QUERY_IN_OUT_NUM error ret=%d\n", ret);
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
        return -1;
    }
    printf(" [RKNN] model input num: %d, output num: %d\n",
                 ctx->io_num.n_input, ctx->io_num.n_output);

    /* input attrs */
    rknn_tensor_attr *input_attrs =
        (rknn_tensor_attr *)calloc(ctx->io_num.n_input, sizeof(rknn_tensor_attr));
    if (!input_attrs) {
        printf(" [RKNN] malloc input_attrs failed\n");
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
        return -1;
    }

    for (uint32_t i = 0; i < ctx->io_num.n_input; i++) {
        memset(&input_attrs[i], 0, sizeof(rknn_tensor_attr));
        input_attrs[i].index = i;
        ret = rknn_query(ctx->ctx, RKNN_QUERY_INPUT_ATTR,
                         &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf(" [RKNN] RKNN_QUERY_INPUT_ATTR error ret=%d\n", ret);
            free(input_attrs);
            rknn_destroy(ctx->ctx);
            ctx->ctx = 0;
            return -1;
        }
        rknn_npu_dump_tensor_attr(&input_attrs[i]);
    }

    /* output attrs */
    ctx->output_attrs =
        (rknn_tensor_attr *)calloc(ctx->io_num.n_output, sizeof(rknn_tensor_attr));
    if (!ctx->output_attrs) {
        printf(" [RKNN] malloc output_attrs failed\n");
        free(input_attrs);
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
        return -1;
    }

    for (uint32_t i = 0; i < ctx->io_num.n_output; i++) {
        memset(&ctx->output_attrs[i], 0, sizeof(rknn_tensor_attr));
        ctx->output_attrs[i].index = i;
        ret = rknn_query(ctx->ctx, RKNN_QUERY_OUTPUT_ATTR,
                         &ctx->output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf(" [RKNN] RKNN_QUERY_OUTPUT_ATTR error ret=%d\n", ret);
            free(input_attrs);
            free(ctx->output_attrs);
            ctx->output_attrs = NULL;
            rknn_destroy(ctx->ctx);
            ctx->ctx = 0;
            return -1;
        }
        rknn_npu_dump_tensor_attr(&ctx->output_attrs[i]);
    }

    /* resolve model H/W/C (копія логіки з C++) */
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf(" [RKNN] model is NCHW input fmt\n");
        ctx->model_width  = (int)input_attrs[0].dims[0];
        ctx->model_height = (int)input_attrs[0].dims[1];
        ctx->in_channel   = (int)input_attrs[0].dims[2];
    } else {
        printf(" [RKNN] model is NHWC input fmt\n");
        ctx->model_height = (int)input_attrs[0].dims[2];
        ctx->model_width  = (int)input_attrs[0].dims[1];
        ctx->in_channel   = (int)input_attrs[0].dims[0];
    }

    printf(" [RKNN] model input image: height='%dpx', width='%dpx', channel='%d'\n",
                 ctx->model_height, ctx->model_width, ctx->in_channel);

    free(input_attrs);

    /* set input template */
    memset(&ctx->input, 0, sizeof(ctx->input));
    ctx->input.index        = 0;
    ctx->input.type         = RKNN_TENSOR_UINT8;
    ctx->input.fmt          = RKNN_TENSOR_NHWC;
    ctx->input.pass_through = 0;
    ctx->input.size         = ctx->model_width * ctx->model_height * ctx->in_channel;

    ret = rknn_run(ctx->ctx, NULL);
    if (ret < 0) {
        printf(" [RKNN] initial rknn_run error, ret=%d\n", ret);
        rknn_destroy(ctx->ctx);
        ctx->ctx = 0;
        free(ctx->output_attrs);
        ctx->output_attrs = NULL;
        return -1;
    }

    init_post_process(obj_class_num);

    m_resize_buf = malloc(ctx->model_height * ctx->model_width * ctx->in_channel);
    if (m_resize_buf == NULL) {
        printf(" [RKNN] Error allocate memory\n");
        return -1;
    }


    return 0;
}

/* Run one inference; без RGA, rgb_frame вже розміру model_width x model_height */
int rknn_npu_process(RknnNpuCtx *ctx, void *rgb_frame, int img_width, int img_height,
                     detect_result_group_t *results,
                     float nms_threshold,
                     float box_conf_threshold,
                     float vis_threshold)
{
    if (!ctx || !rgb_frame || !results) {
        printf(" [RKNN] rknn_npu_process: NULL pointer\n");
        return -1;
    }

    if (img_width <= 0 || img_height <= 0) {
        printf(" [RKNN] Input image is small or invalid\n");
        return -1;
    }

    struct timeval start_time, stop_time;
    int ret;

    float scale_w = (float)ctx->model_width  / (float)img_width;
    float scale_h = (float)ctx->model_height / (float)img_height;

    //m_img_src = wrapbuffer_virtualaddr((void *) rgb_frame, img_width, img_height, RK_FORMAT_RGB_888);
    // m_img_src = wrapbuffer_virtualaddr((void *) rgb_frame, ctx->model_width, ctx->model_height, RK_FORMAT_RGB_888);
    // m_img_dst = wrapbuffer_virtualaddr((void *) m_resize_buf, ctx->model_width, ctx->model_height, RK_FORMAT_RGB_888);
    //
    // ret = imresize_t(m_img_src, m_img_dst, scale_w, scale_h, INTER_LINEAR, 1);
    // if (IM_STATUS_SUCCESS != ret) {
    //     printf(" [RKNN] %d, Resize image error! %s\n", __LINE__, imStrError((IM_STATUS)ret));
    //     ReleaseResizeBuff();
    //     return -1;
    // }


    /* Put input buffer directly (no resize) */
    ctx->input.buf  = rgb_frame;
    //ctx->input.buf  = m_resize_buf;
    ctx->input.size = ctx->model_width * ctx->model_height * ctx->in_channel;

    gettimeofday(&start_time, NULL);

    ret = rknn_inputs_set(ctx->ctx, ctx->io_num.n_input, &ctx->input);
    if (ret < 0) {
        printf(" [RKNN] rknn_inputs_set error ret=%d\n", ret);
        return -1;
    }

    /* prepare outputs */
    rknn_output *outputs =
        (rknn_output *)calloc(ctx->io_num.n_output, sizeof(rknn_output));
    if (!outputs) {
        printf(" [RKNN] malloc outputs failed\n");
        return -1;
    }

    for (uint32_t i = 0; i < ctx->io_num.n_output; i++) {
        outputs[i].want_float = 0;  // uint8 output
    }

    /* Run NPU */
    ret = rknn_run(ctx->ctx, NULL);
    if (ret < 0) {
        printf(" [RKNN] rknn_run error ret=%d\n", ret);
        free(outputs);
        return -1;
    }

    /* Get outputs */
    ret = rknn_outputs_get(ctx->ctx, ctx->io_num.n_output, outputs, NULL);
    if (ret < 0) {
        printf(" [RKNN] rknn_outputs_get error ret=%d\n", ret);
        free(outputs);
        return -1;
    }

    gettimeofday(&stop_time, NULL);
    float run_time = (float)(GetUs(stop_time) - GetUs(start_time)) / 1000.0f;

    if (!ctx->output_attrs) {
        printf(" [RKNN] Output tensor attrs NULL\n");
        rknn_outputs_release(ctx->ctx, ctx->io_num.n_output, outputs);
        free(outputs);
        return -1;
    }

    /* qnt params для постпроцесу */
    uint8_t *out_zps   = (uint8_t *)calloc(ctx->io_num.n_output, sizeof(uint8_t));
    float   *out_scales= (float   *)calloc(ctx->io_num.n_output, sizeof(float));
    if (!out_zps || !out_scales) {
        printf(" [RKNN] malloc out_zps/out_scales failed\n");
        if (out_zps) free(out_zps);
        if (out_scales) free(out_scales);
        rknn_outputs_release(ctx->ctx, ctx->io_num.n_output, outputs);
        free(outputs);
        return -1;
    }

    for (uint32_t i = 0; i < ctx->io_num.n_output; ++i) {
        out_scales[i] = ctx->output_attrs[i].scale;
        out_zps[i]    = (uint8_t)ctx->output_attrs[i].zp;
    }

    ret = post_process(
        (uint8_t *)outputs[0].buf,
        (uint8_t *)outputs[1].buf,
        (uint8_t *)outputs[2].buf,
        ctx->model_height,
        ctx->model_width,
        box_conf_threshold,
        nms_threshold,
        vis_threshold,
        scale_w,
        scale_h,
        out_zps,
        out_scales,
        results
    );

    if (ret < 0) {
        printf(" [RKNN] post_process error ret=%d\n", ret);
        free(out_zps);
        free(out_scales);
        rknn_outputs_release(ctx->ctx, ctx->io_num.n_output, outputs);
        free(outputs);
        return -1;
    }

    free(out_zps);
    free(out_scales);

    ret = rknn_outputs_release(ctx->ctx, ctx->io_num.n_output, outputs);
    if (ret < 0) {
        printf(" [RKNN] rknn_outputs_release error ret=%d\n", ret);
        free(outputs);
        return -1;
    }

    free(outputs);

    results->once_npu_run = run_time;

    return 0;
}
