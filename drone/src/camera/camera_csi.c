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

    if (SAMPLE_COMM_ISP_Init(cfg->cam_id, cfg->hdr_enabled ? RK_AIQ_WORKING_MODE_ISP_HDR2 : RK_AIQ_WORKING_MODE_NORMAL, RK_FALSE, DEFAULT_IQ_FILES_PATH)) {
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
    vi_chn_attr.u32BufCnt = 2;
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

// static void video_cb(void* mb)
// {
//     RK_MPI_MB_BeginCPUAccess(mb, RK_FALSE);
//     //shm_stream_push_frame_buff(SHM_CAMERA_STREAM_KEY, RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetSize(mb));
//     RK_MPI_MB_EndCPUAccess(mb, RK_FALSE);
//     RK_MPI_MB_ReleaseBuffer(mb);
//     printf("video_cb\n");
// }

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

int camera_csi_bind_rknn(int cam_id, int cam_ch, int rga_ch, int rknn_width, int rknn_height)
{
    if (rknn_width <= 0 || rknn_height <= 0) {
        printf("Invalid RKNN input size: %dx%d\n", rknn_width, rknn_height);
        return -1;
    }

    int ret = 0;

    printf("Enable VI[%d]\n", cam_ch);
    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
    vi_chn_attr.pcVideoNode = "rkispp_scale1";
    vi_chn_attr.u32BufCnt = 3;
    vi_chn_attr.u32Width = rknn_width;
    vi_chn_attr.u32Height = rknn_height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_NV12;
    // vi_chn_attr.enWorkMode = VI_WORK_MODE_BUTT;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_DMA;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
    ret = RK_MPI_VI_SetChnAttr(cam_id, cam_ch, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(cam_id, cam_ch);
    if (ret) {
        printf("ERROR: Create VI[%d] ch %d error! code:%d\n", cam_id, cam_ch, ret);
        return -1;
    }

    ret = RK_MPI_VI_StartStream(cam_id, cam_ch);
    if (ret) {
        printf("Start VI[%d] ch %d error! code:%d\n", cam_id, cam_ch, ret);
        return -1;
    }

    // Convert NV12 to RGB
    printf("Creating RGA channels for RKNN input...\n");
    RGA_ATTR_S st_rga_attr;
    memset(&st_rga_attr, 0, sizeof(RGA_ATTR_S));
    st_rga_attr.bEnBufPool = RK_TRUE;
    st_rga_attr.u16BufPoolCnt = 3;
    st_rga_attr.u16Rotaion = 0;
    st_rga_attr.stImgIn.u32X = 0;
    st_rga_attr.stImgIn.u32Y = 0;
    st_rga_attr.stImgIn.imgType = IMAGE_TYPE_NV12;
    st_rga_attr.stImgIn.u32Width = rknn_width;
    st_rga_attr.stImgIn.u32Height = rknn_height;
    st_rga_attr.stImgIn.u32HorStride = rknn_width;
    st_rga_attr.stImgIn.u32VirStride = rknn_height;
    st_rga_attr.stImgOut.u32X = 0;
    st_rga_attr.stImgOut.u32Y = 0;
    st_rga_attr.stImgOut.imgType = IMAGE_TYPE_RGB888;
    st_rga_attr.stImgOut.u32Width = rknn_width;
    st_rga_attr.stImgOut.u32Height = rknn_height;
    st_rga_attr.stImgOut.u32HorStride = rknn_width;
    st_rga_attr.stImgOut.u32VirStride = rknn_height;
    ret = RK_MPI_RGA_CreateChn(rga_ch, &st_rga_attr);
    if (ret) {
        printf("Create RGA[%d] failed! ret=%d\n", rga_ch, ret);
        return -1;
    }

    // Bind Camera VI and RGA
    MPP_CHN_S st_src_chn = { 0 };
    MPP_CHN_S st_dest_chn = { 0 };
    st_src_chn.enModId = RK_ID_VI;
    st_src_chn.s32ChnId = cam_ch;
    st_dest_chn.enModId = RK_ID_RGA;
    st_dest_chn.s32ChnId = rga_ch;
    ret = RK_MPI_SYS_Bind(&st_src_chn, &st_dest_chn);

    if (ret) {
        printf("Bind VI[%d] and RGA[%d] error! ret=%d\n", cam_ch, rga_ch, ret);
        return -1;
    }

    printf("Camera CSI bound to RKNN input via RGA successfully\n");
    return 0;
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

int camera_csi_unbind_rknn(int cam_id, int ch_id, int rga_id, int rga_ch)
{
    int ret = 0;
    MPP_CHN_S stSrcChn = {0};
    MPP_CHN_S stDestChn = {0};

    // UNBind Camera VI and RGA
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = cam_id;
    stSrcChn.s32ChnId = ch_id;
    stDestChn.enModId = RK_ID_RGA;
    stDestChn.s32DevId = rga_id;
    stDestChn.s32ChnId = rga_ch;

    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn)) {
        printf("UnBind VI[%d] and VENC[%d] error! ret=%d\n", ch_id, rga_ch, ret);
    }

    ret = RK_MPI_RGA_DestroyChn(rga_ch);
    if (ret) {
        printf("Destroy RGA[%d] error! ret=%d\n", rga_ch, ret);
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

int set_camera_csi_mirror_flip(int cam_id, bool mirror, bool flip)
{
    RK_U32 mirror_flip_value = mirror + flip * 2;
    return SAMPLE_COMM_ISP_SET_mirror(cam_id, mirror_flip_value);
}

int set_camera_csi_brightness(int cam_id, uint32_t brightness)
{
    return SAMPLE_COMM_ISP_SET_Brightness(cam_id, brightness);
}

int set_camera_csi_contrast(int cam_id, uint32_t contrast)
{
    return SAMPLE_COMM_ISP_SET_Contrast(cam_id, contrast);
}

int set_camera_csi_saturation(int cam_id, uint32_t saturation)
{
    return SAMPLE_COMM_ISP_SET_Saturation(cam_id, saturation);
}

int set_camera_csi_sharpness(int cam_id, uint32_t sharpness)
{
    return SAMPLE_COMM_ISP_SET_Sharpness(cam_id, sharpness);
}

int camera_csi_set_hdr_mode(int cam_id, bool enable)
{
    if (enable) {
        if (SAMPLE_COMM_ISP_SET_HDR(cam_id, RK_AIQ_WORKING_MODE_ISP_HDR2)) {
            printf("ISP: Set HDR mode error!\n");
            return -1;
        }
    } else {
        if (SAMPLE_COMM_ISP_SET_HDR(cam_id, RK_AIQ_WORKING_MODE_NORMAL)) {
            printf("ISP: Disable HDR mode error!\n");
            return -1;
        }
    }
    return 0;
}
