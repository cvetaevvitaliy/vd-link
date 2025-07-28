#include "menu_navigation.h"
#include "joystick.h"
#include "lvgl.h"
#include "menu.h"
#include "log.h"

static const char* module_name_str = "MENU_NAVIGATION";
extern lv_obj_t * menu;
 lv_obj_t *current_menu_obj = NULL; // Reference to current LVGL menu


void menu_handle_navigation(joystick_button_t button_number)
{
    if (!menu_is_visible()) {
        // Menu not visible, check for menu activation button
        if (button_number == JOYSTICK_BUTTON_START) {
            // menu_toggle();
        }
        return;
    }
    
    // if (!current_menu_obj) return;
    
    bool should_toggle = false;
    
    // pthread_mutex_lock(&menu_mutex);
    
    // Check if dropdown is open first
    // if (handle_dropdown_navigation(button_number)) {
    //     pthread_mutex_unlock(&menu_mutex);
    //     return;
    // }
    
    // Menu is visible, handle navigation
    switch (button_number) {
        case JOYSTICK_BUTTON_UP:
            INFO("Menu UP pressed");
            // menu_navigate_items(false); // Navigate up
           
            break;

        case JOYSTICK_BUTTON_DOWN:
            INFO("Menu DOWN pressed"); 
            //menu_navigate_items(true); // Navigate down
            break;

        case JOYSTICK_BUTTON_LEFT:
            INFO("Menu LEFT pressed");
            //menu_handle_horizontal_navigation(false); // Navigate left
            break;

        case JOYSTICK_BUTTON_RIGHT:
            INFO("Menu RIGHT pressed");
            //menu_handle_horizontal_navigation(true); // Navigate right
            break;

        case JOYSTICK_BUTTON_A: // A button - select/confirm
            INFO("Menu SELECT pressed");
            //menu_activate_current_item();
            break;

        case JOYSTICK_BUTTON_B: // B button - back/cancel
            INFO("Menu BACK pressed");
            // Try to go back in menu, or close if at root
            lv_obj_t* back_btn = lv_menu_get_main_header_back_btn(current_menu_obj);
            if (back_btn && !lv_obj_has_flag(back_btn, LV_OBJ_FLAG_HIDDEN)) {
                INFO("Going back to previous menu page");
                lv_event_send(back_btn, LV_EVENT_CLICKED, NULL);
                // Note: current_page will be updated by menu_page_changed_cb
            } else {
                INFO("At root menu - closing menu");
                should_toggle = true; // Close menu if at root
            }
            break;
            
        case 8: // Select button
        case 9: // Start button
            should_toggle = true;
            break;
    }
    
    //pthread_mutex_unlock(&menu_mutex);
    
    // Call toggle after releasing the mutex
    if (should_toggle) {
        // menu_toggle();
    }
}
