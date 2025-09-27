#include "callbacks_wifi.h"
#include "menu_wifi_settings.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define WIFI_INTERNAL "wlan0"
#define WIFI_EXTERNAL "wlan1"

static const char *module_name_str = "CALLBACKS";

// Static array to store frequencies
static wifi_frequency_t available_frequencies[512];
static int frequency_count = 0;
static int frequencies_loaded = 0;

void wifi_settings_click_handler()
{
    lv_obj_t *wifi_menu = show_menu_wifi_settings(lv_scr_act());
    lv_obj_t *menu = lv_obj_get_parent(wifi_menu);
    if (menu) {
        lv_obj_clear_state(menu, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
        lv_obj_invalidate(menu); // Force redraw to remove focus
    }
}

uint32_t wfb_ng_get_frequencies()
{
    // If frequencies are already loaded, return count
    if (frequencies_loaded) {
        return frequency_count;
    }

    frequency_count = wifi_api_get_frequencies(WIFI_EXTERNAL, available_frequencies, sizeof(available_frequencies)/sizeof(available_frequencies[0]));
    if (frequency_count == 0) {
        ERROR("No frequencies found");
        return 0;
    }
    frequencies_loaded = 1;
    
    DEBUG("Total frequencies found: %d", frequency_count);
    return frequency_count;
}

uint32_t wfb_ng_get_frequency()
{
    // Return first available frequency or 0 if none
    if (wfb_ng_get_frequencies() > 0) {
        return available_frequencies[0].frequency;
    }
    return 0;
}

// Helper function to get frequency by index
uint32_t wfb_ng_get_frequency_by_index(int index)
{
    if (index >= 0 && index < frequency_count) {
        return available_frequencies[index].frequency;
    }
    return 0;
}

// Helper function to get frequency structure by index
wifi_frequency_t* wfb_ng_get_frequency_struct_by_index(int index)
{
    if (index >= 0 && index < frequency_count) {
        return &available_frequencies[index];
    }
    return NULL;
}

// Helper function to get pointer to frequencies array
wifi_frequency_t* wfb_ng_get_frequencies_array()
{
    wfb_ng_get_frequencies(); // Make sure frequencies are loaded
    return available_frequencies;
}

const char* wfb_ng_get_frequencies_str()
{
    static char frequencies_str[1024];
    frequencies_str[0] = '\0'; // Initialize with empty string
    
    for (int i = 0; i < frequency_count; i++) {
        char freq_buf[32];
        snprintf(freq_buf, sizeof(freq_buf), "%u MHz [%d]", available_frequencies[i].frequency, available_frequencies[i].channel);
        if (i > 0) {
            strncat(frequencies_str, "\n", sizeof(frequencies_str) - strlen(frequencies_str) - 1);
        }
        strncat(frequencies_str, freq_buf, sizeof(frequencies_str) - strlen(frequencies_str) - 1);
    }
    DEBUG("Available frequencies:\n%s", frequencies_str);
    return frequencies_str;
}

uint16_t wfb_ng_get_current_frequency()
{
    uint32_t current_freq = wifi_api_get_current_frequency(WIFI_EXTERNAL);
    if (current_freq == 0) {
        WARN("Warning: Unable to get current frequency");
        return 0;
    }
    // Find index of current frequency in available frequencies array
    for (int i = 0; i < frequency_count; i++) {
        if (available_frequencies[i].frequency == current_freq) {
            INFO("Current frequency index: %d", i);
            return (uint16_t)i;
        }
    }

    WARN("Current frequency %u MHz not found in available frequencies", current_freq);
    return 0;
}

void wfb_ng_set_frequency(uint16_t frequency_idx)
{
    if (frequency_idx >= frequency_count) {
        ERROR("Invalid frequency index: %u", frequency_idx);
        return;
    }
    uint32_t freq = available_frequencies[frequency_idx].frequency;
    if (wifi_api_set_current_frequency(WIFI_EXTERNAL, freq) == 0) {
        INFO("Set frequency to %u MHz successfully", freq);
    } else {
        ERROR("Failed to set frequency to %u MHz", freq);
    }
}

uint16_t wfb_ng_get_current_bandwidth()
{ 
    uint32_t current_bandwidth = 20;
    current_bandwidth = wifi_api_get_bandwidth(WIFI_EXTERNAL);
    if (current_bandwidth != 0) {
        INFO("Current bandwidth: %u MHz", current_bandwidth);
    } else
    {
        WARN("Warning: Unable to get current bandwidth");
    }
    
    return current_bandwidth == 20 ? 0 : (current_bandwidth == 40 ? 1 : (current_bandwidth == 80 ? 2 : 3));
}

void wfb_ng_set_bandwidth(uint16_t bandwidth_idx)
{
    uint32_t bandwidth = bandwidth_idx == 0 ? 20 : (bandwidth_idx == 1 ? 40 : (bandwidth_idx == 2 ? 80 : 160));
    wifi_api_set_bandwidth(WIFI_EXTERNAL, bandwidth);
}