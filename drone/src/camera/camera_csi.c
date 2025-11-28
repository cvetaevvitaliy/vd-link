/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "camera/camera_csi.h"
#include "camera/isp/sample_common.h"

#include <easymedia/rkmedia_api.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#define DEFAULT_IQ_FILES_PATH "/etc/iqfiles"

// Frame capture for addons (same approach as RKNN)
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t frame_capture_thread;
static uint8_t* current_frame_data = NULL;
static size_t current_frame_size = 0;
static uint32_t current_frame_width = 0;
static uint32_t current_frame_height = 0;
static uint64_t current_frame_timestamp = 0;
static volatile bool new_frame = false;
static volatile bool frame_capture_enabled = false;
static frame_callback_t frame_callback = NULL;

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

static uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static void* frame_capture_thread_func(void* arg)
{
    (void)arg;
    printf("Frame capture thread started\n");
    
    MB_IMAGE_INFO_S stImageInfo = {0};
    MEDIA_BUFFER mb = NULL;

    while (frame_capture_enabled) {
        if (current_frame_data) {
            // Use RGA channel 1 (same as RKNN approach)  
            mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_RGA, 1, 100);
            if (!mb) {
                static int null_buffer_count = 0;
                null_buffer_count++;
                if (null_buffer_count % 100 == 1) { // Print every 100 failures
                    printf("[ FRAME_CAPTURE ] RGA get null buffer! (count: %d)\n", null_buffer_count);
                }
                usleep(10 * 1000); // 10 ms sleep to avoid busy loop
                continue;
            }
            
            int ret = RK_MPI_MB_GetImageInfo(mb, &stImageInfo);
            if (ret == 0) {
                // Update frame dimensions from media buffer
                current_frame_width = stImageInfo.u32Width;
                current_frame_height = stImageInfo.u32Height;
            } else {
                printf("[ FRAME_CAPTURE ] Warn: Get image info failed! ret = %d\n", ret);
            }
            
            pthread_mutex_lock(&frame_mutex);
            
            size_t frame_size = RK_MPI_MB_GetSize(mb);
            if (frame_size > 0 && frame_size <= current_frame_size) {
                memcpy(current_frame_data, RK_MPI_MB_GetPtr(mb), frame_size);
                current_frame_timestamp = RK_MPI_MB_GetTimestamp(mb);
                new_frame = true;
                
                // Call registered callback if exists
                if (frame_callback) {
                    frame_callback(current_frame_data, frame_size, 
                                  current_frame_width, current_frame_height, 
                                  current_frame_timestamp);
                }
            }
            
            pthread_mutex_unlock(&frame_mutex);
            RK_MPI_MB_ReleaseBuffer(mb);
        } else {
            usleep(10 * 1000);
        }
    }
    
    printf("Frame capture thread stopped\n");
    return NULL;
}

int camera_csi_set_frame_callback(frame_callback_t callback)
{
    frame_callback = callback;
    return 0;
}

int camera_csi_get_latest_frame(uint8_t* frame_data, size_t* frame_size, 
                               uint32_t* width, uint32_t* height, uint64_t* timestamp_ms)
{
    if (!frame_data || !frame_size || !width || !height || !timestamp_ms) {
        return -1;
    }
    
    pthread_mutex_lock(&frame_mutex);
    
    if (!current_frame_data || !new_frame) {
        pthread_mutex_unlock(&frame_mutex);
        return -1; // No frame available
    }
    
    if (*frame_size < current_frame_size) {
        *frame_size = current_frame_size; // Return required size
        pthread_mutex_unlock(&frame_mutex);
        return -2; // Buffer too small
    }
    
    memcpy(frame_data, current_frame_data, current_frame_size);
    *frame_size = current_frame_size;
    *width = current_frame_width;
    *height = current_frame_height;
    *timestamp_ms = current_frame_timestamp;
    
    new_frame = false; // Mark frame as consumed
    
    pthread_mutex_unlock(&frame_mutex);
    
    return 0;
}

int camera_csi_enable_frame_capture(int cam_id, uint32_t width, uint32_t height)
{
    if (frame_capture_enabled) {
        return 0; // Already enabled
    }
    
    current_frame_width = width;
    current_frame_height = height;
    
    // Allocate buffer for frame data (same as RKNN approach)
    current_frame_size = width * height * 3; // NV12 is 1.5 bytes per pixel, but allocate more for safety
    current_frame_data = malloc(current_frame_size);
    if (!current_frame_data) {
        printf("ERROR: Failed to allocate frame buffer\n");
        return -1;
    }
    
    frame_capture_enabled = true;
    
    printf("Frame capture enabled %dx%d (VI/RGA channels set up separately)\n", width, height);
    
    int ret = pthread_create(&frame_capture_thread, NULL, frame_capture_thread_func, NULL);
    if (ret != 0) {
        printf("ERROR: Failed to create frame capture thread: %d\n", ret);
        frame_capture_enabled = false;
        free(current_frame_data);
        current_frame_data = NULL;
        return -1;
    }
    
    return 0;
}

void camera_csi_disable_frame_capture(void)
{
    if (!frame_capture_enabled) {
        return;
    }
    
    frame_capture_enabled = false;
    pthread_join(frame_capture_thread, NULL);
    
    if (current_frame_data) {
        free(current_frame_data);
        current_frame_data = NULL;
    }
    
    current_frame_size = 0;
    current_frame_width = 0;
    current_frame_height = 0;
    current_frame_timestamp = 0;
    
    printf("Frame capture disabled\n");
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
