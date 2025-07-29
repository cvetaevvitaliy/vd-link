#include "menu.h"
#include "log.h"
#include "input.h"
#include <malloc.h>

#define MAX_GRID_ROWS 3
#define MAX_GRID_COLS 3

typedef enum {
    MENU_PAGE_WFB_NG,
    MENU_PAGE_VIDEO,
    MENU_PAGE_SYSTEM,
    MENU_PAGE_DISPLAY,
    // MENU_PAGE_ABOUT,
    MENU_PAGE_COUNT
} menu_section_e;

typedef struct {
    lv_obj_t *cells[MAX_GRID_ROWS][MAX_GRID_COLS];
    int current_row;
    int current_col;
    lv_obj_t* tab_page;
    lv_group_t *input_group;
    lv_coord_t* col_dsc;
    lv_coord_t* row_dsc;
    int max_cols;
    int max_rows;
} menu_section_ctx_t;

static const char *module_name_str = "MENU";

extern lv_color_t color_ht_main;
extern lv_color_t color_ht_secondary;
extern lv_color_t color_ht_accent;

extern lv_indev_t* indev;

// Menu objects
static lv_obj_t *menu;
static menu_section_ctx_t menu_tabs[MENU_PAGE_COUNT]; // 5 tabs: WFB NG, Video, System, Display, About
static menu_section_e current_section = MENU_PAGE_WFB_NG;
bool menu_visible = false;

// Forward declarations
static void keypad_event_handler(lv_event_t *e);
static void menu_item_click_handler(lv_event_t *e);
static void tab_view_event_handler(lv_event_t* event);
static void focus_to_tabview(void);
static void create_menu_pages(void);
static lv_obj_t *create_slider_item(lv_obj_t *parent, const char *txt, int32_t min, int32_t max, int32_t val);
static lv_obj_t *create_switch_item(lv_obj_t *parent, const char *txt, bool checked);
static lv_obj_t *create_button_item(lv_obj_t *parent, const char *txt, const char* btn_txt);
static lv_obj_t *create_dropdown_item(lv_obj_t *parent, const char *txt, const char *options);

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
        // Create LVGL menu widget instead of simple container
    menu = lv_tabview_create(parent);
    lv_obj_add_event_cb(menu, tab_view_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set menu size and position (original size for small screen)
    lv_obj_set_size(menu, 960, 520);
    lv_obj_center(menu);

    // Configure menu appearance
    lv_color_t bg_color = lv_obj_get_style_bg_color(menu, 0);
    if(lv_color_brightness(bg_color) > 127) {
        lv_obj_set_style_bg_color(menu, lv_color_darken(bg_color, 10), 0);
    } else {
        lv_obj_set_style_bg_color(menu, lv_color_darken(bg_color, 50), 0);
    }

    // Create menu pages structure
    create_menu_pages();

    // Add tab buttons to main input group for navigation
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(menu);
    if (tab_btns) {
        // Set larger font for tab buttons
        lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_24, 0);
        
        lv_group_add_obj(ui_get_input_group(), tab_btns);
        // Add KEY event handler ONLY to tab buttons
        lv_obj_add_event_cb(tab_btns, tab_view_event_handler, LV_EVENT_KEY, NULL);
        DEBUG("Added tab buttons to main input group: %p", tab_btns);
        
        // Enable KEYPAD input for tab buttons to work with LEFT/RIGHT keys
        lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(tab_btns, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    }

    // Don't set initial focus - let user navigate manually

    // Menu is visible by default
    menu_visible = true;

    INFO("Complex menu created");
}

void menu_destroy(void)
{
    if (menu) {
        // Remove menu from input group
        lv_group_t *input_group = ui_get_input_group();
        if (input_group) {
            lv_group_remove_obj(menu);
        }

        lv_obj_del(menu);
        menu = NULL;
        for (int i = 0; i < MENU_PAGE_COUNT; i++) {
            free(menu_tabs[i].col_dsc);
            free(menu_tabs[i].row_dsc);
        }
    }
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

static menu_section_ctx_t* get_active_menu_section(void)
{
    return &menu_tabs[current_section];
}

static void focus_to_tabview()
{
    DEBUG("Switching focus to tabview");
    
    // Get the tabview buttons (the tab headers)
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(menu);
    if (tab_btns) {
        // Switch to the main UI input group for tabview navigation
        lv_indev_set_group(indev, ui_get_input_group());

        // Focus on the tab buttons
        lv_group_focus_obj(tab_btns);
        DEBUG("Focused on tab buttons: %p, group: %p", tab_btns, ui_get_input_group());

        // Debug: check if tab_btns is actually in the group
        lv_group_t* group = ui_get_input_group();
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

static lv_obj_t* create_menu_section(menu_section_e section, const char *title, int cols)
{
    if (section < 0 || section >= 5) {
        ERROR("Invalid section index: %d", section);
        return NULL;
    }

    // Create a new tab for the section
    lv_obj_t *tab = lv_tabview_add_tab(menu, title);
    if (!tab) {
        ERROR("Failed to create tab for section %d", section);
        return NULL;
    }
    menu_tabs[section].tab_page = tab;
    // Don't add tab_page to input group - it's content, not navigation
    menu_tabs[section].input_group = lv_group_create();
    menu_tabs[section].max_cols = cols;
    menu_tabs[section].max_rows = MAX_GRID_ROWS;

    lv_obj_set_layout(tab, LV_LAYOUT_GRID);
    
    lv_coord_t* col_dsc = malloc((cols + 1) * sizeof(lv_coord_t));
    lv_coord_t* row_dsc = malloc((MAX_GRID_ROWS + 1) * sizeof(lv_coord_t));
    for (int i = 0; i < cols; i++) {
        col_dsc[i] = LV_GRID_FR(1); // Equal width for each column
    }
    col_dsc[cols] = LV_GRID_TEMPLATE_LAST; // End of column description
    for (int i = 0; i < MAX_GRID_ROWS; i++) {
        row_dsc[i] = LV_GRID_CONTENT; // Use content-based height for minimal spacing
    }
    row_dsc[MAX_GRID_ROWS] = LV_GRID_TEMPLATE_LAST; // End of row description
    lv_obj_set_grid_dsc_array(tab, col_dsc, row_dsc);

    // Initialize cells for this section
    for (int i = 0; i < MAX_GRID_ROWS; i++) {
        for (int j = 0; j < MAX_GRID_COLS; j++) {
            menu_tabs[section].cells[i][j] = NULL;
        }
    }

    menu_tabs[section].current_row = 0;
    menu_tabs[section].current_col = 0;

    return tab;
}

static void add_object_to_section(menu_section_e section, lv_obj_t *obj)
{
    if (section < 0 || section >= 5) {
        ERROR("Invalid section index: %d", section);
        return;
    }

    menu_section_ctx_t *tab = &menu_tabs[section];
    if (!tab->tab_page) {
        ERROR("Tabview for section %d is not created", section);
        return;
    }

    // Add object to next available cell
    for (int i = 0; i < tab->max_rows; i++) {
        for (int j = 0; j < tab->max_cols; j++) {
            if (!tab->cells[i][j]) {
                tab->cells[i][j] = obj;
                DEBUG("Adding object to section %d at cell [%d][%d]", section, i, j);
                lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, j, 1, LV_GRID_ALIGN_CENTER, i, 1);
                lv_obj_set_parent(obj, tab->tab_page);
                
                lv_obj_add_event_cb(obj, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
                lv_obj_add_event_cb(obj, keypad_event_handler, LV_EVENT_KEY, NULL);

                lv_obj_add_event_cb(obj, focus_event_cb, LV_EVENT_FOCUSED, NULL);
                lv_obj_add_event_cb(obj, defocus_event_cb, LV_EVENT_DEFOCUSED, NULL);
                
                // Add to section's input group instead of main group
                if (menu_tabs[section].input_group) {
                    lv_group_add_obj(menu_tabs[section].input_group, obj);
                    lv_obj_clear_state(obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                    // Don't set focus automatically - let user navigate manually
                    DEBUG("Added object %p to section %d input group", obj, section);
                } else {
                    ERROR("Section input group is not created");
                }
                return;
            }
        }
    }
}

// Create all menu pages and structure
static void create_menu_pages(void)
{
    lv_obj_t *wfb_ng_tab = create_menu_section(MENU_PAGE_WFB_NG, "WFB-NG Settings", 3);
    lv_obj_t *video_tab = create_menu_section(MENU_PAGE_VIDEO, "Video Settings", 3);
    lv_obj_t *system_tab = create_menu_section(MENU_PAGE_SYSTEM, "System Settings", 3);
    lv_obj_t *display_tab = create_menu_section(MENU_PAGE_DISPLAY, "Display Settings", 3);

    lv_obj_t *item = NULL;
    /* WFB-NG tab */
    item = create_dropdown_item(wfb_ng_tab, "Bitrate", "400 Kbps\n800 Kbps\n1.2 Mbps\n1.6 Mbps\n2.0 Mbps\n4.0 Mbps\n");
    add_object_to_section(MENU_PAGE_WFB_NG, item);

    item = create_dropdown_item(wfb_ng_tab, "Codec", "H.264\nH.265");
    add_object_to_section(MENU_PAGE_WFB_NG, item);

    item = create_slider_item(wfb_ng_tab, "GOP", 1, 30, 2);
    add_object_to_section(MENU_PAGE_WFB_NG, item);

    item = create_dropdown_item(wfb_ng_tab, "Channel width", "20MHz\n40MHz");
    add_object_to_section(MENU_PAGE_WFB_NG, item);

    /* Video tab */
    item = create_switch_item(video_tab, "Focus mode", false);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_switch_item(video_tab, "Use detection", false);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_dropdown_item(video_tab, "Mirror/Flip", "None\nMirror\nFlip\nMirror+Flip");
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_switch_item(video_tab, "Auto Exposure", false);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Brightness", 1, 255, 2);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Contrast", 1, 255, 2);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Saturation", 1, 255, 2);
    add_object_to_section(MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Sharpness", 1, 255, 2);
    add_object_to_section(MENU_PAGE_VIDEO, item);
    /* System settings tab*/
    item = create_button_item(system_tab, "WiFi settings", "Wifi settings");
    add_object_to_section(MENU_PAGE_SYSTEM, item);

    item = create_button_item(system_tab, "Device keys mapping", "Change mapping");
    add_object_to_section(MENU_PAGE_SYSTEM, item);

    item = create_dropdown_item(system_tab, "Language", "English\nUkrainian");
    add_object_to_section(MENU_PAGE_SYSTEM, item);

    item = create_button_item(system_tab, "Reset to factory defaults", "Reset");
    add_object_to_section(MENU_PAGE_SYSTEM, item);

    item = create_button_item(system_tab, "About", "Author and Version");
    add_object_to_section(MENU_PAGE_SYSTEM, item);

    /* Display settings tab*/
    item = create_switch_item(display_tab, "Show CPU load and Temp of remote device", true);
    add_object_to_section(MENU_PAGE_DISPLAY, item);

    item = create_switch_item(display_tab, "Hide status bar by default", true);
    add_object_to_section(MENU_PAGE_DISPLAY, item);

    item = create_dropdown_item(display_tab, "Status bar location", "Top\nBottom");
    add_object_to_section(MENU_PAGE_DISPLAY, item);

    item = create_switch_item(display_tab, "Show WFB-ng telemetry", true);
    add_object_to_section(MENU_PAGE_DISPLAY, item);

    // Initial focus will be set by menu_create after this function
    DEBUG("Menu pages created successfully");
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
    int count = lv_group_get_obj_count(cur_group);
    DEBUG("Current group count: %d", count);
    for (int i = 0; i < count; i++) {
        lv_obj_t *obj_p = lv_group_get_obj_by_index(cur_group, i);
        DEBUG("Object in group: %p", obj_p);
    }
    int next_row = active_section->current_row;
    int next_col = active_section->current_col;

    if (event_code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        
        switch(key) {
           case LV_KEY_UP:
            if (next_row > 0) {
                next_row--;
                // Find valid cell in the new row
                while (next_row >= 0 && active_section->cells[next_row][next_col] == NULL) {
                    if (next_col > 0) {
                        next_col--;
                    } else {
                        next_row--;
                        next_col = active_section->current_col;
                    }
                }
                if (next_row < 0) {
                    // We're at the top, switch to tabview
                    lv_obj_t *focused_obj = lv_group_get_focused(cur_group);
                    if (focused_obj) {
                        lv_obj_clear_state(focused_obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                        lv_obj_invalidate(focused_obj);
                    }
                    focus_to_tabview();
                    return;
                }
            } else {
                // We're at the top row, switch to tabview
                lv_obj_t *focused_obj = lv_group_get_focused(cur_group);
                if (focused_obj) {
                    lv_obj_clear_state(focused_obj, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
                    lv_obj_invalidate(focused_obj); // Force redraw to remove visual focus
                }
                focus_to_tabview();
                return; // Don't call focus_btn after switching to tabview
            }
            break;
        case LV_KEY_DOWN:
            if (next_row + 1 < MAX_GRID_ROWS) {
                next_row++;
                // Find valid cell in the new row
                while (next_row < MAX_GRID_ROWS && active_section->cells[next_row][next_col] == NULL) {
                    if (next_col > 0) {
                        next_col--;
                    } else {
                        next_row++;
                        next_col = active_section->current_col;
                    }
                }
            }
            break;
        case LV_KEY_LEFT:
            if (next_col > 0) {
                next_col--;
                // Find valid cell to the left
                while (next_col >= 0 && active_section->cells[next_row][next_col] == NULL) {
                    next_col--;
                }
            }
            break;
        case LV_KEY_RIGHT:
            if (next_col + 1 < MAX_GRID_COLS) {
                next_col++;
                // Find valid cell to the right
                while (next_col < MAX_GRID_COLS && active_section->cells[next_row][next_col] == NULL) {
                    next_col++;
                }
            }
            break;
        case LV_KEY_ENTER:
            lv_obj_send_event(lv_group_get_focused(cur_group), LV_EVENT_CLICKED, NULL);
            break;
        case LV_KEY_ESC:
            lv_obj_send_event(active_section->tab_page, LV_EVENT_CLICKED, NULL);
            break;
        }
        focus_btn(next_row, next_col);
    } else if (event_code == LV_EVENT_CLICKED) {
        INFO("Menu item clicked");
    }
}

static void tab_view_event_handler(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t* target = lv_event_get_target(event);
    
    DEBUG("TabView event: %d, target: %p", code, target);
    
    // Get the actual tabview object - target might be tab_btns or tabview
    lv_obj_t* tabview_obj = menu; // Use the global menu tabview
    uint32_t curr_tab_id = lv_tabview_get_tab_act(tabview_obj);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Tab was changed, update current_section
        current_section = curr_tab_id;
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
        if (curr_tab_id >= MENU_PAGE_COUNT - 1) {
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
        menu_section_ctx_t *active_section = &menu_tabs[curr_tab_id];
        if (active_section && active_section->input_group) {
            current_section = curr_tab_id;
            lv_indev_set_group(indev, active_section->input_group);
            focus_btn(0, 0);
            DEBUG("Switched to tab content, section: %d", curr_tab_id);
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
    // This can be used for custom item click handling
    INFO("Menu item clicked");
}

static lv_obj_t* create_grid_cell(lv_obj_t *parent, const char* title)
{
    lv_obj_t *cell = lv_obj_create(parent);
    
    // Don't set width - let grid stretch handle it, set fixed height
    lv_obj_set_height(cell, 120);          // Double height for better visibility
    
    // Add minimal padding
    lv_obj_set_style_pad_all(cell, 4, 0);
    
    // Add margin to create spacing between cells
    lv_obj_set_style_margin_all(cell, 2, 0);
    
    // Set flex layout for content alignment
    lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (title) {
        lv_obj_t *label = lv_label_create(cell);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        // Label will be centered by flex layout
    }

    // Make cell focusable
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    
    // Set default focus style
    lv_obj_set_style_border_width(cell, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(cell, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(cell, LV_OPA_100, LV_STATE_FOCUSED);

    return cell;
}

// Create slider menu item
static lv_obj_t *create_slider_item(lv_obj_t *parent, const char *txt, int32_t min, int32_t max, int32_t val)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *slider = lv_slider_create(obj);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);

    // Set smaller slider size for compact cells
    lv_obj_set_width(slider, LV_PCT(70));
    lv_obj_set_height(slider, 15);

    // Create value label
    lv_obj_t *value_label = lv_label_create(obj);
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%d", (int)val);
    lv_label_set_text(value_label, value_text);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_24, 0);
    
    // Store reference to label in slider's user data for updates
    lv_obj_set_user_data(slider, value_label);
    
    // Add event callback to update value label when slider changes
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // The cell (obj) will be added to input group, not the slider
    return obj;
}

// Create switch menu item
static lv_obj_t *create_switch_item(lv_obj_t *parent, const char *txt, bool checked)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);
    
    lv_obj_t *sw = lv_switch_create(obj);
    lv_obj_add_state(sw, checked ? LV_STATE_CHECKED : 0);
    
    // Make switch smaller for compact cells
    lv_obj_set_size(sw, 50, 25);

    // The cell (obj) will be added to input group, not the switch
    return obj;
}

// Create button menu item
static lv_obj_t *create_button_item(lv_obj_t *parent, const char *txt, const char* btn_txt)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *btn = lv_btn_create(obj);
    lv_obj_set_size(btn, LV_PCT(100), 50);
    lv_obj_add_event_cb(btn, menu_item_click_handler, LV_EVENT_CLICKED, NULL);
    
    if (btn_txt) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, btn_txt);
        lv_obj_center(label);
    }

    return obj;
}

// Create dropdown menu item
static lv_obj_t *create_dropdown_item(lv_obj_t *parent, const char *txt, const char *options)
{
    lv_obj_t *obj = create_grid_cell(parent, txt);

    lv_obj_t *dropdown = lv_dropdown_create(obj);
    lv_dropdown_set_options(dropdown, options);
    lv_dropdown_set_selected(dropdown, 0); // Select first option by default
    
    // Set smaller dropdown size for compact cells
    lv_obj_set_width(dropdown, LV_PCT(90));
    
    // The cell (obj) will be added to input group, not the dropdown
    return obj;
}