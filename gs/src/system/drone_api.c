/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "drone_api.h"

#include <stdio.h>
#include <string.h>

// for testing purposes only
static drone_info_t drone_info[] = {
    { .id = "DRONE-01", .status = DRONE_STATUS_ONLINE,  .rc_on = false,  .is_active = false  },
    { .id = "DRONE-02", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-03", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-04", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-05", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-06", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-07", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-08", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-09", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-10", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-11", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-12", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-13", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-14", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-15", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-16", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-17", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-18", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-19", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },
    { .id = "DRONE-20", .status = DRONE_STATUS_OFFLINE, .rc_on = false, .is_active = false },

};

int drone_api_get_list(drone_info_t *out, int max_cnt)
{
    if (!out || max_cnt <= 0) return 0;

    /* Dummy data for now. */
    int size_test_list = (sizeof(drone_info) / sizeof(drone_info[0]));
    int n = (max_cnt > size_test_list) ? size_test_list : max_cnt;

    memcpy(out, drone_info, n * sizeof(drone_info_t));

    return n;
}

int drone_api_get_count(void)
{
    return sizeof(drone_info) / sizeof(drone_info[0]);
}

/* In real code these should control actual connection/RC. */
void drone_api_set_active(const char *id)
{
    if (!id || strlen(id) == 0) return;
    for (int i = 0; i < (int)(sizeof(drone_info) / sizeof(drone_info[0])); ++i) {
        if (strcmp(drone_info[i].id, id) == 0) {
            drone_info[i].is_active = true;
            printf("[ Drone API ] Set active drone ID: %s\n", drone_info[i].id);
        } else {
            drone_info[i].is_active = false;
            drone_info[i].rc_on = false;
            //printf("[ Drone API ] Set active drone ID: %s\n", drone_info[i].id);
        }
    }
}
void drone_api_clear_active(void)
{
    for (int i = 0; i < (int)(sizeof(drone_info) / sizeof(drone_info[0])); ++i) {
        if (drone_info[i].is_active) {
            drone_info[i].is_active = false;
            drone_info[i].rc_on = false;
            printf("[ Drone API ] Cleared active drone ID: %s\n", drone_info[i].id);
        }
    }
}
const char *drone_api_get_active_id(void)
{
    for (int i = 0; i < (int)(sizeof(drone_info) / sizeof(drone_info[0])); ++i) {
        if (drone_info[i].is_active) {
            //printf("[ Drone API ] Active drone ID: %s\n", drone_info[i].id);
            return drone_info[i].id;
        }
    }
    return "\0";
}
bool drone_api_get_rc_enabled(void)
{
    for (int i = 0; i < (int)(sizeof(drone_info) / sizeof(drone_info[0])); ++i) {
        if (drone_info[i].is_active) {
            printf("[ Drone API ] Get RC state for drone [%s]: %s\n", drone_info[i].id,
                   drone_info[i].rc_on ? "ENABLED" : "DISABLED");
            return drone_info[i].rc_on;
        }
    }
    return false;
}
void drone_api_set_rc_enabled(bool enabled)
{
    for (int i = 0; i < (int)(sizeof(drone_info) / sizeof(drone_info[0])); ++i) {
        if (drone_info[i].is_active) {
            drone_info[i].rc_on = enabled;
            printf("[ Drone API ] %s RC for drone [%s]\n", enabled ? "Enable": "Disable", drone_info[i].id);
            return;
        }
    }
    printf("[ Drone API ] No found drone to enable RC\n");
}