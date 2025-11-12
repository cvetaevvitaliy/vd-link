#pragma once
#include <stdint.h>

int start_detection_stream(uint32_t stream_width, uint32_t stream_height, uint32_t fps);
void stop_detection_stream();
