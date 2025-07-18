#include "menu.h"
#include <stdio.h>
#include <pthread.h>

// Global menu instance
static menu_t menu = {0};
static pthread_mutex_t menu_mutex = PTHREAD_MUTEX_INITIALIZER;
static lv_disp_t *display = NULL;

/**
 * Initialize menu system
 */
int menu_init(void)
{
    printf("[ MENU ] Initializing menu system...\n");
    
    display = lv_disp_get_default();
    if (!display) {
        printf("[ MENU ] Error: No display found\n");
        return -1;
    }
    
    menu.visible = false;
    menu.current_item = 0;
    menu.item_count = 5;
    menu.background = NULL;
    
    for (int i = 0; i < 5; i++) {
        menu.items[i] = NULL;
    }
    
    printf("[ MENU ] Menu system initialized\n");
    return 0;
}

/**
 * Update menu item selection visual indicator
 */
void menu_update_selection(void)
{
    if (!menu.visible) return;
    
    // Reset all items to normal color
    for (int i = 0; i < menu.item_count; i++) {
        if (menu.items[i]) {
            lv_obj_set_style_text_color(menu.items[i], lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(menu.items[i], LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    
    // Highlight current selection
    if (menu.items[menu.current_item]) {
        lv_obj_set_style_text_color(menu.items[menu.current_item], lv_color_make(255, 255, 0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(menu.items[menu.current_item], lv_color_make(0, 0, 255), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(menu.items[menu.current_item], LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_radius(menu.items[menu.current_item], 5, LV_PART_MAIN);
    }
    
    printf("[ MENU ] Selected item: %d\n", menu.current_item);
}

/**
 * Show/hide menu
 */
void menu_toggle(void)
{
    if (!menu.background) return;
    
    pthread_mutex_lock(&menu_mutex);
    
    menu.visible = !menu.visible;
    
    if (menu.visible) {
        lv_obj_clear_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
        menu.current_item = 0; // Reset to first item
        printf("[ MENU ] Menu shown\n");
    } else {
        lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
        printf("[ MENU ] Menu hidden\n");
    }
    
    pthread_mutex_unlock(&menu_mutex);
    
    // Update selection after releasing the mutex
    if (menu.visible) {
        menu_update_selection();
    }
}

/**
 * Handle menu navigation
 */
void menu_handle_navigation(int button_number)
{
    if (!menu.visible) {
        // Menu not visible, check for menu activation button
        if (button_number == 9) { // Start button
            menu_toggle();
        }
        return;
    }
    
    bool should_toggle = false;
    
    pthread_mutex_lock(&menu_mutex);
    
    // Menu is visible, handle navigation
    switch (button_number) {
        case 13: // UP
            menu.current_item = (menu.current_item - 1 + menu.item_count) % menu.item_count;
            menu_update_selection();
            break;
            
        case 14: // DOWN
            menu.current_item = (menu.current_item + 1) % menu.item_count;
            menu_update_selection();
            break;
            
        case 1: // A button - select/confirm
            printf("[ MENU ] Selected menu item %d\n", menu.current_item);
            if (menu.current_item == 4) { // Exit item
                should_toggle = true;
            } else {
                // Handle other menu items here
                printf("[ MENU ] Executing action for item %d\n", menu.current_item);
                
                // Add specific actions for each menu item
                switch (menu.current_item) {
                    case 0:
                        printf("[ MENU ] Opening Video Settings...\n");
                        break;
                    case 1:
                        printf("[ MENU ] Opening WiFi Settings...\n");
                        break;
                    case 2:
                        printf("[ MENU ] Showing System Info...\n");
                        break;
                    case 3:
                        printf("[ MENU ] Resetting Settings...\n");
                        break;
                }
            }
            break;
            
        case 0: // B button - back/cancel
        case 8: // Select button
        case 9: // Start button
            should_toggle = true;
            break;
    }
    
    pthread_mutex_unlock(&menu_mutex);
    
    // Call toggle after releasing the mutex
    if (should_toggle) {
        menu_toggle();
    }
}

/**
 * Handle axis movement for menu navigation
 */
void menu_handle_axis(int axis_number, int value)
{
    if (!menu.visible) return;
    
    if (axis_number == 1) { // Y-axis (up/down)
        pthread_mutex_lock(&menu_mutex);
        
        if (value < -16000) { // Up
            menu.current_item = (menu.current_item - 1 + menu.item_count) % menu.item_count;
            menu_update_selection();
        } else if (value > 16000) { // Down
            menu.current_item = (menu.current_item + 1) % menu.item_count;
            menu_update_selection();
        }
        
        pthread_mutex_unlock(&menu_mutex);
    }
}

/**
 * Check if menu is visible
 */
bool menu_is_visible(void)
{
    return menu.visible;
}

/**
 * Create menu UI
 */
void menu_create_ui(void)
{
    if (!display) {
        printf("[ MENU ] Error: Display not initialized\n");
        return;
    }
    
    pthread_mutex_lock(&menu_mutex);
    
    printf("[ MENU ] Creating menu UI...\n");

    // Get the screen dimensions
    lv_coord_t width = lv_disp_get_hor_res(display);
    lv_coord_t height = lv_disp_get_ver_res(display);

    printf("[ MENU ] Creating menu UI for screen %dx%d\n", (int)width, (int)height);

    // Create a semi-transparent background for the menu
    menu.background = lv_obj_create(lv_scr_act());
    lv_obj_set_size(menu.background, width - 40, height - 80);
    lv_obj_align(menu.background, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(menu.background, lv_color_make(0, 0, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu.background, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu.background, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(menu.background, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(menu.background, 10, LV_PART_MAIN);
    
    // Hide menu initially
    lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
    menu.visible = false;
    
    // Add a title label
    lv_obj_t *title_label = lv_label_create(menu.background);
    lv_label_set_text(title_label, "Settings Menu");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Add instruction label
    lv_obj_t *instruction_label = lv_label_create(menu.background);
    lv_label_set_text(instruction_label, "Use UP/DOWN to navigate, A to select, B to exit");
    lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(instruction_label, lv_color_make(200, 200, 200), LV_PART_MAIN);
    lv_obj_align(instruction_label, LV_ALIGN_TOP_MID, 0, 50);
    
    // Add menu items and store references
    const char *menu_items_text[] = {"Video Settings", "WiFi Settings", "System Info", "Reset Settings", "Exit"};
    
    for (int i = 0; i < menu.item_count; i++) {
        menu.items[i] = lv_label_create(menu.background);
        lv_label_set_text(menu.items[i], menu_items_text[i]);
        lv_obj_set_style_text_font(menu.items[i], &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_align(menu.items[i], LV_ALIGN_TOP_MID, 0, 80 + i * 40);
        lv_obj_set_style_text_color(menu.items[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(menu.items[i], LV_OPA_TRANSP, LV_PART_MAIN);
        
        // Add padding to menu items
        lv_obj_set_style_pad_all(menu.items[i], 8, LV_PART_MAIN);
    }

    printf("[ MENU ] Menu UI created successfully\n");
    pthread_mutex_unlock(&menu_mutex);
}

/**
 * Cleanup menu
 */
void menu_deinit(void)
{
    pthread_mutex_lock(&menu_mutex);
    
    if (menu.background) {
        lv_obj_del(menu.background);
        menu.background = NULL;
    }
    
    for (int i = 0; i < 5; i++) {
        menu.items[i] = NULL; // Objects are deleted with background
    }
    
    menu.visible = false;
    menu.current_item = 0;
    
    pthread_mutex_unlock(&menu_mutex);
    pthread_mutex_destroy(&menu_mutex);
    
    printf("[ MENU ] Menu system deinitialized\n");
}
