#pragma once

#include <stdint.h>

extern const char* bitrate_values_str;
extern const uint32_t bitrate_values[];

extern const char* codec_values_str;
extern const uint32_t codec_values[];

uint16_t wfb_ng_get_bitrate(void);
void wfb_ng_set_bitrate(uint16_t bitrate);

uint16_t wfb_ng_get_codec(void);
void wfb_ng_set_codec(uint16_t codec);

int32_t wfb_ng_get_gop(void);
void wfb_ng_set_gop(int32_t gop);