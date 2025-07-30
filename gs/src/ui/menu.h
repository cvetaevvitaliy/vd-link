#ifndef GS_UI_MENU_H
#define GS_UI_MENU_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

// Menu item types
typedef enum {
    MENU_ITEM_TYPE_NONE,
    MENU_ITEM_TYPE_SLIDER,
    MENU_ITEM_TYPE_SWITCH,
    MENU_ITEM_TYPE_DROPDOWN,
    MENU_ITEM_TYPE_BUTTON
} menu_item_type_e;

// Callback storage structure with union for different types
typedef struct {
    menu_item_type_e type;
    union {
        struct {
            int32_t (*get)(void);
            void (*set)(int32_t value);
        } slider;
        struct {
            bool (*get)(void);
            void (*set)(bool state);
        } switch_cb;
        struct {
            uint16_t (*get)(void);
            void (*set)(uint16_t selection);
        } dropdown;
        struct {
            void (*action)(void);
        } button;
    } callbacks;
} menu_item_callbacks_t;

void menu_create(lv_obj_t *parent);
void menu_destroy(void);
void menu_toggle(void);
void menu_handle_navigation(int button_number);
void menu_handle_axis(int axis_number, int value);
void menu_show(void);
void menu_hide(void);

// System integration functions
void menu_set_item_callbacks(lv_obj_t *item, menu_item_callbacks_t *callbacks);
void menu_reload_system_values(void);
lv_group_t* menu_get_current_group(void);

#endif // GS_UI_MENU_H