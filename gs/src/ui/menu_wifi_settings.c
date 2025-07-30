#include "menu.h"
#include "log.h"
#include "input.h"
#include <string.h>

static const char* module_name_str = "MENU WIFI SETTINGS";

lv_obj_t *menu_wifi_settings = NULL;
lv_group_t *wifi_settings_group = NULL;
lv_group_t *current_menu_group = NULL;

void hide_menu_wifi_settings(void);

// Key event handler for WiFi settings navigation
static void wifi_settings_key_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        DEBUG("WiFi settings key pressed: %u", key);
        
        // Handle navigation keys
        if (key == LV_KEY_UP) {
            lv_group_focus_prev(wifi_settings_group);
            DEBUG("Focus moved to previous object");
            return;
        } else if (key == LV_KEY_DOWN) {
            lv_group_focus_next(wifi_settings_group);
            DEBUG("Focus moved to next object");
            return;
        }
        
        // Handle back button (B button or ESC)
        if (key == 7 || key == 11 || key == 27 || key == LV_KEY_ESC) {
            // Don't call hide_menu_wifi_settings directly here to avoid crashes
            // Instead, use lv_async_call to defer the cleanup
            DEBUG("Back action triggered from key press - scheduling cleanup");
            
            // First restore the input group
            if (current_menu_group) {
                ui_set_input_group(current_menu_group);
            }
            
            // Schedule the menu deletion for the next frame
            lv_async_call(hide_menu_wifi_settings, NULL);
            return;
        }
    }
}


void menu_item_click_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
}

void connect_btn_click_handler(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *menu = lv_obj_get_parent(btn);
    
    // Handle connection logic here
    DEBUG("Connect button clicked in WiFi settings menu");
    
    // For example, you can read SSID and password from input fields
    // and initiate connection to the selected WiFi network.
}

void back_btn_click_handler(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *menu = lv_obj_get_parent(btn);
    
    // Hide the WiFi settings menu
    ui_set_input_group(current_menu_group);
    hide_menu_wifi_settings();
    
    // Optionally, you can return to the previous menu or screen
    DEBUG("Back button clicked in WiFi settings menu");
}

lv_obj_t *show_menu_wifi_settings(lv_obj_t *parent)
{
    if (menu_wifi_settings == NULL) {
        // Create the group first
        if (!wifi_settings_group) {
            wifi_settings_group = lv_group_create();
            DEBUG("WiFi settings group created: %p", wifi_settings_group);
        }
        
        menu_wifi_settings = lv_obj_create(parent);
        lv_obj_set_size(menu_wifi_settings, 400, 300);
        lv_obj_set_style_bg_color(menu_wifi_settings, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_wifi_settings, LV_OPA_90, 0);
        lv_obj_set_style_radius(menu_wifi_settings, 10, 0);
        lv_obj_center(menu_wifi_settings);
        
        // Add key handler to the main menu object
        lv_obj_add_event_cb(menu_wifi_settings, wifi_settings_key_handler, LV_EVENT_KEY, NULL);

        lv_obj_t *title = lv_label_create(menu_wifi_settings);
        lv_label_set_text(title, "WiFi Settings");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        // Add WiFi settings UI elements here
        // For example, create a list of available networks, input fields for SSID and password
        // and buttons for connecting/disconnecting.
        lv_obj_t *wifi_list = lv_list_create(menu_wifi_settings);
        lv_obj_set_size(wifi_list, 360, 200);
        lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(wifi_list, lv_color_black(), 0);
        
        // Make the list focusable and add key handler
        lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_list, wifi_settings_key_handler, LV_EVENT_KEY, NULL);

        lv_obj_t *network_item = lv_list_add_btn(wifi_list, NULL, "Network 1");
        lv_obj_add_event_cb(network_item, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(network_item, wifi_settings_key_handler, LV_EVENT_KEY, NULL);
        lv_obj_set_style_border_width(network_item, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(network_item, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(network_item, LV_OPA_100, LV_STATE_FOCUSED);
        lv_obj_add_flag(network_item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(network_item, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        
        lv_obj_t *network_item2 = lv_list_add_btn(wifi_list, NULL, "Network 2");
        lv_obj_add_event_cb(network_item2, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(network_item2, wifi_settings_key_handler, LV_EVENT_KEY, NULL);
        lv_obj_set_style_border_width(network_item2, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(network_item2, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(network_item2, LV_OPA_100, LV_STATE_FOCUSED);
        lv_obj_add_flag(network_item2, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(network_item2, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        
        lv_obj_t *network_item3 = lv_list_add_btn(wifi_list, NULL, "Network 3");
        lv_obj_add_event_cb(network_item3, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(network_item3, wifi_settings_key_handler, LV_EVENT_KEY, NULL);
        lv_obj_set_style_border_width(network_item3, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(network_item3, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(network_item3, LV_OPA_100, LV_STATE_FOCUSED);
        lv_obj_add_flag(network_item3, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(network_item3, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        
        // Add list items to group individually for proper navigation
        if (wifi_settings_group) {
            lv_group_add_obj(wifi_settings_group, network_item);
            lv_group_add_obj(wifi_settings_group, network_item2);
            lv_group_add_obj(wifi_settings_group, network_item3);
        }

        lv_obj_t *connect_btn = lv_button_create(menu_wifi_settings);
        lv_obj_set_size(connect_btn, 100, 40);
        lv_obj_align(connect_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_t *connect_label = lv_label_create(connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_24, 0);
        lv_obj_center(connect_label);
        
        // Add focus styles for connect button
        lv_obj_set_style_border_width(connect_btn, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(connect_btn, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(connect_btn, LV_OPA_100, LV_STATE_FOCUSED);
        lv_obj_add_flag(connect_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(connect_btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        
        if (wifi_settings_group) {
            lv_group_add_obj(wifi_settings_group, connect_btn);
        }
        lv_obj_add_event_cb(connect_btn, connect_btn_click_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(connect_btn, wifi_settings_key_handler, LV_EVENT_KEY, NULL);

        lv_obj_t* back_btn = lv_button_create(menu_wifi_settings);
        lv_obj_set_size(back_btn, 100, 40);
        lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        lv_obj_t *back_label = lv_label_create(back_btn);
        lv_label_set_text(back_label, "Back");
        lv_obj_set_style_text_font(back_label, &lv_font_montserrat_24, 0);
        lv_obj_center(back_label);
        
        // Add focus styles for back button
        lv_obj_set_style_border_width(back_btn, 3, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(back_btn, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_opa(back_btn, LV_OPA_100, LV_STATE_FOCUSED);
        lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        
        if (wifi_settings_group) {
            lv_group_add_obj(wifi_settings_group, back_btn);
        }
        lv_obj_add_event_cb(back_btn, back_btn_click_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(back_btn, wifi_settings_key_handler, LV_EVENT_KEY, NULL);

        // Save current group and switch to wifi settings group
        current_menu_group = menu_get_current_group();
        DEBUG("Current menu group: %p, switching to wifi group: %p", current_menu_group, wifi_settings_group);
        
        if (wifi_settings_group) {
            ui_set_input_group(wifi_settings_group);
            
            // Debug: print all objects in the group
            int count = lv_group_get_obj_count(wifi_settings_group);
            DEBUG("WiFi settings group has %d objects:", count);
            for (int i = 0; i < count; i++) {
                lv_obj_t *obj = lv_group_get_obj_by_index(wifi_settings_group, i);
                DEBUG("  Object %d: %p", i, obj);
            }
            
            lv_group_focus_obj(network_item); // Set focus to the first network item initially
            DEBUG("Focus set to first network item: %p", network_item);
        }
        
    }
    return menu_wifi_settings;
}

void hide_menu_wifi_settings(void)
{
    DEBUG("Hiding WiFi settings menu");
    
    // First clear the group to prevent any further event processing
    if (wifi_settings_group) {
        lv_group_del(wifi_settings_group);
        wifi_settings_group = NULL;
        DEBUG("WiFi settings group deleted");
    }
    
    // Then delete the menu object
    if (menu_wifi_settings != NULL) {
        lv_obj_del(menu_wifi_settings);
        menu_wifi_settings = NULL;
        DEBUG("WiFi settings menu object deleted");
    }
}
