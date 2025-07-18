#include "interface_lvgl.h"
#include "compositor.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/joystick.h>

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
    lv_obj_t *signal;
    lv_obj_t *bitrate;
    lv_obj_t *battery_charge;
    lv_obj_t *battery;  // Battery percentage label
    lv_obj_t *clock;   // Clock label
    lv_obj_t *curr_button; // Current button label
} ui_elements_t;

typedef struct {
    int signal;
    int battery;
    bool battery_charging;
} ui_values_t;

ui_elements_t ui_elements = {0};
ui_values_t ui_values = {0};

// Joystick handling
static int joystick_fd = -1;
static pthread_t joystick_thread;
static bool joystick_running = false;
static char current_button_text[32] = "none";

// Joystick button names for display
static const char* button_names[] = {
    "B", "A", "X", "Y", "LB", "RB", "LT", "RT", 
    "Select", "Start", "??", "L3", "R3", "UP", "DOWN", "LEFT", "RIGHT"
};

/**
 * Joystick reading thread
 */
static void* joystick_reader_thread(void* arg)
{
    struct js_event event;
    
    printf("[ JOYSTICK ] Starting joystick reader thread\n");
    
    while (joystick_running) {
        if (joystick_fd < 0) {
            // Try to open joystick
            joystick_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
            if (joystick_fd < 0) {
                usleep(1000000); // Wait 1 second before retry
                continue;
            }
            printf("[ JOYSTICK ] Connected to /dev/input/js0\n");
        }
        
        ssize_t bytes = read(joystick_fd, &event, sizeof(event));
        if (bytes == sizeof(event)) {
            // Process joystick event
            if (event.type == JS_EVENT_BUTTON) {
                if (event.value == 1) { // Button pressed
                    if (event.number < sizeof(button_names)/sizeof(button_names[0])) {
                        snprintf(current_button_text, sizeof(current_button_text), 
                                "%s", button_names[event.number]);
                    } else {
                        snprintf(current_button_text, sizeof(current_button_text), 
                                "BTN%d", event.number);
                    }
                    printf("[ JOYSTICK ] Button %d (%s) pressed\n", 
                           event.number, current_button_text);
                } else { // Button released
                    strcpy(current_button_text, "none");
                    printf("[ JOYSTICK ] Button %d released\n", event.number);
                }
                
                // Update UI element if it exists
                if (ui_elements.curr_button) {
                    lv_label_set_text(ui_elements.curr_button, current_button_text);
                }
            } else if (event.type == JS_EVENT_AXIS) {
                // Handle axis movement if needed
                printf("[ JOYSTICK ] Axis %d moved to %d\n", event.number, event.value);
                if (ui_elements.curr_button) {
                    lv_label_set_text_fmt(ui_elements.curr_button, "Axis: %d : %d", 
                                          event.number, event.value);
                }
            }
        } else if (bytes < 0) {
            // No data available or error
            if (errno == ENODEV) {
                printf("[ JOYSTICK ] Device disconnected\n");
                close(joystick_fd);
                joystick_fd = -1;
            }
            usleep(10000); // Wait 10ms
        }
    }
    
    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }
    
    printf("[ JOYSTICK ] Joystick reader thread stopped\n");
    return NULL;
}

/**
 * Initialize joystick handling
 */
static int init_joystick(void)
{
    joystick_running = true;
    
    if (pthread_create(&joystick_thread, NULL, joystick_reader_thread, NULL) != 0) {
        fprintf(stderr, "[ JOYSTICK ] Failed to create joystick thread\n");
        joystick_running = false;
        return -1;
    }
    
    pthread_detach(joystick_thread);
    printf("[ JOYSTICK ] Joystick handling initialized\n");
    return 0;
}

/**
 * Cleanup joystick handling
 */
static void cleanup_joystick(void)
{
    joystick_running = false;
    
    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }
    
    printf("[ JOYSTICK ] Joystick handling cleaned up\n");
}

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
            fprintf(stderr, "Failed to read battery capacity\n");
        }
        fclose(capacity_file);
    }

    // Update both battery elements if they exist
    if (ui_elements.battery) {
        lv_label_set_text_fmt(ui_elements.battery, " %d%%", capacity);
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
        lv_label_set_text(ui_elements.battery_charge, charge_symbol);
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
    printf("[ UI ] Initializing LVGL interface...\n");
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
    disp_drv.full_refresh = 0;  // 1- Force full refresh to avoid partial update issues
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
    
    // Initialize joystick handling
    init_joystick();
    
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
    // Stop joystick handling first
    cleanup_joystick();
    
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

void lvgl_create_ui(void)
{
    pthread_mutex_lock(&lvgl_mutex);

    const lv_font_t *default_font = &lv_font_montserrat_20;

    printf("[ UI ] Creating HUD UI...\n");

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
    ui_elements.battery_charge = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.battery_charge, "_");
    lv_obj_set_style_text_font(ui_elements.battery_charge, default_font, LV_PART_MAIN);
    lv_obj_align(ui_elements.battery_charge, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_text_color(ui_elements.battery_charge, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.battery_charge, LV_OPA_TRANSP, LV_PART_MAIN); // Make label background transparent

    // Battery percentage indicator (top left)
    ui_elements.battery = lv_label_create(top_bar);
    lv_label_set_text(ui_elements.battery, "BAT: 0%");
    lv_obj_set_style_text_font(ui_elements.battery, default_font, LV_PART_MAIN); // Example: Set font size to 16
    lv_obj_align(ui_elements.battery, LV_ALIGN_LEFT_MID, 30, 0);
    lv_obj_set_style_text_color(ui_elements.battery, lv_color_make(0, 255, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_elements.battery, LV_OPA_TRANSP, LV_PART_MAIN); // Make label background transparent

    // Create only one timer for battery update
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

    // // Bottom status bar
    // lv_obj_t *bottom_bar = lv_obj_create(lv_scr_act());
    // lv_obj_set_size(bottom_bar, width - 20, 60);
    // lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    // lv_obj_set_style_bg_color(bottom_bar, lv_color_make(0, 0, 0), LV_PART_MAIN);
    // lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_80, LV_PART_MAIN);
    // lv_obj_set_style_border_width(bottom_bar, 1, LV_PART_MAIN);
    // lv_obj_set_style_border_color(bottom_bar, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_radius(bottom_bar, 5, LV_PART_MAIN);
    // mark_static_object(bottom_bar);

    // // Recording status (bottom center)
    // lv_obj_t *rec_label = lv_label_create(bottom_bar);
    // lv_label_set_text(rec_label, "REC 02:34");
    // lv_obj_align(rec_label, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_text_color(rec_label, lv_color_make(255, 0, 0), LV_PART_MAIN);
    // mark_static_object(rec_label);

    printf("[ LVGL ] Drone HUD UI created successfully\n");
    pthread_mutex_unlock(&lvgl_mutex);

}
