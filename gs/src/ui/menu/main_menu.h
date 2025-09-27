#pragma once

#include "lvgl.h"

int main_menu_create(lv_obj_t *parent);
void main_menu_destroy(void);
int main_menu_toggle(void);
int main_menu_show(void);
int main_menu_hide(void);