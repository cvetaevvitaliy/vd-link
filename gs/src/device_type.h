#ifndef DEVICE_TYPE_H
#define DEVICE_TYPE_H

#include <stdbool.h>

typedef enum {
    DEVICE_TYPE_UNDEFINED = 0,
    DEVICE_TYPE_RADXA_ZERO,
    DEVICE_TYPE_POWKIDDY_X55,
    DEVICE_TYPE_UNKNOWN
} device_type_t;

device_type_t get_device_type(void);

bool is_battery_supported(void);
bool is_keyboard_supported(void);

#endif // DEVICE_TYPE_H