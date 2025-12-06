/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef LANG_H
#define LANG_H
#include "lvgl/lvgl.h"

typedef enum {
    STR_HELLO,
    STR_GOODBYE,
    STR_START,
    STR_STOP,
    STR_BATT,
    STR_STATUS,
    STR_VIDEO,
    STR_SERVER_PING,
    STR_RSSI,
    STR_DRONES,
    STR_ACTIVATE_DRONE_CONTROL,
    STR_MENU_SERVER_IP,
    STR_MENU_LOGIN,
    STR_MENU_PASSWORD,
    STR_MENU_SHOW_PASSWORD,
    STR_MENU_AUTOCONNECT,
    STR_MENU_CONNECT,
    STR_MENU_DISCONNECT,
    STR_MENU_CONN_CONNECTED,
    STR_MENU_CONN_CONNECTING,
    STR_MENU_CONN_ERROR,
    STR_COUNT
} lang_key_t;

// Ukrainian
static const char *lang_ua[STR_COUNT] = {
    [STR_HELLO]     = "Привіт",
    [STR_GOODBYE]   = "До побачення",
    [STR_START]     = "Пуск",
    [STR_STOP]      = "Стоп",
    [STR_BATT]      = "Батарея",
    [STR_STATUS]    = "Статус",
    [STR_VIDEO]     = "Відео",
    [STR_SERVER_PING] = "Пінг сервера",
    [STR_RSSI]     = "RSSI",
    [STR_DRONES]   = "Список дронів",
    [STR_ACTIVATE_DRONE_CONTROL] = "Активувати керування дроном (RC)",
    [STR_MENU_SERVER_IP]      = "IP сервера:",
    [STR_MENU_LOGIN]          = "Логін:",
    [STR_MENU_PASSWORD]       = "Пароль:",
    [STR_MENU_SHOW_PASSWORD]  = "Показати пароль",
    [STR_MENU_AUTOCONNECT]    = "Автопідключення",
    [STR_MENU_CONNECT]        = "Підключитися",
    [STR_MENU_DISCONNECT]     = "Відключитися",
    [STR_MENU_CONN_CONNECTED] = "Підключено",
    [STR_MENU_CONN_CONNECTING]= "Підключення...",
    [STR_MENU_CONN_ERROR]     = "Помилка з'єднання",
};

// English
static const char *lang_en[STR_COUNT] = {
    [STR_HELLO]     = "Hello",
    [STR_GOODBYE]   = "Goodbye",
    [STR_START]     = "Start",
    [STR_STOP]      = "Stop",
    [STR_BATT]      = "Battery",
    [STR_STATUS]    = "Status",
    [STR_VIDEO]     = "Video",
    [STR_SERVER_PING] = "Server Ping",
    [STR_RSSI]     = "RSSI",
    [STR_DRONES]   = "Drones List",
    [STR_ACTIVATE_DRONE_CONTROL] = "Activate Drone Control",
    [STR_MENU_SERVER_IP]      = "Server IP:",
    [STR_MENU_LOGIN]          = "Login:",
    [STR_MENU_PASSWORD]       = "Password:",
    [STR_MENU_SHOW_PASSWORD]  = "Show password",
    [STR_MENU_AUTOCONNECT]    = "Autoconnect",
    [STR_MENU_CONNECT]        = "Connect",
    [STR_MENU_DISCONNECT]     = "Disconnect",
    [STR_MENU_CONN_CONNECTED] = "Connected",
    [STR_MENU_CONN_CONNECTING]= "Connecting...",
    [STR_MENU_CONN_ERROR]     = "Connection error",
};

void lang_set_ukrainian(void);
void lang_set_english(void);
const char *lang_get_str(lang_key_t key);
void lang_switch_cb(lv_event_t *e);

#endif //LANG_H
