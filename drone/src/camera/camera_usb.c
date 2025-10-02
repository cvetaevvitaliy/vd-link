/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "camera/camera_usb.h"
#include <stdio.h>
#include <easymedia/rkmedia_api.h>
#include <easymedia/rkmedia_vi.h>
#include "common.h"

int camera_usb_init(camera_info_t* camera_info, common_config_t* common_config)
{
    if (!camera_info || !common_config) {
        printf("Camera USB config or common config is NULL\n");
        return -1;
    }

    camera_usb_config_t* config = &common_config->camera_usb_config;
    int ret;
    config->height = camera_info->supported_resolutions[0].height;
    config->width = camera_info->supported_resolutions[0].width;

    printf("Initializing USB camera at %s with resolution %dx%d\n",
           camera_info->device_path, config->width, config->height);

    VI_CHN_ATTR_S vi_chn_attr = {0};
    vi_chn_attr.pcVideoNode = camera_info->device_path;
    vi_chn_attr.u32BufCnt = 3;
    vi_chn_attr.u32Width = config->width;
    vi_chn_attr.u32Height = config->height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_YUYV422;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_MMAP;
    
    ret = RK_MPI_VI_SetChnAttr(0, 1, &vi_chn_attr);
    if (ret) {
        printf("Create vi[1] failed! ret=%d\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(0, 1);
    if (ret) {
        printf("Enable vi[1] failed! ret=%d\n", ret);
        return -1;
    }

    RGA_ATTR_S rga_attr = {0};
    rga_attr.stImgIn.imgType = IMAGE_TYPE_YUYV422;
    rga_attr.stImgIn.u32Width = config->width;
    rga_attr.stImgIn.u32Height = config->height;
    rga_attr.stImgIn.u32HorStride = config->width * 2; // YUYV has 2 bytes per pixel
    rga_attr.stImgIn.u32VirStride = config->height;
    rga_attr.stImgIn.u32X = 0;
    rga_attr.stImgIn.u32Y = 0;
    
    rga_attr.stImgOut.imgType = IMAGE_TYPE_NV12; // Force NV12 output
    rga_attr.stImgOut.u32Width = common_config->stream_width;
    rga_attr.stImgOut.u32Height = common_config->stream_height;
    rga_attr.stImgOut.u32HorStride = common_config->stream_width;
    rga_attr.stImgOut.u32VirStride = common_config->stream_height;
    rga_attr.stImgOut.u32X = 0;
    rga_attr.stImgOut.u32Y = 0;

    ret = RK_MPI_RGA_CreateChn(0, &rga_attr);
    if (ret) {
        printf("Create RGA[0] for USB camera failed! ret=%d\n", ret);
        return -1;
    }

    // Bind Camera VI[1] and RGA[0]
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 1;
    stDestChn.enModId = RK_ID_RGA;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("Bind VI[1] and RGA[0] for USB camera failed! ret=%d\n", ret);
        return -1;
    }

    return 0;
}

void camera_usb_deinit(void)
{
    RK_MPI_VI_DisableChn(0, 1);
}


int camera_usb_bind_encoder(int cam_id, int enc_id)
{
    (void)cam_id; // Unused parameter
    int ret = 0;
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // Bind Camera VI[1] and Encoder VENC[0]
    stSrcChn.enModId = RK_ID_RGA;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = enc_id;
    stDestChn.s32ChnId = 0;
    ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (ret) {
        printf("Bind VI[1] and VENC[0] failed! ret=%d\n", ret);
        return -1;
    }
    return 0;
}

int camera_usb_unbind_encoder(int cam_id, int enc_id)
{
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // UNBind Camera VI[1] and Encoder VENC[0]
    stSrcChn.enModId = RK_ID_RGA;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = enc_id;
    stDestChn.s32ChnId = 0;
    RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

    return 0;
}