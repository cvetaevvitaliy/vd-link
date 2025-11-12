#include "detection_stream.h"
#include "detection/detection.h"
#include <easymedia/rkmedia_api.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include "bytetrack_api.h"
#include <cairo/cairo.h>



#define DETECTION_UPDATE_INTERVAL_US  1000000
#define DETECTION_REGION_ID         0
#define DETECTION_REGION_WIDTH      1280
#define DETECTION_REGION_HEIGHT     720
#define DETECTION_REGION_POS_X      0
#define DETECTION_REGION_POS_Y      0


static uint32_t model_width = 0;
static uint32_t model_height = 0;
static uint32_t stream_width = 0;
static uint32_t stream_height = 0;

void* detection_processing_thread_func(void* arg);
pthread_t processing_thread;
volatile bool detection_run = true;

int start_detection_stream(uint32_t stream_width_param, uint32_t stream_height_param, uint32_t fps)
{
    int ret = 0;

    detection_init();
    bytetrack_init(fps, 30);

    model_width = detection_get_nn_model_width();
    model_height = detection_get_nn_model_height();

    stream_height = stream_height_param;
    stream_width = stream_width_param;

    // Note: RGA pipeline is created by camera initialization. Start pulling buffers.
    ret = RK_MPI_SYS_StartGetMediaBuffer(RK_ID_RGA, 0);
    if (ret) {
        printf("WARNING: RK_MPI_SYS_StartGetMediaBuffer(RGA,0) failed ret=%d\n", ret);
        // continue — thread will attempt recovery on GetMediaBuffer failures
    } else {
        printf("RK_MPI_SYS_StartGetMediaBuffer(RGA,0) started\n");
    }

    pthread_create(&processing_thread, NULL, detection_processing_thread_func, NULL);

    return 0;
}

void stop_detection_stream()
{
    if (!detection_run) {
        printf("Detection stream is already stopped\n");
        return;
    }
    printf("Stopping detection stream...\n");
    detection_run = false;
    // Stop media buffer flow first
    RK_MPI_SYS_StopGetMediaBuffer(RK_ID_RGA, 0);
    // Give thread a moment to finish current processing
    usleep(100000); // 100ms
    // Wait for thread to finish
    pthread_join(processing_thread, NULL);
    printf("Detection thread stopped\n");
    
    detection_deinit();

    // Note: RGA pipeline cleanup is now handled by camera deinitialization
    // No need to unbind or destroy RGA channel here

    printf("Detection stream stopped successfully\n");
}


static void detection_draw_boxes(detection_result_group_t* results, int stream_width, int stream_height)
{
    //printf("Drawing %d detection boxes on %dx%d stream\n", results->count, stream_width, stream_height);
    
    // Use stream resolution for drawing, not model resolution
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, DETECTION_REGION_WIDTH, DETECTION_REGION_HEIGHT);
    cairo_t *cr = cairo_create(surface);

    // Transparent background
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 1, 0, 0, 1); // Red color for boxes
    
    for (int i = 0; i < results->count; i++) {
        detection_result_t *result = &results->results[i];
        
        // Coordinates are already in pixel format, use them directly
        int x = result->norm_box.x * stream_width;
        int y = result->norm_box.y * stream_height;
        int width = result->norm_box.width * stream_width;
        int height = result->norm_box.height * stream_height;

#if 0
        printf("Box %d: pixel [%0.2f,%0.2f,%0.2f,%0.2f] -> drawing [%d,%d,%d,%d]\n", 
               i, result->norm_box.x, result->norm_box.y, result->norm_box.width, result->norm_box.height,
               x, y, width, height);
#endif
        // Draw rectangle
        cairo_rectangle(cr, x, y, width, height);
        cairo_stroke(cr);

        // Draw label
        char label[64];
        snprintf(label, sizeof(label), "%s: %.2f", detection_get_class_name(result->obj_class), result->confidence);
        cairo_move_to(cr, x + 5, y + 15);
        cairo_set_font_size(cr, 12);
        cairo_show_text(cr, label);
    }

    // Prepare bitmap
    BITMAP_S bmp = {0};
    bmp.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    bmp.u32Width = DETECTION_REGION_WIDTH;
    bmp.u32Height = DETECTION_REGION_HEIGHT;
    bmp.pData = (void *) cairo_image_surface_get_data(surface);

    // Region info
    OSD_REGION_INFO_S RgnInfo = {0};
    RgnInfo.enRegionId = DETECTION_REGION_ID;
    RgnInfo.u32Width = DETECTION_REGION_WIDTH;
    RgnInfo.u32Height = DETECTION_REGION_HEIGHT;
    RgnInfo.u32PosX = DETECTION_REGION_POS_X;
    RgnInfo.u32PosY = DETECTION_REGION_POS_Y;
    RgnInfo.u8Enable = 1;
    
    // int ret = RK_MPI_VENC_RGN_SetCover(0, &RgnInfo, &cover_info);
    int ret = RK_MPI_VENC_RGN_SetBitMap(0, &RgnInfo, &bmp);
    if (ret) {
        printf("Failed to set bitmap for detection OSD region %d: %d\n", DETECTION_REGION_ID, ret);
    }
    // Clean up Cairo resources
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}


static void detection_video_packet_cb(MEDIA_BUFFER mb, RK_VOID *pHandle)
{
    (void)(pHandle);
    static int callback_count = 0;
    
    callback_count++;

    if (!detection_run || !mb) {
        printf("Early return: detection_run=%d, mb=%p\n", detection_run, mb);
        return;
    }

    uint8_t *data = (uint8_t *)RK_MPI_MB_GetPtr(mb);
    size_t size = RK_MPI_MB_GetSize(mb);

    if (size == 0 || !data) {
        printf("Invalid media buffer: size=%zu, data=%p\n", size, data);
        return;
    }

    detection_result_group_t results = {0};
    detection_process_frame(data, model_width, model_height, &results);

    // debug output
    if (results.count > 0) {
        bytetrack_update(&results);
        normalize_detection_results(&results);
        //link_send_detection((const link_detection_box_t*)results.results, results.count);
#if 0
        for (int i = 0; i < results.count; i++) {
            detection_result_t *result = &results.results[i];
            printf("Object %d: Class %d, Score %.2f, Box [%d, %d, %d, %d]\n",
                   i, result->obj_class, result->confidence,
                   result->box.bottom, result->box.left,
                   result->box.right, result->box.top);
        }
#else
        detection_draw_boxes(&results, stream_width, stream_height);
#endif
    }
}

void* detection_processing_thread_func(void* arg)
{
    MEDIA_BUFFER mb = NULL;
    static int failed_count = 0;
    static int total_frames = 0;
    static time_t last_success_time = 0;
    static time_t start_time = 0;

    printf("Detection processing thread started\n");
    start_time = time(NULL);
    last_success_time = start_time;

    const int retry_restart_threshold = 4; // after this many timeouts try restarting buffer flow
    const int stop_threshold = 50; // if still failing after many retries, stop
    while (detection_run) {
        if (failed_count > stop_threshold) {
            printf("Too many failed attempts to get media buffer, stopping detection thread\n");
            break;
        }
        
        time_t current_time = time(NULL);
        mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_RGA, 0, 1000);
        if (!mb) {
            failed_count++;
            printf("Failed to get media buffer (attempt %d) - %ld seconds since last success\n", 
                   failed_count, current_time - last_success_time);

            // attempt to restart buffer flow after several consecutive failures
            if (failed_count == retry_restart_threshold) {
                printf("Attempting to restart RGA media buffer flow (Stop/Start)\n");
                RK_MPI_SYS_StopGetMediaBuffer(RK_ID_RGA, 0);
                usleep(200 * 1000); // longer backoff
                int r = RK_MPI_SYS_StartGetMediaBuffer(RK_ID_RGA, 0);
                if (r) {
                    printf("Restart RK_MPI_SYS_StartGetMediaBuffer failed ret=%d\n", r);
                } else {
                    printf("Restarted RK_MPI_SYS_StartGetMediaBuffer successfully\n");
                }
            }
            
            // More aggressive recovery - try unbind/rebind after many failures
            if (failed_count == (retry_restart_threshold * 3)) {
                printf("Critical recovery: VI->RGA pipeline reset after %d failures (runtime: %ld sec, last success: %ld sec ago)\n", 
                       failed_count, current_time - start_time, current_time - last_success_time);
                       
                printf("Stopping RGA buffer flow...\n");
                RK_MPI_SYS_StopGetMediaBuffer(RK_ID_RGA, 0);
                
                // Unbind VI->RGA  
                MPP_CHN_S stSrcChn = {0};
                MPP_CHN_S stDestChn = {0};
                stSrcChn.enModId = RK_ID_VI;
                stSrcChn.s32DevId = 0; // Camera device 0
                stSrcChn.s32ChnId = 0; // VI channel 0 (not 1!)
                stDestChn.enModId = RK_ID_RGA;
                stDestChn.s32DevId = 0;
                stDestChn.s32ChnId = 0; // RGA channel 0
                
                int ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
                printf("UnBind VI(chn0)->RGA(chn0) result: %d\n", ret);
                
                usleep(500 * 1000); // 500ms pause
                
                // Rebind VI->RGA
                ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
                printf("Rebind VI(chn0)->RGA(chn0) result: %d\n", ret);
                
                usleep(200 * 1000); // 200ms pause before restart
                
                // Restart buffer flow
                ret = RK_MPI_SYS_StartGetMediaBuffer(RK_ID_RGA, 0);
                printf("Restart GetMediaBuffer after rebind result: %d\n", ret);
                
                if (ret == RK_SUCCESS) {
                    failed_count = 0; // Reset only if restart succeeded
                    last_success_time = current_time;
                    printf("Pipeline recovery successful, resetting counters\n");
                } else {
                    printf("ERROR: Pipeline recovery failed\n");
                }
            }

            // small sleep to avoid hot loop if API returns quickly
            usleep(50 * 1000);
            continue;
        }
        // Reset failed count on successful buffer get
        failed_count = 0;
        total_frames++;
        last_success_time = current_time;
        
        // Print stats every 100 frames
        if (total_frames % 100 == 0) {
            printf("Detection stats: %d frames processed, runtime: %ld seconds\n", 
                   total_frames, current_time - start_time);
        }
        
        // Check again if we should continue before processing
        if (!detection_run) {
            printf("Detection stopped, releasing buffer %p\n", mb);
            RK_MPI_MB_ReleaseBuffer(mb);
            break;
        }
        // Process the frame
        detection_video_packet_cb(mb, NULL);
        // Release buffer safely
        if (mb) {
            RK_MPI_MB_ReleaseBuffer(mb);
            mb = NULL;
        }
    }

    return NULL;
}
