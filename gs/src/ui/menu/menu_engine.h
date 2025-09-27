#pragma once

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

typedef struct _menu_ctx_s menu_ctx_t;

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

lv_obj_t* create_menu_section(menu_ctx_t *ctx, uint8_t section, const char *title, int cols);
lv_obj_t *create_slider_item(lv_obj_t *parent, const char *txt, int32_t min, int32_t max, int32_t val);
lv_obj_t *create_switch_item(lv_obj_t *parent, const char *txt, bool checked);
lv_obj_t *create_button_item(lv_obj_t *parent, const char *txt, const char* btn_txt);
lv_obj_t *create_dropdown_item(lv_obj_t *parent, const char *txt, const char *options);

void menu_show(menu_ctx_t *ctx);
void menu_hide(menu_ctx_t *ctx);
menu_ctx_t* menu_create(lv_obj_t *parent, uint8_t page_count, void (*create_menu_pages)(menu_ctx_t *));
void menu_toggle(menu_ctx_t *ctx);
void menu_set_item_callbacks(menu_ctx_t *ctx, lv_obj_t *item, menu_item_callbacks_t *callbacks);
void menu_reload_system_values(menu_ctx_t *ctx);
void add_object_to_section(menu_ctx_t *ctx, uint8_t section, lv_obj_t *obj);