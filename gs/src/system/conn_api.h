/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CONN_API_H
#define CONN_API_H
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CONN_STATUS_DISCONNECTED = 0,
    CONN_STATUS_CONNECTING,
    CONN_STATUS_CONNECTED,
    CONN_STATUS_ERROR
} conn_status_e;

/**
 * Get current connection parameters.
 * Buffers are always zero-terminated.
 */
void conn_api_get_params(char *ip,    size_t ip_len,
                         char *login, size_t login_len,
                         char *pass,  size_t pass_len,
                         bool *autoconnect);

/**
 * Set (and remember) connection parameters.
 * In real implementation this can also save them to a file.
 */
void conn_api_set_params(const char *ip,
                         const char *login,
                         const char *pass,
                         bool autoconnect);

/**
 * Get current connection status (for status bar / menu).
 */
conn_status_e conn_api_get_status(void);

/**
 * Request connection to the server using stored parameters.
 * Real implementation should start async connect here.
 * UI will poll/notify status via conn_api_get_status().
 */
void conn_api_request_connect(void);
#endif //CONN_API_H
