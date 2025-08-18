#include "device_type.h"
#include "log.h"
#include <stdio.h>
#include <string.h>


static const char *module_name_str = "DEVICE_TYPE";

device_type_t get_device_type(void)
{
    static device_type_t detected_type = DEVICE_TYPE_UNDEFINED;

    if (detected_type != DEVICE_TYPE_UNDEFINED) {
        return detected_type; // Return cached type if already detected
    }

    FILE *fp = fopen("/proc/device-tree/compatible", "rb");
    if (!fp) {
        ERROR("Failed to open /proc/device-tree/compatible");
        detected_type = DEVICE_TYPE_UNKNOWN;
    }

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    if (n == 0) {
        ERROR("Failed to read /proc/device-tree/compatible");
        detected_type = DEVICE_TYPE_UNKNOWN;
    }

    // The compatible file may contain multiple null-terminated strings
    if (strstr(buf, "radxa,zero3w-aic8800ds2") || strstr(buf, "radxa,zero3") || strstr(buf, "rockchip,rk3566-zero3")) {
        detected_type = DEVICE_TYPE_RADXA_ZERO;
    }
    if (strstr(buf, "rk3566-rk817-tablet") || strstr(buf, "rk3566-firefly-roc-pc") || strstr(buf, "rockchip,rk3566")) {
        detected_type = DEVICE_TYPE_POWKIDDY_X55;
    }

    return detected_type;
}

bool is_battery_supported(void)
{
    device_type_t type = get_device_type();
    if (type == DEVICE_TYPE_POWKIDDY_X55) {
        return true; // Battery is supported on Powkiddy X55
    }
    return false; // Other devices do not support battery
}

bool is_keyboard_supported(void)
{
    device_type_t type = get_device_type();
    if (type == DEVICE_TYPE_POWKIDDY_X55) {
        return true; // Keyboard is supported on Radxa Zero and Powkiddy X55
    }
    return false; // Other devices do not support keyboard
}