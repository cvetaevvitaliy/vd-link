#include "menu.h"
#include "log.h"

static const char *module_name_str = "MENU";
lv_obj_t *menu;
bool menu_visible = false;

void menu_toggle(void)
{
    if (menu_visible) {
        menu_hide();
    } else {
        menu_show();
    }
}

void menu_show(void)
{
    if (!menu) {
        ERROR("Menu not created");
        return;
    }

    lv_obj_clear_flag(menu, LV_OBJ_FLAG_HIDDEN);
    menu_visible = true;
    INFO("Menu shown");
}

void menu_hide(void)
{
    if (!menu) {
        ERROR("Menu not created");
        return;
    }

    lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
    menu_visible = false;
    INFO("Menu hidden");
}

void menu_create(lv_obj_t *parent)
{
    menu = lv_obj_create(parent);
    lv_obj_set_size(menu, 200, 300);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(menu, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, LV_PART_MAIN);
}

void menu_destroy(void)
{
    if (menu) {
        lv_obj_del(menu);
    }
}