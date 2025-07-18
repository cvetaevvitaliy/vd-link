#ifndef MENU_H
#define MENU_H

#include "ui_interface.h"
#include <stdbool.h>

// Menu navigation structure
typedef struct {
    bool visible;
    lv_obj_t *background;
    lv_obj_t *items[5];
    int current_item;
    int item_count;
} menu_t;

// Initialize menu system
int menu_init(void);

// Create menu UI
void menu_create_ui(void);

// Handle button navigation
void menu_handle_navigation(int button_number);

// Update menu selection visual indicator
void menu_update_selection(void);

// Show/hide menu
void menu_toggle(void);

// Check if menu is visible
bool menu_is_visible(void);

// Cleanup menu
void menu_deinit(void);

#endif // MENU_H
