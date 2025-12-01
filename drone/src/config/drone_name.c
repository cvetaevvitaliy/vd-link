#include "drone_name.h"
#include "hal/cpuinfo.h"
#include "fc_conn.h"
#include <string.h>
#include <stdio.h>

static char drone_name[128];

const char* get_drone_name(const char* pattern)
{
    memset(drone_name, 0, sizeof(drone_name));
    if (!pattern || pattern[0] == '\0') {
        return NULL;
    }

    int pattern_idx = 0;
    int result_idx = 0;
    for(; pattern[pattern_idx] != '\0' && result_idx < (int)(sizeof(drone_name) - 1); pattern_idx++) {
        if (memcmp(&pattern[pattern_idx], "<cpu_serial>", 12) == 0) {
            const char* cpu_serial = get_cpu_serial_number();
            if (cpu_serial) {
                int copy_len = snprintf(&drone_name[result_idx], sizeof(drone_name) - result_idx, "%s", cpu_serial);
                result_idx += copy_len;
            }
            pattern_idx += 11; // Skip past <cpu_serial>
        } else if (memcmp(&pattern[pattern_idx], "<fc_uid>", 8) == 0) {
            const char* fc_serial = get_device_uid();
            if (fc_serial) {
                int copy_len = snprintf(&drone_name[result_idx], sizeof(drone_name) - result_idx, "%s", fc_serial);
                result_idx += copy_len;
            }
            pattern_idx += 7; // Skip past <fc_uid>
        } else if (memcmp(&pattern[pattern_idx], "<craft_name>", 12) == 0) {
            const char* craft_name = get_craft_name();
            if (craft_name) {
                int copy_len = snprintf(&drone_name[result_idx], sizeof(drone_name) - result_idx, "%s", craft_name);
                result_idx += copy_len;
            }
            pattern_idx += 11; // Skip past <craft_name>
        } else if (memcmp(&pattern[pattern_idx], "<fc_variant>", 12) == 0) {
            const char* fc_variant = get_fc_variant();
            if (fc_variant) {
                int copy_len = snprintf(&drone_name[result_idx], sizeof(drone_name) - result_idx, "%s", fc_variant);
                result_idx += copy_len;
            }
            pattern_idx += 11; // Skip past <fc_variant>
        } else {
            drone_name[result_idx++] = pattern[pattern_idx];
        }
    }
    drone_name[result_idx] = '\0';

    return drone_name;
}