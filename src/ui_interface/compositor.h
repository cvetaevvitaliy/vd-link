#ifndef VD_LINK_COMPOSITOR_H
#define VD_LINK_COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Compositor for merging UI and OSD layers
 * This module manages the composition of LVGL UI and MSP OSD overlays
 * before sending the final result to DRM for display
 */

/**
 * Initialize the compositor
 * @param width Screen width (before rotation)
 * @param height Screen height (before rotation)
 * @param rotation Rotation angle (0, 90, 180, 270 degrees)
 * @return 0 on success, -1 on failure
 */
int compositor_init(int width, int height, int rotation);

/**
 * Update the UI layer in the compositor
 * @param ui_buffer LVGL UI buffer data (ARGB8888 format)
 * @param width Buffer width
 * @param height Buffer height
 * @param area_x X coordinate of the updated area
 * @param area_y Y coordinate of the updated area
 * @param area_width Width of the updated area
 * @param area_height Height of the updated area
 */
void compositor_update_ui(const uint32_t *ui_buffer, int width, int height,
                         int area_x, int area_y, int area_width, int area_height);


/**
 * Update the OSD layer in the compositor with ARGB data
 * @param osd_buffer MSP OSD buffer data (32-bit ARGB)
 * @param width Buffer width
 * @param height Buffer height
 */
void compositor_update_osd_argb(const uint32_t *osd_buffer, int width, int height);

/**
 * Compose and push the final frame to DRM
 * This merges UI and OSD layers and sends the result to DRM display
 */
void compositor_present_frame(void);

/**
 * Get the current composite buffer (for debugging)
 * @return Pointer to the composite buffer
 */
const uint32_t* compositor_get_buffer(void);

/**
 * Clean up compositor resources
 */
void compositor_deinit(void);

#endif // VD_LINK_COMPOSITOR_H
