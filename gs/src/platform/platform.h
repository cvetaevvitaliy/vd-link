#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

typedef enum {
    PLATFORM_TYPE_UNDEFINED = 0,
    PLATFORM_TYPE_RADXA_ZERO,
    PLATFORM_TYPE_POWKIDDY_X55,
    PLATFORM_TYPE_UNKNOWN
} platform_type_t;

platform_type_t get_platform_type(void);

bool is_battery_supported(void);
bool is_keyboard_supported(void);

#endif // PLATFORM_H