#pragma once

typedef struct {
    float usage_percent;
    float temperature_celsius;
} cpu_info_t;

cpu_info_t get_cpu_info(void);