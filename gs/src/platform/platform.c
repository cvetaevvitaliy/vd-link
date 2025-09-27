#include "platform.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static const char *module_name_str = "PLATFORM";

struct platform_capabilities {
    platform_type_t type;
    bool battery;
    bool keyboard;
};

static struct platform_capabilities platform_capabilities[] = {
    {.type = PLATFORM_TYPE_UNDEFINED,    .battery = false, .keyboard = false},
    {.type = PLATFORM_TYPE_RADXA_ZERO,   .battery = false, .keyboard = true },
    {.type = PLATFORM_TYPE_POWKIDDY_X55, .battery = true,  .keyboard = true },
    {.type = PLATFORM_TYPE_UNKNOWN,      .battery = false, .keyboard = false}
};

platform_type_t get_platform_type(void)
{
    static platform_type_t detected_type = PLATFORM_TYPE_UNDEFINED;

    if (detected_type != PLATFORM_TYPE_UNDEFINED) {
        return detected_type; // Return cached type if already detected
    }

    FILE *fp = fopen("/proc/device-tree/compatible", "rb");
    if (!fp) {
        ERROR("Failed to open /proc/device-tree/compatible");
        detected_type = PLATFORM_TYPE_UNKNOWN;
    }

    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    if (n == 0) {
        ERROR("Failed to read /proc/device-tree/compatible");
        detected_type = PLATFORM_TYPE_UNKNOWN;
    }

    // The compatible file may contain multiple null-terminated strings
    if (strstr(buf, "radxa,zero3w-aic8800ds2") || strstr(buf, "radxa,zero3") || strstr(buf, "rockchip,rk3566-zero3")) {
        detected_type = PLATFORM_TYPE_RADXA_ZERO;
    }
    if (strstr(buf, "rk3566-rk817-tablet") || strstr(buf, "rk3566-firefly-roc-pc") || strstr(buf, "rockchip,rk3566")) {
        detected_type = PLATFORM_TYPE_POWKIDDY_X55;
    }

    return detected_type;
}

bool is_battery_supported(void)
{
    platform_type_t type = get_platform_type();
    return platform_capabilities[type].battery;
}

bool is_keyboard_supported(void)
{
    platform_type_t type = get_platform_type();
    return platform_capabilities[type].keyboard;
}