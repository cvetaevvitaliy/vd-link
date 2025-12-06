/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "conn_api.h"
#include <string.h>
#include <stdio.h>

/* Static storage for parameters (stub; no file I/O yet) */
static char         g_ip[64]       = "";
static char         g_login[64]    = "";
static char         g_pass[64]     = "";
static bool         g_autoconnect  = false;
static conn_status_e g_status      = CONN_STATUS_DISCONNECTED;

void conn_api_get_params(char *ip, size_t ip_len, char *login, size_t login_len, char *pass, size_t pass_len, bool *autoconnect)
{
    if (ip && ip_len > 0) {
        strncpy(ip, g_ip, ip_len - 1);
        ip[ip_len - 1] = '\0';
    }

    if (login && login_len > 0) {
        strncpy(login, g_login, login_len - 1);
        login[login_len - 1] = '\0';
    }

    if (pass && pass_len > 0) {
        strncpy(pass, g_pass, pass_len - 1);
        pass[pass_len - 1] = '\0';
    }

    if (autoconnect)
        *autoconnect = g_autoconnect;
}

void conn_api_set_params(const char *ip, const char *login, const char *pass, bool autoconnect)
{
    if (ip) {
        strncpy(g_ip, ip, sizeof(g_ip) - 1);
        g_ip[sizeof(g_ip) - 1] = '\0';
    }

    if (login) {
        strncpy(g_login, login, sizeof(g_login) - 1);
        g_login[sizeof(g_login) - 1] = '\0';
    }

    if (pass) {
        strncpy(g_pass, pass, sizeof(g_pass) - 1);
        g_pass[sizeof(g_pass) - 1] = '\0';
    }

    g_autoconnect = autoconnect;

    /* TODO: optional file save here */
    printf("[CONN_API] Set params: ip='%s' login='%s' autoconnect=%d\n",
           g_ip, g_login, g_autoconnect ? 1 : 0);
}

conn_status_e conn_api_get_status(void)
{
    return g_status;
}

void conn_api_request_connect(void)
{
    /* Stub: just simulate immediate success.
     * Replace with real async connection logic.
     */
    printf("[CONN_API] Request connect to '%s' as '%s'\n", g_ip, g_login);

    if (g_ip[0] == '\0') {
        g_status = CONN_STATUS_ERROR;
    } else {
        g_status = CONN_STATUS_CONNECTED;
    }
}