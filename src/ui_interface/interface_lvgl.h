#ifndef VD_LINK_UI_INTERFACE_H
#define VD_LINK_UI_INTERFACE_H

#include "lvgl.h"
#include "../drm_display.h"
#include "compositor.h"

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
void lvgl_create_test_ui(void);

#endif // VD_LINK_UI_INTERFACE_H
