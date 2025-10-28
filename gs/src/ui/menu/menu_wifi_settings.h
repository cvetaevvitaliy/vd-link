#ifndef MENU_WIFI_SETTINGS_H
#define MENU_WIFI_SETTINGS_H

#include "lvgl.h"

typedef void (*wifi_connect_callback_t)(const char* ssid);

lv_obj_t *show_menu_wifi_settings(lv_obj_t *parent);
void hide_menu_wifi_settings(void* arg);
void wifi_settings_refresh_networks(void);
void wifi_settings_set_on_connect_cb(wifi_connect_callback_t cb);

#endif // MENU_WIFI_SETTINGS_H