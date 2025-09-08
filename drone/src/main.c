#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/un.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>
#include "pidfile.h"
#include "rtp.h"
#include "rtp-profile.h"
#include "rtp-payload.h"
#include "sample_common.h"
#include <easymedia/rkmedia_api.h>
#include <easymedia/rkmedia_vi.h>
#include <easymedia/rkmedia_venc.h>
#include <cairo/cairo.h>
#include "detection.h"
#include "bytetrack_api.h"
#include "link.h"

// TODO: need all refactoring

#define APP_NAME                    "rtp-streamer"
#define PID_FILE                    "/var/run/" APP_NAME ".pid"
#define DEFAULT_IQ_FILES_PATH       "/etc/iqfiles"
#define DEFAULT_STREAM_WIDTH        (1280)
#define DEFAULT_STREAM_HEIGHT       (720)

#define RTP_PAYLOAD_TYPE_DYNAMIC    (96)
#define RTP_VERSION                 (2)
#define RTP_CLOCK_RATE              90000

#define OSD_OVERLAY_ENABLE          1

static int udp_socket = -1;
static struct sockaddr_in dst_addr = {0};
static volatile bool run = true;
static volatile bool detection_run = false;
pthread_t processing_thread;

#if OSD_OVERLAY_ENABLE
#include <cairo/cairo.h>
/******************* OSD **********************/
// Internal static for CPU usage calculation
typedef struct {
    float usage_percent;
    float temperature_celsius;
} cpu_info_t;

static uint64_t last_total = 0;
static uint64_t last_idle = 0;

#define OSD_UPDATE_INTERVAL_US      1000000
#define OSD_REGION_ID               1
#define OSD_REGION_WIDTH            256
#define OSD_REGION_HEIGHT           128
#define OSD_REGION_POS_X            32   // 16-aligned
#define OSD_REGION_POS_Y            96   // 16-aligned

#define DETECTION_UPDATE_INTERVAL_US  1000000
#define DETECTION_REGION_ID         0
#define DETECTION_REGION_WIDTH      1280
#define DETECTION_REGION_HEIGHT     720
#define DETECTION_REGION_POS_X      0
#define DETECTION_REGION_POS_Y      0

#endif

static struct rtp_payload_encode_t* encoder = NULL;

typedef struct {
    int width;
    int height;
    int default_bitrate;
    const char *name;
} resolution_preset_t;

static const resolution_preset_t resolution_presets[] = {
        {1920, 1080, 8000000, "FullHD"},
        {1280, 720, 4000000, "HD"},
        {960,  540, 2000000, "qHD"},
        {640,  360, 1000000, "SD"},
        {1024,  768,  2500000, "4:3 XGA"}, 
        {640,  480,  1500000, "4:3 VGA"}
};

#define NUM_PRESETS (sizeof(resolution_presets) / sizeof(resolution_presets[0]))

typedef enum {
    WB_1700K = 1700,
    WB_1800K = 1800,
    WB_2400K = 2400,
    WB_2550K = 2550,
    WB_2700K = 2700,
    WB_3000K = 3000,
    WB_3200K = 3200,
    WB_3350K = 3350,
    WB_5500K = 5500,
    WB_6200K = 6200,
    WB_6500K = 6500
} manual_white_balance_e;

typedef struct {
    int cam_id;
    int fps;
    char* iq_files_path;
    int flip;
    int mirror;
    int brightness;
    int contrast;
    int saturation;
    int sharpness;
    bool auto_white_balance;
    int correction;
    int stream_width;
    int stream_height;
    IMAGE_TYPE_E img_type;
    rk_aiq_working_mode_t hdr_mode;
    manual_white_balance_e manual_white_balance;
} isp_config_t;

isp_config_t isp_config = {
    .cam_id = 0,
    .fps = 60,
    .iq_files_path = DEFAULT_IQ_FILES_PATH,
    .flip = 0,
    .mirror = 0,
    .brightness = 128,
    .contrast = 128,
    .saturation = 128,
    .sharpness = 128,
    .auto_white_balance = true,
    .manual_white_balance = WB_5500K,
    .hdr_mode = RK_AIQ_WORKING_MODE_NORMAL,
    .correction = 128,
    .stream_width = DEFAULT_STREAM_WIDTH,
    .stream_height = DEFAULT_STREAM_HEIGHT,
    .img_type = IMAGE_TYPE_NV12
};

// Encoder configuration
typedef struct {
    CODEC_TYPE_E codec_type;
    int fps;
    int gop;
    int payload_size;
    bool use_cbr;
    bool low_latency;
    bool enable_focus;
    int focus_quality;
} encoder_config_t;

// Default encoder settings TODO: add as param to encoder
encoder_config_t encoder_config = {
        .codec_type = RK_CODEC_TYPE_H265,
        .fps = 60,
        .gop = 15,
        .payload_size = 1400,
        .use_cbr = true,
        .low_latency = false,
        .enable_focus = false,
        .focus_quality = -51
};

static struct option long_options[] = {
        {"fps", required_argument, 0, 'f'},            // already exists
        {"gop", required_argument, 0, 1},              // custom gop
        {"payload-size", required_argument, 0, 2},     // custom payload size
        {"vbr", no_argument, 0, 3},                    // force VBR
        {"focus-mode", required_argument, 0, 4},       // Focus mode
        {"codec", required_argument, 0, 5},            // Codec type
        {"mirror", no_argument, 0, 6},                 // ISP mirror
        {"flip", no_argument, 0, 7},                   // ISP flip    
        {"help", no_argument, 0, 'h'},
        {"detection", no_argument, 0, 'N'},             // Enable detection
        {0, 0, 0, 0}
};

static void handle_sigint(int sig)
{
    printf("Caught signal %d\n", sig);
    remove_pid(PID_FILE);
    run = false;
}

static void print_help(char* prog_name)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -h, --help             Show this help message and exit\n"
            "  -a <ip>                Set destination IP address (default: 127.0.0.1)\n"
            "  -p <port>              Set destination UDP port (default: 5004)\n"
            "  -b <bitrate>           Set encoder bitrate (e.g., 4000k for 4 Mbps, 8M for 8 Mbps)\n"
            "  -f <fps>, --fps <fps>  Set frames per second (FPS), 1–60 (default: 30)\n"
            "  -P <profile>           Select preset resolution profile:\n"
            "  -U <path>              Send RTP packets via UNIX domain socket to <path> instead of UDP\n"
            "  -N, --detection        Enable object detection and tracking overlay\n",
            prog_name);

    for (size_t i = 0; i < NUM_PRESETS; i++) {
        fprintf(stderr, "                         %zu: %s (%dx%d @ default %d bps)\n",
                i,
                resolution_presets[i].name,
                resolution_presets[i].width,
                resolution_presets[i].height,
                resolution_presets[i].default_bitrate);
    }

    fprintf(stderr,
            "  --codec <type>         Set codec type: h264 or h265 (default: h264)\n"
            "                         H.265 provides better compression, but may increase latency.\n"
            "                         H.264 is more widely supported.\n"
            "  --gop <frames>         Set GOP size (I-frame interval), e.g., 10 for low-latency, 30 for normal, 60 for better quality\n"
            "  --payload-size <size>  Set RTP payload size in bytes (500–1400, default: 1400)\n"
            "  --vbr                  Force encoder to use VBR (Variable Bitrate) without param\n"
            "  --focus-mode <qp>      Enable focus mode with QP delta (-51 ... 51)\n"
            "\n"
            "Description of parameters:\n"
            "                Recommended for stable network conditions.\n"
            "  --vbr         Variable Bitrate mode: encoder adapts bitrate depending on complexity.\n"
            "                Can improve quality, but leads to bitrate fluctuations.\n"
            "  --flip         Vertically flip the image (upside-down).\n"
            "                Useful if the camera is mounted inverted.\n"
            "  --mirror       Horizontally mirror the image (left-right).\n"
            "                Useful for correcting mirrored views or front-facing cameras.\n"
            "\n");

    exit(0);
}

static int rtp_socket_init(const char* ip, int port)
{
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("socket");
        return -1;
    }

    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &dst_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }

    return 0;
}

static void* rtp_alloc(void* param, int bytes)
{
    (void)(param);
    return malloc(bytes);
}

static void rtp_free(void* param, void* packet)
{
    (void)(param);
    free(packet);
}

static int rtp_encode_packet(void* param, const void* packet, int bytes, uint32_t timestamp, int flags)
{
    (void)(timestamp);
    (void)(flags);

    int sock = *(int*)param;
    sendto(sock, packet, bytes, 0, (struct sockaddr*)&dst_addr, sizeof(dst_addr));

    return 0;
}

static void init_rtp(int frame_size)
{
    rtp_packet_setsize(frame_size);

    struct rtp_payload_t handler = {0};
    handler.alloc  = rtp_alloc;
    handler.free   = rtp_free;
    handler.packet = rtp_encode_packet;

    uint16_t seq = (uint16_t)(rand() & 0xFFFF);  // random start sequence
    uint32_t ssrc = (uint32_t)rand();            // random SSRC

    // static char buffer[20 * 1024 * 1024];        // frame buffer

    encoder = rtp_payload_encode_create(RTP_PAYLOAD_TYPE_DYNAMIC, encoder_config.codec_type == RK_CODEC_TYPE_H264 ? "H264" : "H265", seq, ssrc, &handler, &udp_socket);
}

#if 0
static uint8_t sps_buffer[256];
static int sps_size = 0;
static uint8_t pps_buffer[256];
static int pps_size = 0;

void extract_sps_pps_from_idr(const uint8_t* data, size_t size) {
    size_t i = 0;
    int found_sps = 0, found_pps = 0;

    while (i + 4 < size) {
        // Check start 00 00 00 01
        if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
            uint8_t nal_type = data[i+4] & 0x1F;
            size_t nal_start = i + 4;

            // Look for the end of this NAL
            size_t j = nal_start + 1;
            while (j + 4 < size) {
                if (data[j] == 0x00 && data[j+1] == 0x00 && data[j+2] == 0x00 && data[j+3] == 0x01) {
                    break; // found the beginning of the next NAL
                }
                j++;
            }
            size_t nal_size = j - nal_start;

            if (nal_type == 7 && !found_sps) {
                // SPS
                if (nal_size < sizeof(sps_buffer)) {
                    memcpy(sps_buffer, &data[nal_start], nal_size);
                    sps_size = nal_size;
                    found_sps = 1;
                    printf("Found SPS of size %d\n", sps_size);
                }
            } else if (nal_type == 8 && !found_pps) {
                // PPS
                if (nal_size < sizeof(pps_buffer)) {
                    memcpy(pps_buffer, &data[nal_start], nal_size);
                    pps_size = nal_size;
                    found_pps = 1;
                    printf("Found PPS of size %d\n", pps_size);
                }
            }

            if (found_sps && found_pps) {
                break; // Found both — can stop
            }
            i = j; // Let's move on to the next start code
        } else {
            i++;
        }
    }
}
#endif

void video_packet_cb(MEDIA_BUFFER mb)
{
    if (!run)
        return;

    uint8_t *data = (uint8_t *)RK_MPI_MB_GetPtr(mb);
    size_t size = RK_MPI_MB_GetSize(mb);

    // Static state for frame counter and initial RTP timestamp base
#if 1
    static uint32_t frame_index = 0;
    static uint32_t rtp_base = 0;
    static bool initialized = false;

    // One-time RTP timestamp base initialization
    if (!initialized) {
        rtp_base = (uint32_t)rand();  // Random base timestamp for RTP session
        initialized = true;
    }

    // Calculate RTP timestamp using fixed FPS and 90kHz clock rate
    uint32_t rtp_timestamp = rtp_base + frame_index * (RTP_CLOCK_RATE / isp_config.fps);
    frame_index++;
#else
    static uint32_t rtp_base = 0;
    uint64_t mb_timestamp_us = RK_MPI_MB_GetTimestamp(mb);
    uint32_t rtp_timestamp = rtp_base + (uint32_t)((mb_timestamp_us * RTP_CLOCK_RATE) / 1000000);
#endif

#if 0
    // Get NALU type (e.g., IDR or P-slice)
    RK_S32 nalu_type = RK_MPI_MB_GetFlag(mb);

    switch (nalu_type) {
        case VENC_NALU_IDRSLICE:
            // Optional: extract SPS/PPS for debug
            if (sps_size == 0 || pps_size == 0) {
                extract_sps_pps_from_idr(data, size);
            }
            break;
        case VENC_NALU_PSLICE:
            break;
        default:
            break;
    }
#endif
    rtp_payload_encode_input(encoder, data, (int)size, rtp_timestamp);

    RK_MPI_MB_ReleaseBuffer(mb);

    // Move to next frame for timestamp calculation
}

static int start_camera_stream(isp_config_t* config)
{
    int ret = 0;

    LOG_LEVEL_CONF_S pst_conf;
    RK_MPI_LOG_GetLevelConf(&pst_conf);
    pst_conf.s32Level = 2;
    RK_MPI_LOG_SetLevelConf(&pst_conf);

    RK_BOOL b_multictx = RK_FALSE;

    if (SAMPLE_COMM_ISP_Init(config->cam_id, config->hdr_mode, b_multictx, config->iq_files_path)) {
        printf("ISP: Init\n");
        return -1;
    }

    if (SAMPLE_COMM_ISP_Run(config->cam_id)) {
        printf("ISP: Run\n");
        return -1;
    }

    RK_U32 mirror_flip_value = config->mirror + config->flip * 2;
    SAMPLE_COMM_ISP_SET_mirror(config->cam_id, mirror_flip_value);

    SAMPLE_COMM_ISP_SET_Brightness(config->cam_id, config->brightness);
    SAMPLE_COMM_ISP_SET_Contrast(config->cam_id, config->contrast);
    SAMPLE_COMM_ISP_SET_Saturation(config->cam_id, config->saturation);
    SAMPLE_COMM_ISP_SET_Sharpness(config->cam_id, config->sharpness);

    SAMPLE_COMM_ISP_SET_Correction(config->cam_id, RK_TRUE, config->correction); // LDC (Lens Distortion Correction)
    SAMPLE_COMM_ISP_SetFecEn(config->cam_id, RK_FALSE); // FEC (Fish-Eye Correction)

    SAMPLE_COMMON_ISP_SET_DNRStrength(config->cam_id, 3 /* 0:off, 1:2D, 3:2D+3D */, 16 /* 2D */, 8 /* 3D */);

    SAMPLE_COMMON_ISP_SET_AutoWhiteBalance(config->cam_id, config->auto_white_balance);

    RK_MPI_SYS_Init();

    VI_CHN_ATTR_S vi_chn_attr = {0};
    vi_chn_attr.pcVideoNode = "rkispp_scale0";
    vi_chn_attr.u32BufCnt = 2;
    vi_chn_attr.u32Width = config->stream_width;
    vi_chn_attr.u32Height = config->stream_height;
    vi_chn_attr.enPixFmt = IMAGE_TYPE_NV12;
    vi_chn_attr.enBufType = VI_CHN_BUF_TYPE_DMA;
    vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;

    ret = RK_MPI_VI_SetChnAttr(0, 0, &vi_chn_attr);
    if (ret) {
        printf("Create vi[0] failed! ret=%d\n", ret);
        return -1;
    }

    ret = RK_MPI_VI_EnableChn(0, 0);
    if (ret) {
        printf("Enable vi[0] failed! ret=%d\n", ret);
        return -1;
    }

    return 0;
}

uint32_t model_width = 0;
uint32_t model_height = 0;

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
        link_send_detection((const link_detection_box_t*)results.results, results.count);
#if 0
        for (int i = 0; i < results.count; i++) {
            detection_result_t *result = &results.results[i];
            printf("Object %d: Class %d, Score %.2f, Box [%d, %d, %d, %d]\n",
                   i, result->obj_class, result->confidence,
                   result->box.bottom, result->box.left,
                   result->box.right, result->box.top);
        }
#else
        detection_draw_boxes(&results, isp_config.stream_width, isp_config.stream_height);
#endif
    }
}

 void* detection_processing_thread_func(void* arg) {
    MEDIA_BUFFER mb = NULL;
    static int failed_count = 0;

    printf("Detection processing thread started\n");

    while (detection_run) {
        if (failed_count > 20) {
            printf("Too many failed attempts to get media buffer, stopping detection thread\n");
            break;
        }
        mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_RGA, 0, 1000);
        if (!mb) {
            printf("Failed to get media buffer (attempt %d)\n", failed_count + 1);
            failed_count++;
            continue;
        }
        // Reset failed count on successful buffer get
        failed_count = 0;
        
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

static int start_detection_stream(isp_config_t* config)
{
    int ret = 0;

    detection_init();
    bytetrack_init(config->fps, 30);

    model_width = detection_get_nn_model_width();
    model_height = detection_get_nn_model_height();

    RGA_ATTR_S rga_attr = {0};
    rga_attr.stImgIn.imgType = IMAGE_TYPE_NV12;
    rga_attr.stImgIn.u32Width = config->stream_width;
    rga_attr.stImgIn.u32Height = config->stream_height;
    rga_attr.stImgIn.u32HorStride = config->stream_width;
    rga_attr.stImgIn.u32VirStride = config->stream_height;
    rga_attr.stImgIn.u32X = 0;
    rga_attr.stImgIn.u32Y = 0;
    rga_attr.stImgOut.imgType = IMAGE_TYPE_RGB888;
    rga_attr.stImgOut.u32Width = model_width;
    rga_attr.stImgOut.u32Height = model_height;
    rga_attr.stImgOut.u32HorStride = model_width;
    rga_attr.stImgOut.u32VirStride = model_height;
    rga_attr.stImgOut.u32X = 0;
    rga_attr.stImgOut.u32Y = 0;
    
    ret = RK_MPI_RGA_CreateChn(0, &rga_attr);
    if (ret) {
        printf("Enable vi[0] failed! ret=%d\n", ret);
        return -1;
    }

    MPP_CHN_S source_chn = {0};
    source_chn.enModId = RK_ID_VI;
    source_chn.s32DevId = 0;
    source_chn.s32ChnId = 0;
    MPP_CHN_S dest_chn = {0};
    dest_chn.enModId = RK_ID_RGA;
    dest_chn.s32DevId = 0;
    dest_chn.s32ChnId = 0; // RGA channel
    ret = RK_MPI_SYS_Bind(&source_chn, &dest_chn);
    if (ret) {
        printf("Bind VI to RGA failed! ret=%d\n", ret);
        return -1;
    }

    RK_MPI_SYS_StartGetMediaBuffer(RK_ID_RGA, 0);
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

    MPP_CHN_S source_chn = {0};
    source_chn.enModId = RK_ID_VI;
    source_chn.s32DevId = 0;
    source_chn.s32ChnId = 0;
    MPP_CHN_S dest_chn = {0};
    dest_chn.enModId = RK_ID_RGA;
    dest_chn.s32DevId = 0;
    dest_chn.s32ChnId = 0; // RGA channel
    RK_MPI_SYS_UnBind(&source_chn, &dest_chn);

    RK_MPI_VI_DisableChn(0, 0);

    RK_MPI_RGA_DestroyChn(0);

    printf("Detection stream stopped successfully\n");
}

static int enable_focus_mode(RK_U32 width, RK_U32 height)
{
    VENC_ROI_ATTR_S roi_attr[1];
    memset(roi_attr, 0, sizeof(roi_attr));

    roi_attr[0].u32Index = 0;          // ROI region index
    roi_attr[0].bEnable = RK_TRUE;     // Enable this ROI region
    roi_attr[0].bAbsQp = RK_FALSE;     // Relative QP mode
    roi_attr[0].s32Qp = encoder_config.focus_quality; // Low quality Range:[-51, 51]; QP value,only relative mode can QP value
    roi_attr[0].bIntra = RK_FALSE;     // No forced intra-refresh for ROI

    // Center 65% of frame
    RK_U32 focus_width = (width * 65) / 100;
    RK_U32 focus_height = (height * 65) / 100;

    roi_attr[0].stRect.s32X = (width - focus_width) / 2;
    roi_attr[0].stRect.s32Y = (height - focus_height) / 2;
    roi_attr[0].stRect.u32Width = focus_width;
    roi_attr[0].stRect.u32Height = focus_height;

    int ret = RK_MPI_VENC_SetRoiAttr(0, roi_attr, 1);
    if (ret != 0) {
        printf("Failed to set ROI: %d\n", ret);
        return -1;
    }

    printf("Focus Mode ROI set successfully: Center region %ux%u\n",
           roi_attr[0].stRect.u32Width, roi_attr[0].stRect.u32Height);

    return 0;
}

#if OSD_OVERLAY_ENABLE
int init_overlay_region(int venc_chn, int region_id, int width, int height, int pos_x, int pos_y)
{
    RK_MPI_VENC_RGN_Init(0, NULL);
    OSD_REGION_INFO_S RgnInfo = {0};
    RgnInfo.enRegionId = region_id;
    RgnInfo.u32Width = width;
    RgnInfo.u32Height = height;
    RgnInfo.u32PosX = pos_x;
    RgnInfo.u32PosY = pos_y;
    RgnInfo.u8Enable = 1;
    RgnInfo.u8Inverse = 0;

    // Initialize VENC region system if not already
    RK_MPI_VENC_RGN_Init(venc_chn, NULL);

    // Start with an empty BitMap
    BITMAP_S dummy = {0};
    dummy.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    dummy.u32Width = width;
    dummy.u32Height = height;
    dummy.pData = calloc(width * height, 4);  // ARGB8888 = 4 bytes per pixel

    int ret = RK_MPI_VENC_RGN_SetBitMap(venc_chn, &RgnInfo, &dummy);
    free(dummy.pData);
    return ret;
}

int draw_text_to_region(int venc_chn, int region_id, int width, int height, const char *text)
{
    // Create ARGB surface for drawing
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surface);

    // Transparent background
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    // Font setup
    cairo_select_font_face(cr, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    double font_size = 36;
    cairo_set_font_size(cr, font_size);

    // Line splitting and rendering
    double line_spacing = 1.2;
    double x = 10;
    double y = 40;
    int line_num = 0;

    char *text_copy = strdup(text);
    char *line = strtok(text_copy, "\n");

    // Draw black outline
    while (line) {
        cairo_set_source_rgb(cr, 0, 0, 0);
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (dx || dy) {
                    cairo_move_to(cr, x + dx, y + line_num * font_size * line_spacing + dy);
                    cairo_show_text(cr, line);
                }
            }
        }

        // Draw white main text
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, x, y + line_num * font_size * line_spacing);
        cairo_show_text(cr, line);

        line = strtok(NULL, "\n");
        line_num++;
    }
    free(text_copy);
    cairo_destroy(cr);

    // Prepare bitmap
    BITMAP_S bmp = {0};
    bmp.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    bmp.u32Width = width;
    bmp.u32Height = height;
    bmp.pData = (void *) cairo_image_surface_get_data(surface);

    // Region info
    OSD_REGION_INFO_S RgnInfo = {0};
    RgnInfo.enRegionId = region_id;
    RgnInfo.u32Width = width;
    RgnInfo.u32Height = height;
    // Use the correct position for CPU info overlay
    if (region_id == OSD_REGION_ID) {
        RgnInfo.u32PosX = OSD_REGION_POS_X;
        RgnInfo.u32PosY = OSD_REGION_POS_Y;
    } else {
        RgnInfo.u32PosX = 0;
        RgnInfo.u32PosY = 0;
    }
    RgnInfo.u8Enable = 1;

    int ret = RK_MPI_VENC_RGN_SetBitMap(venc_chn, &RgnInfo, &bmp);
    cairo_surface_destroy(surface);

    return ret;
}

static cpu_info_t get_cpu_info(void)
{
    static cpu_info_t cached = {-1.0f, -1.0f};
    static uint64_t last_total = 0, last_idle = 0;
    static struct timespec last_time = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long delta_ms = (now.tv_sec - last_time.tv_sec) * 1000 +
                    (now.tv_nsec - last_time.tv_nsec) / 1000000;
    if (delta_ms < 500) {
        return cached;
    }

    // CPU Usage
    FILE *fp_stat = fopen("/proc/stat", "r");
    if (fp_stat) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp_stat)) {
            uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
            if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
                uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
                uint64_t total_diff = total - last_total;
                uint64_t idle_diff = idle - last_idle;

                last_total = total;
                last_idle = idle;

                if (total_diff > 0) {
                    cached.usage_percent = 100.0f * (1.0f - (float)idle_diff / total_diff);
                }
            }
        }
        fclose(fp_stat);
    }

    // CPU Temperature
    FILE *fp_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r"); // zone 0 is usually CPU temp, 1 is NPU
    if (fp_temp) {
        int temp_millic;
        if (fscanf(fp_temp, "%d", &temp_millic) == 1) {
            cached.temperature_celsius = temp_millic / 1000.0f;
        }
        fclose(fp_temp);
    }

    last_time = now;
    return cached;
}

int update_cpu_info_overlay(int venc_chn)
{
    cpu_info_t info = get_cpu_info();

    if (info.usage_percent < 0 || info.temperature_celsius < 0) {
        printf("Failed to get CPU info: usage=%.1f, temp=%.1f\n", info.usage_percent, info.temperature_celsius);
        return -1;
    }

    char text[256];
    snprintf(text, sizeof(text),
             "Temp: %.1f\n"
             "CPU: %.1f%%",
             info.temperature_celsius,
             info.usage_percent);

    return draw_text_to_region(venc_chn, OSD_REGION_ID, OSD_REGION_WIDTH, OSD_REGION_HEIGHT, text);
}
#endif

static int start_video_encoder(CODEC_TYPE_E enCodecType, int width, int height, int bitrate, int fps)
{
    printf("Starting video encoder with resolution %dx%d, bitrate %d bps\n", width, height, bitrate);
    int ret = 0;
    VENC_CHN_ATTR_S venc_chn_attr = {0};

    venc_chn_attr.stVencAttr.enType = enCodecType;
    venc_chn_attr.stVencAttr.imageType = IMAGE_TYPE_NV12;
    venc_chn_attr.stVencAttr.u32PicWidth = width;
    venc_chn_attr.stVencAttr.u32PicHeight = height;
    venc_chn_attr.stVencAttr.u32VirWidth = width;
    venc_chn_attr.stVencAttr.u32VirHeight = height;
    venc_chn_attr.stVencAttr.bByFrame = RK_TRUE; // Enable frame-by-frame encoding for lower latency

    // Set profile depending on codec
    if (enCodecType == RK_CODEC_TYPE_H264) {
        venc_chn_attr.stVencAttr.u32Profile = 66; // H.264 baseline Profile
        venc_chn_attr.stVencAttr.stAttrH264e.u32Level = 40; // Level 4.0 (suitable for 1080p@30fps)
    }
    if (enCodecType == RK_CODEC_TYPE_H265) {
        venc_chn_attr.stVencAttr.u32Profile = 1;  // H.265 Main Profile
        venc_chn_attr.stVencAttr.stAttrH265e.bScaleList = RK_FALSE; // Disable scaling lists for faster encoding
    }

    // GOP settings: only I/P frames, no B-frames
    venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    venc_chn_attr.stGopAttr.u32GopSize = encoder_config.gop;; // Short GOP for lower latency (I-frame)
    venc_chn_attr.stGopAttr.s32IPQpDelta = 0; // No QP offset between 'I' and 'P' frames
    venc_chn_attr.stGopAttr.s32ViQpDelta = 0; // No additional QP adjustment for video input
    venc_chn_attr.stGopAttr.u32BgInterval = 0; // No background refresh

    // Rate control settings
    switch (enCodecType) {
        case RK_CODEC_TYPE_H265:
            if (encoder_config.use_cbr) {
                venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
                venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = encoder_config.gop;
                venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = bitrate;
                venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
                venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
            } else {
                venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
                venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = encoder_config.gop;
                venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = bitrate;
                venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
                venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
            }
            break;
        case RK_CODEC_TYPE_H264:
        default:
            if (encoder_config.use_cbr) {
                venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
                venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = encoder_config.gop;
                venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = bitrate;
                venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
                venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
            } else {
                venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
                venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = encoder_config.gop;
                venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = bitrate;
                venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
                venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = fps;
                venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
            }
            break;
    }

    ret = RK_MPI_VENC_CreateChn(0, &venc_chn_attr);
    if (ret) {
        printf("ERROR: failed to create VENC[0]! ret=%d\n", ret);
        return -1;
    }

    MPP_CHN_S stEncChn = {0};
    stEncChn.enModId = RK_ID_VENC;
    stEncChn.s32DevId = 0;
    stEncChn.s32ChnId = 0;
    ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
    if (ret) {
        printf("ERROR: failed to register output callback for VENC[0]! ret=%d\n", ret);
        return -1;
    }

    // === SET MAX/MIN QP PARAMETERS FOR ENCODER ===
    VENC_RC_PARAM_S rc_param = {0};

    // Get current rate control parameters
    RK_MPI_VENC_GetRcParam(0, &rc_param);

    // Initial QP for the first frame
    // Lower value = better quality but more data; higher = less bitrate but more artifacts
    rc_param.s32FirstFrameStartQp = 28;

    if (enCodecType == RK_CODEC_TYPE_H264) {
        rc_param.stParamH264.u32MaxQp  = 38; // Maximum QP for P-frames — controls maximum compression level (higher = more compression, lower quality)
        rc_param.stParamH264.u32MinQp  = 32; // Minimum QP for P-frames — ensures quality doesn't drop below this level
        rc_param.stParamH264.u32MaxIQp = 38; // Maximum QP for I-frames — I-frames are keyframes, so limit their compression too
        rc_param.stParamH264.u32MinIQp = 32; // Minimum QP for I-frames — keeps I-frame quality above a threshold

    } else if (enCodecType == RK_CODEC_TYPE_H265) {
        rc_param.stParamH265.u32MaxQp  = 38;  // Max QP for P-frames
        rc_param.stParamH265.u32MinQp  = 32;  // Min QP for P-frames
        rc_param.stParamH265.u32MaxIQp = 38;  // Max QP for I-frames
        rc_param.stParamH265.u32MinIQp = 32;  // Min QP for I-frames
    }

    RK_MPI_VENC_SetRcParam(0, &rc_param);

    int avg_frame_bits = bitrate / fps;

    float i_ratio = 2.7f; // TODO: carefully configure these settings
    float p_ratio = 2.2f;

    VENC_SUPERFRAME_CFG_S superFrmCfg = {
        .enSuperFrmMode = SUPERFRM_REENCODE, // Superframe reencoding to increase smoothness
        .u32SuperIFrmBitsThr = (uint32_t)(avg_frame_bits * i_ratio),
        .u32SuperPFrmBitsThr = (uint32_t)(avg_frame_bits * p_ratio),
        .enRcPriority = VENC_RC_PRIORITY_FRAMEBITS_FIRST // Prioritize adhering to per-frame size limits
    };

    RK_MPI_VENC_SetSuperFrameStrategy(0, &superFrmCfg);

#if OSD_OVERLAY_ENABLE
    init_overlay_region(0, OSD_REGION_ID, OSD_REGION_WIDTH, OSD_REGION_HEIGHT, OSD_REGION_POS_X, OSD_REGION_POS_Y);
    init_overlay_region(0, DETECTION_REGION_ID, DETECTION_REGION_WIDTH, DETECTION_REGION_HEIGHT, DETECTION_REGION_POS_X, DETECTION_REGION_POS_Y);
#endif

    return 0;
}

static int parse_bitrate(const char* arg)
{
    char *end;
    long value = strtol(arg, &end, 10);
    if (end == arg) {
        fprintf(stderr, "Invalid bitrate value: %s\n", arg);
        exit(EXIT_FAILURE);
    }

    switch (*end) {
        case 'M':
        case 'm':
            value *= 1024 * 1024;
            break;
        case 'K':
        case 'k':
            value *= 1024;
            break;
        case '\0':
            // No suffix — raw bits
            break;
        default:
            fprintf(stderr, "Unsupported bitrate suffix: %c (only k/K or m/M allowed)\n", *end);
            exit(EXIT_FAILURE);
    }

    if (value <= 1024 || value > (50 * 1024 * 1024)) {
        fprintf(stderr, "Bitrate out of range: %ld bps (max 50M)\n", value);
        exit(EXIT_FAILURE);
    }

    return (int)value;
}

float read_scene_brightness_estimate(RK_S32 cam_id)
{
    if (cam_id >= MAX_AIQ_CTX || !g_aiq_ctx[cam_id]) {
        return -1.0f;
    }

    Uapi_ExpQueryInfo_t info;
    memset(&info, 0, sizeof(info));

    XCamReturn ret = rk_aiq_user_api_ae_queryExpResInfo(g_aiq_ctx[cam_id], &info);
    if (ret != XCAM_RETURN_NO_ERROR) {
        fprintf(stderr, "rk_aiq_user_api_ae_queryExpResInfo failed: %d\n", ret);
        return -1.0f;
    }

    // MeanLuma is in range 0..255
    float norm_luma = info.MeanLuma / 255.0f;
    //printf("Scene MeanLuma = %.1f (norm = %.3f)\n", info.MeanLuma, norm_luma);
    return norm_luma;
}

void ae_set_center_exposure_region(RK_S32 cam_id, int stream_width, int stream_height, int target_width_px, int target_height_px)
{
    const int grid_cols = 15;
    const int grid_rows = 15;

    int cell_w = stream_width / grid_cols;
    int cell_h = stream_height / grid_rows;

    int region_w = target_width_px / cell_w;
    int region_h = target_height_px / cell_h;

    // Ensure at least 1x1 and not larger than grid
    if (region_w < 1) region_w = 1;
    if (region_h < 1) region_h = 1;
    if (region_w > grid_cols) region_w = grid_cols;
    if (region_h > grid_rows) region_h = grid_rows;

    // Center the region
    int start_x = (grid_cols - region_w) / 2;
    int start_y = (grid_rows - region_h) / 2;

    SAMPLE_COMM_ISP_SET_FastExposureRegion(cam_id, start_x, start_y, region_w, region_h, 255);

    printf("[AE] Center region: (%d,%d) size=%dx%d (%.1f%% of frame)\n",
           start_x, start_y, region_w, region_h,
           100.0f * (region_w * region_h) / (grid_cols * grid_rows));
}

static void* ae_auto_adjust_thread(void *arg)
{
    isp_config_t *cfg = (isp_config_t *)arg;

    float smoothed_luma = -1.0f;
    const float alpha = 0.1f;       // Smoothing factor for brightness changes
    const float hysteresis = 0.03f; // Minimum delta to trigger AE update
    float last_applied_luma = -1.0f;

    const float min_time = 0.00005f;       // Min exposure time
    const float max_time_day = 0.010f;    // Max exposure during day
    const float max_gain_day = 10.0f;      // Max gain during day

    const float max_time_night = 0.060f;   // Max exposure during night
    const float max_gain_night = 60.0f;   // Max gain during night

    const float night_threshold = 0.1f;  // Enter night mode below this
    const float day_threshold = 0.25f;    // Exit night mode above this

    bool night_mode = false;
    bool prev_night_mode = false;

    float prev_max_time = -1.0f;
    float prev_max_gain = -1.0f;

    // set initial exposure region to center of the frame
    ae_set_center_exposure_region(cfg->cam_id, cfg->stream_width, cfg->stream_height, cfg->stream_width/2, cfg->stream_height/2);

    while (run) {
        float brightness = read_scene_brightness_estimate(cfg->cam_id);
        if (brightness < 0.0f) {
            printf("[AE Thread] Invalid brightness reading\n");
            usleep(50 * 1000);
            continue;
        }

        if (brightness > 0.9f && smoothed_luma < 0.2f) {
            brightness = smoothed_luma; // clamp peak
        }

        // Exponential moving average of brightness
        if (smoothed_luma < 0.0f)
            smoothed_luma = brightness;
        else
            smoothed_luma = (1.0f - alpha) * smoothed_luma + alpha * brightness;

        // Clamp value between [0.0, 1.0]
        smoothed_luma = fminf(fmaxf(smoothed_luma, 0.0f), 1.0f);

        // Print debug info
        //printf("[AE Thread] Raw=%.3f Smoothed=%.3f\n", brightness, smoothed_luma);

        // Skip update if change is insignificant
        if (fabsf(smoothed_luma - last_applied_luma) < hysteresis) {
            usleep(50 * 1000);
            continue;
        }
        last_applied_luma = smoothed_luma;

        // Determine if night mode should toggle
        if (!night_mode && smoothed_luma < night_threshold)
            night_mode = true;
        else if (night_mode && smoothed_luma > day_threshold)
            night_mode = false;

        if (night_mode != prev_night_mode) {
            // mechanical IR filter control, maybe add this hw feature for new cam
            // SAMPLE_COMM_ISP_SET_GrayOpenLed(cfg->cam_id, night_mode ? RK_TRUE : RK_FALSE);
            printf("[AE Thread] Night mode: %s\n", night_mode ? "ON" : "OFF");
            prev_night_mode = night_mode;

            if (night_mode) {
                SAMPLE_COMM_ISP_SET_Saturation(cfg->cam_id, 0);
                SAMPLE_COMM_ISP_SET_LightInhibition(cfg->cam_id, RK_TRUE, 150, 255);
                SAMPLE_COMM_ISP_SET_BackLight(cfg->cam_id, RK_TRUE, 8);
                usleep(500 * 1000);
            } else {
                SAMPLE_COMM_ISP_SET_Saturation(cfg->cam_id, cfg->saturation);
                SAMPLE_COMM_ISP_SET_LightInhibition(cfg->cam_id, RK_FALSE, 0, 0);
                //SAMPLE_COMM_ISP_SET_LightInhibition(cfg->cam_id, RK_TRUE, 10, 255);
                SAMPLE_COMM_ISP_SET_BackLight(cfg->cam_id, RK_FALSE, 0);
                usleep(500 * 1000);
            }
        }

        // Calculate AE scaling parameters
        float scale = 1.0f - smoothed_luma;
        float max_time = night_mode
                         ? min_time + (max_time_night - min_time) * scale
                         : min_time + (max_time_day - min_time) * scale;
        float max_gain = night_mode
                         ? 1.0f + (max_gain_night - 1.0f) * scale
                         : 1.0f + (max_gain_day - 1.0f) * scale;

        // Avoid reapplying same settings
        if (fabsf(max_time - prev_max_time) < 0.0001f &&
            fabsf(max_gain - prev_max_gain) < 0.01f) {
            usleep(50 * 1000);
            continue;
        }
        prev_max_time = max_time;
        prev_max_gain = max_gain;

        // Apply exposure settings
        SAMPLE_COMM_ISP_SET_FastAutoExposure(cfg->cam_id, min_time, max_time, max_gain);

        printf("[AE Thread] Luma=%.3f Smoothed Luma=%.3f "
               "AE range → Time: [%.4f - %.4f]s, Gain: [%.2f - %.2f]\n",
               brightness, smoothed_luma,
               min_time, max_time, 1.0f, max_gain);

        usleep(50 * 1000); // Sleep 50 ms
    }

    return NULL;
}

void link_cmd_rx_callback(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size) {
    // Handle received command
    switch (sub_cmd_id) {
        case LINK_SUBCMD_SYS_INFO:
            if (cmd_id == LINK_CMD_GET) {
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_SYS_INFO, NULL, 0);
            }
            break;
        case LINK_SUBCMD_WFB_KEY:
            if (cmd_id == LINK_CMD_SET) {
                char buf[64] = {0};
                memcpy(buf, data, size);
                printf("Received WFB key: %s\n", buf);
                // Process the key...
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_WFB_KEY, NULL, 0);
            } else if (cmd_id == LINK_CMD_GET) {
                // Respond with current WFB key
                char wfb_key[64] = "my_wfb_key"; // Replace with actual key retrieval logic
                link_send_cmd(LINK_CMD_ACK, LINK_SUBCMD_WFB_KEY, wfb_key, strlen(wfb_key));
            }
            break;
        // Handle other commands...
        default:
            printf("Unknown command ID: %d", cmd_id);
            link_send_cmd(LINK_CMD_NACK, sub_cmd_id, NULL, 0);
            break;
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, handle_sigint);
    int ret = 0;
    int cam_id = 0;
    int bitrate = 4 * 1024 * 1024; // Default to 4 Mbps
    int selected_profile = -1; // -1 means no profile selected
    char destination_ip[64] = "127.0.0.1"; // Default IP
    int destination_port = 5602;           // Default port

    printf("%s: %s\n", APP_NAME, "Starting RTP streamer");

    /** Pars arguments */
    bool bitrate_set = false;

    static int opt;
    static int option_index = 0;
    while ((opt = getopt_long(argc, argv, "ha:p:b:P:f:U:N", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                break;
            case 'a':
                strncpy(destination_ip, optarg, sizeof(destination_ip) - 1);
                destination_ip[sizeof(destination_ip) - 1] = '\0';
                break;
            case 'p':
                destination_port = atoi(optarg);
                if (destination_port <= 0 || destination_port > 65535) {
                    fprintf(stderr, "Invalid port number: %d\n", destination_port);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                encoder_config.fps = atoi(optarg);
                isp_config.fps = encoder_config.fps;
                if (isp_config.fps <= 0 || isp_config.fps > 60) {
                    fprintf(stderr, "Invalid FPS value: %d (allowed range: 1-60)\n", isp_config.fps);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'P':
                selected_profile = atoi(optarg);
                if (selected_profile < 0 || selected_profile >= NUM_PRESETS) {
                    fprintf(stderr, "Invalid profile number. Available profiles: 0 to %lu\n", NUM_PRESETS - 1);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'b':
                bitrate = parse_bitrate(optarg);
                bitrate_set = true;
                break;
            case 'N':
                detection_run = true;
                break;
            case 0: // from long_options
                if (strcmp("fps", long_options[option_index].name) == 0) {
                    encoder_config.fps = atoi(optarg);
                    isp_config.fps = encoder_config.fps;
                }
                break;
            case 1: // --gop
                encoder_config.gop = atoi(optarg);
                if (encoder_config.gop <= 1 || encoder_config.gop > 60) {
                    fprintf(stderr, "Invalid GOP value: %d (allowed range: 1-60)\n", encoder_config.gop);
                    exit(EXIT_FAILURE);
                }
                break;
            case 2: // --payload-size
                encoder_config.payload_size = atoi(optarg);
                if (encoder_config.payload_size < 500 || encoder_config.payload_size > 1400) {
                    fprintf(stderr, "Invalid payload size: %d (allowed range: 500-1400)\n",
                            encoder_config.payload_size);
                    exit(EXIT_FAILURE);
                }
                break;
            case 3: // --vbr
                encoder_config.use_cbr = false;
                break;
            case 4: // --focus-mode
                encoder_config.enable_focus = true;
                encoder_config.focus_quality = atoi(optarg);
                if (encoder_config.focus_quality < -51 || encoder_config.focus_quality > 51) {
                    fprintf(stderr, "Invalid focus quality QP delta: %d (allowed range: -51 to 51)\n", encoder_config.focus_quality);
                    exit(EXIT_FAILURE);
                }
                break;
            case 5: // --codec
                if (strcmp(optarg, "h264") == 0) {
                    encoder_config.codec_type = RK_CODEC_TYPE_H264;
                } else if (strcmp(optarg, "h265") == 0) {
                    encoder_config.codec_type = RK_CODEC_TYPE_H265;
                } else {
                    fprintf(stderr, "Invalid codec type: %s (allowed: h264, h265)\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 6: // --mirror
                isp_config.mirror = 1;
                break;
            case 7: // --flip
                isp_config.flip = 1;
                break;
            default:
                print_help(argv[0]);
                break;
        }
    }

    // If a profile is selected, override resolution
    if (selected_profile >= 0) {
        isp_config.stream_width = resolution_presets[selected_profile].width;
        isp_config.stream_height = resolution_presets[selected_profile].height;
        if (!bitrate_set) {
            bitrate = resolution_presets[selected_profile].default_bitrate;
        }
        printf("Selected profile %d: %s (%dx%d) bitrate=%d\n", selected_profile,
               resolution_presets[selected_profile].name, isp_config.stream_width, isp_config.stream_height, bitrate);
    }

    printf("Final configuration:\n");
    printf("  Codec type: %s\n", encoder_config.codec_type == RK_CODEC_TYPE_H264 ? "H.264" : "H.265");
    printf("  FPS: %d\n", isp_config.fps);
    printf("  Encoder FPS: %d\n", encoder_config.fps);
    printf("  Bitrate: %d bps\n", bitrate);
    printf("  GOP size: %d\n", encoder_config.gop);
    printf("  Payload size: %d bytes\n", encoder_config.payload_size);
    printf("  Rate Control: %s\n", encoder_config.use_cbr ? "CBR" : "VBR");
    printf("  Destination IP: %s\n", destination_ip);
    printf("  Destination Port: %d\n", destination_port);

    printf("  Stream resolution: %dx%d\n", isp_config.stream_width, isp_config.stream_height);
    printf("  Focus mode: %s\n", encoder_config.enable_focus ? "Enabled" : "Disabled");
    printf("  Mirror: %s\n", isp_config.mirror ? "Enabled" : "Disabled");
    printf("  Flip: %s\n", isp_config.flip ? "Enabled" : "Disabled");
    printf("  Object Detection: %s\n", detection_run ? "Enabled" : "Disabled");

    /** Check, create, write pid lock file */
    if (write_pidfile(PID_FILE) != 0) {
        /** Exit */
        exit(0);
    }

    if (rtp_socket_init(destination_ip, destination_port) != 0) {
        printf("Failed to init RTP socket\n");
        return -1;
    }

    init_rtp(encoder_config.payload_size);

    if (start_camera_stream(&isp_config) != 0) {
        printf("Failed to start camera stream\n");
        // TODO: add handler for cleanup
    }

    if (detection_run && start_detection_stream(&isp_config) != 0) {
        printf("Failed to start detection stream\n");
        // TODO: add handler for cleanup
    }

    if (start_video_encoder(encoder_config.codec_type, isp_config.stream_width, isp_config.stream_height,
                        bitrate, encoder_config.fps) != 0) {
        printf("Failed to start video encoder\n");
        // TODO: add handler for cleanup
    }

    if (link_init(LINK_DRONE) != 0) {
        printf("Failed to initialize link\n");
    } else {
        link_register_cmd_rx_cb(link_cmd_rx_callback);
    }

    if (encoder_config.enable_focus && encoder_config.codec_type != RK_CODEC_TYPE_H265) { // No focus mode for H265 for now 
        enable_focus_mode(isp_config.stream_width, isp_config.stream_height);
    }

    // Bind Camera VI[0] and Encoder VENC[0]
    MPP_CHN_S stSrcChn = {0};
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    MPP_CHN_S stDestChn = {0};
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn)) {
        printf("ERROR: Bind VI[0] and VENC[0] error! ret=%d\n", ret);
        // TODO: add handler for cleanup
    }

    pthread_t ae_thread;
    pthread_create(&ae_thread, NULL, ae_auto_adjust_thread, &isp_config);

    cpu_info_t cpu = get_cpu_info();
    while (run) {
        // link_send_sys_telemetry(cpu.temperature_celsius, cpu.usage_percent);
        char* test_data = "Test data for link";
        link_send_displayport(test_data, strlen(test_data));

#if OSD_OVERLAY_ENABLE
        //update_cpu_info_overlay(0);
        usleep(OSD_UPDATE_INTERVAL_US);
#else
        sleep(1); // doo nothing, just wait for SIGINT
#endif
    }
    pthread_join(ae_thread, NULL);

    // Stop detection stream first
    stop_detection_stream();

    // unbind first
    ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn)) {
        printf("ERROR: UnBind VI[0] and VENC[0] error! ret=%d\n", ret);
    }

    // destroy venc before vi
    if (RK_MPI_VENC_DestroyChn(0)) {
        printf("ERROR: Destroy VENC[0] error! ret=%d\n", ret);
    }

    // destroy vi
    if (RK_MPI_VI_DisableChn(cam_id, 0)) {
        printf("ERROR: Disable VI[0] error! ret=%d\n", ret);
    }

    if (encoder) {
        rtp_payload_encode_destroy(encoder);
        encoder = NULL;
    }

    close(udp_socket);
    SAMPLE_COMM_ISP_Stop(cam_id);

    return 0;
}
