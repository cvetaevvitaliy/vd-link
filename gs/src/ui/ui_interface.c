#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "ui_interface.h"
#include "device_type.h"

extern _Atomic int termination_requested;
static const char *module_name_str = "UI";

static lv_obj_t *signal_strength;
static lv_obj_t *bitrate;
static lv_obj_t *battery_charge;
static lv_obj_t *clock_l;   // Clock label

static lv_obj_t *drone_telemetry_panel;
static lv_obj_t *drone_cpu_temp;
static lv_obj_t *drone_cpu_usage;

static lv_obj_t *notification_area; // Notification area
static lv_obj_t *notification_label; // Notification label
static lv_timer_t *notification_timer; // Timer for notifications

lv_color_t color_ht_main = LV_COLOR_MAKE(0x12, 0x14, 0x1A);
lv_color_t color_ht_secondary = LV_COLOR_MAKE(0x28, 0x2A, 0x31);
lv_color_t color_ht_accent = LV_COLOR_MAKE(0x5B, 0x9F, 0xFF);

// WFB telemetry variables
static float last_bitrate_mbps = 0.0f;
static float last_signal_strength = 0.0f;
// System telemetry variables
static float last_cpu_temp = 0.0f;
static float last_cpu_usage = 0.0f;

void drone_telemetry_panel_init(lv_display_t *disp);

void ui_update_wfb_ng_telemetry(const wfb_rx_status *st)
{
    if (!st || termination_requested) return;

    if (st->id[0] != 'v') return; // id could be "video rx", "msposd rx", "mavlink rx". We need only "video rx" telemetry
    last_bitrate_mbps = st->ants[0].bitrate_mbps;
    last_signal_strength = st->ants[0].rssi_avg;
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
        if (fscanf(capacity_file, "%d", &capacity) != 1) {
            ERROR_M("BATTERY", "Failed to read battery capacity");
        }
        fclose(capacity_file);
    }

    if (battery_charge) {
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
        lv_label_set_text_fmt(battery_charge, "%s %d%%", charge_symbol, capacity);
    }

    // Force full area refresh for left side elements
    // lv_obj_invalidate(lv_obj_get_parent(battery_charge));
}

static void update_signal_strength(lv_timer_t *t)
{
    // Update bitrate
    if (bitrate) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %.2f Mbps", LV_SYMBOL_WIFI, last_bitrate_mbps);
        lv_label_set_text(bitrate, buf);
    }
    
    // Update RSSI
    if (signal_strength) {
        lv_label_set_text_fmt(signal_strength, "/ %ddBm", (int)last_signal_strength);
    }
}

static void update_drone_telemetry(lv_timer_t *t)
{
    // Update the UI with drone telemetry data
    if (drone_cpu_temp) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Drone CPU %.2f °C | load %.2f%%", last_cpu_temp, last_cpu_usage);
        lv_label_set_text(drone_cpu_temp, buf);
    }
    
    // if (drone_cpu_usage) {
    //     char buf[32];
    //     snprintf(buf, sizeof(buf), "%.2f%%", last_cpu_usage);
    //     lv_label_set_text(drone_cpu_usage, buf);
    // }
}

void ui_update_system_telemetry(float cpu_temp, float cpu_usage)
{
    last_cpu_temp = cpu_temp;
    last_cpu_usage = cpu_usage;
}

/**
 * Clock update function for the UI
//  */
static void update_clock(lv_timer_t *t)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(t);
    time_t raw_time;
    struct tm *time_info;
    char buffer[32];
    
    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(buffer, sizeof(buffer), "%H:%M:%S", time_info);
    lv_label_set_text(label, buffer);
}

int ui_interface_init(lv_display_t *disp)
{
    lv_obj_t *top_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(top_bar, lv_disp_get_hor_res(disp) - 20, 50);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(top_bar, color_ht_main, LV_PART_MAIN); // Set blue background
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_50, LV_PART_MAIN); // Make background semi-transparent
    lv_obj_set_style_border_width(top_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(top_bar, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(top_bar, 5, LV_PART_MAIN);
    // Try to fix white corners by disabling antialiasing or setting proper blend mode
    lv_obj_set_style_blend_mode(top_bar, LV_BLEND_MODE_NORMAL, LV_PART_MAIN);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    if (is_battery_supported()) {
        battery_charge = lv_label_create(top_bar);
        lv_label_set_text(battery_charge, LV_SYMBOL_BATTERY_EMPTY " 100%");
        lv_obj_align(battery_charge, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_text_color(battery_charge, color_ht_accent, LV_PART_MAIN);
        lv_obj_set_style_text_font(battery_charge, &lv_font_montserrat_30, LV_PART_MAIN);
        lv_obj_set_style_text_color(battery_charge, lv_color_white(), LV_PART_MAIN);
        lv_timer_t *battery_timer = lv_timer_create(update_battery_charge, 1000, NULL);
        if (battery_timer == NULL) {
            ERROR("Failed to create battery timer");
        }
    }

    signal_strength = lv_label_create(top_bar);
    lv_label_set_text(signal_strength, LV_SYMBOL_WIFI " -100dBm");
    lv_obj_align(signal_strength, LV_ALIGN_CENTER, +100, 0);
    lv_obj_set_style_text_color(signal_strength, color_ht_accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(signal_strength, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(signal_strength, lv_color_white(), LV_PART_MAIN);

    bitrate = lv_label_create(top_bar);
    lv_label_set_text(bitrate, LV_SYMBOL_WIFI " 0.00 Mbps");
    lv_obj_align(bitrate, LV_ALIGN_CENTER, -100, 0);
    lv_obj_set_style_text_color(bitrate, color_ht_accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(bitrate, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(bitrate, lv_color_white(), LV_PART_MAIN);
    lv_timer_t *signal_timer = lv_timer_create(update_signal_strength, 1000, NULL);
    if (signal_timer == NULL) {
        ERROR("Failed to create signal strength timer");
    }


    clock_l = lv_label_create(top_bar);
    lv_label_set_text(clock_l, "00:00:00");
    lv_obj_align(clock_l, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_color(clock_l, color_ht_accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(clock_l, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(clock_l, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock_l, lv_color_white(), LV_PART_MAIN);
    // Create a timer to update the clock every second
    lv_timer_t *clock_timer = lv_timer_create(update_clock, 1000, clock_l);
    if (clock_timer == NULL) {
        ERROR("Failed to create clock timer");
    }
    DEBUG("Top bar created");

    // Init notification area
    notification_area = lv_obj_create(lv_scr_act());
    lv_obj_set_size(notification_area, 520, 50);
    lv_obj_align(notification_area, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(notification_area, color_ht_secondary, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(notification_area, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(notification_area, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(notification_area, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(notification_area, 5, LV_PART_MAIN);
    lv_obj_clear_flag(notification_area, LV_OBJ_FLAG_SCROLLABLE);

    notification_label = lv_label_create(notification_area);
    lv_label_set_text(notification_label, "No new notifications");
    lv_obj_align(notification_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(notification_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(notification_label, &lv_font_montserrat_30, LV_PART_MAIN);

    lv_obj_set_flag(notification_area, LV_OBJ_FLAG_HIDDEN, true); // Hide notification area initially

    drone_telemetry_panel_init(disp);

    return 0;
}

void drone_telemetry_panel_init(lv_display_t *disp)
{
    // Create a panel for drone telemetry
    lv_obj_t *panel = drone_telemetry_panel;
    panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel, 570, 50);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 10, 70);
    lv_obj_set_style_bg_color(panel, color_ht_secondary, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(panel, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 5, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    // drone_telemetry_panel = panel;
    
    // Add telemetry labels here
    // Example: create a label for CPU temperature
    drone_cpu_temp = lv_label_create(panel);
    lv_label_set_text(drone_cpu_temp, "CPU Temp: 0.0 °C");
    lv_obj_align(drone_cpu_temp, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(drone_cpu_temp, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(drone_cpu_temp, &lv_font_montserrat_30, LV_PART_MAIN);

    // drone_cpu_usage = lv_label_create(panel);
    // lv_label_set_text(drone_cpu_usage, "CPU Usage: 0.0%");
    // lv_obj_align(drone_cpu_usage, LV_ALIGN_LEFT_MID, 380, 0);
    // lv_obj_set_style_text_color(drone_cpu_usage, lv_color_white(), LV_PART_MAIN);
    // lv_obj_set_style_text_font(drone_cpu_usage, &lv_font_montserrat_30, LV_PART_MAIN);

    // Create a timer to update the CPU temperature every second
    lv_timer_t *cpu_temp_timer = lv_timer_create(update_drone_telemetry, 1000, NULL);
    if (cpu_temp_timer == NULL) {
        ERROR("Failed to create CPU temperature timer");
    }

}

void drone_telemetry_panel_deinit(void)
{
    if (drone_telemetry_panel) {
        lv_obj_del(drone_telemetry_panel);
        drone_telemetry_panel = NULL;
    }

}

void ui_interface_deinit(void)
{
    if (battery_charge) {
        lv_obj_del(battery_charge);
        battery_charge = NULL;
    }
    if (signal_strength) {
        lv_obj_del(signal_strength);
        signal_strength = NULL;
    }
    if (clock_l) {
        lv_obj_del(clock_l);
        clock_l = NULL;
    }
    if (notification_area) {
        lv_obj_del(notification_area);
        notification_area = NULL;
    }
    if (notification_label) {
        lv_obj_del(notification_label);
        notification_label = NULL;
    }
}

void ui_notification_timer_cb(lv_timer_t *t)
{
    if (notification_area) {
        lv_obj_add_flag(notification_area, LV_OBJ_FLAG_HIDDEN); // Hide notification area
    }
    if (notification_timer) {
        lv_timer_del(notification_timer); // Delete the timer
        notification_timer = NULL;
    }
}

void ui_interface_notification(const char *text)
{
    if (notification_label) {
        lv_label_set_text(notification_label, text);
        lv_obj_clear_flag(notification_area, LV_OBJ_FLAG_HIDDEN); // Show notification area
        lv_obj_invalidate(notification_area); // Force redraw
        if (notification_timer) {
            lv_timer_reset(notification_timer); // Reset the timer if it exists
        } else {
            // Create a new timer to hide the notification after 2 seconds
            notification_timer = lv_timer_create(ui_notification_timer_cb, 2000, NULL);
            if (notification_timer == NULL) {
                ERROR("Failed to create notification timer");
            }
        }
    }
}