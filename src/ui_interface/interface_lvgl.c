#include "interface_lvgl.h"
#include "compositor.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

// Store our timers for cleanup
#define MAX_TIMERS 10
static lv_timer_t *app_timers[MAX_TIMERS] = {0};
static int timer_count = 0;

// LVGL display buffer
static lv_disp_draw_buf_t disp_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;  // Add second buffer for double buffering
static lv_disp_drv_t disp_drv;
static lv_disp_t *disp;
static pthread_mutex_t lvgl_mutex = PTHREAD_MUTEX_INITIALIZER;

// Store rotation for UI layer rendering
static int ui_rotation = 0;

// Structure for drone telemetry animation
typedef struct {
    lv_obj_t *alt_value;
    lv_obj_t *speed_value;
    lv_obj_t *throttle_label;
    int alt_counter;
    int speed_counter;
    int throttle_counter;
} drone_telemetry_t;

/**
 * Update drone telemetry values
 */
static void update_drone_telemetry(lv_timer_t *t)
{
    drone_telemetry_t *data = (drone_telemetry_t *)t->user_data;
    if (!data) return;
    
    // Update altitude (simulate slowly changing altitude)
    data->alt_counter++;
    float altitude = 125.0f + sin(data->alt_counter * 0.05f) * 10.0f;
    char alt_buf[16];
    snprintf(alt_buf, sizeof(alt_buf), "%.1fm", altitude);
    if (data->alt_value) lv_label_set_text(data->alt_value, alt_buf);
    
    // Update speed (simulate varying speed)
    data->speed_counter++;
    float speed = 15.0f + sin(data->speed_counter * 0.08f) * 5.0f;
    char speed_buf[16];
    snprintf(speed_buf, sizeof(speed_buf), "%.1fm/s", speed);
    if (data->speed_value) lv_label_set_text(data->speed_value, speed_buf);
    
    // Update throttle
    data->throttle_counter++;
    int throttle = 65 + (int)(sin(data->throttle_counter * 0.03f) * 20);
    char throttle_buf[16];
    snprintf(throttle_buf, sizeof(throttle_buf), "THR: %d%%", throttle);
    if (data->throttle_label) lv_label_set_text(data->throttle_label, throttle_buf);
}

/**
 * Clock update function for the UI
 */
static void update_clock(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)t->user_data;
    time_t raw_time;
    struct tm *time_info;
    char buffer[32];
    
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, sizeof(buffer), "%H:%M:%S", time_info);
    lv_label_set_text(label, buffer);
}

// Static UI buffer to reduce allocations
static uint32_t *ui_buffer = NULL;
static int ui_buffer_width = 0;
static int ui_buffer_height = 0;

/**
 * LVGL flush callback - transforms swapped LVGL buffer to DRM buffer with rotation
 */
static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    static uint32_t last_flush_time = 0;
    uint32_t current_time = lv_tick_get();
    
    // Throttle frequent flushes
    if (current_time - last_flush_time < 20) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
    last_flush_time = current_time;
    
    int32_t src_width = area->x2 - area->x1 + 1;
    int32_t src_height = area->y2 - area->y1 + 1;
    
    // Get LVGL and DRM dimensions
    int lvgl_width = lv_disp_get_hor_res(disp);
    int lvgl_height = lv_disp_get_ver_res(disp);
    
    int drm_width, drm_height;
    if (ui_rotation == 90 || ui_rotation == 270) {
        drm_width = lvgl_height;
        drm_height = lvgl_width;
    } else {
        drm_width = lvgl_width;
        drm_height = lvgl_height;
    }
    
    // Allocate buffer for DRM dimensions
    if (!ui_buffer || ui_buffer_width != drm_width || ui_buffer_height != drm_height) {
        if (ui_buffer) free(ui_buffer);
        ui_buffer = (uint32_t*)calloc(drm_width * drm_height, sizeof(uint32_t));
        ui_buffer_width = drm_width;
        ui_buffer_height = drm_height;
        if (!ui_buffer) {
            lv_disp_flush_ready(disp_drv);
            return;
        }
    }
    
    // Transform pixels with rotation
    for (int y = 0; y < src_height; y++) {
        for (int x = 0; x < src_width; x++) {
            int src_idx = y * src_width + x;
            lv_color_t color = color_p[src_idx];
            
            uint8_t r = color.ch.red;
            uint8_t g = color.ch.green;
            uint8_t b = color.ch.blue;
            
            // Alpha handling
            uint32_t alpha = (r == 0 && g == 0 && b == 0) ? 0x60000000 : 0xFF000000;
            uint32_t pixel = alpha | (r << 16) | (g << 8) | b;
            
            // Calculate positions
            int lvgl_x = area->x1 + x;
            int lvgl_y = area->y1 + y;
            
            // Transform coordinates based on rotation
            int drm_x, drm_y;
            switch (ui_rotation) {
            case 90:
                drm_x = drm_width - 1 - lvgl_y;
                drm_y = lvgl_x;
                break;
            case 270:
                drm_x = lvgl_y;
                drm_y = drm_height - 1 - lvgl_x;
                break;
            case 180:
                drm_x = drm_width - 1 - lvgl_x;
                drm_y = drm_height - 1 - lvgl_y;
                break;
            default: // 0 degrees
                drm_x = lvgl_x;
                drm_y = lvgl_y;
                break;
            }
            
            // Store pixel in DRM buffer
            if (drm_x >= 0 && drm_y >= 0 && drm_x < drm_width && drm_y < drm_height) {
                ui_buffer[drm_y * drm_width + drm_x] = pixel;
            }
        }
    }
    
    // Send to compositor
    compositor_update_ui(ui_buffer, drm_width, drm_height, 0, 0, drm_width, drm_height);
    
    lv_disp_flush_ready(disp_drv);
}

/**
 * LVGL tick provider 
 */
static int tick_init(void)
{
    // Create a 1ms periodic timer
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;  // 1ms
    
    // Call lv_tick_inc periodically
    while (1) {
        nanosleep(&ts, NULL);
        lv_tick_inc(1);
    }
    
    return 0;
}

int ui_interface_init(void)
{
    int ui_width, ui_height, ui_rotate;
    if (drm_get_overlay_frame_size(&ui_width, &ui_height, &ui_rotate) != 0) {
        fprintf(stderr, "[ LVGL ] Failed to get UI frame size\n");
        return -1;
    }
    
    ui_rotation = ui_rotate;
    
    // Swap dimensions for LVGL when rotated
    int lvgl_width, lvgl_height;
    if (ui_rotate == 90 || ui_rotate == 270) {
        lvgl_width = ui_height;
        lvgl_height = ui_width;
    } else {
        lvgl_width = ui_width;
        lvgl_height = ui_height;
    }
    
    // Initialize compositor and LVGL
    if (compositor_init(ui_width, ui_height, ui_rotate) != 0) {
        fprintf(stderr, "[ LVGL ] Failed to initialize compositor\n");
        return -1;
    }
    
    lv_init();
    
    // Allocate LVGL buffers
    size_t buf_size = lvgl_width * lvgl_height * sizeof(lv_color_t);
    buf1 = (lv_color_t *)malloc(buf_size);
    buf2 = (lv_color_t *)malloc(buf_size);
    if (buf1 == NULL || buf2 == NULL) {
        fprintf(stderr, "[ LVGL ] Failed to allocate display buffers\n");
        if (buf1) free(buf1);
        if (buf2) free(buf2);
        buf1 = buf2 = NULL;
        compositor_deinit();
        return -1;
    }
    
    // Setup LVGL display
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, lvgl_width * lvgl_height);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.hor_res = lvgl_width;
    disp_drv.ver_res = lvgl_height;
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = 0;
    disp_drv.antialiasing = 1;
    
    // Register the driver
    disp = lv_disp_drv_register(&disp_drv);
    if (disp == NULL) {
        fprintf(stderr, "[ LVGL ] Failed to register display driver\n");
        free(buf1);
        free(buf2);
        buf1 = buf2 = NULL;
        compositor_deinit();
        return -1;
    }
    
    // Create a thread for LVGL tick
    pthread_t tick_thread;
    if (pthread_create(&tick_thread, NULL, (void *)tick_init, NULL) != 0) {
        fprintf(stderr, "[ LVGL ] Failed to create tick thread\n");
        free(buf1);
        free(buf2);
        buf1 = buf2 = NULL;
        compositor_deinit();
        return -1;
    }
    pthread_detach(tick_thread);
    
    printf("[ LVGL ] Initialized successfully with %dx%d LVGL resolution (DRM: %dx%d, rotation: %d°) using compositor\n", 
           lvgl_width, lvgl_height, ui_width, ui_height, ui_rotate);
    return 0;
}

void ui_interface_update(void)
{
    if (!buf1 || !buf2) return;
    
    static uint32_t last_update = 0;
    uint32_t current_time = lv_tick_get();
    
    // Limit to ~30 FPS
    if (current_time - last_update < 33) {
        return;
    }
    last_update = current_time;
    
    pthread_mutex_lock(&lvgl_mutex);
    lv_timer_handler();
    pthread_mutex_unlock(&lvgl_mutex);
    
    compositor_present_frame();
}

void ui_interface_deinit(void)
{
    pthread_mutex_lock(&lvgl_mutex);
    
    // Delete all animations to prevent callbacks after deinitialization
    // Delete our tracked timers
    for (int i = 0; i < timer_count; i++) {
        if (app_timers[i]) {
            lv_timer_del(app_timers[i]);
            app_timers[i] = NULL;
        }
    }
    timer_count = 0;
    
    // Clean up LVGL
    if (disp != NULL) {
        lv_disp_remove(disp);
        disp = NULL;
    }
    
    // Free the buffers
    if (buf1 != NULL) {
        free(buf1);
        buf1 = NULL;
    }
    if (buf2 != NULL) {
        free(buf2);
        buf2 = NULL;
    }
    
    // Clean up static UI buffer
    if (ui_buffer != NULL) {
        free(ui_buffer);
        ui_buffer = NULL;
        ui_buffer_width = 0;
        ui_buffer_height = 0;
    }
    
    pthread_mutex_unlock(&lvgl_mutex);
    pthread_mutex_destroy(&lvgl_mutex);
    
    // Clean up compositor
    compositor_deinit();
    
    printf("[ LVGL ] Deinitialized\n");
}

/**
 * Helper function to create and track a timer
 */
static lv_timer_t* create_tracked_timer(lv_timer_cb_t timer_cb, uint32_t period, void *user_data)
{
    if (timer_count >= MAX_TIMERS) {
        printf("[ LVGL ] Warning: Maximum number of timers reached\n");
        return NULL;
    }
    
    lv_timer_t *timer = lv_timer_create(timer_cb, period, user_data);
    if (timer) {
        app_timers[timer_count++] = timer;
    }
    return timer;
}

/**
 * Mark static objects (placeholder for optimization)
 */
static void mark_static_object(lv_obj_t *obj)
{
    if (obj == NULL) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void lvgl_create_test_ui(void)
{
    pthread_mutex_lock(&lvgl_mutex);
    
    // Check if display is initialized
    if (!disp) {
        printf("[ LVGL ] Display not initialized, cannot create UI\n");
        pthread_mutex_unlock(&lvgl_mutex);
        return;
    }
    
    // Get the screen dimensions from our registered display
    lv_coord_t width = lv_disp_get_hor_res(disp);
    lv_coord_t height = lv_disp_get_ver_res(disp);
    
    printf("[ LVGL ] Creating drone camera HUD UI for screen %dx%d\n", (int)width, (int)height);
    
    // Force a full screen refresh before creating UI
    lv_obj_invalidate(lv_scr_act());
    
    // Set semi-transparent background for HUD overlay (not fully transparent)
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(0, 0, 0), LV_PART_MAIN);
    
    printf("[ LVGL ] Screen background set\n");
    
    // Create top status bar
    lv_obj_t *top_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(top_bar, width - 20, 50);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(top_bar, lv_color_make(0, 0, 255), LV_PART_MAIN); // Set blue background
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_50, LV_PART_MAIN); // Make background semi-transparent
    lv_obj_set_style_border_width(top_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(top_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(top_bar, 5, LV_PART_MAIN);
    mark_static_object(top_bar);
    printf("[ LVGL ] Top bar created\n");
    
    // Battery indicator (top left)
    lv_obj_t *battery_label = lv_label_create(top_bar);
    lv_label_set_text(battery_label, "BAT: 87%");
    lv_obj_align(battery_label, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_label, LV_OPA_TRANSP, LV_PART_MAIN); // Make label background transparent
    mark_static_object(battery_label);
    
    // GPS status (top center-left)
    lv_obj_t *gps_label = lv_label_create(top_bar);
    lv_label_set_text(gps_label, "GPS: 12");
    lv_obj_align(gps_label, LV_ALIGN_LEFT_MID, 120, 0);
    lv_obj_set_style_text_color(gps_label, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gps_label, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(gps_label);
    
    // Flight mode (top center)
    lv_obj_t *mode_label = lv_label_create(top_bar);
    lv_label_set_text(mode_label, "STAB");
    lv_obj_align(mode_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(mode_label, lv_color_make(255, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mode_label, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(mode_label);
    
    // Signal strength (top center-right)
    lv_obj_t *signal_label = lv_label_create(top_bar);
    lv_label_set_text(signal_label, "RSSI: -45dBm");
    lv_obj_align(signal_label, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_set_style_text_color(signal_label, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(signal_label, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(signal_label);
    
    // Digital clock (top right)
    lv_obj_t *clock_label = lv_label_create(top_bar);
    lv_obj_align(clock_label, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_set_style_text_color(clock_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(clock_label, LV_OPA_TRANSP, LV_PART_MAIN);
    create_tracked_timer(update_clock, 1000, clock_label);
    
    // Left side altitude bar
    lv_obj_t *alt_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(alt_container, 80, height - 120);
    lv_obj_align(alt_container, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(alt_container, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alt_container, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(alt_container, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(alt_container, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(alt_container, 5, LV_PART_MAIN);
    mark_static_object(alt_container);
    
    printf("[ LVGL ] Altitude container created\n");
    
    // Altitude label
    lv_obj_t *alt_title = lv_label_create(alt_container);
    lv_label_set_text(alt_title, "ALT");
    lv_obj_align(alt_title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(alt_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alt_title, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(alt_title);
    
    // Altitude value (animated)
    lv_obj_t *alt_value = lv_label_create(alt_container);
    lv_label_set_text(alt_value, "125.3m");
    lv_obj_align(alt_value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(alt_value, lv_color_make(0, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alt_value, LV_OPA_TRANSP, LV_PART_MAIN);
    
    // Right side speed bar
    lv_obj_t *speed_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(speed_container, 80, height - 120);
    lv_obj_align(speed_container, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(speed_container, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speed_container, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(speed_container, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(speed_container, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(speed_container, 5, LV_PART_MAIN);
    mark_static_object(speed_container);
    
    // Speed label
    lv_obj_t *speed_title = lv_label_create(speed_container);
    lv_label_set_text(speed_title, "SPD");
    lv_obj_align(speed_title, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(speed_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speed_title, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(speed_title);
    
    // Speed value (animated)
    lv_obj_t *speed_value = lv_label_create(speed_container);
    lv_label_set_text(speed_value, "15.2m/s");
    lv_obj_align(speed_value, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(speed_value, lv_color_make(0, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speed_value, LV_OPA_TRANSP, LV_PART_MAIN);
    
    // Bottom status bar
    lv_obj_t *bottom_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bottom_bar, width - 20, 60);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bottom_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(bottom_bar, 5, LV_PART_MAIN);
    mark_static_object(bottom_bar);
    
    // Distance to home (bottom left)
    lv_obj_t *home_dist_label = lv_label_create(bottom_bar);
    lv_label_set_text(home_dist_label, "HOME: 324m");
    lv_obj_align(home_dist_label, LV_ALIGN_LEFT_MID, 15, -10);
    lv_obj_set_style_text_color(home_dist_label, lv_color_make(255, 255, 0), LV_PART_MAIN);
    mark_static_object(home_dist_label);
    
    // Drone position coordinates (bottom center-left)
    lv_obj_t *coords_label = lv_label_create(bottom_bar);
    lv_label_set_text(coords_label, "50.4501°N 30.5234°E");
    lv_obj_align(coords_label, LV_ALIGN_LEFT_MID, 15, 10);
    lv_obj_set_style_text_color(coords_label, lv_color_make(200, 200, 200), LV_PART_MAIN);
    mark_static_object(coords_label);
    
    // Recording status (bottom center)
    lv_obj_t *rec_label = lv_label_create(bottom_bar);
    lv_label_set_text(rec_label, "REC 02:34");
    lv_obj_align(rec_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(rec_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
    mark_static_object(rec_label);
    
    // Throttle/Power level (bottom right)
    lv_obj_t *throttle_label = lv_label_create(bottom_bar);
    lv_label_set_text(throttle_label, "THR: 65%");
    lv_obj_align(throttle_label, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_set_style_text_color(throttle_label, lv_color_make(255, 165, 0), LV_PART_MAIN);
    
    // Central crosshair/target indicator
    lv_obj_t *crosshair = lv_obj_create(lv_scr_act());
    lv_obj_set_size(crosshair, 40, 40);
    lv_obj_align(crosshair, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(crosshair, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(crosshair, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(crosshair, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(crosshair, 20, LV_PART_MAIN);
    mark_static_object(crosshair);
    
    // Add center dot
    lv_obj_t *center_dot = lv_obj_create(crosshair);
    lv_obj_set_size(center_dot, 4, 4);
    lv_obj_align(center_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(center_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(center_dot, 2, LV_PART_MAIN);
    mark_static_object(center_dot);
    
    // Gimbal angle indicator (near crosshair)
    lv_obj_t *gimbal_label = lv_label_create(lv_scr_act());
    lv_label_set_text(gimbal_label, "CAM: -15°");
    lv_obj_align(gimbal_label, LV_ALIGN_CENTER, 60, 60);
    lv_obj_set_style_text_color(gimbal_label, lv_color_make(255, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(gimbal_label, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gimbal_label, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gimbal_label, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(gimbal_label, 3, LV_PART_MAIN);
    mark_static_object(gimbal_label);
    
    // Create telemetry animation
    drone_telemetry_t *telemetry_data = (drone_telemetry_t*)malloc(sizeof(drone_telemetry_t));
    if (telemetry_data) {
        telemetry_data->alt_value = alt_value;
        telemetry_data->speed_value = speed_value;
        telemetry_data->throttle_label = throttle_label;
        telemetry_data->alt_counter = 0;
        telemetry_data->speed_counter = 0;
        telemetry_data->throttle_counter = 0;
        
        // Create timer for telemetry updates
        create_tracked_timer(update_drone_telemetry, 200, telemetry_data);
    }
    
    printf("[ LVGL ] Drone HUD UI created successfully\n");
    pthread_mutex_unlock(&lvgl_mutex);
}
