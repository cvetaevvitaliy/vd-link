#ifndef MENU_WIFI_SETTINGS_H
#define MENU_WIFI_SETTINGS_H

#include "lvgl.h"

lv_obj_t *show_menu_wifi_settings(lv_obj_t *parent);
void hide_menu_wifi_settings(void* arg);
void wifi_settings_refresh_networks(void);

#endif // MENU_WIFI_SETTINGS_H