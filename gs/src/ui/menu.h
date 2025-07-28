#ifndef GS_UI_MENU_H
#define GS_UI_MENU_H

#include "lvgl.h"

void menu_create(lv_obj_t *parent);
void menu_destroy(void);
void menu_toggle(void);
void menu_handle_navigation(int button_number);
void menu_handle_axis(int axis_number, int value);
void menu_show(void);
void menu_hide(void);

#endif // GS_UI_MENU_H