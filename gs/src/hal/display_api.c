#include "display_api.h"
#include <stdio.h>

int32_t display_api_get_brightness(void)
{
    FILE *file = fopen("/sys/class/backlight/backlight/brightness", "r");
    if (file == NULL) {
        return 0;
    }

    int32_t brightness = 0;
    if (fscanf(file, "%d", &brightness) != 1) {
        brightness = 0; // Set default value if read fails
    }
    fclose(file);
    return brightness;
}

void display_api_set_brightness(int32_t brightness)
{
    if (brightness > 255) {
        brightness = 255; // Cap brightness to max value
    }
    if (brightness < 1) {
        brightness = 1; // Avoid setting to 0 which may turn off backlight
    }

    FILE *file = fopen("/sys/class/backlight/backlight/brightness", "w");
    if (file == NULL) {
        return;
    }

    fprintf(file, "%u\n", brightness);
    fclose(file);
}   
