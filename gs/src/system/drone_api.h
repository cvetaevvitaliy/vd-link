/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef DRONE_API_H
#define DRONE_API_H
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DRONE_STATUS_OFFLINE = 0,
    DRONE_STATUS_ONLINE
} drone_status_e;

typedef struct {
    char            id[32];      /* DroneID string */
    drone_status_e  status;      /* online / offline */
    bool            rc_on;       /* RC state for that drone */
    bool            is_active;   /* currently connected */
} drone_info_t;

/* Fill array with current drones, return count (<= max) */
int drone_api_get_list(drone_info_t *out, int max_cnt);
int drone_api_get_count(void);

/* Connect / disconnect to specific drone by id */
void drone_api_connect(const char *drone_id);
void drone_api_disconnect(const char *drone_id);

/* Global RC control (bottom On/Off switch) */
bool drone_api_get_rc_enabled(void);
void drone_api_set_rc_enabled(bool enabled);

void drone_api_set_active(const char *id);
void drone_api_clear_active(void);
const char *drone_api_get_active_id(void);

#endif //DRONE_API_H
