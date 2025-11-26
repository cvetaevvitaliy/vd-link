/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "encoder/encoder.h"
#include <stdio.h>
#include <easymedia/rkmedia_api.h>
#include <easymedia/rkmedia_venc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define RTP_CLOCK_RATE 90000

static encoder_callback enc_callback;
extern common_config_t config; // from common.c

static inline uint64_t monotonic_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static inline uint32_t align_down(uint32_t v, uint32_t a)
{
    return v & ~(a - 1u);
}

static inline uint32_t clampu32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void video_packet_cb(MEDIA_BUFFER mb)
{
    uint8_t *data = (uint8_t *)RK_MPI_MB_GetPtr(mb);
    size_t size = RK_MPI_MB_GetSize(mb);

    // Static state for frame counter and initial RTP timestamp base
#if 0
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
    enc_callback(data, (int)size, rtp_timestamp);

    RK_MPI_MB_ReleaseBuffer(mb);

}

static int init_overlay_region(int venc_chn, int region_id, encoder_config_t *enc_cfg)
{
    if (!enc_cfg) return -1;

    // Align to 16 as required by RKMEDIA
    uint32_t w = align_down((uint32_t)enc_cfg->width, 16);
    uint32_t h = align_down((uint32_t)enc_cfg->height, 16);
    // uint32_t x = align_down((uint32_t)enc_cfg->osd_config.pos_x, 16);
    // uint32_t y = align_down((uint32_t)enc_cfg->osd_config.pos_y, 16);
    uint32_t x = 0;
    uint32_t y = 0;

    // Guard against zero after alignment
    if (w == 0 || h == 0) {
        fprintf(stderr, "%s: width/height become zero after 16-align -> disabling OSD\n", __FUNCTION__);
        return 0; // treat as "disabled", not a hard error
    }

    // Clamp so that the region fits inside the frame
    const uint32_t fw = (uint32_t)enc_cfg->width;
    const uint32_t fh = (uint32_t)enc_cfg->height;
    if (w > fw) w = align_down(fw, 16);
    if (h > fh) h = align_down(fh, 16);

    if (x + w > fw) x = align_down((fw > w) ? (fw - w) : 0u, 16);
    if (y + h > fh) y = align_down((fh > h) ? (fh - h) : 0u, 16);

    enc_cfg->osd_config.width  = (int)w;
    enc_cfg->osd_config.height = (int)h;
    enc_cfg->osd_config.pos_x  = (int)x;
    enc_cfg->osd_config.pos_y  = (int)y;

    // Optional: log the adjustment
    fprintf(stderr, "%s: aligned/clamped to <x=%u, y=%u, w=%u, h=%u>\n", __FUNCTION__, x, y, w, h);

    // Init OSD system for this VENC channel
    int ret = RK_MPI_VENC_RGN_Init(venc_chn, NULL);
    if (ret) {
        fprintf(stderr, "%s: RGN_Init failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    OSD_REGION_INFO_S RgnInfo;
    memset(&RgnInfo, 0, sizeof(RgnInfo));
    RgnInfo.enRegionId = region_id;
    RgnInfo.u32Width   = w;
    RgnInfo.u32Height  = h;
    RgnInfo.u32PosX    = x;
    RgnInfo.u32PosY    = y;
    RgnInfo.u8Enable   = 1;
    RgnInfo.u8Inverse  = 0;

    // Prepare an empty ARGB8888 bitmap
    BITMAP_S bmp;
    memset(&bmp, 0, sizeof(bmp));
    bmp.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    bmp.u32Width  = w;
    bmp.u32Height = h;
    bmp.pData = calloc((size_t)w * (size_t)h, 4);
    if (!bmp.pData) {
        fprintf(stderr, "%s: OOM for bitmap %ux%u\n", __FUNCTION__, w, h);
        return -1;
    }

    ret = RK_MPI_VENC_RGN_SetBitMap(venc_chn, &RgnInfo, &bmp);
    free(bmp.pData);
    if (ret) {
        fprintf(stderr, "%s: SetBitMap failed: %d\n", __FUNCTION__, ret);
        return -1;
    }
    return 0;
}

static int encoder_fill_venc_params(encoder_config_t *cfg, VENC_CHN_ATTR_S *venc_chn_attr)
{
    if (!cfg || !venc_chn_attr) {
        fprintf(stderr, "%s: Invalid arguments\n", __FUNCTION__);
        return -1;
    }
    CODEC_TYPE_E codec_type = RK_CODEC_TYPE_NONE;
    
    switch (cfg->codec) {
    case CODEC_H264: {
        codec_type = RK_CODEC_TYPE_H264;
        venc_chn_attr->stVencAttr.u32Profile = 66; // H.264 baseline Profile
        venc_chn_attr->stVencAttr.stAttrH264e.u32Level = 40; // Level 4.0 (suitable for 1080p@30fps)

        if (cfg->rate_mode == RATE_CONTROL_CBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
            venc_chn_attr->stRcAttr.stH264Cbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH264Cbr.u32BitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH264Cbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH264Cbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_VBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
            venc_chn_attr->stRcAttr.stH264Vbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH264Vbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH264Vbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH264Vbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_AVBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
            venc_chn_attr->stRcAttr.stH264Avbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH264Avbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH264Avbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Avbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH264Avbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH264Avbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_FIXQP) {
            printf("FIXQP - Not supported mow\n");
            return -1;
        } else {
            printf("Unsupported rate control mode\n");
            return -1;
        }
    } break;
    case CODEC_H265: {
        codec_type = RK_CODEC_TYPE_H265;
        venc_chn_attr->stVencAttr.u32Profile = 1;  // H.265 Main Profile
        venc_chn_attr->stVencAttr.stAttrH265e.bScaleList = RK_FALSE; // Disable scaling lists for faster encoding

        if (cfg->rate_mode == RATE_CONTROL_CBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
            venc_chn_attr->stRcAttr.stH265Cbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH265Cbr.u32BitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH265Cbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH265Cbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_VBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
            venc_chn_attr->stRcAttr.stH265Vbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH265Vbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH265Vbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH265Vbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_AVBR) {
            venc_chn_attr->stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;
            venc_chn_attr->stRcAttr.stH265Avbr.u32Gop = cfg->gop;
            venc_chn_attr->stRcAttr.stH265Avbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr->stRcAttr.stH265Avbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Avbr.fr32DstFrameRateDen = 1;
            venc_chn_attr->stRcAttr.stH265Avbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr->stRcAttr.stH265Avbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_FIXQP) {
            printf("FIXQP - Not supported mow\n");
            return -1;
        } else {
            printf("Unsupported rate control mode\n");
            return -1;
        }
    } break;
    default:
        printf("Unsupported codec type\n");
        return -1;
    }

    venc_chn_attr->stVencAttr.enType = codec_type;
    venc_chn_attr->stVencAttr.imageType = cfg->pixel_format;
    venc_chn_attr->stVencAttr.u32PicWidth = cfg->width;
    venc_chn_attr->stVencAttr.u32PicHeight = cfg->height;
    venc_chn_attr->stVencAttr.u32VirWidth = cfg->width;
    venc_chn_attr->stVencAttr.u32VirHeight = cfg->height;
    venc_chn_attr->stVencAttr.bByFrame = RK_TRUE; // Enable frame-by-frame encoding for lower latency

    // GOP settings: only I/P frames, no B-frames
    venc_chn_attr->stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    venc_chn_attr->stGopAttr.u32GopSize = cfg->gop;; // Short GOP for lower latency (I-frame)
    venc_chn_attr->stGopAttr.s32IPQpDelta = 0; // No QP offset between 'I' and 'P' frames
    venc_chn_attr->stGopAttr.s32ViQpDelta = 0; // No additional QP adjustment for video input
    venc_chn_attr->stGopAttr.u32BgInterval = 0; // No background refresh

}

int encoder_init(encoder_config_t *cfg)
{
    int ret = 0;
    VENC_CHN_ATTR_S venc_chn_attr = {0};
    MPP_CHN_S stEncChn = {0};
    VENC_RC_PARAM_S rc_param = {0};

    RK_MPI_SYS_Init();

    if (cfg == NULL || cfg->callback == NULL) {
        fprintf(stderr, "Encoder config or callback is NULL\n");
        return -1;
    }
    printf("%sStarting video encoder with resolution %dx%d, bitrate %d bps\n", __FUNCTION__,
        cfg->width, cfg->height, cfg->bitrate);
    stEncChn.enModId = RK_ID_VENC;
    stEncChn.s32DevId = 0;
    stEncChn.s32ChnId = 0;

    encoder_fill_venc_params(cfg, &venc_chn_attr);

    ret = RK_MPI_VENC_CreateChn(0, &venc_chn_attr);
    if (ret) {
        printf("ERROR: failed to create VENC[0]! ret=%d\n", ret);
        return -1;
    }

    ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
    enc_callback = cfg->callback;
    if (ret) {
        printf("ERROR: failed to register output callback for VENC[0]! ret=%d\n", ret);
        return -1;
    }

    // Get current rate control parameters
    RK_MPI_VENC_GetRcParam(0, &rc_param);
    // Initial QP for the first frame
    // Lower value = better quality but more data; higher = less bitrate but more artifacts
    rc_param.s32FirstFrameStartQp = 28; // TODO: need to tune this parameter

    // TODO: tune these settings
    if (cfg->codec == CODEC_H264) {
        rc_param.stParamH264.u32MaxQp  = 38; // Maximum QP for P-frames — controls maximum compression level (higher = more compression, lower quality)
        rc_param.stParamH264.u32MinQp  = 32; // Minimum QP for P-frames — ensures quality doesn't drop below this level
        rc_param.stParamH264.u32MaxIQp = 38; // Maximum QP for I-frames — I-frames are keyframes, so limit their compression too
        rc_param.stParamH264.u32MinIQp = 32; // Minimum QP for I-frames — keeps I-frame quality above a threshold

    }
    if (cfg->codec == CODEC_H265) {
        rc_param.stParamH265.u32MaxQp  = 38;  // Max QP for P-frames
        rc_param.stParamH265.u32MinQp  = 32;  // Min QP for P-frames
        rc_param.stParamH265.u32MaxIQp = 38;  // Max QP for I-frames
        rc_param.stParamH265.u32MinIQp = 32;  // Min QP for I-frames
    }

    RK_MPI_VENC_SetRcParam(0, &rc_param);

    int avg_frame_bits = cfg->bitrate / cfg->fps;

    float i_ratio = 2.7f; // TODO: carefully configure these settings
    float p_ratio = 2.2f;

    VENC_SUPERFRAME_CFG_S superFrmCfg = {
        .enSuperFrmMode = SUPERFRM_REENCODE, // Superframe reencoding to increase smoothness
        .u32SuperIFrmBitsThr = (uint32_t)(avg_frame_bits * i_ratio),
        .u32SuperPFrmBitsThr = (uint32_t)(avg_frame_bits * p_ratio),
        .enRcPriority = VENC_RC_PRIORITY_FRAMEBITS_FIRST // Prioritize adhering to per-frame size limits
    };

    RK_MPI_VENC_SetSuperFrameStrategy(0, &superFrmCfg);

    RK_MPI_SYS_SetMediaBufferDepth(RK_ID_VENC, 0, 8); // Set VENC channel buffer depth to 8 frames

    VENC_RECV_PIC_PARAM_S recv_param = { .s32RecvPicNum = -1 };
    ret = RK_MPI_VENC_StartRecvFrame(0, &recv_param);
    if (ret) {
        fprintf(stderr, "ERROR: VENC_StartRecvFrame failed! ret=%d\n", ret);
        RK_MPI_VENC_DestroyChn(0);
        return -1;
    }

    ret = RK_MPI_SYS_StartGetMediaBuffer(RK_ID_VENC, 0);
    if (ret) {
        fprintf(stderr, "ERROR: SYS_StartGetMediaBuffer failed! ret=%d\n", ret);
        RK_MPI_VENC_DestroyChn(0);
        return -1;
    }

    if (init_overlay_region(0, REGION_ID_0, cfg) < 0) {;
        printf("Failed to initialize OSD region\n");
        return -1;
    }

    return 0;
}

int encoder_set_input_image_format(pixfmt_t  pixel_format, int width, int height)
{
    encoder_clean();
    config.encoder_config.pixel_format = pixel_format;
    config.encoder_config.width = width;
    config.encoder_config.height = height;
    
    printf("Setting encoder input image format to %d, width=%d, height=%d\n", pixel_format, width, height);

    encoder_init(&config.encoder_config);
    return 0;
}

encoder_config_t* encoder_get_input_image_format(void)
{
    return &config.encoder_config;
}

void encoder_focus_mode(encoder_config_t *cfg)
{
    VENC_ROI_ATTR_S roi_attr[1];
    memset(roi_attr, 0, sizeof(roi_attr));

    roi_attr[0].u32Index = 0;          // ROI region index
    roi_attr[0].bEnable = RK_TRUE;     // Enable this ROI region
    roi_attr[0].bAbsQp = RK_FALSE;     // Relative QP mode
    roi_attr[0].s32Qp = cfg->encoder_focus_mode.focus_quality; // Low quality Range:[-51, 51]; QP value,only relative mode can QP value
    roi_attr[0].bIntra = RK_FALSE;     // No forced intra-refresh for ROI

    // Center 65% of frame
    RK_U32 focus_width = (cfg->width *  cfg->encoder_focus_mode.frame_size) / 100;
    RK_U32 focus_height = (cfg->height * cfg->encoder_focus_mode.frame_size) / 100;

    roi_attr[0].stRect.s32X = (RK_S32)(cfg->width - focus_width) / 2;
    roi_attr[0].stRect.s32Y = (RK_S32)(cfg->height - focus_height) / 2;
    roi_attr[0].stRect.u32Width = focus_width;
    roi_attr[0].stRect.u32Height = focus_height;

    int ret = RK_MPI_VENC_SetRoiAttr(0, roi_attr, 1);
    if (ret != 0) {
        printf("%s:Failed to set ROI for focus mode: %d\n", __FUNCTION__, ret);
        return;
    }

    printf("Focus Mode ROI set successfully: Center region %ux%u\n",
           roi_attr[0].stRect.u32Width, roi_attr[0].stRect.u32Height);
}

int encoder_manual_push_frame(encoder_config_t *cfg, void *data, int size)
{
    if (!cfg || !data || size <= 0) {
        fprintf(stderr, "%s: bad args\n", __FUNCTION__);
        return -1;
    }

    const uint32_t w = (uint32_t)cfg->width;
    const uint32_t h = (uint32_t)cfg->height;
    if (w == 0 || h == 0) {
        fprintf(stderr, "%s: invalid WxH\n", __FUNCTION__);
        return -1;
    }

    // Tightly packed NV12 payload size
    const int expected = (int)((uint64_t)w * h * 3ull / 2ull);
    if (size != expected) {
        fprintf(stderr,
                "%s: size mismatch (got %d, expect %d for NV12 %ux%u)\n",
                __FUNCTION__, size, expected, w, h);
        return -1;
    }

    // Strides must match what VENC expects; per твої логи — рівно width/height.
    MB_IMAGE_INFO_S info;
    info.u32Width     = w;
    info.u32Height    = h;
    info.u32HorStride = w;  // no alignment
    info.u32VerStride = h;  // no alignment
    info.enImgType    = IMAGE_TYPE_NV12;

    MEDIA_BUFFER mb = RK_MPI_MB_CreateImageBuffer(&info, RK_TRUE /*hardware*/,MB_FLAG_NOCACHED);
    if (!mb) {
        fprintf(stderr, "%s: CreateImageBuffer failed\n", __FUNCTION__);
        return -1;
    }

    uint8_t *dst = (uint8_t *)RK_MPI_MB_GetPtr(mb);
    if (!dst) {
        fprintf(stderr, "%s: MB ptr is NULL\n", __FUNCTION__);
        RK_MPI_MB_ReleaseBuffer(mb);
        return -1;
    }

    memcpy(dst, data, size);
    RK_MPI_MB_SetSize(mb, size);
    RK_MPI_MB_SetTimestamp(mb, monotonic_time_us());

    int ret = RK_MPI_SYS_SendMediaBuffer(RK_ID_VENC, 0, mb);
    if (ret != 0) {
        fprintf(stderr, "%s: SendMediaBuffer failed: %d\n", __FUNCTION__, ret);
        RK_MPI_MB_ReleaseBuffer(mb);
        return -1;
    }

    RK_MPI_MB_ReleaseBuffer(mb);

    return 0;
}

int encoder_draw_overlay_buffer(const void *data, int width, int height)
{
    if (!data) {
        fprintf(stderr, "%s: null args\n", __FUNCTION__);
        return -1;
    }
    if (width != config.encoder_config.width ||
        height != config.encoder_config.height) {
        fprintf(stderr, "%s: size mismatch (got %dx%d, expect %dx%d)\n",
                __FUNCTION__, width, height,
                config.encoder_config.width,
                config.encoder_config.height);
        return -1;
    }

    // 16-align as required by RKMEDIA
    uint32_t w = align_down( config.encoder_config.width,  16);
    uint32_t h = align_down( config.encoder_config.height, 16);
    // uint32_t x = align_down((uint32_t)cfg->pos_x,  16);
    // uint32_t y = align_down((uint32_t)cfg->pos_y,  16);
    uint32_t x = align_down(0,  16);
    uint32_t y = align_down(0,  16);


    if (w == 0 || h == 0) {
        return 0;
    }

    // OSD Region info
    OSD_REGION_INFO_S rgn;
    memset(&rgn, 0, sizeof(rgn));
    rgn.enRegionId = REGION_ID_0;
    rgn.u32Width   = w;
    rgn.u32Height  = h;
    rgn.u32PosX    = x;
    rgn.u32PosY    = y;
    rgn.u8Enable   = 1;
    rgn.u8Inverse  = 0;

    // Bitmap descriptor (points to caller's memory)
    BITMAP_S bmp = {0};
    bmp.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    bmp.u32Width = w;
    bmp.u32Height = h;
    bmp.pData = (void*)data;

    // Push to VENC[0]
    int ret = RK_MPI_VENC_RGN_SetBitMap(0, &rgn, &bmp);
    if (ret) {
        fprintf(stderr, "%s: SetBitMap failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    return 0;
}

int encoder_set_bitrate(int bitrate)
{
    VENC_CHN_ATTR_S venc_attr;
    RK_MPI_VENC_SetBitrate(0, bitrate, bitrate * 0.8, bitrate * 1.2);
    config.encoder_config.bitrate = bitrate;
    return 0;
    int ret = RK_MPI_VENC_GetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: GetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return 0;
    }
    // Update bitrate based on current rate control mode
    switch (venc_attr.stRcAttr.enRcMode) {
    case VENC_RC_MODE_H264CBR:
        venc_attr.stRcAttr.stH264Cbr.u32BitRate = (RK_U32)bitrate;
        break;
    case VENC_RC_MODE_H264VBR:
        venc_attr.stRcAttr.stH264Vbr.u32MaxBitRate = (RK_U32)bitrate;
        break;
    case VENC_RC_MODE_H264AVBR:
        venc_attr.stRcAttr.stH264Avbr.u32MaxBitRate = (RK_U32)bitrate;
        break;
    case VENC_RC_MODE_H265CBR:
        venc_attr.stRcAttr.stH265Cbr.u32BitRate = (RK_U32)bitrate;
        break;
    case VENC_RC_MODE_H265VBR:
        venc_attr.stRcAttr.stH265Vbr.u32MaxBitRate = (RK_U32)bitrate;
        break;
    case VENC_RC_MODE_H265AVBR:
        venc_attr.stRcAttr.stH265Avbr.u32MaxBitRate = (RK_U32)bitrate;
        break;
    default:
        printf("%s: Unsupported RC mode: %d\n", __FUNCTION__, venc_attr.stRcAttr.enRcMode);
        return 0;
    }

    ret = RK_MPI_VENC_SetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: SetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    printf("%s: Bitrate updated to %d bps\n", __FUNCTION__, bitrate);
    config.encoder_config.bitrate = bitrate;
    return 0;
}

int encoder_set_fps(int fps)
{
    VENC_CHN_ATTR_S venc_attr;
    int ret = RK_MPI_VENC_GetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: GetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    // Update frame rate based on current rate control mode
    switch (venc_attr.stRcAttr.enRcMode) {
    case VENC_RC_MODE_H264CBR:
        venc_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = fps;
        break;
    case VENC_RC_MODE_H264VBR:
        venc_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = fps;
        break;
    case VENC_RC_MODE_H264AVBR:
        venc_attr.stRcAttr.stH264Avbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH264Avbr.u32SrcFrameRateNum = fps;
        break;
    case VENC_RC_MODE_H265CBR:
        venc_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = fps;
        break;
    case VENC_RC_MODE_H265VBR:
        venc_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = fps;
        break;
    case VENC_RC_MODE_H265AVBR:
        venc_attr.stRcAttr.stH265Avbr.fr32DstFrameRateNum = fps;
        venc_attr.stRcAttr.stH265Avbr.u32SrcFrameRateNum = fps;
        break;
    default:
        printf("%s: Unsupported RC mode for FPS change: %d\n", __FUNCTION__, venc_attr.stRcAttr.enRcMode);
        return -1;
    }

    ret = RK_MPI_VENC_SetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: SetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    printf("%s: FPS updated to %d\n", __FUNCTION__, fps);
    config.encoder_config.fps = fps;
    return 0;
}

int encoder_set_gop(int gop)
{
    VENC_CHN_ATTR_S venc_attr;
    RK_MPI_VENC_SetGop(0, gop);
    config.encoder_config.gop = gop;
    return 0;
}

int encoder_set_rate_control(rate_control_mode_t mode)
{
    VENC_CHN_ATTR_S venc_attr;
    int ret = RK_MPI_VENC_GetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: GetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return -1;
    }
    config.encoder_config.rate_mode = mode;
    encoder_fill_venc_params(&config.encoder_config, &venc_attr);

    ret = RK_MPI_VENC_SetVencChnAttr(0, &venc_attr);
    if (ret != 0) {
        printf("%s: SetVencChnAttr failed: %d\n", __FUNCTION__, ret);
        return -1;
    }

    printf("%s: Rate control mode updated to %d\n", __FUNCTION__, mode);
    return 0;
}

int encoder_set_codec(codec_type_t codec)
{
    VENC_CHN_ATTR_S venc_attr;

    encoder_clean();
    config.encoder_config.codec = codec;
    encoder_init(&config.encoder_config);

    // Changing codec on-the-fly is not supported
    printf("%s: Changing codec on-the-fly is not supported\n", __FUNCTION__);
    return -1;
}

void encoder_clean(void)
{
    int ret = RK_MPI_VENC_DestroyChn(0);
    if (ret) {
        printf("%s: Destroy VENC[0] error! ret=%d\n", __FUNCTION__, ret);
    }
}
