#include "menu.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include "../log.h"

static const char *module_name_str = "MENU";

// Forward declarations
static void menu_navigate_items(bool down);
static void menu_activate_current_item(void);
static void menu_page_changed_cb(lv_event_t * e);
static void save_page_position(lv_obj_t *page, int position);
static int get_page_position(lv_obj_t *page);
static void menu_navigate_to_position(int position);
static void menu_handle_horizontal_navigation(bool right);
static void dropdown_event_cb(lv_event_t * e);
static bool handle_dropdown_navigation(int button);

// Global menu instance
static menu_t menu = {0};
static pthread_mutex_t menu_mutex = PTHREAD_MUTEX_INITIALIZER;
static lv_disp_t *display = NULL;
static lv_obj_t *current_menu_obj = NULL; // Reference to current LVGL menu
static lv_obj_t *current_page = NULL; // Current active page
static lv_obj_t *main_page = NULL; // Main menu page reference
static lv_obj_t *open_dropdown = NULL; // Reference to currently open dropdown

// Page position tracking
#define MAX_PAGES 10
static struct {
    lv_obj_t *page;
    int position;
} page_positions[MAX_PAGES];
static int page_count = 0;

/**
 * Initialize menu system
 */
int menu_init(void)
{
    INFO("Initializing menu system...");
    
    display = lv_disp_get_default();
    if (!display) {
        ERROR("No display found");
        return -1;
    }
    
    menu.visible = false;
    menu.current_item = 0;
    menu.item_count = 5;
    menu.background = NULL;
    
    for (int i = 0; i < 5; i++) {
        menu.items[i] = NULL;
    }
    
    INFO("Menu system initialized");
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
    
    INFO("Selected item: %d", menu.current_item);
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
        menu.visible = true;
        INFO("Menu shown");
    } else {
        lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
        menu.visible = false;
        INFO("Menu hidden");
    }
    
    pthread_mutex_unlock(&menu_mutex);
    
    // Update selection after releasing the mutex
    if (menu.visible) {
        menu_update_selection();
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
 * Handle horizontal navigation for sliders and other controls
 */
static void menu_handle_horizontal_navigation(bool right)
{
    if (!current_page) return;
    
    // Find currently focused item
    uint32_t child_count = lv_obj_get_child_cnt(current_page);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(current_page, i);
        if (lv_obj_has_state(child, LV_STATE_FOCUSED)) {
            // Check if this container has any interactive controls
            uint32_t container_child_count = lv_obj_get_child_cnt(child);
            for (uint32_t j = 0; j < container_child_count; j++) {
                lv_obj_t *control = lv_obj_get_child(child, j);
                
                if (lv_obj_check_type(control, &lv_slider_class)) {
                    // Adjust slider value directly
                    int32_t current_value = lv_slider_get_value(control);
                    int32_t min_value = lv_slider_get_min_value(control);
                    int32_t max_value = lv_slider_get_max_value(control);
                    int32_t step = (max_value - min_value) / 20; // 5% steps
                    if (step < 1) step = 1;
                    
                    int32_t new_value;
                    if (right) {
                        new_value = current_value + step;
                        if (new_value > max_value) new_value = max_value;
                    } else {
                        new_value = current_value - step;
                        if (new_value < min_value) new_value = min_value;
                    }
                    
                    lv_slider_set_value(control, new_value, LV_ANIM_OFF);
                    
                    // Update label if present - find label in container
                    for (uint32_t k = 0; k < container_child_count; k++) {
                        lv_obj_t *sibling = lv_obj_get_child(child, k);
                        if (lv_obj_check_type(sibling, &lv_label_class)) {
                            char buf[64];
                            const char* label_text = lv_label_get_text(sibling);
                            
                            if (strstr(label_text, "Brightness")) {
                                snprintf(buf, sizeof(buf), "Brightness: %d", (int)new_value);
                            } else if (strstr(label_text, "Contrast")) {
                                snprintf(buf, sizeof(buf), "Contrast: %d", (int)new_value);
                            } else if (strstr(label_text, "Saturation")) {
                                snprintf(buf, sizeof(buf), "Saturation: %d", (int)new_value);
                            } else if (strstr(label_text, "Sharpness")) {
                                snprintf(buf, sizeof(buf), "Sharpness: %d", (int)new_value);
                            } else {
                                // Generic label update
                                continue;
                            }
                            lv_label_set_text(sibling, buf);
                            break;
                        }
                    }
                    
                    INFO("Slider value changed to: %d", (int)new_value);
                    return;
                } else if (lv_obj_check_type(control, &lv_dropdown_class)) {
                    // Navigate dropdown options only if closed
                    if (!lv_dropdown_is_open(control)) {
                        uint16_t current_option = lv_dropdown_get_selected(control);
                        uint16_t option_count = lv_dropdown_get_option_cnt(control);
                        
                        uint16_t new_option;
                        if (right) {
                            new_option = (current_option + 1) % option_count;
                        } else {
                            new_option = (current_option - 1 + option_count) % option_count;
                        }
                        
                        lv_dropdown_set_selected(control, new_option);
                        INFO("Dropdown option changed to: %d", new_option);
                    }
                    return;
                }
            }
            return;
        }
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
    
    if (!current_menu_obj) return;
    
    bool should_toggle = false;
    
    pthread_mutex_lock(&menu_mutex);
    
    // Check if dropdown is open first
    if (handle_dropdown_navigation(button_number)) {
        pthread_mutex_unlock(&menu_mutex);
        return;
    }
    
    // Menu is visible, handle navigation
    switch (button_number) {
        case 13: // UP
            INFO("Menu UP pressed");
            menu_navigate_items(false); // Navigate up
            break;
            
        case 14: // DOWN
            INFO("Menu DOWN pressed"); 
            menu_navigate_items(true); // Navigate down
            break;
            
        case 11: // LEFT
            INFO("Menu LEFT pressed");
            menu_handle_horizontal_navigation(false); // Navigate left
            break;
            
        case 12: // RIGHT
            INFO("Menu RIGHT pressed");
            menu_handle_horizontal_navigation(true); // Navigate right
            break;
            
        case 1: // A button - select/confirm
            INFO("Menu SELECT pressed");
            menu_activate_current_item();
            break;
            
        case 0: // B button - back/cancel
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
    if (!menu.visible || !current_menu_obj) return;
    
    pthread_mutex_lock(&menu_mutex);
    
    if (axis_number == 1) { // Y-axis (up/down)
        if (value < -16000) { // Up
            INFO("Menu axis UP");
            menu_navigate_items(false);
        } else if (value > 16000) { // Down
            INFO("Menu axis DOWN");
            menu_navigate_items(true);
        }
    } else if (axis_number == 0) { // X-axis (left/right)
        if (value < -16000) { // Left
            INFO("Menu axis LEFT");
            menu_handle_horizontal_navigation(false);
        } else if (value > 16000) { // Right
            INFO("Menu axis RIGHT");
            menu_handle_horizontal_navigation(true);
        }
    }
    
    pthread_mutex_unlock(&menu_mutex);
}

/**
 * Navigate to next/previous menu item programmatically
 */
static void menu_navigate_items(bool down)
{
    if (!current_page) {
        INFO("No current page for navigation");
        return;
    }
    
    // Get all container children (menu items) from current page
    uint32_t child_count = lv_obj_get_child_cnt(current_page);
    if (child_count == 0) {
        INFO("No children in current page");
        return;
    }
    
    // Find currently focused item or use first item
    lv_obj_t *focused_item = NULL;
    int current_index = -1;
    
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(current_page, i);
        if (lv_obj_has_state(child, LV_STATE_FOCUSED) || 
            lv_obj_has_state(child, LV_STATE_PRESSED)) {
            focused_item = child;
            current_index = i;
            break;
        }
    }
    
    // If no item is focused, start with first item
    if (current_index == -1) {
        current_index = 0;
    } else {
        // Move to next/previous item only if we're navigating
        if (down) {
            current_index = (current_index + 1) % child_count;
        } else {
            // For up navigation, if current_index is -1, keep it as 0
            current_index = (current_index - 1 + child_count) % child_count;
        }
    }
    
    // Use the navigate to position function
    menu_navigate_to_position(current_index);
    
    // Save the new position
    save_page_position(current_page, current_index);
}

/**
 * Activate currently selected menu item
 */
static void menu_activate_current_item(void)
{
    if (!current_page) return;
    
    // Find currently focused item
    uint32_t child_count = lv_obj_get_child_cnt(current_page);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(current_page, i);
        if (lv_obj_has_state(child, LV_STATE_FOCUSED)) {
            INFO("Activating menu item %d", (int)i);
            
            // Check if this container has any interactive controls
            uint32_t container_child_count = lv_obj_get_child_cnt(child);
            for (uint32_t j = 0; j < container_child_count; j++) {
                lv_obj_t *control = lv_obj_get_child(child, j);
                
                // Check control type and activate accordingly
                if (lv_obj_check_type(control, &lv_dropdown_class)) {
                    INFO("Activating dropdown");
                    // Send click event to dropdown to open it naturally
                    lv_event_send(control, LV_EVENT_CLICKED, NULL);
                    return;
                } else if (lv_obj_check_type(control, &lv_checkbox_class)) {
                    INFO("Toggling checkbox");
                    if (lv_obj_get_state(control) & LV_STATE_CHECKED) {
                        lv_obj_clear_state(control, LV_STATE_CHECKED);
                    } else {
                        lv_obj_add_state(control, LV_STATE_CHECKED);
                    }
                    return;
                } else if (lv_obj_check_type(control, &lv_slider_class)) {
                    INFO("Slider focused - use left/right to adjust");
                    // For sliders, we can add special handling later
                    return;
                }
            }
            
            // If no interactive controls found, send click event to the container
            lv_event_send(child, LV_EVENT_CLICKED, NULL);
            return;
        }
    }
    
    // If no item is focused, activate the first item
    if (child_count > 0) {
        lv_obj_t *first_item = lv_obj_get_child(current_page, 0);
        lv_event_send(first_item, LV_EVENT_CLICKED, NULL);
    }
}

/**
 * Menu page changed callback - update current page reference
 */
static void menu_page_changed_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* menu_obj = lv_event_get_target(e);
        lv_obj_t* new_page = lv_menu_get_cur_main_page(menu_obj);
        
        if (new_page && new_page != current_page) {
            INFO("Menu page changed");
            
            // Save current position of old page
            if (current_page) {
                // Find currently focused item
                uint32_t child_count = lv_obj_get_child_cnt(current_page);
                int current_pos = 0;
                for (uint32_t i = 0; i < child_count; i++) {
                    lv_obj_t *child = lv_obj_get_child(current_page, i);
                    if (lv_obj_has_state(child, LV_STATE_FOCUSED)) {
                        current_pos = i;
                        break;
                    }
                }
                save_page_position(current_page, current_pos);
                INFO("Saved position %d for previous page", current_pos);
            }
            
            // Switch to new page
            current_page = new_page;
            
            // Restore position for new page
            int saved_position = get_page_position(new_page);
            INFO("Restoring position %d for new page", saved_position);
            menu_navigate_to_position(saved_position);
        }
    }
}

/**
 * Save position for a specific page
 */
static void save_page_position(lv_obj_t *page, int position)
{
    // Look for existing entry
    for (int i = 0; i < page_count; i++) {
        if (page_positions[i].page == page) {
            page_positions[i].position = position;
            return;
        }
    }
    
    // Add new entry if space available
    if (page_count < MAX_PAGES) {
        page_positions[page_count].page = page;
        page_positions[page_count].position = position;
        page_count++;
    }
}

/**
 * Get saved position for a specific page
 */
static int get_page_position(lv_obj_t *page)
{
    for (int i = 0; i < page_count; i++) {
        if (page_positions[i].page == page) {
            return page_positions[i].position;
        }
    }
    return 0; // Default to first position
}

/**
 * Navigate to specific position in current page
 */
static void menu_navigate_to_position(int position)
{
    if (!current_page) {
        INFO("No current page for navigation");
        return;
    }
    
    // Get all container children (menu items) from current page
    uint32_t child_count = lv_obj_get_child_cnt(current_page);
    if (child_count == 0) {
        INFO("No children in current page");
        return;
    }
    
    // Clamp position to valid range
    if (position >= (int)child_count) {
        position = child_count - 1;
    }
    if (position < 0) {
        position = 0;
    }
    
    // Clear focus from all items
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *child = lv_obj_get_child(current_page, i);
        lv_obj_clear_state(child, LV_STATE_FOCUSED);
        lv_obj_clear_state(child, LV_STATE_PRESSED);
        // Reset styling
        lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    
    // Focus on selected item
    lv_obj_t *selected_item = lv_obj_get_child(current_page, position);
    if (selected_item) {
        lv_obj_add_state(selected_item, LV_STATE_FOCUSED);
        // Add visual highlight
        lv_obj_set_style_bg_color(selected_item, lv_color_make(50, 50, 100), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(selected_item, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_radius(selected_item, 5, LV_PART_MAIN);
        
        INFO("Menu navigation: positioned at item %d of %d", position, (int)child_count);
    }
}

/**
 * Dropdown event handler
 */
static void dropdown_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * dropdown = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        // Only track state, don't interfere with natural dropdown behavior
        if (lv_dropdown_is_open(dropdown)) {
            INFO("Dropdown opened");
            open_dropdown = dropdown;
        } else {
            INFO("Dropdown closed");
            open_dropdown = NULL;
        }
    } else if (code == LV_EVENT_CANCEL) {
        INFO("Dropdown cancelled");
        open_dropdown = NULL;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        INFO("Dropdown value changed");
        open_dropdown = NULL;
    }
}

/**
 * Navigate dropdown options when dropdown is open
 */
static bool handle_dropdown_navigation(int button)
{
    if (!open_dropdown) return false;
    
    // Check if dropdown is still open
    if (!lv_dropdown_is_open(open_dropdown)) {
        open_dropdown = NULL;
        return false;
    }
    
    if (button == 13) { // UP
        uint16_t current = lv_dropdown_get_selected(open_dropdown);
        uint16_t count = lv_dropdown_get_option_cnt(open_dropdown);
        uint16_t new_sel = (current - 1 + count) % count;
        lv_dropdown_set_selected(open_dropdown, new_sel);
        INFO("Dropdown UP: %d -> %d", current, new_sel);
        return true;
    } else if (button == 14) { // DOWN
        uint16_t current = lv_dropdown_get_selected(open_dropdown);
        uint16_t count = lv_dropdown_get_option_cnt(open_dropdown);
        uint16_t new_sel = (current + 1) % count;
        lv_dropdown_set_selected(open_dropdown, new_sel);
        INFO("Dropdown DOWN: %d -> %d", current, new_sel);
        return true;
    } else if (button == 1) { // A - select
        lv_dropdown_close(open_dropdown);
        open_dropdown = NULL;
        INFO("Dropdown selected and closed");
        return true;
    } else if (button == 0) { // B - cancel
        lv_dropdown_close(open_dropdown);
        open_dropdown = NULL;
        INFO("Dropdown cancelled and closed");
        return true;
    }
    
    return false;
}

/**
 * Menu button event handler
 */
static void menu_btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        const char * txt = lv_label_get_text(label);
        
        INFO("Menu button clicked: %s", txt);
        
        if (strstr(txt, "Exit")) {
            INFO("Exit button clicked - hiding menu");
            // Don't call menu_toggle directly to avoid mutex deadlock
            // Instead, set a flag or use a different approach
            if (menu.background) {
                lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
                menu.visible = false;
                INFO("Menu hidden");
            }
        } else if (strstr(txt, "Reset Settings")) {
            INFO("Reset Settings clicked");
            // Add reset functionality here
        } else if (strstr(txt, "Resolution") || strstr(txt, "1280x720") || strstr(txt, "1920x1080") || strstr(txt, "3840x2160")) {
            INFO("Resolution setting clicked: %s", txt);
            // Add resolution change functionality
            if (strstr(txt, "720p")) {
                INFO("Setting resolution to 720p");
            } else if (strstr(txt, "1080p")) {
                INFO("Setting resolution to 1080p (current)");
            } else if (strstr(txt, "4K")) {
                INFO("Setting resolution to 4K");
            }
        } else if (strstr(txt, "Bitrate")) {
            INFO("Bitrate setting clicked");
            // Add bitrate change functionality
        } else if (strstr(txt, "FPS")) {
            INFO("FPS setting clicked");
            // Add FPS change functionality
        } else if (strstr(txt, "SSID")) {
            INFO("SSID setting clicked");
            // Add SSID change functionality
        } else if (strstr(txt, "Channel")) {
            INFO("Channel setting clicked");
            // Add channel change functionality
        } else if (strstr(txt, "TX Power")) {
            INFO("TX Power setting clicked");
            // Add power setting functionality
        } else {
            INFO("Unknown menu item clicked: %s", txt);
        }
    }
}

/**
 * Create menu UI
 */
void menu_create_ui(void)
{
    if (!display) {
        ERROR("Display not initialized");
        return;
    }
    
    pthread_mutex_lock(&menu_mutex);
    
    INFO("Creating menu UI...");

    // Get the screen dimensions
    lv_coord_t width = lv_disp_get_hor_res(display);
    lv_coord_t height = lv_disp_get_ver_res(display);
    
    // INFO("Creating menu UI for screen %dx%d", (int)width, (int)height);    // Create a semi-transparent background for the menu
    // menu.background = lv_obj_create(lv_scr_act());
    // lv_obj_set_size(menu.background, width - 40, height - 80);
    // lv_obj_align(menu.background, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_bg_color(menu.background, lv_color_make(0, 0, 255), LV_PART_MAIN);
    // lv_obj_set_style_bg_opa(menu.background, LV_OPA_0, LV_PART_MAIN);
    // lv_obj_set_style_border_width(menu.background, 1, LV_PART_MAIN);
    // lv_obj_set_style_border_color(menu.background, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_radius(menu.background, 10, LV_PART_MAIN);
    
    // // Hide menu initially
    // lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
    menu.visible = false;
    
    // // Add a title label
    // lv_obj_t *title_label = lv_label_create(menu.background);
    // lv_label_set_text(title_label, "Settings Menu");
    // lv_obj_set_style_text_font(title_label, &lv_font_montserrat_40, LV_PART_MAIN);
    // lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    // lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *menu1 =  lv_menu_create(lv_scr_act());
    menu.background = menu1; // Store reference to menu object
    lv_obj_add_flag(menu.background, LV_OBJ_FLAG_HIDDEN);
    current_menu_obj = menu1; // Store reference for navigation
    lv_obj_set_size(menu1, width - 60, height - 120);
    lv_obj_align(menu1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(menu1, lv_color_make(20, 20, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu1, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu1, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(menu1, lv_color_make(100, 100, 150), LV_PART_MAIN);
    lv_obj_set_style_radius(menu1, 8, LV_PART_MAIN);
    
    // Enable key processing for navigation
    lv_obj_add_flag(menu1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(menu1, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add page change event handler
    lv_obj_add_event_cb(menu1, menu_page_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Create main menu page
    lv_obj_t* main_page_obj = lv_menu_page_create(menu1, "Main Menu");
    lv_obj_set_style_bg_color(main_page_obj, lv_color_make(20, 20, 30), LV_PART_MAIN);
    main_page = main_page_obj; // Store reference
    current_page = main_page_obj; // Set as current page
    
    // Create sub-pages
    lv_obj_t* video_page = lv_menu_page_create(menu1, "Video Settings");
    lv_obj_set_style_bg_color(video_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    lv_obj_t* wifi_page = lv_menu_page_create(menu1, "WiFi Settings");
    lv_obj_set_style_bg_color(wifi_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    lv_obj_t* system_page = lv_menu_page_create(menu1, "System Settings");
    lv_obj_set_style_bg_color(system_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    lv_obj_t* display_page = lv_menu_page_create(menu1, "Display Settings");
    lv_obj_set_style_bg_color(display_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    lv_obj_t* about_page = lv_menu_page_create(menu1, "About");
    lv_obj_set_style_bg_color(about_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create ISP settings sub-page
    lv_obj_t* isp_page = lv_menu_page_create(menu1, "ISP Settings");
    lv_obj_set_style_bg_color(isp_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create paired devices sub-page
    lv_obj_t* paired_devices_page = lv_menu_page_create(menu1, "Paired Devices");
    lv_obj_set_style_bg_color(paired_devices_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create add device sub-page
    lv_obj_t* add_device_page = lv_menu_page_create(menu1, "Add Device");
    lv_obj_set_style_bg_color(add_device_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create key bindings sub-page
    lv_obj_t* key_bindings_page = lv_menu_page_create(menu1, "Key Bindings");
    lv_obj_set_style_bg_color(key_bindings_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create WFB Key sub-page
    lv_obj_t* wfb_key_page = lv_menu_page_create(menu1, "WFB Key");
    lv_obj_set_style_bg_color(wfb_key_page, lv_color_make(20, 20, 30), LV_PART_MAIN);
    
    // Create Record Directory sub-page
    lv_obj_t* record_dir_page = lv_menu_page_create(menu1, "Record Directory");
    lv_obj_set_style_bg_color(record_dir_page, lv_color_make(20, 20, 30), LV_PART_MAIN);

    // Main menu items
    lv_obj_t* video_btn = lv_menu_cont_create(main_page_obj);
    lv_obj_t* video_label = lv_label_create(video_btn);
    lv_label_set_text(video_label, LV_SYMBOL_VIDEO " Video Settings");
    lv_obj_set_style_text_color(video_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(video_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, video_btn, video_page);
    
    lv_obj_t* wifi_btn = lv_menu_cont_create(main_page_obj);
    lv_obj_t* wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI " WiFi Settings");
    lv_obj_set_style_text_color(wifi_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, wifi_btn, wifi_page);
    
    lv_obj_t* system_btn = lv_menu_cont_create(main_page_obj);
    lv_obj_t* system_label = lv_label_create(system_btn);
    lv_label_set_text(system_label, LV_SYMBOL_SETTINGS " System Settings");
    lv_obj_set_style_text_color(system_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(system_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, system_btn, system_page);
    
    lv_obj_t* display_btn = lv_menu_cont_create(main_page_obj);
    lv_obj_t* display_label = lv_label_create(display_btn);
    lv_label_set_text(display_label, LV_SYMBOL_IMAGE " Display Settings");
    lv_obj_set_style_text_color(display_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(display_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, display_btn, display_page);
    
    lv_obj_t* about_btn = lv_menu_cont_create(main_page_obj);
    lv_obj_t* about_label = lv_label_create(about_btn);
    lv_label_set_text(about_label, LV_SYMBOL_LIST " About");
    lv_obj_set_style_text_color(about_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(about_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, about_btn, about_page);

    // Video settings page content
    // Bitrate dropdown
    lv_obj_t* bitrate_cont = lv_menu_cont_create(video_page);
    lv_obj_t* bitrate_dropdown = lv_dropdown_create(bitrate_cont);
    lv_dropdown_set_options(bitrate_dropdown, "1 Mbps\n4 Mbps\n8 Mbps\n16 Mbps\n25 Mbps");
    lv_dropdown_set_selected(bitrate_dropdown, 2); // 8 Mbps
    lv_obj_set_style_text_font(bitrate_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(bitrate_dropdown, 200);
    lv_obj_add_event_cb(bitrate_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(bitrate_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(bitrate_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* bitrate_label = lv_label_create(bitrate_cont);
    lv_label_set_text(bitrate_label, "Bitrate:");
    lv_obj_set_style_text_color(bitrate_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(bitrate_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(bitrate_label, bitrate_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // Codec dropdown
    lv_obj_t* codec_cont = lv_menu_cont_create(video_page);
    lv_obj_t* codec_dropdown = lv_dropdown_create(codec_cont);
    lv_dropdown_set_options(codec_dropdown, "H.264\nH.265\nAV1");
    lv_dropdown_set_selected(codec_dropdown, 0); // H.264
    lv_obj_set_style_text_font(codec_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(codec_dropdown, 200);
    lv_obj_add_event_cb(codec_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(codec_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(codec_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* codec_label = lv_label_create(codec_cont);
    lv_label_set_text(codec_label, "Codec:");
    lv_obj_set_style_text_color(codec_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(codec_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(codec_label, codec_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // OSD Language dropdown
    lv_obj_t* osd_lang_cont = lv_menu_cont_create(video_page);
    lv_obj_t* osd_lang_dropdown = lv_dropdown_create(osd_lang_cont);
    lv_dropdown_set_options(osd_lang_dropdown, "English\nUkrainian\nRussian\nGerman\nFrench");
    lv_dropdown_set_selected(osd_lang_dropdown, 0); // English
    lv_obj_set_style_text_font(osd_lang_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(osd_lang_dropdown, 200);
    lv_obj_add_event_cb(osd_lang_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(osd_lang_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(osd_lang_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* osd_lang_label = lv_label_create(osd_lang_cont);
    lv_label_set_text(osd_lang_label, "OSD Language:");
    lv_obj_set_style_text_color(osd_lang_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(osd_lang_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(osd_lang_label, osd_lang_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // ISP Settings button
    lv_obj_t* isp_btn = lv_menu_cont_create(video_page);
    lv_obj_t* isp_label = lv_label_create(isp_btn);
    lv_label_set_text(isp_label, "ISP Settings >");
    lv_obj_set_style_text_color(isp_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(isp_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, isp_btn, isp_page);

    // ISP Settings page content
    // Brightness slider
    lv_obj_t* brightness_cont = lv_menu_cont_create(isp_page);
    lv_obj_t* brightness_slider = lv_slider_create(brightness_cont);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_slider_set_value(brightness_slider, 50, LV_ANIM_OFF);
    lv_obj_set_width(brightness_slider, 200);
    
    lv_obj_t* brightness_label = lv_label_create(brightness_cont);
    lv_label_set_text(brightness_label, "Brightness: 50");
    lv_obj_set_style_text_color(brightness_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(brightness_label, brightness_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // Contrast slider
    lv_obj_t* contrast_cont = lv_menu_cont_create(isp_page);
    lv_obj_t* contrast_slider = lv_slider_create(contrast_cont);
    lv_slider_set_range(contrast_slider, 0, 200);
    lv_slider_set_value(contrast_slider, 100, LV_ANIM_OFF);
    lv_obj_set_width(contrast_slider, 200);
    
    lv_obj_t* contrast_label = lv_label_create(contrast_cont);
    lv_label_set_text(contrast_label, "Contrast: 100");
    lv_obj_set_style_text_color(contrast_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(contrast_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(contrast_label, contrast_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // Saturation slider
    lv_obj_t* saturation_cont = lv_menu_cont_create(isp_page);
    lv_obj_t* saturation_slider = lv_slider_create(saturation_cont);
    lv_slider_set_range(saturation_slider, 0, 255);
    lv_slider_set_value(saturation_slider, 128, LV_ANIM_OFF);
    lv_obj_set_width(saturation_slider, 200);
    
    lv_obj_t* saturation_label = lv_label_create(saturation_cont);
    lv_label_set_text(saturation_label, "Saturation: 128");
    lv_obj_set_style_text_color(saturation_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(saturation_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(saturation_label, saturation_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // Sharpness slider
    lv_obj_t* sharpness_cont = lv_menu_cont_create(isp_page);
    lv_obj_t* sharpness_slider = lv_slider_create(sharpness_cont);
    lv_slider_set_range(sharpness_slider, 0, 512);
    lv_slider_set_value(sharpness_slider, 256, LV_ANIM_OFF);
    lv_obj_set_width(sharpness_slider, 200);
    
    lv_obj_t* sharpness_label = lv_label_create(sharpness_cont);
    lv_label_set_text(sharpness_label, "Sharpness: 256");
    lv_obj_set_style_text_color(sharpness_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(sharpness_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(sharpness_label, sharpness_slider, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    // WiFi settings page content
    // Channel dropdown
    lv_obj_t* channel_cont = lv_menu_cont_create(wifi_page);
    lv_obj_t* channel_dropdown = lv_dropdown_create(channel_cont);
    lv_dropdown_set_options(channel_dropdown, "Auto\n1\n6\n11\n36\n40\n44\n48");
    lv_dropdown_set_selected(channel_dropdown, 0); // Auto
    lv_obj_set_style_text_font(channel_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(channel_dropdown, 150);
    lv_obj_add_event_cb(channel_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(channel_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(channel_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* channel_label = lv_label_create(channel_cont);
    lv_label_set_text(channel_label, "Channel:");
    lv_obj_set_style_text_color(channel_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(channel_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(channel_label, channel_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // Channel Width dropdown
    lv_obj_t* channel_width_cont = lv_menu_cont_create(wifi_page);
    lv_obj_t* channel_width_dropdown = lv_dropdown_create(channel_width_cont);
    lv_dropdown_set_options(channel_width_dropdown, "20MHz\n40MHz\n80MHz\n160MHz");
    lv_dropdown_set_selected(channel_width_dropdown, 0); // 20MHz
    lv_obj_set_style_text_font(channel_width_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(channel_width_dropdown, 150);
    lv_obj_add_event_cb(channel_width_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(channel_width_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(channel_width_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* channel_width_label = lv_label_create(channel_width_cont);
    lv_label_set_text(channel_width_label, "Channel Width:");
    lv_obj_set_style_text_color(channel_width_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(channel_width_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(channel_width_label, channel_width_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // TX Power dropdown  
    lv_obj_t* power_cont = lv_menu_cont_create(wifi_page);
    lv_obj_t* power_dropdown = lv_dropdown_create(power_cont);
    lv_dropdown_set_options(power_dropdown, "10 dBm\n15 dBm\n20 dBm\n25 dBm\n30 dBm");
    lv_dropdown_set_selected(power_dropdown, 2); // 20 dBm
    lv_obj_set_style_text_font(power_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(power_dropdown, 150);
    lv_obj_add_event_cb(power_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(power_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(power_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* power_label = lv_label_create(power_cont);
    lv_label_set_text(power_label, "TX Power:");
    lv_obj_set_style_text_color(power_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(power_label, power_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    
    // WFB Key button
    lv_obj_t* wfb_key_btn = lv_menu_cont_create(wifi_page);
    lv_obj_t* wfb_key_label = lv_label_create(wfb_key_btn);
    lv_label_set_text(wfb_key_label, "WFB Key >");
    lv_obj_set_style_text_color(wfb_key_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(wfb_key_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, wfb_key_btn, wfb_key_page);

    // WFB Key page content
    lv_obj_t* wfb_status_cont = lv_menu_cont_create(wfb_key_page);
    lv_obj_t* wfb_status_label = lv_label_create(wfb_status_cont);
    lv_label_set_text(wfb_status_label, "Current Key: DEFAULT_KEY");
    lv_obj_set_style_text_color(wfb_status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(wfb_status_label, &lv_font_montserrat_30, LV_PART_MAIN);
    
    lv_obj_t* wfb_change_btn = lv_menu_cont_create(wfb_key_page);
    lv_obj_t* wfb_change_label = lv_label_create(wfb_change_btn);
    lv_label_set_text(wfb_change_label, "Change Key");
    lv_obj_set_style_text_color(wfb_change_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(wfb_change_label, &lv_font_montserrat_30, LV_PART_MAIN);

    // System settings page content
    // Auto Record checkbox
    lv_obj_t* auto_record_cont = lv_menu_cont_create(system_page);
    lv_obj_t* auto_record_checkbox = lv_checkbox_create(auto_record_cont);
    lv_checkbox_set_text(auto_record_checkbox, "Auto Record");
    lv_obj_set_style_text_color(auto_record_checkbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(auto_record_checkbox, &lv_font_montserrat_30, LV_PART_MAIN);
    // Set to unchecked by default
    
    // Record Directory button
    lv_obj_t* record_dir_btn = lv_menu_cont_create(system_page);
    lv_obj_t* record_dir_label = lv_label_create(record_dir_btn);
    lv_label_set_text(record_dir_label, "Record Directory >");
    lv_obj_set_style_text_color(record_dir_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(record_dir_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_menu_set_load_page_event(menu1, record_dir_btn, record_dir_page);
    
    // Reset Settings button
    lv_obj_t* reset_btn = lv_menu_cont_create(system_page);
    lv_obj_t* reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset Settings");
    lv_obj_set_style_text_color(reset_label, lv_color_make(255, 100, 100), LV_PART_MAIN);
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_30, LV_PART_MAIN);

    // Record Directory page content
    lv_obj_t* dir_status_cont = lv_menu_cont_create(record_dir_page);
    lv_obj_t* dir_status_label = lv_label_create(dir_status_cont);
    lv_label_set_text(dir_status_label, "Current: /tmp/recordings");
    lv_obj_set_style_text_color(dir_status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(dir_status_label, &lv_font_montserrat_30, LV_PART_MAIN);
    
    lv_obj_t* dir_change_btn = lv_menu_cont_create(record_dir_page);
    lv_obj_t* dir_change_label = lv_label_create(dir_change_btn);
    lv_label_set_text(dir_change_label, "Change Directory");
    lv_obj_set_style_text_color(dir_change_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(dir_change_label, &lv_font_montserrat_30, LV_PART_MAIN);

    // Display settings page content
    // OSD On/Off checkbox
    lv_obj_t* osd_onoff_cont = lv_menu_cont_create(display_page);
    lv_obj_t* osd_onoff_checkbox = lv_checkbox_create(osd_onoff_cont);
    lv_checkbox_set_text(osd_onoff_checkbox, "OSD Display");
    lv_obj_set_style_text_color(osd_onoff_checkbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(osd_onoff_checkbox, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_state(osd_onoff_checkbox, LV_STATE_CHECKED); // ON by default
    
    // OSD Position dropdown
    lv_obj_t* osd_pos_cont = lv_menu_cont_create(display_page);
    lv_obj_t* osd_pos_dropdown = lv_dropdown_create(osd_pos_cont);
    lv_dropdown_set_options(osd_pos_dropdown, "Top Left\nTop Right\nBottom Left\nBottom Right\nCenter");
    lv_dropdown_set_selected(osd_pos_dropdown, 0); // Top Left
    lv_obj_set_style_text_font(osd_pos_dropdown, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_width(osd_pos_dropdown, 200);
    lv_obj_add_event_cb(osd_pos_dropdown, dropdown_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(osd_pos_dropdown, dropdown_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(osd_pos_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    lv_obj_t* osd_pos_label = lv_label_create(osd_pos_cont);
    lv_label_set_text(osd_pos_label, "OSD Position:");
    lv_obj_set_style_text_color(osd_pos_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(osd_pos_label, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_align_to(osd_pos_label, osd_pos_dropdown, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    // About page content
    lv_obj_t* version_cont = lv_menu_cont_create(about_page);
    lv_obj_t* version_label = lv_label_create(version_cont);
    lv_label_set_text(version_label, "Version: 2.0.0");
    lv_obj_set_style_text_color(version_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_30, LV_PART_MAIN);
    
    lv_obj_t* build_cont = lv_menu_cont_create(about_page);
    lv_obj_t* build_label = lv_label_create(build_cont);
    lv_label_set_text(build_label, "Build: Dec 2024");
    lv_obj_set_style_text_color(build_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(build_label, &lv_font_montserrat_30, LV_PART_MAIN);
    
    lv_obj_t* author_cont = lv_menu_cont_create(about_page);
    lv_obj_t* author_label = lv_label_create(author_cont);
    lv_label_set_text(author_label, "Author: LCC HardTech");
    lv_obj_set_style_text_color(author_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(author_label, &lv_font_montserrat_30, LV_PART_MAIN);

    // Set the main page as default
    lv_menu_set_page(menu1, main_page_obj);
    
    // Initialize navigation by highlighting first item
    menu_navigate_to_position(0); // This will highlight the first item



    // // Add instruction label
    // lv_obj_t *instruction_label = lv_label_create(menu.background);
    // lv_label_set_text(instruction_label, "Use UP/DOWN to navigate, A to select, B to exit");
    // lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_12, LV_PART_MAIN);
    // lv_obj_set_style_text_color(instruction_label, lv_color_make(200, 200, 200), LV_PART_MAIN);
    // lv_obj_align(instruction_label, LV_ALIGN_TOP_MID, 0, 50);
    
    // // Add menu items and store references
    // const char *menu_items_text[] = {"Video Settings", "WiFi Settings", "System Info", "Reset Settings", "Exit"};
    
    // for (int i = 0; i < menu.item_count; i++) {
    //     menu.items[i] = lv_label_create(menu.background);
    //     lv_label_set_text(menu.items[i], menu_items_text[i]);
    //     lv_obj_set_style_text_font(menu.items[i], &lv_font_montserrat_30, LV_PART_MAIN);
    //     lv_obj_align(menu.items[i], LV_ALIGN_TOP_MID, 0, 80 + i * 50);
    //     lv_obj_set_style_text_color(menu.items[i], lv_color_white(), LV_PART_MAIN);
    //     lv_obj_set_style_bg_opa(menu.items[i], LV_OPA_TRANSP, LV_PART_MAIN);
        
    //     // Add padding to menu items
    //     lv_obj_set_style_pad_all(menu.items[i], 8, LV_PART_MAIN);
    // }

    INFO("Menu UI created successfully");
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
    
    current_menu_obj = NULL; // Clear menu reference
    current_page = NULL; // Clear page reference
    main_page = NULL; // Clear main page reference
    open_dropdown = NULL; // Clear dropdown reference
    
    // Clear page positions
    page_count = 0;
    for (int i = 0; i < MAX_PAGES; i++) {
        page_positions[i].page = NULL;
        page_positions[i].position = 0;
    }
    
    for (int i = 0; i < 5; i++) {
        menu.items[i] = NULL; // Objects are deleted with background
    }
    
    menu.visible = false;
    menu.current_item = 0;
    
    pthread_mutex_unlock(&menu_mutex);
    pthread_mutex_destroy(&menu_mutex);
    
    INFO("Menu system deinitialized");
}
