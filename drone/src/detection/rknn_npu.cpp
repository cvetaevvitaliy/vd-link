/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#include <fstream>
#include <rga/im2d.h>
#include <rga/rga.h>
#include <sys/time.h>
#include <rknn/rknn_api.h>
#include <cstdlib>
#include <cstring>
#include "rknn_npu.h"
#include "postprocess.h"

#define SAVE_TO_FILES 0
#if SAVE_TO_FILES
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

#define DEBUG_RKNNNN 0
/* Temporrary defined here until log system be implemented */
#if DEBUG_RKNNNN
    #define SYS_LOG_DEBUG(fmt, args...) printf("[DEBUG] " fmt, ##args)
#else
    #define SYS_LOG_DEBUG(fmt, args...) do {} while (0)
#endif
#define SYS_LOG_INFO(fmt, args...) printf("[INFO] " fmt, ##args)
#define SYS_LOG_ERROR(fmt, args...) printf("[ERROR] " fmt, ##args)


RknnNpu::RknnNpu()
{
    printf("NPU: %s\n", __FUNCTION__);
}

RknnNpu::~RknnNpu()
{
    SYS_LOG_DEBUG("Destroy NPU: %s\n", __FUNCTION__);

    if (m_rknn_tensor_attr != nullptr) {
        free(m_rknn_tensor_attr);
    }

    ReleaseResizeBuff();

    rknn_destroy(m_ctx);
}

int RknnNpu::RknnNpuInit(char* path_to_rknn_model, char* path_to_anchors, int obj_class_num)
{
    int ret = -1; //TODO: need to add enum with return error status

    if (path_to_rknn_model == nullptr || obj_class_num <= 0) {
        SYS_LOG_ERROR("NULL pointer'\n");
        return -1;
    }

    /** Load the neural network and init */
    SYS_LOG_INFO("Loading model: %s\n", path_to_rknn_model);
    int model_data_size = 0;
    unsigned char* model_data = LoadModel(path_to_rknn_model, &model_data_size);
    ret = rknn_init(&m_ctx, model_data, model_data_size, 0);
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_init error ret=%d\n", ret);
        return -1;
    }

    /** Load anchors */
    SYS_LOG_INFO("Loading anchors: %s\n", path_to_anchors);
    anchors = LoadAnchors(path_to_anchors);
    if (anchors.empty()) {
        SYS_LOG_ERROR("anchor file cannot be read. \n");
        rknn_destroy(m_ctx);
        return -1;
    }

    /** Get information of SDK */
    rknn_sdk_version version;
    ret = rknn_query(m_ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_query error ret=%d\n", ret);
        rknn_destroy(m_ctx);
        return -1;
    }
    SYS_LOG_INFO("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    /** Get information of model input and output information */
    ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &m_io_num, sizeof(m_io_num));
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_query error ret=%d\n", ret);
        rknn_destroy(m_ctx);
        return -1;
    }
    SYS_LOG_INFO("model input num: %d, output num: %d\n", m_io_num.n_input, m_io_num.n_output);

    rknn_tensor_attr input_attrs[m_io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (uint32_t i = 0; i < m_io_num.n_input; i++) {
        input_attrs[i].index = i;
        ret = rknn_query(m_ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            SYS_LOG_ERROR("Set input attribute error ret=%d\n", ret);
            rknn_destroy(m_ctx);
            return -1;
        }
        DumpTensorAttr(&(input_attrs[i]));
    }

    rknn_tensor_attr output_attrs[m_io_num.n_output];
    memset(output_attrs, 0, sizeof(output_attrs));
    for (uint32_t i = 0; i < m_io_num.n_output; i++) {
        output_attrs[i].index = i;
        ret = rknn_query(m_ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            SYS_LOG_ERROR("Set output attribute error ret=%d\n", ret);
            rknn_destroy(m_ctx);
            return -1;
        }
        DumpTensorAttr(&(output_attrs[i]));
    }

    m_rknn_tensor_attr = (rknn_tensor_attr*)malloc(sizeof(output_attrs) * m_io_num.n_output);
    if (m_rknn_tensor_attr == nullptr) {
        SYS_LOG_ERROR("Error create tensor atr error ret=%d\n", ret);
        rknn_destroy(m_ctx);
        return -1;
    }
    memcpy(m_rknn_tensor_attr, output_attrs, sizeof(rknn_tensor_attr) * m_io_num.n_output);

    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        SYS_LOG_INFO("model is NCHW input fmt\n");
        m_model_width = (int)input_attrs[0].dims[0];
        m_model_height = (int)input_attrs[0].dims[1];
        m_in_channel = (int)input_attrs[0].dims[2];
    } else {
        SYS_LOG_INFO("model is NHWC input fmt\n");
        m_model_height = (int)input_attrs[0].dims[2];
        m_model_width = (int)input_attrs[0].dims[1];
        m_in_channel = (int)input_attrs[0].dims[0];
    }

    SYS_LOG_INFO("model input image: height='%dpx', width='%dpx', color channel='%d'\n", m_model_height, m_model_width,
                 m_in_channel);

    memset(&m_inputs, 0, sizeof(m_inputs));
    m_inputs.index = 0;
    m_inputs.type = RKNN_TENSOR_UINT8;
    m_inputs.size = m_model_width * m_model_height * m_in_channel;
    m_inputs.fmt = RKNN_TENSOR_NHWC;
    m_inputs.pass_through = 0;

    ret = rknn_run(m_ctx, NULL);
    if (ret < 0) {
        SYS_LOG_ERROR("Run NPU error, ret=%d\n", ret);
        rknn_destroy(m_ctx);
        return -1;
    }

    m_resize_buf = malloc(m_model_height * m_model_width * m_in_channel);
    if (m_resize_buf == nullptr) {
        SYS_LOG_ERROR("Error allocate memory\n");
    }

    init_post_process(obj_class_num);

    return 0;
}

int RknnNpu::RknnNpuProcess(void* rgb_frame, int img_width, int img_height, model_type_e model_type,
                            detection_result_group_t* results, float nms_threshold, float box_conf_threshold,
                            float vis_threshold)
{
    int ret = -1;
    struct timeval start_time {
    };
    struct timeval stop_time {
    };
    const float scale_w = (float)m_model_width / (float)img_width;
    const float scale_h = (float)m_model_height / (float)img_height;

    if (rgb_frame == nullptr || results == nullptr) {
        SYS_LOG_ERROR("NULL pointer'\n");
        return -1;
    }
    if (img_width <= 0 || img_height <= 0) {
        SYS_LOG_ERROR("Input image is small'\n");
        return -1;
    }
    if (img_width != m_model_width || img_height != m_model_height) {
        /** create buff for resize image
         * resize with RGA - hardware feature RK3588
         * RGA API: "im2d.h", "RgaApi.h", "rga.h"
         * */

        //SYS_LOG_DEBUG("scale_w = %.3f, scale_h = %.3f\n", scale_w, scale_h);

        m_img_src = wrapbuffer_virtualaddr((void*)rgb_frame, img_width, img_height, RK_FORMAT_RGB_888);
        m_img_dst = wrapbuffer_virtualaddr((void*)m_resize_buf, m_model_width, m_model_height, RK_FORMAT_RGB_888);

        /** Resize image to input size with scale */
        ret = imresize_t(m_img_src, m_img_dst, scale_w, scale_h, INTER_LINEAR, 1);
        if (IM_STATUS_SUCCESS != ret) {
            SYS_LOG_ERROR("%d, Resize image error! %s\n", __LINE__, imStrError((IM_STATUS)ret));
            ReleaseResizeBuff();
            return -1;
        }
#if SAVE_TO_FILES
        // for debug, save frame to file
        char str[50];
        static int cnt = 0;
        sprintf(str, "resize_input_%d.jpg", cnt++);
        cv::Mat resize_img(cv::Size(m_model_width, m_model_height), CV_8UC3, m_resize_buf);
        cv::imwrite(str, resize_img);
#endif
        m_inputs.buf = m_resize_buf;
    } else {
        m_inputs.buf = rgb_frame;
    }
    /** Put the resized RGA image to NPU */
    m_inputs.size = m_model_height * m_model_width * m_in_channel;
    SYS_LOG_DEBUG("Size: %d \n", m_inputs.size);

    gettimeofday(&start_time, NULL);
    ret = rknn_inputs_set(m_ctx, m_io_num.n_input, &m_inputs);
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_inputs_set error ret=%d\n", ret);
        ReleaseResizeBuff();
        return -1;
    }

    rknn_output outputs[m_io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (uint32_t i = 0; i < m_io_num.n_output; i++) {
        outputs[i].want_float = 0;
    }

    /** Run NPU process */
    ret = rknn_run(m_ctx, nullptr);
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_run error ret=%d\n", ret);
        ReleaseResizeBuff();
        return -1;
    }

    /** Get results from NPU */
    ret = rknn_outputs_get(m_ctx, m_io_num.n_output, outputs, NULL);
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_outputs_get error ret=%d\n", ret);
        ReleaseResizeBuff();
        return -1;
    }

    /** Calculate NPU execution time */
    gettimeofday(&stop_time, NULL);
    auto run_time = (GetUs(stop_time) - GetUs(start_time)) / 1000.0;
    SYS_LOG_DEBUG("NPU execution time: %.2f ms\n", run_time);

    /** post process */
    if (m_rknn_tensor_attr == nullptr) {
        SYS_LOG_ERROR("Output tensor size is NULL\n");
        ReleaseResizeBuff();
        return -1;
    }

    if (post_process(anchors, outputs, m_rknn_tensor_attr, model_type, m_model_height, m_model_width,
                     box_conf_threshold, nms_threshold, vis_threshold, scale_w, scale_h, results) < 0) {
        SYS_LOG_ERROR("post_process error ret=%d\n", ret);
        ReleaseResizeBuff();
        return -1;
    }

    ret = rknn_outputs_release(m_ctx, m_io_num.n_output, outputs);
    if (ret < 0) {
        SYS_LOG_ERROR("rknn_outputs_release error ret=%d\n", ret);
        return -1;
    }

    results->once_npu_run = (float)run_time;

    return 0;
}

unsigned char* RknnNpu::LoadData(FILE* fp, size_t ofst, size_t sz)
{
    unsigned char* data = nullptr;
    int ret = 0;

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0) {
        SYS_LOG_ERROR("blob seek failure.\n");
        return nullptr;
    }

    data = (unsigned char*)malloc(sz);
    if (data == nullptr) {
        SYS_LOG_ERROR("buffer malloc failure.\n");
        return nullptr;
    }
    const size_t rb = fread(data, 1, sz, fp);
    if (rb > 0) {
        return data;
    }
    return nullptr;
}

std::vector<std::vector<int> > RknnNpu::LoadAnchors(const char* filename)
{
    std::ifstream file(filename);
    if (!file) {
        return {};
    }
    int n_rows = 0;
    int n_cols = 0;
    file >> n_rows >> n_cols;

    std::vector<std::vector<int> > anchors(n_rows, std::vector<int>(n_cols));
    for (int i = 0; i < n_rows; ++i) {
        for (int j = 0; j < n_cols; ++j) {
            file >> anchors[i][j];
        }
    }

    file.close();
    return anchors;
}

unsigned char* RknnNpu::LoadModel(const char* filename, int* model_size)
{
    FILE* fp = nullptr;
    unsigned char* data = nullptr;

    fp = fopen(filename, "rb");
    if (nullptr == fp) {
        SYS_LOG_ERROR("Open file %s failed.\n", filename);
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    const int size = ftell(fp);

    data = LoadData(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}

void RknnNpu::DumpTensorAttr(rknn_tensor_attr* attr)
{
    SYS_LOG_INFO(
        "  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
        "zp=%d, scale=%f\n",
        attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
        attr->n_elems, attr->size, getFormatString(attr->fmt), getTypeString(attr->type),
        getQntTypeString(attr->qnt_type), attr->zp, attr->scale);
}

void RknnNpu::ReleaseResizeBuff()
{
    if (m_resize_buf) {
        free(m_resize_buf);
    }
}

#undef SYS_LOG_DEBUG
#undef SYS_LOG_INFO
#undef SYS_LOG_ERROR