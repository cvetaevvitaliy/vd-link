#include "menu_engine.h"
#include "input.h"
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "log.h"

#define MAX_GRID_ROWS 3
#define MAX_GRID_COLS 3
#define MAX_MENU_CTX_HISTORY 5
#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0

typedef struct {
    lv_obj_t *cells[MAX_GRID_ROWS][MAX_GRID_COLS];
    menu_item_callbacks_t cell_callbacks[MAX_GRID_ROWS][MAX_GRID_COLS];
    int current_row;
    int current_col;
    lv_obj_t* tab_page;
    lv_group_t *input_group;
    lv_coord_t* col_dsc;
    lv_coord_t* row_dsc;
    int max_cols;
    int max_rows;
    bool was_focused; // Track if this section had focus when menu was hidden
} menu_section_ctx_t;

typedef struct _menu_ctx_s {
    lv_obj_t *menu;
    uint8_t page_count;
    uint8_t current_section;
    lv_group_t* tabview_group;
    bool menu_visible;
    menu_section_ctx_t menu_tabs[];
} menu_ctx_t;

static const char *module_name_str = "MENU_ENGINE";

menu_ctx_t* active_menu_ctx_history[MAX_MENU_CTX_HISTORY]; // History of last 5 menu contexts
int active_menu_idx = -1; // Index of the current active menu context
menu_ctx_t* active_menu_ctx; // Current active menu context

extern lv_color_t color_ht_main;
extern lv_color_t color_ht_secondary;
extern lv_color_t color_ht_accent;

// Edit mode tracking
static bool edit_mode = false;
static lv_obj_t *edit_obj = NULL;
static lv_obj_t *edit_cell = NULL; // Track the cell container

// Store original values for cancel functionality
static int32_t original_slider_value = 0;
static bool original_switch_state = false;
static uint16_t original_dropdown_selection = 0;

// Forward declarations
static menu_section_ctx_t* get_active_menu_section(void);
static void keypad_event_handler(lv_event_t *e);
static void menu_item_click_handler(lv_event_t *e);
static void tab_view_event_handler(lv_event_t* event);
static void focus_to_tabview();
static void create_menu_pages(void);
static void cancel_edit_mode(void);
static void load_system_values(menu_ctx_t *ctx);
static bool handle_edit_mode_input(uint32_t key);
static void focus_btn(int row, int col);


void menu_toggle(menu_ctx_t *ctx)
{
    if (ctx->menu_visible) {
        menu_hide(ctx);
    } else {
        menu_show(ctx);
    }
}

void menu_show(menu_ctx_t *ctx)
{
    if (!ctx->menu) {
        ERROR("Menu not created");
        return;
    }

    if (!ctx->tabview_group) {
        ERROR("Tabview group not created - call menu_create() first");
        return;
    }
    // Save current context to history
    if (active_menu_idx + 1 >= MAX_MENU_CTX_HISTORY) {
        // Shift history left to make room
        for (int i = 1; i < MAX_MENU_CTX_HISTORY; i++) {
            active_menu_ctx_history[i - 1] = active_menu_ctx_history[i];
        }
        // active_menu_idx stays the same
    } else {
        active_menu_idx++;
    }
    active_menu_ctx_history[active_menu_idx] = ctx;
    active_menu_ctx = ctx;

    ctx->menu_visible = true;
    lv_obj_clear_flag(ctx->menu, LV_OBJ_FLAG_HIDDEN);

    // Check if any section had focus when menu was hidden
    bool restored_focus = false;
    for (int i = 0; i < ctx->page_count; i++) {
        if (ctx->menu_tabs[i].was_focused) {
            DEBUG("Restoring focus to section %d", i);
            ctx->current_section = i;
            lv_tabview_set_act(ctx->menu, i, LV_ANIM_OFF);
            ui_set_input_group(ctx->menu_tabs[i].input_group);
            focus_btn(ctx->menu_tabs[i].current_row, ctx->menu_tabs[i].current_col);
            ctx->menu_tabs[i].was_focused = false; // Reset the flag
            restored_focus = true;
            break;
        }
    }

    // If no section had focus, default to tab buttons
    if (!restored_focus) {
        ui_set_input_group(ctx->tabview_group);
        lv_obj_t* tab_btns = lv_tabview_get_tab_btns(ctx->menu);
        if (tab_btns) {
            lv_group_focus_obj(tab_btns);
        }
    }

    INFO("Menu shown");
}

void menu_hide(menu_ctx_t *ctx) // hide the last menu
{
    if (!ctx->menu) {
        ERROR("Menu not created");
        return;
    }

    // Save current focus state before hiding
    lv_group_t* current_group = ui_get_input_group();
    for (int i = 0; i < ctx->page_count; i++) {
        ctx->menu_tabs[i].was_focused = (current_group == ctx->menu_tabs[i].input_group);
        if (ctx->menu_tabs[i].was_focused) {
            DEBUG("Section %d had focus when menu was hidden", i);
        }
    }

    lv_obj_add_flag(ctx->menu, LV_OBJ_FLAG_HIDDEN);
    ctx->menu_visible = false;

    active_menu_idx--;
    if (active_menu_idx >= 0) {
        active_menu_ctx = active_menu_ctx_history[active_menu_idx];
    }

    INFO("Menu hidden");
}

menu_ctx_t* menu_create(lv_obj_t *parent, uint8_t page_count, void (*create_menu_pages)(menu_ctx_t *))
{
    menu_ctx_t *ctx = (menu_ctx_t *)malloc(sizeof(menu_ctx_t) + page_count * sizeof(menu_section_ctx_t));
    memset(ctx, 0, sizeof(menu_ctx_t) + page_count * sizeof(menu_section_ctx_t));
    ctx->current_section = 0;
    edit_mode = false;
    ctx->page_count = page_count;

    ctx->menu = lv_tabview_create(parent);
    lv_obj_add_event_cb(ctx->menu, tab_view_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_opa(ctx->menu, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(ctx->menu, 20, 0);

    lv_obj_set_size(ctx->menu, 960, 520);
    lv_obj_center(ctx->menu);

    ctx->tabview_group = lv_group_create();

    // Configure menu appearance
    lv_color_t bg_color = lv_obj_get_style_bg_color(ctx->menu, 0);
    if(lv_color_brightness(bg_color) > 127) {
        lv_obj_set_style_bg_color(ctx->menu, lv_color_darken(bg_color, 10), 0);
    } else {
        lv_obj_set_style_bg_color(ctx->menu, lv_color_darken(bg_color, 50), 0);
    }

    create_menu_pages(ctx);
    load_system_values(ctx);

    // Setup tab buttons for navigation
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(ctx->menu);
    if (tab_btns) {
        lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_24, 0);
        lv_group_add_obj(ctx->tabview_group, tab_btns);
        lv_obj_add_event_cb(tab_btns, tab_view_event_handler, LV_EVENT_KEY, NULL);
        lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }

    menu_hide(ctx);
    INFO("Complex menu created");
    return ctx;
}


void focus_event_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_add_state(obj, LV_STATE_FOCUSED);
    
    // Add visual focus style - bright border
    lv_obj_set_style_border_width(obj, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(obj, LV_OPA_100, LV_STATE_FOCUSED);
    
    DEBUG("Focused on object: %p", obj);
}

void defocus_event_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_clear_state(obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    
    // Remove visual focus style
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    
    DEBUG("Defocused from object: %p", obj);
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *value_label = (lv_obj_t *)lv_obj_get_user_data(slider);
    
    if (value_label) {
        int32_t value = lv_slider_get_value(slider);
        char value_text[16];
        snprintf(value_text, sizeof(value_text), "%d", (int)value);
        lv_label_set_text(value_label, value_text);
    }
}

static void enter_edit_mode(lv_obj_t *cell)
{
    edit_mode = true;
    edit_cell = cell;
    
    // Find the interactive element inside the cell
    uint32_t child_cnt = lv_obj_get_child_cnt(cell);
    lv_obj_t *interactive_child = NULL;
    
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(cell, i);
        
        if (lv_obj_check_type(child, &lv_slider_class) ||
            lv_obj_check_type(child, &lv_switch_class) ||
            lv_obj_check_type(child, &lv_dropdown_class)) {
            interactive_child = child;
            edit_obj = child; // Set the actual interactive element as edit_obj
            break;
        }

        // If it is a button, we can handle it already
        if ( lv_obj_check_type(child, &lv_button_class))
        {
            menu_section_ctx_t *active_section = get_active_menu_section();
            menu_item_callbacks_t *callbacks = NULL;
            // Find the cell position and get callbacks
            for (int row = 0; row < active_section->max_rows; row++) {
                for (int col = 0; col < active_section->max_cols; col++) {
                    if (active_section->cells[row][col] == edit_cell) {
                        callbacks = &active_section->cell_callbacks[row][col];
                        break;
                    }
                }
                if (callbacks) break;
            }
            if (callbacks->type == MENU_ITEM_TYPE_BUTTON && callbacks->callbacks.button.action) {
                callbacks->callbacks.button.action();
            }
        }
    }
    
    if (!interactive_child) {
        DEBUG("No interactive element found in cell");
        edit_mode = false;
        edit_cell = NULL;
        edit_obj = NULL;
        return;
    }
    
    // Store original values for cancel functionality
    if (lv_obj_check_type(interactive_child, &lv_slider_class)) {
        original_slider_value = lv_slider_get_value(interactive_child);
        DEBUG("Stored original slider value: %d", original_slider_value);
    } else if (lv_obj_check_type(interactive_child, &lv_switch_class)) {
        original_switch_state = lv_obj_has_state(interactive_child, LV_STATE_CHECKED);
        DEBUG("Stored original switch state: %s", original_switch_state ? "ON" : "OFF");
    } else if (lv_obj_check_type(interactive_child, &lv_dropdown_class)) {
        original_dropdown_selection = lv_dropdown_get_selected(interactive_child);
        DEBUG("Stored original dropdown selection: %u", original_dropdown_selection);
    }
    
    // Visual indication - green border on the cell
    lv_obj_set_style_border_color(cell, lv_color_hex(0x00FF00), LV_STATE_FOCUSED);
    lv_obj_invalidate(cell);
    
    // Also highlight the interactive element
    lv_obj_set_style_border_width(interactive_child, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(interactive_child, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(interactive_child, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_invalidate(interactive_child);
    
    DEBUG("Entered edit mode for cell: %p, interactive element: %p", cell, interactive_child);
}


static void exit_edit_mode(void)
{
    if (edit_cell) {
        // Find the position of this cell in the current section
        menu_section_ctx_t *active_section = get_active_menu_section();
        menu_item_callbacks_t *callbacks = NULL;
        
        // Find the cell position and get callbacks
        for (int row = 0; row < active_section->max_rows; row++) {
            for (int col = 0; col < active_section->max_cols; col++) {
                if (active_section->cells[row][col] == edit_cell) {
                    callbacks = &active_section->cell_callbacks[row][col];
                    break;
                }
            }
            if (callbacks) break;
        }
        
        if (callbacks && edit_obj) {
            if (lv_obj_check_type(edit_obj, &lv_slider_class) && 
                callbacks->type == MENU_ITEM_TYPE_SLIDER && callbacks->callbacks.slider.set) {
                int32_t value = lv_slider_get_value(edit_obj);
                callbacks->callbacks.slider.set(value);
                DEBUG("Saved slider value to system: %d", value);
            } else if (lv_obj_check_type(edit_obj, &lv_switch_class) && 
                       callbacks->type == MENU_ITEM_TYPE_SWITCH && callbacks->callbacks.switch_cb.set) {
                bool state = lv_obj_has_state(edit_obj, LV_STATE_CHECKED);
                callbacks->callbacks.switch_cb.set(state);
                DEBUG("Saved switch state to system: %s", state ? "ON" : "OFF");
            } else if (lv_obj_check_type(edit_obj, &lv_dropdown_class) && 
                       callbacks->type == MENU_ITEM_TYPE_DROPDOWN && callbacks->callbacks.dropdown.set) {
                uint16_t selection = lv_dropdown_get_selected(edit_obj);
                callbacks->callbacks.dropdown.set(selection);
                DEBUG("Saved dropdown selection to system: %u", selection);
            }
        }
        
        // Restore normal focus border on the cell
        lv_obj_set_style_border_color(edit_cell, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_invalidate(edit_cell);
    }
    
    if (edit_obj) {
        // Remove highlight from interactive element
        lv_obj_set_style_border_width(edit_obj, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(edit_obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_invalidate(edit_obj);
    }
    
    edit_mode = false;
    edit_obj = NULL;
    edit_cell = NULL;
    
    DEBUG("Exited edit mode");
}

static void cancel_edit_mode(void)
{
    if (!edit_mode || !edit_obj) {
        DEBUG("Not in edit mode, nothing to cancel");
        return;
    }
    
    DEBUG("Cancelling edit mode and restoring original values");
    
    // Restore original values
    if (lv_obj_check_type(edit_obj, &lv_slider_class)) {
        lv_slider_set_value(edit_obj, original_slider_value, LV_ANIM_OFF);
        lv_obj_send_event(edit_obj, LV_EVENT_VALUE_CHANGED, NULL);
        DEBUG("Restored slider to original value: %d", original_slider_value);
    } else if (lv_obj_check_type(edit_obj, &lv_switch_class)) {
        if (original_switch_state) {
            lv_obj_add_state(edit_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(edit_obj, LV_STATE_CHECKED);
        }
        DEBUG("Restored switch to original state: %s", original_switch_state ? "ON" : "OFF");
    } else if (lv_obj_check_type(edit_obj, &lv_dropdown_class)) {
        lv_dropdown_set_selected(edit_obj, original_dropdown_selection);
        DEBUG("Restored dropdown to original selection: %u", original_dropdown_selection);
    }
    
    // Exit edit mode
    exit_edit_mode();
}

// Public functions for system integration
void menu_set_item_callbacks(menu_ctx_t *ctx, lv_obj_t *item, menu_item_callbacks_t *callbacks)
{
    // Find the item in all sections and set callbacks
    for (int section = 0; section < ctx->page_count; section++) {
        menu_section_ctx_t *menu_section = &ctx->menu_tabs[section];
        for (int row = 0; row < menu_section->max_rows; row++) {
            for (int col = 0; col < menu_section->max_cols; col++) {
                if (menu_section->cells[row][col] == item) {
                    menu_section->cell_callbacks[row][col] = *callbacks;
                    DEBUG("Set callbacks for menu item at [%d][%d]: %p", row, col, item);
                    return;
                }
            }
        }
    }
    ERROR("Menu item not found: %p", item);
}


static void load_system_values(menu_ctx_t *ctx)
{
    DEBUG("Loading system values for all menu items");
    
    // Iterate through all sections and their cells
    for (int section = 0; section < ctx->page_count; section++) {
        menu_section_ctx_t *menu_section = &ctx->menu_tabs[section];

        for (int row = 0; row < menu_section->max_rows; row++) {
            for (int col = 0; col < menu_section->max_cols; col++) {
                lv_obj_t *cell = menu_section->cells[row][col];
                if (!cell) continue;
                
                // Get callbacks from section structure
                menu_item_callbacks_t *callbacks = &menu_section->cell_callbacks[row][col];
                if (callbacks->type == MENU_ITEM_TYPE_NONE) continue;
                
                // Find the interactive element inside the cell
                uint32_t child_cnt = lv_obj_get_child_cnt(cell);
                for (uint32_t i = 0; i < child_cnt; i++) {
                    lv_obj_t *child = lv_obj_get_child(cell, i);
                    
                    if (lv_obj_check_type(child, &lv_slider_class) && 
                        callbacks->type == MENU_ITEM_TYPE_SLIDER && callbacks->callbacks.slider.get) {
                        int32_t value = callbacks->callbacks.slider.get();
                        lv_slider_set_value(child, value, LV_ANIM_OFF);
                        lv_obj_send_event(child, LV_EVENT_VALUE_CHANGED, NULL);
                        DEBUG("Loaded slider value: %d for cell [%d][%d]", value, row, col);
                        break;
                    } else if (lv_obj_check_type(child, &lv_switch_class) && 
                               callbacks->type == MENU_ITEM_TYPE_SWITCH && callbacks->callbacks.switch_cb.get) {
                        bool state = callbacks->callbacks.switch_cb.get();
                        if (state) {
                            lv_obj_add_state(child, LV_STATE_CHECKED);
                        } else {
                            lv_obj_clear_state(child, LV_STATE_CHECKED);
                        }
                        DEBUG("Loaded switch state: %s for cell [%d][%d]", state ? "ON" : "OFF", row, col);
                        break;
                    } else if (lv_obj_check_type(child, &lv_dropdown_class) && 
                               callbacks->type == MENU_ITEM_TYPE_DROPDOWN && callbacks->callbacks.dropdown.get) {
                        uint16_t selection = callbacks->callbacks.dropdown.get();
                        lv_dropdown_set_selected(child, selection);
                        DEBUG("Loaded dropdown selection: %u for cell [%d][%d]", selection, row, col);
                        break;
                    }
                }
            }
        }
    }
}

static bool handle_edit_mode_input(uint32_t key)
{
    if (!edit_mode || !edit_obj) {
        DEBUG("Edit mode not active or no edit object");
        return false;
    }
    
    DEBUG("Handling edit mode input, key: %u for object: %p", key, edit_obj);
    
    if (lv_obj_check_type(edit_obj, &lv_slider_class)) {
        // Handle slider editing
        int32_t current_val = lv_slider_get_value(edit_obj);
        int32_t min_val = lv_slider_get_min_value(edit_obj);
        int32_t max_val = lv_slider_get_max_value(edit_obj);
        int32_t step = (max_val - min_val) / 20; // 20 steps across range
        if (step < 1) step = 1;
        
        DEBUG("Slider edit: current=%d, min=%d, max=%d, step=%d", current_val, min_val, max_val, step);
        
        switch (key) {
            case LV_KEY_RIGHT:
            case LV_KEY_UP:
                if (current_val + step <= max_val) {
                    lv_slider_set_value(edit_obj, current_val + step, LV_ANIM_OFF);
                    lv_obj_send_event(edit_obj, LV_EVENT_VALUE_CHANGED, NULL);
                    DEBUG("Slider increased to: %d", current_val + step);
                }
                return true;
            case LV_KEY_LEFT:
            case LV_KEY_DOWN:
                if (current_val - step >= min_val) {
                    lv_slider_set_value(edit_obj, current_val - step, LV_ANIM_OFF);
                    lv_obj_send_event(edit_obj, LV_EVENT_VALUE_CHANGED, NULL);
                    DEBUG("Slider decreased to: %d", current_val - step);
                }
                return true;
        }
        return true; // We are editing a slider, so we handled the input
        
    } else if (lv_obj_check_type(edit_obj, &lv_switch_class)) {
        // Handle switch editing
        bool current_state = lv_obj_has_state(edit_obj, LV_STATE_CHECKED);
        DEBUG("Switch edit: current_state=%s", current_state ? "ON" : "OFF");
        
        switch (key) {
            case LV_KEY_RIGHT:
            case LV_KEY_UP:
            case LV_KEY_LEFT:
            case LV_KEY_DOWN:
                if (current_state) {
                    lv_obj_clear_state(edit_obj, LV_STATE_CHECKED);
                    DEBUG("Switch turned OFF");
                } else {
                    lv_obj_add_state(edit_obj, LV_STATE_CHECKED);
                    DEBUG("Switch turned ON");
                }
                return true;
        }
        return true; // We are editing a switch, so we handled the input
        
    } else if (lv_obj_check_type(edit_obj, &lv_dropdown_class)) {
        // Handle dropdown editing
        uint16_t current_sel = lv_dropdown_get_selected(edit_obj);
        uint16_t option_cnt = lv_dropdown_get_option_cnt(edit_obj);
        
        DEBUG("Dropdown edit: current=%u, count=%u", current_sel, option_cnt);
        
        switch (key) {
            case LV_KEY_UP:
            case LV_KEY_LEFT:
                if (current_sel > 0) {
                    lv_dropdown_set_selected(edit_obj, current_sel - 1);
                    DEBUG("Dropdown selected: %u", current_sel - 1);
                }
                return true;
            case LV_KEY_DOWN:
            case LV_KEY_RIGHT:
                if (current_sel < option_cnt - 1) {
                    lv_dropdown_set_selected(edit_obj, current_sel + 1);
                    DEBUG("Dropdown selected: %u", current_sel + 1);
                }
                return true;
        }
        return true; // We are editing a dropdown, so we handled the input
    }
    
    DEBUG("Edit object is not a recognized interactive element");
    return false;
}

static menu_section_ctx_t* get_active_menu_section(void)
{
    return &(active_menu_ctx->menu_tabs[active_menu_ctx->current_section]);
}

static void focus_to_tabview()
{
    DEBUG("Switching focus to tabview");
    menu_ctx_t *ctx = active_menu_ctx;
    
    // Get the tabview buttons (the tab headers)
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(ctx->menu);
    if (tab_btns) {
        // Switch to the main UI input group for tabview navigation
        ui_set_input_group(ctx->tabview_group);

        // Focus on the tab buttons
        lv_group_focus_obj(tab_btns);
        DEBUG("Focused on tab buttons: %p, group: %p", tab_btns, ctx->tabview_group);

        // Debug: check if tab_btns is actually in the group
        lv_group_t* group = ctx->tabview_group;
        int count = lv_group_get_obj_count(group);
        DEBUG("Main group has %d objects", count);
        for (int i = 0; i < count; i++) {
            lv_obj_t* obj = lv_group_get_obj_by_index(group, i);
            DEBUG("Group object %d: %p (tab_btns: %s)", i, obj, obj == tab_btns ? "YES" : "NO");
        }
    } else {
        ERROR("Failed to get tab buttons");
    }
}

lv_obj_t* create_menu_section(menu_ctx_t *ctx, uint8_t section, const char *title, int cols)
{
    if (section < 0 || section >= ctx->page_count) {
        ERROR("Invalid section index: %d", section);
        return NULL;
    }

    lv_obj_t *tab = lv_tabview_add_tab(ctx->menu, title);
    if (!tab) {
        ERROR("Failed to create tab for section %d", section);
        return NULL;
    }
    
    ctx->menu_tabs[section].tab_page = tab;
    ctx->menu_tabs[section].input_group = lv_group_create();
    ctx->menu_tabs[section].max_cols = cols;
    ctx->menu_tabs[section].max_rows = MAX_GRID_ROWS;
    ctx->menu_tabs[section].current_row = 0;
    ctx->menu_tabs[section].current_col = 0;
    ctx->menu_tabs[section].was_focused = false;

    lv_obj_set_layout(tab, LV_LAYOUT_GRID);
    
    // Setup grid layout
    lv_coord_t* col_dsc = malloc((cols + 1) * sizeof(lv_coord_t));
    lv_coord_t* row_dsc = malloc((MAX_GRID_ROWS + 1) * sizeof(lv_coord_t));
    
    for (int i = 0; i < cols; i++) {
        col_dsc[i] = LV_GRID_FR(1);
    }
    col_dsc[cols] = LV_GRID_TEMPLATE_LAST;
    
    for (int i = 0; i < MAX_GRID_ROWS; i++) {
        row_dsc[i] = LV_GRID_CONTENT;
    }
    row_dsc[MAX_GRID_ROWS] = LV_GRID_TEMPLATE_LAST;
    
    lv_obj_set_grid_dsc_array(tab, col_dsc, row_dsc);

    // Initialize cells and callbacks
    for (int i = 0; i < MAX_GRID_ROWS; i++) {
        for (int j = 0; j < MAX_GRID_COLS; j++) {
            ctx->menu_tabs[section].cells[i][j] = NULL;
            ctx->menu_tabs[section].cell_callbacks[i][j].type = MENU_ITEM_TYPE_NONE;
        }
    }

    return tab;
}

void add_object_to_section(menu_ctx_t *ctx, uint8_t section, lv_obj_t *obj)
{
    if (section < 0 || section >= ctx->page_count) {
        ERROR("Invalid section index: %d", section);
        return;
    }

    menu_section_ctx_t *tab = &ctx->menu_tabs[section];
    if (!tab->tab_page) {
        ERROR("Tabview for section %d is not created", section);
        return;
    }

    // Find next available cell
    for (int i = 0; i < tab->max_rows; i++) {
        for (int j = 0; j < tab->max_cols; j++) {
            if (!tab->cells[i][j]) {
                tab->cells[i][j] = obj;
                tab->cell_callbacks[i][j].type = MENU_ITEM_TYPE_NONE;
                
                lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, j, 1, LV_GRID_ALIGN_CENTER, i, 1);
                lv_obj_set_parent(obj, tab->tab_page);
                
                lv_obj_add_event_cb(obj, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
                lv_obj_add_event_cb(obj, keypad_event_handler, LV_EVENT_KEY, NULL);
                lv_obj_add_event_cb(obj, focus_event_cb, LV_EVENT_FOCUSED, NULL);
                lv_obj_add_event_cb(obj, defocus_event_cb, LV_EVENT_DEFOCUSED, NULL);

                if (ctx->menu_tabs[section].input_group) {
                    lv_group_add_obj(ctx->menu_tabs[section].input_group, obj);
                    lv_obj_clear_state(obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                } else {
                    ERROR("Section input group is not created");
                }
                return;
            }
        }
    }
}


static void focus_btn(int row, int col) {
    menu_section_ctx_t *active_section = get_active_menu_section();
    
    DEBUG("Focusing button at (%d, %d)", row, col);
    if (row >= MAX_GRID_ROWS || col >= MAX_GRID_COLS || row < 0 || col < 0) {
        DEBUG("Invalid coordinates: (%d, %d)", row, col);
        return;
    }
    
    // Find the next available cell starting from the requested position
    for (int r = row; r < MAX_GRID_ROWS; r++) {
        int start_col = (r == row) ? col : 0;
        for (int c = start_col; c < MAX_GRID_COLS; c++) {
            if (active_section->cells[r][c] != NULL) {
                lv_group_focus_obj(active_section->cells[r][c]);
                active_section->current_row = r;
                active_section->current_col = c;
                DEBUG("Successfully focused on object at (%d, %d): %p", r, c, active_section->cells[r][c]);
                return;
            }
        }
    }
    
    // If no cell found forward, try backward
    for (int r = row; r >= 0; r--) {
        int end_col = (r == row) ? col : MAX_GRID_COLS - 1;
        for (int c = end_col; c >= 0; c--) {
            if (active_section->cells[r][c] != NULL) {
                lv_group_focus_obj(active_section->cells[r][c]);
                active_section->current_row = r;
                active_section->current_col = c;
                DEBUG("Successfully focused on object at (%d, %d): %p", r, c, active_section->cells[r][c]);
                return;
            }
        }
    }
    
    DEBUG("No available cell found for focus");
}

// Event handler for menu events
static void keypad_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_obj_t *menu_obj = lv_event_get_user_data(e);
    lv_event_code_t event_code = lv_event_get_code(e);
    DEBUG("Keypad event: %d on object %p", event_code, obj);
    
    menu_section_ctx_t *active_section = get_active_menu_section();

    lv_group_t * cur_group = active_section->input_group;
#if 0
    int count = lv_group_get_obj_count(cur_group);
    DEBUG("Current group count: %d", count);
    for (int i = 0; i < count; i++) {
        lv_obj_t *obj_p = lv_group_get_obj_by_index(cur_group, i);
        DEBUG("Object in group: %p", obj_p);
    }
#endif
    int next_row = active_section->current_row;
    int next_col = active_section->current_col;

    if (event_code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        // Handle A and B buttons first, before any other processing
        if (key == 6 || key == 10) { // A button - Enter/Exit edit mode (trying both key codes)
            DEBUG("A button pressed (key=%u), edit_mode=%s", key, edit_mode ? "true" : "false");
            if (edit_mode) {
                // Exit edit mode and save changes
                exit_edit_mode();
            } else {
                // Enter edit mode for currently focused object
                lv_obj_t *focused = lv_group_get_focused(cur_group);
                DEBUG("Trying to enter edit mode for focused object: %p", focused);
                if (focused) {
                    enter_edit_mode(focused);
                } else {
                    DEBUG("No focused object found");
                }
            }
            return; // Don't process further navigation
        }
        
        if (key == 7 || key == 11 || key == 27) { // B button - Cancel edit mode (trying multiple key codes)
            DEBUG("B button pressed (key=%u), edit_mode=%s", key, edit_mode ? "true" : "false");
            if (edit_mode) {
                cancel_edit_mode(); // Use cancel instead of exit to restore original values
            }
            return; // Don't process further navigation
        }
        
        // Check if we're in edit mode and handle directional keys for value changes
        if (edit_mode && handle_edit_mode_input(key)) {
            return; // Input was handled in edit mode
        }
        
        switch(key) {
           case LV_KEY_UP:
            if (!edit_mode) {
                next_row = active_section->current_row - 1;
                next_col = active_section->current_col;
                if (next_row < 0) {
                    // Can't go up, switch to tabview
                    lv_obj_t *focused_obj = lv_group_get_focused(cur_group);
                    if (focused_obj) {
                        lv_obj_clear_state(focused_obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                        lv_obj_invalidate(focused_obj);
                    }
                    focus_to_tabview();
                    return;
                } else {
                    // Find the last valid cell in the new row, starting from current_col
                    bool found = false;
                    for (int c = next_col; c >= 0; c--) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell in the row from current_col, start from end
                        for (int c = MAX_GRID_COLS - 1; c >= 0; c--) {
                            if (active_section->cells[next_row][c] != NULL) {
                                next_col = c;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        // No cell in the row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                }
                focus_btn(next_row, next_col);
            }
            break;
        case LV_KEY_DOWN:
            if (!edit_mode) {
                next_row = active_section->current_row + 1;
                next_col = active_section->current_col;
                if (next_row >= MAX_GRID_ROWS) {
                    // Can't go down, stay
                    next_row = active_section->current_row;
                    next_col = active_section->current_col;
                } else {
                    // Find the first valid cell in the new row, starting from current_col
                    bool found = false;
                    for (int c = next_col; c < MAX_GRID_COLS; c++) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell in the row from current_col, start from beginning
                        for (int c = 0; c < MAX_GRID_COLS; c++) {
                            if (active_section->cells[next_row][c] != NULL) {
                                next_col = c;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        // No cell in the row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                }
                focus_btn(next_row, next_col);
            }
            break;
        case LV_KEY_LEFT:
            if (!edit_mode) {
                next_row = active_section->current_row;
                next_col = active_section->current_col - 1;
                if (next_col < 0) {
                    // Wrap to previous row, last column
                    next_row = active_section->current_row - 1;
                    if (next_row < 0) {
                        next_row = MAX_GRID_ROWS - 1;
                    }
                    // Find last valid cell in the row
                    bool found = false;
                    for (int c = MAX_GRID_COLS - 1; c >= 0; c--) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell in the row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                } else {
                    // Find the last valid cell before current_col in the row
                    bool found = false;
                    for (int c = next_col; c >= 0; c--) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell before, wrap to end of row
                        for (int c = MAX_GRID_COLS - 1; c > next_col; c--) {
                            if (active_section->cells[next_row][c] != NULL) {
                                next_col = c;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        // No other cell in row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                }
                focus_btn(next_row, next_col);
            }
            break;
        case LV_KEY_RIGHT:
            if (!edit_mode) {
                next_row = active_section->current_row;
                next_col = active_section->current_col + 1;
                if (next_col >= MAX_GRID_COLS) {
                    // Wrap to next row, first column
                    next_row = active_section->current_row + 1;
                    if (next_row >= MAX_GRID_ROWS) {
                        next_row = 0;
                    }
                    // Find first valid cell in the row
                    bool found = false;
                    for (int c = 0; c < MAX_GRID_COLS; c++) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell in the row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                } else {
                    // Find the first valid cell after current_col in the row
                    bool found = false;
                    for (int c = next_col; c < MAX_GRID_COLS; c++) {
                        if (active_section->cells[next_row][c] != NULL) {
                            next_col = c;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // No cell after, wrap to beginning of row
                        for (int c = 0; c < next_col; c++) {
                            if (active_section->cells[next_row][c] != NULL) {
                                next_col = c;
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        // No other cell in row, stay
                        next_row = active_section->current_row;
                        next_col = active_section->current_col;
                    }
                }
                focus_btn(next_row, next_col);
            }
            break;
        case LV_KEY_ENTER:
            if (!edit_mode) {
                lv_obj_send_event(lv_group_get_focused(cur_group), LV_EVENT_CLICKED, NULL);
            }
            break;
        case LV_KEY_ESC:
            if (!edit_mode) {
                lv_obj_send_event(active_section->tab_page, LV_EVENT_CLICKED, NULL);
            }
            break;
        }
    } else if (event_code == LV_EVENT_CLICKED) {
        DEBUG("CLICKED event on object %p - ignoring during key navigation", obj);
        // Don't navigate on click events - just handle the click
        return; // Add return to prevent further processing
    } else {
        DEBUG("Other event: %d on object %p", event_code, obj);
    }
}

static void tab_view_event_handler(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t* target = lv_event_get_target(event);
    
    DEBUG("TabView event: %d, target: %p", code, target);
    
    // Get the actual tabview object - target might be tab_btns or tabview
    lv_obj_t* tabview_obj = active_menu_ctx->menu; // Use the global menu tabview
    uint32_t curr_tab_id = lv_tabview_get_tab_act(tabview_obj);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Tab was changed, update current_section
        active_menu_ctx->current_section = curr_tab_id;
        DEBUG("Tab changed to: %d", curr_tab_id);
        return;
    }
    
    if (code != LV_EVENT_KEY) {
        DEBUG("Not a key event, ignoring");
        return;
    }
    
    lv_key_t key = lv_event_get_key(event);
    DEBUG("Key pressed: %d on tab: %d", key, curr_tab_id);
    
    switch (key) {
    case LV_KEY_RIGHT:
        DEBUG("RIGHT key pressed");
        if (curr_tab_id >= active_menu_ctx->page_count - 1) {
            DEBUG("Already at last tab, ignoring RIGHT");
            return;
        }
        DEBUG("Switching to next tab: %d -> %d", curr_tab_id, curr_tab_id + 1);
        lv_tabview_set_act(tabview_obj, curr_tab_id + 1, LV_ANIM_OFF);
        break;
        
    case LV_KEY_LEFT:
        DEBUG("LEFT key pressed");
        if (curr_tab_id <= 0) {
            DEBUG("Already at first tab, ignoring LEFT");
            return;
        }
        DEBUG("Switching to previous tab: %d -> %d", curr_tab_id, curr_tab_id - 1);
        lv_tabview_set_act(tabview_obj, curr_tab_id - 1, LV_ANIM_OFF);
        break;
        
    case LV_KEY_DOWN:
        DEBUG("DOWN key pressed - switching to content");
        menu_section_ctx_t *active_section = &active_menu_ctx->menu_tabs[curr_tab_id];
        if (active_section && active_section->input_group) {
            active_menu_ctx->current_section = curr_tab_id;
            ui_set_input_group(active_section->input_group);
            focus_btn(0, 0);
            DEBUG("Switched to tab content, section: %d", curr_tab_id);
        }
        break;
        
    case LV_KEY_ENTER:
        DEBUG("ENTER key pressed - switching to content");
        active_section = &active_menu_ctx->menu_tabs[curr_tab_id];
        if (active_section && active_section->input_group) {
            active_menu_ctx->current_section = curr_tab_id;
            ui_set_input_group(active_section->input_group);
            focus_btn(0, 0);
            DEBUG("Switched to tab content via Enter, section: %d", curr_tab_id);
        }
        break;
        
    case LV_KEY_UP:
        DEBUG("UP key pressed - staying on tabs");
        break;
        
    default:
        DEBUG("Unhandled key: %d", key);
        break;
    }
}

// Generic menu item click handler
static void menu_item_click_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    DEBUG("Menu item click handler called with event: %d", code);
    
    // Only handle actual clicks, not key-generated events
    if (code == LV_EVENT_CLICKED) {
        // Check if this was triggered by a key press vs actual click
        lv_indev_t *indev_act = lv_indev_get_act();
        if (indev_act && lv_indev_get_type(indev_act) == LV_INDEV_TYPE_KEYPAD) {
            DEBUG("Click event from keypad - ignoring");
            return;
        }
        INFO("Actual menu item clicked");
    }
}

lv_obj_t* create_grid_cell(lv_obj_t *parent, const char* title)
{
    lv_obj_t *cell = lv_obj_create(parent);
    
    lv_obj_set_height(cell, 120);
    lv_obj_set_style_pad_all(cell, 4, 0);
    lv_obj_set_style_margin_all(cell, 2, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_10, LV_PART_MAIN);

    lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (title) {
        lv_obj_t *label = lv_label_create(cell);
        lv_label_set_text(label, title);
        lv_obj_set_width(label, LV_PCT(90));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }

    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_border_width(cell, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(cell, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(cell, LV_OPA_100, LV_STATE_FOCUSED);

    return cell;
}

lv_obj_t *create_slider_item(lv_obj_t *parent, const char *txt, int32_t min, int32_t max, int32_t val)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *slider = lv_slider_create(obj);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);
    lv_obj_set_width(slider, LV_PCT(70));
    lv_obj_set_height(slider, 15);

    lv_obj_t *value_label = lv_label_create(obj);
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%d", (int)val);
    lv_label_set_text(value_label, value_text);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
    
    lv_obj_set_user_data(slider, value_label);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return obj;
}

lv_obj_t *create_switch_item(lv_obj_t *parent, const char *txt, bool checked)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);
    
    lv_obj_t *sw = lv_switch_create(obj);
    lv_obj_add_state(sw, checked ? LV_STATE_CHECKED : 0);
    lv_obj_set_size(sw, 50, 25);

    return obj;
}

lv_obj_t *create_button_item(lv_obj_t *parent, const char *txt, const char* btn_txt)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *btn = lv_btn_create(obj);
    lv_obj_set_size(btn, LV_PCT(100), 50);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_24, 0);
    lv_obj_add_event_cb(btn, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
    
    if (btn_txt) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, btn_txt);
        lv_obj_center(label);
    }

    return obj;
}

lv_obj_t *create_dropdown_item(lv_obj_t *parent, const char *txt, const char *options)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *dropdown = lv_dropdown_create(obj);
    lv_dropdown_set_options(dropdown, options);
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_24, 0);
    lv_dropdown_set_selected(dropdown, 0);
    lv_obj_set_width(dropdown, LV_PCT(90));

    return obj;
}

lv_group_t* menu_get_current_group(void)
{
    menu_section_ctx_t *active_section = get_active_menu_section();
    if (active_section) {
        return active_section->input_group;
    }
    return NULL;
}

#undef ENABLE_DEBUG
#define ENABLE_DEBUG 0