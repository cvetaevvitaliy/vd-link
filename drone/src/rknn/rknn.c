/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
//#include "rknn/yolov5/yolov5.h"
#include "rknn.h"

#include "overlay.h"
#include <stdlib.h>
#include <string.h>
#include "camera/camera_manager.h"
#include "encoder/encoder.h"
#include "yolov5/rknn_yolov5.h"

#include <easymedia/rkmedia_api.h>
#include <easymedia/rkmedia_buffer.h>

#define DEFAULT_RKNN_MODEL_PATH "/etc/default_model.rknn"

#define NMS_THRESH        0.60f
#define BOX_THRESH        0.50f
#define VIS_THRESH        0.45f

static uint8_t* tmp_buff = NULL;


static pthread_t rknn_thread;
static pthread_t rknn_read_frame_thread;
static pthread_mutex_t rknn_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool new_frame = false;
static volatile bool rknn_thread_started = false; // flag to join safely
extern common_config_t config; // from common.c

static rknn_model_info_t rknn_model_info = {0, 0, 0};
extern camera_manager_t camera_manager; // External from camera_manager.c

static void *rknn_read_frame_func(void *arg)
{
    MB_IMAGE_INFO_S stImageInfo = {0};

    while (rknn_thread_started) {

        if (tmp_buff) {
            if (camera_manager_get_current_camera(&camera_manager) == NULL) {;
                printf("[ RKNN ] No camera selected\n");
                usleep(250 * 1000); // 100 ms sleep to avoid busy loop
                continue;
            }

            MEDIA_BUFFER mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_RGA, 1, 100);
            if (!mb) {
                printf("[ RKNN ] RGA get null buffer!\n");
                usleep(10 * 1000); // 10 ms sleep to avoid busy loop
                continue;
            }
            int ret = RK_MPI_MB_GetImageInfo(mb, &stImageInfo);
            if (ret) {
                printf("[ RKNN ] Warn: Get image info failed! ret = %d\n", ret);
            }
#if 0 // For debug

            printf("[ RKNN ] Get Frame:ptr:%p, fd:%d, size:%zu, mode:%d, channel:%d, "
                   "timestamp:%lld, ImgInfo:<wxh %dx%d, fmt 0x%x>\n",
                   RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetFD(mb), RK_MPI_MB_GetSize(mb),
                   RK_MPI_MB_GetModeID(mb), RK_MPI_MB_GetChannelID(mb),
                   RK_MPI_MB_GetTimestamp(mb), stImageInfo.u32Width,
                   stImageInfo.u32Height, stImageInfo.enImgType);
#endif

            if (stImageInfo.u32Width != rknn_model_info.width ||
                stImageInfo.u32Height != rknn_model_info.height) {
                printf("[ RKNN ] ERROE: Input image size (%dx%d) does not match model size (%dx%d)\n",
                       stImageInfo.u32Width, stImageInfo.u32Height,
                       rknn_model_info.width, rknn_model_info.height);
                RK_MPI_MB_ReleaseBuffer(mb);
                usleep(10 * 1000); // 10 ms sleep to avoid busy loop
                continue;
                }

            if (!new_frame) {
                pthread_mutex_lock(&rknn_mutex);
                memcpy(tmp_buff, RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetSize(mb));
                new_frame = true;
                pthread_mutex_unlock(&rknn_mutex);
            }
            RK_MPI_MB_ReleaseBuffer(mb);
        } else {
            usleep(100 * 1000); // 100 ms sleep to avoid busy loop
        }

    }
    return NULL;
}

// RKNN worker thread
static void *rknn_thread_func(void *arg)
{
    (void)arg;
    printf("[ RKNN ] Thread started\n");
    rknn_thread_started = true;

    // rknn_app_context_t app_ctx = {0};
    // if (init_yolov5_model(DEFAULT_RKNN_MODEL_PATH, &app_ctx) < 0) {
    //     printf("[ RKNN ] Failed to initialize default model\n");
    //     rknn_thread_started = false;
    //     return NULL;
    // }
    // printf("[ RKNN ] Model width: %d, height: %d, channel: %d\n",
    //        app_ctx.model_width, app_ctx.model_height,  app_ctx.model_channel);

    // rknn_model_info.width = app_ctx.model_width;
    // rknn_model_info.height = app_ctx.model_height;
    // rknn_model_info.channel = app_ctx.model_channel;

    RknnNpuCtx *npu = rknn_npu_create();
    if (!npu) {
        rknn_thread_started = false;
        return NULL;
    }
    if (rknn_npu_init(npu, DEFAULT_RKNN_MODEL_PATH, 3) < 0) {
        printf("[ RKNN ] rknn_npu_init failed\n");
        rknn_npu_destroy(npu);
        rknn_thread_started = false;
        return NULL;
    }

    rknn_model_info.width = npu->model_width;
    rknn_model_info.height = npu->model_height;
    rknn_model_info.channel = npu->in_channel;
    printf("[ RKNN ] Model width: %d, height: %d, channel: %d\n",
           rknn_model_info.width, rknn_model_info.height, rknn_model_info.channel);

    pthread_mutex_init(&rknn_mutex, NULL);

    tmp_buff = malloc(rknn_model_info.width * rknn_model_info.height * rknn_model_info.channel);
    if (!tmp_buff) {
        printf("[ RKNN ] malloc failed\n");
        rknn_npu_destroy(npu);
        rknn_thread_started = false;
        return NULL;
    }

    if (overlay_init() < 0) {
        printf("[ RKNN ] Failed to initialize overlay\n");
        //release_yolov5_model(&app_ctx);
        rknn_thread_started = false;
        return NULL;
    }

   // object_detect_result_list od_results = {0};

    // MEDIA_BUFFER mb = NULL;
    MB_IMAGE_INFO_S stImageInfo = {0};
    // image_buffer_t image_buffer = {0};
    detect_result_group_t results = {0};

    encoder_config_t *enc_cfg = encoder_get_input_image_format(); // ensure encoder is initialized
    if (enc_cfg == NULL) {
        printf("[ OVERLAY ] ERROR: Failed to get encoder config\n");
    }
    if (enc_cfg->width <=0 || enc_cfg->height <=0) {
        printf("[ OVERLAY ] ERROR: Invalid encoder dimensions: %dx%d\n", enc_cfg->width, enc_cfg->height);
        return NULL;
    }

    int overlay_buffer_width = enc_cfg->width;
    int overlay_buffer_height = enc_cfg->height;
    while (rknn_thread_started) {

        if (tmp_buff && new_frame) {
            pthread_mutex_lock(&rknn_mutex);
            if (rknn_npu_process(npu, tmp_buff, overlay_buffer_width, overlay_buffer_height, &results, NMS_THRESH, BOX_THRESH, VIS_THRESH) < 0) {
                printf("[ RKNN ] Failed to process results!\n");
            }
            pthread_mutex_unlock(&rknn_mutex);

            uint32_t box_color = ARGB(0xFF, 0xFF, 0x00, 0x00);
            int thickness = 2;
            overlay_clear();
            if ( results.count > 0) {
                //printf("[ RKNN ] results count: %d\n", results.count);
                for (int i = 0; i < results.count; i++) {
#if 0
                    printf("  ID: %d, Class: %d, Prop: %.2f, Box: [%d, %d, %d, %d]\n",
                           i,
                           results.results[i].id,
                           results.results[i].confidence,
                           results.results[i].box.left,
                           results.results[i].box.top,
                           results.results[i].box.right,
                           results.results[i].box.bottom);
#endif
                    int x1 = results.results[i].box.left;
                    int y1 = results.results[i].box.top;
                    int x2 = results.results[i].box.right;
                    int y2 = results.results[i].box.bottom;
                    overlay_draw_rect(x1, y1, x2, y2, box_color, thickness);
                }
            }
            overlay_push_to_encoder();
            new_frame = false;
        } else {
            usleep(100 * 1000);
        }

        // image_buffer.width = (int)stImageInfo.u32Width;
        // image_buffer.height =  (int)stImageInfo.u32Height;
        // image_buffer.width_stride =  (int)stImageInfo.u32Width;
        // image_buffer.height_stride =  (int)stImageInfo.u32Height;
        // image_buffer.format = IMAGE_FORMAT_RGB888;
        // image_buffer.virt_addr = RK_MPI_MB_GetPtr(mb);
        // image_buffer.size =  (int)RK_MPI_MB_GetSize(mb);
        // image_buffer.fd = RK_MPI_MB_GetFD(mb);


#if 0 // For debug
        ret = RK_MPI_MB_GetImageInfo(mb, &stImageInfo);
        if (ret)
            printf("[ RKNN ] Warn: Get image info failed! ret = %d\n", ret);

        printf("[ RKNN ] Get Frame:ptr:%p, fd:%d, size:%zu, mode:%d, channel:%d, "
               "timestamp:%lld, ImgInfo:<wxh %dx%d, fmt 0x%x>\n",
               RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetFD(mb), RK_MPI_MB_GetSize(mb),
               RK_MPI_MB_GetModeID(mb), RK_MPI_MB_GetChannelID(mb),
               RK_MPI_MB_GetTimestamp(mb), stImageInfo.u32Width,
               stImageInfo.u32Height, stImageInfo.enImgType);
#endif

#if    0
        inference_yolov5_model(&app_ctx, &image_buffer, &od_results);

        printf("[ RKNN ] od_results count: %d\n", od_results.count);
        // ARGB: A=0xFF (opaque), R=0xFF, G=0x00, B=0x00 => red boxes
        uint32_t box_color = ARGB(0xFF, 0xFF, 0x00, 0x00);
        int thickness = 2;
        overlay_clear();
        //overlay_draw_rect(10, 10, 500, 500, box_color, thickness);
        for (int i = 0; i < od_results.count; i++) {
            printf("  ID: %d, Class: %s, Prop: %.2f, Box: [%d, %d, %d, %d]\n",
                   i,
                   coco_cls_to_name(od_results.results[i].cls_id),
                   od_results.results[i].prop,
                   od_results.results[i].box.left,
                   od_results.results[i].box.top,
                   od_results.results[i].box.right,
                   od_results.results[i].box.bottom);

            int x1 = od_results.results[i].box.left;
            int y1 = od_results.results[i].box.top;
            int x2 = od_results.results[i].box.right;
            int y2 = od_results.results[i].box.bottom;
            overlay_draw_rect(x1, y1, x2, y2, box_color, thickness);
        }
        overlay_push_to_encoder();
#endif

        //usleep(10 * 1000); // 10 ms sleep to avoid busy loop
    }

    if (tmp_buff)
        free(tmp_buff);

    pthread_mutex_destroy(&rknn_mutex);
    rknn_npu_destroy(npu);
    overlay_deinit();

    printf("[ RKNN ] Thread stopped\n");
    return NULL;
}

void rknn_thread_start(void)
{
    // Start RKNN thread
    if (pthread_create(&rknn_thread, NULL, rknn_thread_func, NULL) != 0) {
        perror("Failed to start RKNN thread");
        rknn_thread_started = false;
    }

    if (pthread_create(&rknn_read_frame_thread, NULL, rknn_read_frame_func, NULL) != 0) {
        perror("Failed to start RKNN thread");
        rknn_thread_started = false;
    }
}

void rknn_thread_stop(void)
{
    if (rknn_thread_started) {
        rknn_thread_started = false;
        rknn_model_info.width = 0;
        rknn_model_info.height = 0;
        rknn_model_info.channel = 0;
        pthread_join(rknn_thread, NULL);
        pthread_join(rknn_read_frame_thread, NULL);
    }
}

rknn_model_info_t* rknn_get_model_info(void)
{
    return &rknn_model_info;
}