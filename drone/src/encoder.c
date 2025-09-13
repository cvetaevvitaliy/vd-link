/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include <stdio.h>
#include <easymedia/rkmedia_api.h>
#include <easymedia/rkmedia_venc.h>
#include "encoder.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define RTP_CLOCK_RATE 90000

static encoder_callback enc_callback;

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

static int init_overlay_region(int venc_chn, int region_id, encoder_osd_config_t *osd_cfg)
{
    if (osd_cfg == NULL) { return -1; }

    RK_MPI_VENC_RGN_Init(0, NULL);
    OSD_REGION_INFO_S RgnInfo = {0};
    RgnInfo.enRegionId = region_id;
    RgnInfo.u32Width = osd_cfg->width;
    RgnInfo.u32Height = osd_cfg->height;
    RgnInfo.u32PosX = osd_cfg->pos_x;
    RgnInfo.u32PosY = osd_cfg->pos_y;
    RgnInfo.u8Enable = 1;
    RgnInfo.u8Inverse = 0;

    // Initialize VENC region system if not already
    RK_MPI_VENC_RGN_Init(venc_chn, NULL);

    // Start with an empty BitMap
    BITMAP_S dummy = {0};
    dummy.enPixelFormat = PIXEL_FORMAT_ARGB_8888;
    dummy.u32Width = osd_cfg->width;
    dummy.u32Height = osd_cfg->height;
    dummy.pData = calloc(osd_cfg->width * osd_cfg->height, 4);  // ARGB8888 = 4 bytes per pixel

    int ret = RK_MPI_VENC_RGN_SetBitMap(venc_chn, &RgnInfo, &dummy);
    free(dummy.pData);
    return ret;
}

int encoder_init(encoder_config_t *cfg)
{
    int ret = 0;
    VENC_CHN_ATTR_S venc_chn_attr = {0};
    CODEC_TYPE_E codec_type = RK_CODEC_TYPE_NONE;
    MPP_CHN_S stEncChn = {0};
    VENC_RC_PARAM_S rc_param = {0};

    if (cfg == NULL || cfg->callback == NULL) {
        fprintf(stderr, "Encoder config or callback is NULL\n");
        return -1;
    }
    printf("Starting video encoder with resolution %dx%d, bitrate %d bps\n", cfg->width, cfg->height, cfg->bitrate);
    stEncChn.enModId = RK_ID_VENC;
    stEncChn.s32DevId = 0;
    stEncChn.s32ChnId = 0;
    ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
    enc_callback = cfg->callback;
    if (ret) {
        printf("ERROR: failed to register output callback for VENC[0]! ret=%d\n", ret);
        return -1;
    }

    switch (cfg->codec) {
    case CODEC_H264: {
        codec_type = RK_CODEC_TYPE_H264;
        venc_chn_attr.stVencAttr.u32Profile = 66; // H.264 baseline Profile
        venc_chn_attr.stVencAttr.stAttrH264e.u32Level = 40; // Level 4.0 (suitable for 1080p@30fps)

        if (cfg->rate_mode == RATE_CONTROL_CBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
            venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_VBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
            venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_AVBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264AVBR;
            venc_chn_attr.stRcAttr.stH264Avbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH264Avbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH264Avbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Avbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH264Avbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH264Avbr.u32SrcFrameRateDen = 1;
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
        venc_chn_attr.stVencAttr.u32Profile = 1;  // H.265 Main Profile
        venc_chn_attr.stVencAttr.stAttrH265e.bScaleList = RK_FALSE; // Disable scaling lists for faster encoding

        if (cfg->rate_mode == RATE_CONTROL_CBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
            venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_VBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
            venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen = 1;
        } else if (cfg->rate_mode == RATE_CONTROL_AVBR) {
            venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265AVBR;
            venc_chn_attr.stRcAttr.stH265Avbr.u32Gop = cfg->gop;
            venc_chn_attr.stRcAttr.stH265Avbr.u32MaxBitRate = cfg->bitrate;
            venc_chn_attr.stRcAttr.stH265Avbr.fr32DstFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Avbr.fr32DstFrameRateDen = 1;
            venc_chn_attr.stRcAttr.stH265Avbr.u32SrcFrameRateNum = cfg->fps;
            venc_chn_attr.stRcAttr.stH265Avbr.u32SrcFrameRateDen = 1;
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

    venc_chn_attr.stVencAttr.enType = codec_type;
    venc_chn_attr.stVencAttr.imageType = IMAGE_TYPE_NV12;
    venc_chn_attr.stVencAttr.u32PicWidth = cfg->width;
    venc_chn_attr.stVencAttr.u32PicHeight = cfg->height;
    venc_chn_attr.stVencAttr.u32VirWidth = cfg->width;
    venc_chn_attr.stVencAttr.u32VirHeight = cfg->height;
    venc_chn_attr.stVencAttr.bByFrame = RK_TRUE; // Enable frame-by-frame encoding for lower latency

    // GOP settings: only I/P frames, no B-frames
    venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
    venc_chn_attr.stGopAttr.u32GopSize = cfg->gop;; // Short GOP for lower latency (I-frame)
    venc_chn_attr.stGopAttr.s32IPQpDelta = 0; // No QP offset between 'I' and 'P' frames
    venc_chn_attr.stGopAttr.s32ViQpDelta = 0; // No additional QP adjustment for video input
    venc_chn_attr.stGopAttr.u32BgInterval = 0; // No background refresh

    // Get current rate control parameters
    RK_MPI_VENC_GetRcParam(0, &rc_param);
    // Initial QP for the first frame
    // Lower value = better quality but more data; higher = less bitrate but more artifacts
    rc_param.s32FirstFrameStartQp = 28; // TODO: need to tune this parameter

    // TODO: tune these settings
    if (codec_type == RK_CODEC_TYPE_H264) {
        rc_param.stParamH264.u32MaxQp  = 38; // Maximum QP for P-frames — controls maximum compression level (higher = more compression, lower quality)
        rc_param.stParamH264.u32MinQp  = 32; // Minimum QP for P-frames — ensures quality doesn't drop below this level
        rc_param.stParamH264.u32MaxIQp = 38; // Maximum QP for I-frames — I-frames are keyframes, so limit their compression too
        rc_param.stParamH264.u32MinIQp = 32; // Minimum QP for I-frames — keeps I-frame quality above a threshold

    }
    if (codec_type == RK_CODEC_TYPE_H265) {
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

    if (init_overlay_region(0, REGION_ID_0, &cfg->osd_config) < 0) {;
        printf("Failed to initialize OSD region\n");
        return -1;
    }

    return 0;
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
        printf("Failed to set ROI for focus mode: %d\n", ret);
        return;
    }

    printf("Focus Mode ROI set successfully: Center region %ux%u\n",
           roi_attr[0].stRect.u32Width, roi_attr[0].stRect.u32Height);
}

void encoder_deinit(void)
{
    int ret = RK_MPI_VENC_DestroyChn(0);
    if (ret) {
        printf("ERROR: Destroy VENC[0] error! ret=%d\n", ret);
    }
}
