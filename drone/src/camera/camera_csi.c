/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "camera/camera_csi.h"
#include "camera/isp/sample_common.h"

#include <easymedia/rkmedia_api.h>

#define DEFAULT_IQ_FILES_PATH "/etc/iqfiles"

int camera_csi_init(camera_csi_config_t *cfg)
{
    int ret = 0;
    if (!cfg) {
        printf("Camera config is NULL\n");
        return -1;
    }

    if (SAMPLE_COMM_ISP_Init(cfg->cam_id, RK_AIQ_WORKING_MODE_NORMAL, RK_FALSE, DEFAULT_IQ_FILES_PATH)) {
        printf("ISP Init error!\n");
        return -1;
    }

    if (SAMPLE_COMM_ISP_Run(cfg->cam_id)) {
        printf("ISP Run error!\n");
        return -1;
    }

    RK_U32 mirror_flip_value = cfg->mirror + cfg->flip * 2;
    SAMPLE_COMM_ISP_SET_mirror(cfg->cam_id, mirror_flip_value);

    SAMPLE_COMM_ISP_SET_Brightness(cfg->cam_id, cfg->brightness);
    SAMPLE_COMM_ISP_SET_Contrast(cfg->cam_id, cfg->contrast);
    SAMPLE_COMM_ISP_SET_Saturation(cfg->cam_id, cfg->saturation);
    SAMPLE_COMM_ISP_SET_Sharpness(cfg->cam_id, cfg->sharpness);

    SAMPLE_COMM_ISP_SET_Correction(cfg->cam_id, RK_TRUE, cfg->correction); // LDC (Lens Distortion Correction)
    SAMPLE_COMM_ISP_SetFecEn(cfg->cam_id, RK_FALSE); // FEC (Fish-Eye Correction)

    SAMPLE_COMMON_ISP_SET_DNRStrength(cfg->cam_id, 3 /* off */, 16 /* 2D */, 8 /* 3D */);

    SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(cfg->cam_id, cfg->auto_white_balance);

    // Initialize exposure parameters
    SAMPLE_COMM_ISP_SET_AutoExposure(cfg->cam_id);
    SAMPLE_COMM_ISP_SET_FastAutoExposure(cfg->cam_id, cfg->fast_ae_min_time,
                                        cfg->fast_ae_max_time, cfg->fast_ae_max_gain);

    // Initialize light inhibition and backlight
    SAMPLE_COMM_ISP_SET_LightInhibition(cfg->cam_id, cfg->light_inhibition_enable,
                                       cfg->light_inhibition_strength, cfg->light_inhibition_level);
    SAMPLE_COMM_ISP_SET_BackLight(cfg->cam_id, cfg->backlight_enable, cfg->backlight_strength);

    // RK_MPI_SYS_Init();

    VI_CHN_ATTR_S vi_chn_attr = {0};
    vi_chn_attr.pcVideoNode = "rkispp_scale0";
    // Increase buffer count to reduce risk of transient buffer starvation
    vi_chn_attr.u32BufCnt = 6;
    vi_chn_attr.u32Width = cfg->width;
    vi_chn_attr.u32Height = cfg->height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_NV12;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_DMA;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;

    ret = RK_MPI_VI_SetChnAttr(0, 0, &vi_chn_attr);
    if (ret) {
        printf("Create vi[0] error! ret=%d\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(0, 0);
    if (ret) {
        printf("Enable vi[0] error! ret=%d\n", ret);
        return -1;
    }

    return 0;
}

int camera_csi_bind_encoder(int cam_id, int enc_id)
{
    int ret = 0;
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // Bind Camera VI[0] and Encoder VENC[0]
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = cam_id;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = enc_id;
    stDestChn.s32ChnId = 0;
    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("Bind VI[0] and VENC[0] error! ret=%d\n", ret);
        return -1;
    }
    return 0;
}

int camera_csi_bind_detection(camera_csi_config_t *camera_csi_config, common_config_t *common_config)
{
    int ret = 0;
    
    // Create RGA channel for detection preprocessing
    RGA_ATTR_S rga_attr = {0};
    rga_attr.stImgIn.imgType = IMAGE_TYPE_NV12;
    rga_attr.stImgIn.u32Width = camera_csi_config->width;
    rga_attr.stImgIn.u32Height = camera_csi_config->height;
    rga_attr.stImgIn.u32HorStride = camera_csi_config->width;
    rga_attr.stImgIn.u32VirStride = camera_csi_config->height;
    rga_attr.stImgIn.u32X = 0;
    rga_attr.stImgIn.u32Y = 0;

    rga_attr.stImgOut.imgType = IMAGE_TYPE_RGB888; // Detection needs RGB format
    rga_attr.stImgOut.u32Width = 640;  // Model input width
    rga_attr.stImgOut.u32Height = 384; // Model input height
    rga_attr.stImgOut.u32HorStride = 640;
    rga_attr.stImgOut.u32VirStride = 384;
    rga_attr.stImgOut.u32X = 0;
    rga_attr.stImgOut.u32Y = 0;

    ret = RK_MPI_RGA_CreateChn(0, &rga_attr);
    if (ret) {
        printf("Create RGA[0] for CSI camera detection failed! ret=%d\n", ret);
        return -1;
    }

    // Bind Camera VI[0] and RGA[0]
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};
    
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = camera_csi_config->cam_id;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_RGA;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    
    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("Bind VI[%d] and RGA[0] for detection failed! ret=%d\n", camera_csi_config->cam_id, ret);
        RK_MPI_RGA_DestroyChn(0);
        return -1;
    }
    
    printf("CSI camera detection RGA pipeline created successfully\n");
    return 0;
}

int camera_csi_unbind_detection(int cam_id)
{
    int ret = 0;
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // Unbind VI and RGA
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = cam_id;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_RGA;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;

    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("UnBind VI[%d] and RGA[0] for detection failed! ret=%d\n", cam_id, ret);
    }

    // Destroy RGA channel
    ret = RK_MPI_RGA_DestroyChn(0);
    if (ret) {
        printf("Destroy RGA[0] for detection failed! ret=%d\n", ret);
    }

    return ret;
}

int camera_csi_unbind_encoder(int cam_id, int enc_id)
{
    int ret = 0;
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // UNBind Camera VI[0] and Encoder VENC[0]
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = cam_id;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = enc_id;
    stDestChn.s32ChnId = 0;

    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn)) {
        printf("UnBind VI[0] and VENC[0] error! ret=%d\n", ret);
        return -1;
    }

    return 0;
}

int camera_csi_deinit(camera_csi_config_t *cfg)
{
    int ret = 0;
    if (RK_MPI_VI_DisableChn(cfg->cam_id, 0)) {
        printf("Disable VI[0] error! ret=%d\n", ret);
    }

    if (SAMPLE_COMM_ISP_Stop(cfg->cam_id)) {
        printf("ISP: Stop error!\n");
        return -1;
    }
    return 0;
}
