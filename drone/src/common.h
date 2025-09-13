/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CONFIG_H
#define CONFIG_H
#include "../../version.h"

typedef enum {
  CODEC_UNKNOWN = 0,
  CODEC_H264,
  CODEC_H265
} codec_type_t;

struct config_t {
  const char* ip;
  int port;
  codec_type_t codec_type;
  int stream_width;
  int stream_height;
  int stream_bitrate;

};

#endif //CONFIG_H
