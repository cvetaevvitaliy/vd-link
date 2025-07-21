#ifndef VD_LINK_UI_INTERFACE_H
#define VD_LINK_UI_INTERFACE_H

#include "lvgl.h"
#include "../drm_display.h"
#include "compositor.h"
#include "wfb_status_link.h"

/**
 * Initialize LVGL with the compositor
 * This function sets up LVGL to use the compositor for rendering
 * @return 0 on success, -1 on failure
 */
int ui_interface_init(void);

/**
 * Update LVGL display - should be called periodically
 * This function handles LVGL tasks and sends updates to the compositor
 */
void ui_interface_update(void);

/**
 * Clean up LVGL resources
 */
void ui_interface_deinit(void);

/**
 * Create a test UI element to verify the overlay is working
 * This will create a simple demo UI with various elements
 * to confirm that the LVGL rendering is working properly
 */
void lvgl_create_ui(void);

/**
 * Create a menu UI
 * This function sets up the main menu interface for the application
 */
void lvgl_create_menu();

/**
 * Update the UI with the current WFB status
 * @param st Pointer to the WFB status structure
 */
void ui_update_wfb_ng_telemetry(const wfb_rx_status *st);

/**
 * Show a notification message on the UI
 * @param text The notification text to display
 * @param timeout Duration in milliseconds to show the notification
 */
void show_notification_with_timeout(const char *text);

/**
 * Show a notification message with default timeout
 * @param text The notification text to display
 */
void show_notification(const char *text);


#endif // VD_LINK_UI_INTERFACE_H
