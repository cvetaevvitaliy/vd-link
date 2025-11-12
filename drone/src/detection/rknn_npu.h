/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 serhii.machuk@hard-tech.org.ua
 */

#ifndef VISION_CONSOLE_RKNN_NPU_H
#define VISION_CONSOLE_RKNN_NPU_H
#include "postprocess.h"
#include <rga/im2d.h>
#include <rknn/rknn_api.h>

#define NMS_THRESH 0.60f
#define BOX_THRESH 0.50f
#define VIS_THRESH 0.45f

class RknnNpu {
public:
    RknnNpu();
    ~RknnNpu();
    int RknnNpuInit(char* path_to_rknn_model, char* path_to_anchors, int obj_class_num);
    int RknnNpuProcess(void* rgb_frame, int img_width, int img_height, model_type_e model_type,
                       detection_result_group_t* results, float nms_threshold = NMS_THRESH,
                       float box_conf_threshold = BOX_THRESH, float vis_threshold = VIS_THRESH);
    inline int GetModelWidth() const { return m_model_width; }
    inline int GetModelHeight() const { return m_model_height; }

private:
    rknn_context m_ctx{};
    rknn_input_output_num m_io_num{};
    rknn_input m_inputs{};

    std::vector<std::vector<int> > anchors;

    /** Resize buffer for size to input */
    void* m_resize_buf = nullptr;

    /** Variable for input model, default chanel: RGB  */
    int m_in_channel = 3;
    int m_model_width = 0;
    int m_model_height = 0;

    /** image buffer definition, see RGA API*/
    rga_buffer_t m_img_src{};
    rga_buffer_t m_img_dst{};
    im_rect m_src_rect{};
    im_rect m_dst_rect{};

    /** Output model attribute */
    rknn_tensor_attr* m_rknn_tensor_attr = nullptr;

    std::vector<std::vector<int> > LoadAnchors(const char* filename);
    static unsigned char* LoadModel(const char* filename, int* model_size);
    static unsigned char* LoadData(FILE* fp, size_t ofst, size_t sz);
    static void DumpTensorAttr(rknn_tensor_attr* attr);
    void ReleaseResizeBuff();
    static double GetUs(struct timeval t)
    {
        return ((double)t.tv_sec * 1000000 + (double)t.tv_usec);
    }

    inline static const char* getQntTypeString(rknn_tensor_qnt_type type)
    {
        switch (type) {
        case RKNN_TENSOR_QNT_NONE:
            return "NONE";
        case RKNN_TENSOR_QNT_DFP:
            return "DFP";
        case RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC:
            return "AFFINE";
        default:
            return "UNKNOW";
        }
    }

    inline static const char* getFormatString(rknn_tensor_format fmt)
    {
        switch (fmt) {
        case RKNN_TENSOR_NCHW:
            return "NCHW";
        case RKNN_TENSOR_NHWC:
            return "NHWC";
        default:
            return "UNKNOW";
        }
    }

    inline static const char* getTypeString(rknn_tensor_type type)
    {
        switch (type) {
        case RKNN_TENSOR_FLOAT32:
            return "FP32";
        case RKNN_TENSOR_FLOAT16:
            return "FP16";
        case RKNN_TENSOR_INT8:
            return "INT8";
        case RKNN_TENSOR_UINT8:
            return "UINT8";
        case RKNN_TENSOR_INT16:
            return "INT16";
        default:
            return "UNKNOW";
        }
    }
};

#endif //VISION_CONSOLE_RKNN_NPU_H
