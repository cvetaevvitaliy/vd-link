#include "main_menu.h"
#include "menu_engine.h"
#include "log.h"
#include "callbacks_wifi.h"
#include "callbacks_rtp.h"


static const char *module_name_str = "MAIN_MENU";

typedef enum {
    MENU_PAGE_WFB_NG,
    MENU_PAGE_VIDEO,
    MENU_PAGE_SYSTEM,
    MENU_PAGE_DISPLAY,
    // MENU_PAGE_ABOUT,
    MENU_PAGE_COUNT
} menu_section_e;

menu_ctx_t* main_menu_ctx = NULL;

void set_detection_handler(bool state)
{
    // This function should be defined in your system code to handle detection state changes
    int value = state ? 1 : 0;
    //link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_DETECTION, &value, sizeof(value));
}

// Create all menu pages and structure
static void create_menu_pages(menu_ctx_t *ctx)
{
    lv_obj_t *wfb_ng_tab = create_menu_section(ctx, MENU_PAGE_WFB_NG, "WFB-NG Settings", 3);
    lv_obj_t *video_tab = create_menu_section(ctx, MENU_PAGE_VIDEO, "Video Settings", 3);
    lv_obj_t *system_tab = create_menu_section(ctx, MENU_PAGE_SYSTEM, "System Settings", 3);
    lv_obj_t *display_tab = create_menu_section(ctx, MENU_PAGE_DISPLAY, "Display Settings", 3);

    lv_obj_t *item = NULL;
    
    /* WFB-NG tab */
    item = create_dropdown_item(wfb_ng_tab, "Bitrate", bitrate_values_str);
    add_object_to_section(ctx, MENU_PAGE_WFB_NG, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_DROPDOWN,
        .callbacks.dropdown = {
            .get =  wfb_ng_get_bitrate,
            .set = wfb_ng_set_bitrate
        }
    });
    
    item = create_dropdown_item(wfb_ng_tab, "Codec", codec_values_str);
    add_object_to_section(ctx, MENU_PAGE_WFB_NG, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_DROPDOWN,
        .callbacks.dropdown = {
            .get =  wfb_ng_get_codec,
            .set = wfb_ng_set_codec
        }
    });

    item = create_slider_item(wfb_ng_tab, "GOP", 1, 30, 2);
    add_object_to_section(ctx, MENU_PAGE_WFB_NG, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_SLIDER,
        .callbacks.slider = {
            .get = wfb_ng_get_gop,
            .set = wfb_ng_set_gop
        }
    });

    wfb_ng_get_frequencies();
    item = create_dropdown_item(wfb_ng_tab, "Frequency", wfb_ng_get_frequencies_str());
    add_object_to_section(ctx, MENU_PAGE_WFB_NG, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_DROPDOWN,
        .callbacks.dropdown = {
            .get = wfb_ng_get_current_frequency,
            .set = wfb_ng_set_frequency
        }
    });

    item = create_dropdown_item(wfb_ng_tab, "Channel width", "20MHz\n40MHz");
    add_object_to_section(ctx, MENU_PAGE_WFB_NG, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_DROPDOWN,
        .callbacks.dropdown = {
            .get = wfb_ng_get_current_bandwidth,
            .set = wfb_ng_set_bandwidth
        }
    });

    /* Video tab */
    item = create_switch_item(video_tab, "Focus mode", false);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);
    // TODO: Add focus mode callbacks when system functions are available

    item = create_switch_item(video_tab, "Use detection", false);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_SWITCH,
        .callbacks.switch_cb = {
            .get = NULL, // Define this handler in your system code
            .set = set_detection_handler
        }
    });

    item = create_dropdown_item(video_tab, "Mirror/Flip", "None\nMirror\nFlip\nMirror+Flip");
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    item = create_switch_item(video_tab, "Auto Exposure", false);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Brightness", 1, 255, 2);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Contrast", 1, 255, 2);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Saturation", 1, 255, 2);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    item = create_slider_item(video_tab, "Sharpness", 1, 255, 2);
    add_object_to_section(ctx, MENU_PAGE_VIDEO, item);

    /* System settings tab*/
    item = create_button_item(system_tab, "WiFi settings", "Wifi settings");
    add_object_to_section(ctx, MENU_PAGE_SYSTEM, item);
    menu_set_item_callbacks(ctx, item, &(menu_item_callbacks_t){
        .type = MENU_ITEM_TYPE_BUTTON,
        .callbacks.button = {
            .action = wifi_settings_click_handler // Define this handler in your system code
        }
    });

    item = create_button_item(system_tab, "Device keys mapping", "Change mapping");
    add_object_to_section(ctx, MENU_PAGE_SYSTEM, item);

    item = create_dropdown_item(system_tab, "Language", "English\nUkrainian");
    add_object_to_section(ctx, MENU_PAGE_SYSTEM, item);

    item = create_button_item(system_tab, "Reset to factory defaults", "Reset");
    add_object_to_section(ctx, MENU_PAGE_SYSTEM, item);

    item = create_button_item(system_tab, "About", "Author and Version");
    add_object_to_section(ctx, MENU_PAGE_SYSTEM, item);

    /* Display settings tab*/
    item = create_switch_item(display_tab, "Show CPU load and Temp of remote device", true);
    add_object_to_section(ctx, MENU_PAGE_DISPLAY, item);

    item = create_switch_item(display_tab, "Hide status bar by default", true);
    add_object_to_section(ctx, MENU_PAGE_DISPLAY, item);

    item = create_dropdown_item(display_tab, "Status bar location", "Top\nBottom");
    add_object_to_section(ctx, MENU_PAGE_DISPLAY, item);

    item = create_switch_item(display_tab, "Show WFB-ng telemetry", true);
    add_object_to_section(ctx, MENU_PAGE_DISPLAY, item);

    DEBUG("Menu pages created successfully");
}

int main_menu_create(lv_obj_t *parent)
{
    main_menu_ctx = menu_create(parent, MENU_PAGE_COUNT, create_menu_pages);
    return main_menu_ctx != NULL;
}

void main_menu_destroy(void)
{
    if (main_menu_ctx) {
        menu_hide(main_menu_ctx);
        // Free resources if needed
        main_menu_ctx = NULL;
        INFO("Main menu destroyed");
    } else {
        ERROR("Main menu context is NULL");
    }
}

int main_menu_show(void)
{
    if (!main_menu_ctx) {
        ERROR("Main menu context is NULL - call main_menu_create() first");
        return -1;
    }
    menu_show(main_menu_ctx);
}

int main_menu_hide(void)
{
    if (!main_menu_ctx) {
        ERROR("Main menu context is NULL - call main_menu_create() first");
        return -1;
    }
    menu_hide(main_menu_ctx);
}

int main_menu_toggle(void)
{
    if (!main_menu_ctx) {
        ERROR("Main menu context is NULL - call main_menu_create() first");
        return -1;
    }
    menu_toggle(main_menu_ctx);
}