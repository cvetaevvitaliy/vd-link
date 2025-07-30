#include "callbacks.h"
#include "menu_wifi_settings.h"

void wifi_settings_click_handler()
{
    lv_obj_t *wifi_menu = show_menu_wifi_settings(lv_scr_act());
    lv_obj_t *menu = lv_obj_get_parent(wifi_menu);
    if (menu) {
        lv_obj_clear_state(menu, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_obj_invalidate(menu); // Force redraw to remove focus
    }
}