#include "ui_interface.h"
#include "compositor.h"
#include "menu.h"
#include "../log.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>
#include "joystick.h"
#include "tracked_timer.h"


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
    lv_obj_t *signal;
    lv_obj_t *bitrate;
    lv_obj_t *battery_charge;
    lv_obj_t *battery;  // Battery percentage label
    lv_obj_t *clock;   // Clock label
    lv_obj_t *curr_button; // Current button label
    lv_obj_t *notification; // Notification label
    lv_obj_t *notification_bar; // Notification bar
    lv_timer_t *notification_timer; // Timer for notification
} ui_elements_t;

typedef struct {
    int signal;
    int battery;
    bool battery_charging;
} ui_values_t;

ui_elements_t ui_elements = {0};
ui_values_t ui_values = {0};

/**
 * Update drone telemetry values
 */
void ui_update_wfb_ng_telemetry(const wfb_rx_status *st)
{
    if (!st) return;

    if (st->id[0] != 'v') return; // id could be "video rx", "msposd rx", "mavlink rx". We need only "video rx" telemetry

    // Update RSSI
    if (ui_elements.signal) {
        lv_label_set_text_fmt(ui_elements.signal, "RSSI: %d dBm", st->ants[0].rssi_avg);
    }

    // Update bitrate
    if (ui_elements.bitrate) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %.2f Mbps", LV_SYMBOL_WIFI, st->ants[0].bitrate_mbps);
        lv_label_set_text(ui_elements.bitrate, buf);
    }
}

static void update_battery_charge(lv_timer_t *t)
{
    int capacity = 0;
    char status[32] = "Unknown";
    
    // Read battery status
    FILE *status_file = fopen("/sys/class/power_supply/battery/status", "r");
    if (!status_file) {
        perror("Failed to open battery status file");
    } else {
        /* Status could be: Charging, Discharging, Full */
        if (fgets(status, sizeof(status), status_file)) {
            status[strcspn(status, "\n")] = '\0'; // Remove newline character
        }
        fclose(status_file);
    }

    // Read battery capacity
    FILE *capacity_file = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (!capacity_file) {
        perror("Failed to open battery capacity file");
    } else {
        if (fscanf(capacity_file, "%d", &capacity) == 1) {
            ui_values.battery = capacity; // Update battery percentage
        } else {
            ERROR_M("BATTERY", "Failed to read battery capacity");
        }
        fclose(capacity_file);
    }

    if (ui_elements.battery_charge) {
        char* charge_symbol = LV_SYMBOL_BATTERY_EMPTY;
        if (status[0] == 'C' /* Charging */) {
            charge_symbol = LV_SYMBOL_CHARGE;
        } else {
            if (capacity >= 90) {
                charge_symbol = LV_SYMBOL_BATTERY_FULL;
            } else if (capacity >= 75) {
                charge_symbol = LV_SYMBOL_BATTERY_3;
            } else if (capacity >= 50) {
                charge_symbol = LV_SYMBOL_BATTERY_2;
            } else if (capacity >= 25) {
                charge_symbol = LV_SYMBOL_BATTERY_1;
            } else if (capacity > 0) {
                charge_symbol = LV_SYMBOL_BATTERY_EMPTY;
            }
        }
        lv_label_set_text_fmt(ui_elements.battery_charge, "%s %d%%", charge_symbol, capacity);
    }

    // Force full area refresh for left side elements
    lv_obj_invalidate(lv_obj_get_parent(ui_elements.battery_charge));
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
    INFO_M("UI", "Initializing LVGL interface...");
    int ui_width, ui_height, ui_rotate;
    if (drm_get_overlay_frame_size(&ui_width, &ui_height, &ui_rotate) != 0) {
        ERROR_M("LVGL", "Failed to get UI frame size");
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
        ERROR_M("LVGL", "Failed to initialize compositor");
        return -1;
    }
    
    lv_init();
    
    // Allocate LVGL buffers
    size_t buf_size = lvgl_width * lvgl_height * sizeof(lv_color_t);
    buf1 = (lv_color_t *)malloc(buf_size);
    buf2 = (lv_color_t *)malloc(buf_size);
    if (buf1 == NULL || buf2 == NULL) {
        ERROR_M("LVGL", "Failed to allocate display buffers");
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
    disp_drv.full_refresh = 0;  // 1- Force full refresh to avoid partial update issues
    disp_drv.direct_mode = 0;
    disp_drv.antialiasing = 1;
    
    // Register the driver
    disp = lv_disp_drv_register(&disp_drv);
    if (disp == NULL) {
        ERROR_M("LVGL", "Failed to register display driver");
        free(buf1);
        free(buf2);
        buf1 = buf2 = NULL;
        compositor_deinit();
        return -1;
    }
    
    // Create a thread for LVGL tick
    pthread_t tick_thread;
    if (pthread_create(&tick_thread, NULL, (void *)tick_init, NULL) != 0) {
        ERROR_M("LVGL", "Failed to create tick thread");
        free(buf1);
        free(buf2);
        buf1 = buf2 = NULL;
        compositor_deinit();
        return -1;
    }
    pthread_detach(tick_thread);
    
    // Initialize joystick handling
    init_joystick();
    
    INFO_M("LVGL", "Initialized successfully with %dx%d LVGL resolution (DRM: %dx%d, rotation: %d°) using compositor", 
           lvgl_width, lvgl_height, ui_width, ui_height, ui_rotate);
    return 0;
}

void ui_interface_update(void)
{
    if (!buf1 || !buf2) return;
    
    static uint32_t last_update = 0;
    static uint32_t last_cleanup = 0;
    static uint32_t last_present = 0;
    uint32_t current_time = lv_tick_get();
    
    // Limit to ~30 FPS for LVGL updates
    if (current_time - last_update < 33) {
        return;
    }
    last_update = current_time;
    
    pthread_mutex_lock(&lvgl_mutex);
    
    // Handle LVGL timers and drawing
    lv_timer_handler();
    
    // Periodically cleanup timer array (every 5 seconds)
    if (current_time - last_cleanup > 5000) {
        cleanup_tracked_timers();
        last_cleanup = current_time;
    }
    
    pthread_mutex_unlock(&lvgl_mutex);
    
    compositor_present_frame();
}

void ui_interface_deinit(void)
{
    // Stop joystick handling first
    cleanup_joystick();
    
    pthread_mutex_lock(&lvgl_mutex);

    // Delete our tracked timers
    remove_all_tracked_timers();
    
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
    
    INFO_M("LVGL", "Deinitialized");
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

void lvgl_create_ui(void)
{
    pthread_mutex_lock(&lvgl_mutex);

    const lv_font_t *default_font = &lv_font_montserrat_20;

    INFO_M("UI", "Creating HUD UI...");

    // Check if display is initialized
    if (!disp) {
        ERROR_M("LVGL", "Display not initialized, cannot create UI");
        pthread_mutex_unlock(&lvgl_mutex);
        return;
    }

    // Get the screen dimensions from our registered display
    lv_coord_t width = lv_disp_get_hor_res(disp);
    lv_coord_t height = lv_disp_get_ver_res(disp);
    
    INFO_M("LVGL", "Creating drone camera HUD UI for screen %dx%d", (int)width, (int)height);    // Force a full screen refresh before creating UI
    lv_obj_invalidate(lv_scr_act());

    // Set semi-transparent background for HUD overlay (not fully transparent)
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(0, 0, 0), LV_PART_MAIN);
    
    INFO_M("LVGL", "Screen background set");

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
    INFO_M("LVGL", "Top bar created");

    // Battery indicator (top left)
    ui_elements.battery_charge = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.battery_charge, "_ ?%%");
    lv_obj_set_style_text_font(ui_elements.battery_charge, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.battery_charge, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_text_color(ui_elements.battery_charge, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.battery_charge, LV_OPA_TRANSP, LV_PART_MAIN); // Make label background transparent

    create_tracked_timer(update_battery_charge, 1000, NULL);

    // Signal strength (top center-right)
    ui_elements.signal = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.signal, "RSSI: 0dBm");
    lv_obj_set_style_text_font(ui_elements.signal, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.signal, LV_ALIGN_RIGHT_MID, -120, 0);
    lv_obj_set_style_text_color(ui_elements.signal, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.signal, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(ui_elements.signal);

    // Bitrate indicator (top center)
    ui_elements.bitrate = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.bitrate, LV_SYMBOL_WIFI" 0Mbps");
    lv_obj_set_style_text_font(ui_elements.bitrate, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.bitrate, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(ui_elements.bitrate, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.bitrate, LV_OPA_TRANSP, LV_PART_MAIN);
    mark_static_object(ui_elements.bitrate);

    // Digital clock (top right)
    ui_elements.clock = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.clock, "00:00:00");
    lv_obj_set_style_text_font(ui_elements.clock, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.clock, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_set_style_text_color(ui_elements.clock, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.clock, LV_OPA_TRANSP, LV_PART_MAIN);
    create_tracked_timer(update_clock, 1000, ui_elements.clock);

    ui_elements.curr_button = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.curr_button, "none");
    lv_obj_set_style_text_font(ui_elements.curr_button, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.curr_button, LV_ALIGN_CENTER, 150, 0);
    lv_obj_set_style_text_color(ui_elements.curr_button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.curr_button, LV_OPA_TRANSP, LV_PART_MAIN);

    // Bottom notification bar
    ui_elements.notification_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_elements.notification_bar, width / 2, 60);
    lv_obj_align(ui_elements.notification_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(ui_elements.notification_bar, lv_color_make(0, 0, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.notification_bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_elements.notification_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_elements.notification_bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(ui_elements.notification_bar, 5, LV_PART_MAIN);
    mark_static_object(ui_elements.notification_bar);

    // Recording status (bottom center)
    ui_elements.notification = lv_label_create(ui_elements.notification_bar);
    lv_label_set_text(ui_elements.notification, "Starting...");
    lv_obj_set_style_text_font(ui_elements.notification, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.notification, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(ui_elements.notification, lv_color_make(255, 128, 0), LV_PART_MAIN);
    mark_static_object(ui_elements.notification);

    show_notification_with_timeout("Starting...");

    INFO_M("LVGL", "Drone HUD UI created successfully");
    pthread_mutex_unlock(&lvgl_mutex);

    lvgl_create_menu();
}

/**
 * Direct notification timeout callback - hides notification immediately
 * Delete here to avoid issues rendering UI from a timer callback
 */
static void hide_notification_callback(lv_timer_t *timer) 
{
    if (ui_elements.notification_bar) {
        lv_obj_add_flag(ui_elements.notification_bar, LV_OBJ_FLAG_HIDDEN);
    }

    ui_elements.notification_timer = NULL;
}

void show_notification_with_timeout(const char *text)
{
    show_notification(text);
    
    if (ui_elements.notification_timer) {
        lv_timer_reset(ui_elements.notification_timer);
    } else {
        ui_elements.notification_timer = create_tracked_timer(hide_notification_callback, 1500, NULL);
        lv_timer_set_repeat_count(ui_elements.notification_timer, 1); // Run only once
    }
}

void show_notification(const char *text)
{
        if (ui_elements.notification && ui_elements.notification_bar) {
        lv_label_set_text(ui_elements.notification, text);
        lv_obj_clear_flag(ui_elements.notification_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        ERROR_M("LVGL", "Notification label not initialized");
    }
}

void lvgl_create_menu()
{
    pthread_mutex_lock(&lvgl_mutex);

    INFO_M("LVGL", "Creating menu UI...");

    // Check if display is initialized
    if (!disp) {
        ERROR_M("LVGL", "Display not initialized, cannot create menu UI");
        pthread_mutex_unlock(&lvgl_mutex);
        return;
    }

    // Get the screen dimensions from our registered display
    lv_coord_t width = lv_disp_get_hor_res(disp);
    lv_coord_t height = lv_disp_get_ver_res(disp);

    INFO_M("LVGL", "Initializing menu");
    
    // Initialize and create menu
    menu_init();
    menu_create_ui();
    
    INFO_M("LVGL", "Menu initialized successfully");
    pthread_mutex_unlock(&lvgl_mutex);
}