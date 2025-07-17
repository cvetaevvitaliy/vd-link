/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef VRX_COMMON_H
#define VRX_COMMON_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CODEC_UNKNOWN = 0,
    CODEC_H264,
    CODEC_H265
} codec_type_t;

struct config_t {
    const char* ip;
    int port;
    int wfb_port;
    int pt;
    codec_type_t codec;
} ;


#endif //VRX_COMMON_H
