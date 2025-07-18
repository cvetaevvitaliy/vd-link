#include "compositor.h"
#include "../drm_display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// Compositor state
static struct {
    uint32_t *composite_buffer;    // Final composite buffer
    uint32_t *ui_layer;           // UI layer buffer
    uint8_t *osd_layer;           // OSD layer buffer (8-bit indexed)
    uint32_t *osd_argb_layer;     // OSD layer buffer (32-bit ARGB for high quality)
    int width;                    // Original width (before rotation)
    int height;                   // Original height (before rotation)
    int rotation;                 // Rotation angle (0, 90, 180, 270)
    int output_width;             // Final output width (after rotation)
    int output_height;            // Final output height (after rotation)
    bool ui_dirty;                // UI layer needs update
    bool osd_dirty;               // OSD layer needs update
    bool osd_argb_dirty;          // ARGB OSD layer needs update
    bool use_argb_osd;            // Whether to use ARGB OSD (higher quality)
    pthread_mutex_t compositor_mutex;
    bool initialized;
} compositor = {
    .composite_buffer = NULL,
    .ui_layer = NULL,
    .osd_layer = NULL,
    .osd_argb_layer = NULL,
    .width = 0,
    .height = 0,
    .rotation = 0,
    .output_width = 0,
    .output_height = 0,
    .ui_dirty = false,
    .osd_dirty = false,
    .osd_argb_dirty = false,
    .use_argb_osd = false,
    .compositor_mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false
};

int compositor_init(int width, int height, int rotation)
{
    pthread_mutex_lock(&compositor.compositor_mutex);
    
    if (compositor.initialized) {
        compositor_deinit();
    }
    
    compositor.width = width;
    compositor.height = height;
    compositor.rotation = rotation;
    
    // Keep same dimensions - layers handle their own rotation
    compositor.output_width = width;
    compositor.output_height = height;
    
    // Allocate buffers - all use the same dimensions
    compositor.composite_buffer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    compositor.ui_layer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    compositor.osd_layer = (uint8_t*)calloc(width * height, sizeof(uint8_t));
    compositor.osd_argb_layer = (uint32_t*)calloc(width * height, sizeof(uint32_t));
    
    if (!compositor.composite_buffer || !compositor.ui_layer || !compositor.osd_layer || !compositor.osd_argb_layer) {
        fprintf(stderr, "[ COMPOSITOR ] Failed to allocate buffers\n");
        compositor_deinit();
        pthread_mutex_unlock(&compositor.compositor_mutex);
        return -1;
    }
    
    compositor.initialized = true;
    compositor.ui_dirty = false;
    compositor.osd_dirty = false;
    
    printf("[ COMPOSITOR ] Initialized with %dx%d resolution, rotation %d°, output %dx%d\n", 
           width, height, rotation, compositor.output_width, compositor.output_height);
    
    pthread_mutex_unlock(&compositor.compositor_mutex);
    return 0;
}

void compositor_update_ui(const uint32_t *ui_buffer, int width, int height,
                         int area_x, int area_y, int area_width, int area_height)
{
    if (!compositor.initialized || !ui_buffer) return;
    
    pthread_mutex_lock(&compositor.compositor_mutex);
    
    // Copy the updated area from UI buffer to our UI layer
    for (int y = 0; y < area_height; y++) {
        int src_y = y;
        int dst_y = area_y + y;
        
        if (dst_y >= compositor.height || src_y >= height) continue;
        
        for (int x = 0; x < area_width; x++) {
            int src_x = x;
            int dst_x = area_x + x;
            
            if (dst_x >= compositor.width || src_x >= width) continue;
            
            int src_idx = src_y * area_width + src_x;
            int dst_idx = dst_y * compositor.width + dst_x;
            
            compositor.ui_layer[dst_idx] = ui_buffer[src_idx];
        }
    }
    
    compositor.ui_dirty = true;
    
    pthread_mutex_unlock(&compositor.compositor_mutex);
}

void compositor_update_osd_argb(const uint32_t *osd_buffer, int width, int height)
{
    if (!compositor.initialized || !osd_buffer) return;
    
    pthread_mutex_lock(&compositor.compositor_mutex);
    
    // Copy ARGB OSD buffer directly for best quality
    int copy_width = (width < compositor.width) ? width : compositor.width;
    int copy_height = (height < compositor.height) ? height : compositor.height;
    
    for (int y = 0; y < copy_height; y++) {
        memcpy(&compositor.osd_argb_layer[y * compositor.width],
               &osd_buffer[y * width],
               copy_width * sizeof(uint32_t));
    }
    
    compositor.use_argb_osd = true;
    compositor.osd_argb_dirty = true;
    
    pthread_mutex_unlock(&compositor.compositor_mutex);
}

// OSD color palette (typical for MSP OSD)
static const uint32_t osd_palette[16] = {
    0x00000000, // 0: Transparent
    0xFF000000, // 1: Black
    0xFFFFFFFF, // 2: White  
    0xFF808080, // 3: Gray
    0xFFFF0000, // 4: Red
    0xFF00FF00, // 5: Green
    0xFF0000FF, // 6: Blue
    0xFFFFFF00, // 7: Yellow
    0xFFFF00FF, // 8: Magenta
    0xFF00FFFF, // 9: Cyan
    0xFF800000, // 10: Dark Red
    0xFF008000, // 11: Dark Green
    0xFF000080, // 12: Dark Blue
    0xFF808000, // 13: Dark Yellow
    0xFF800080, // 14: Dark Magenta
    0xFF008080  // 15: Dark Cyan
};

// Transform coordinates based on rotation
static void transform_coordinates(int src_x, int src_y, int *dst_x, int *dst_y, int rotation, int src_width, int src_height)
{
    switch (rotation) {
    case 0:
        *dst_x = src_x;
        *dst_y = src_y;
        break;
    case 90:
        *dst_x = src_height - 1 - src_y;
        *dst_y = src_x;
        break;
    case 180:
        *dst_x = src_width - 1 - src_x;
        *dst_y = src_height - 1 - src_y;
        break;
    case 270:
        *dst_x = src_y;
        *dst_y = src_width - 1 - src_x;
        break;
    default:
        *dst_x = src_x;
        *dst_y = src_y;
        break;
    }
}

static void compose_layers(void)
{
    // Clear composite buffer with transparent background
    memset(compositor.composite_buffer, 0, 
           compositor.output_width * compositor.output_height * sizeof(uint32_t));
    
    // Simple layer composition without rotation - layers handle their own rotation
    for (int y = 0; y < compositor.height; y++) {
        for (int x = 0; x < compositor.width; x++) {
            int idx = y * compositor.width + x;
            uint32_t final_pixel = 0x00000000; // Start with transparent
            
            // Layer 1: UI (LVGL) - base layer
            uint32_t ui_pixel = compositor.ui_layer[idx];
            uint8_t ui_alpha = (ui_pixel >> 24) & 0xFF;
            uint8_t ui_r = (ui_pixel >> 16) & 0xFF;
            uint8_t ui_g = (ui_pixel >> 8) & 0xFF;
            uint8_t ui_b = ui_pixel & 0xFF;
            
            // Check if this is not a UI background pixel
            bool ui_background = (ui_alpha == 0) || (ui_r == 0 && ui_g == 0 && ui_b == 0);
            
            if (!ui_background) {
                // Show UI with semi-transparency
                uint8_t blend_alpha = 200; // 78% opacity for UI base
                final_pixel = (blend_alpha << 24) | (ui_r << 16) | (ui_g << 8) | ui_b;
            }
            
            // Layer 2: OSD (on top of UI) - use ARGB if available for best quality
            if (compositor.use_argb_osd) {
                uint32_t osd_pixel = compositor.osd_argb_layer[idx];
                uint8_t osd_alpha = (osd_pixel >> 24) & 0xFF;
                if (osd_alpha > 0) {
                    // Use original ARGB OSD pixel for maximum quality
                    final_pixel = osd_pixel;
                }
            } else {
                // Fallback to indexed OSD
                uint8_t osd_pixel = compositor.osd_layer[idx];
                if (osd_pixel != 0) {
                    uint32_t osd_color = osd_palette[osd_pixel & 0x0F];
                    if ((osd_color & 0xFF000000) != 0) {
                        final_pixel = osd_color;
                    }
                }
            }
            
            compositor.composite_buffer[idx] = final_pixel;
        }
    }
}

void compositor_present_frame(void)
{
    if (!compositor.initialized) return;
    
    pthread_mutex_lock(&compositor.compositor_mutex);
    
    // Only compose if something has changed
    if (!compositor.ui_dirty && !compositor.osd_dirty) {
        pthread_mutex_unlock(&compositor.compositor_mutex);
        return;
    }
    
    // Compose the layers
    compose_layers();
    
    // Get DRM buffer and copy our composite
    void *drm_buffer = drm_get_next_overlay_fb();
    if (drm_buffer) {
        memcpy(drm_buffer, compositor.composite_buffer,
               compositor.output_width * compositor.output_height * sizeof(uint32_t));
        
        // Push the frame to DRM
        drm_push_new_overlay_frame();
    }
    
    // Reset dirty flags
    compositor.ui_dirty = false;
    compositor.osd_dirty = false;
    
    pthread_mutex_unlock(&compositor.compositor_mutex);
}

const uint32_t* compositor_get_buffer(void)
{
    return compositor.composite_buffer;
}

void compositor_deinit(void)
{
    pthread_mutex_lock(&compositor.compositor_mutex);
    
    if (compositor.composite_buffer) {
        free(compositor.composite_buffer);
        compositor.composite_buffer = NULL;
    }
    
    if (compositor.ui_layer) {
        free(compositor.ui_layer);
        compositor.ui_layer = NULL;
    }
    
    if (compositor.osd_layer) {
        free(compositor.osd_layer);
        compositor.osd_layer = NULL;
    }
    
    compositor.initialized = false;
    compositor.ui_dirty = false;
    compositor.osd_dirty = false;
    
    printf("[ COMPOSITOR ] Deinitialized\n");
    
    pthread_mutex_unlock(&compositor.compositor_mutex);
}
