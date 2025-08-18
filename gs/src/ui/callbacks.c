#include "callbacks.h"
#include "menu_wifi_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


// Static array to store frequencies
static wifi_frequency_t available_frequencies[256];
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
    
    FILE *fp;
    char line[256];
    char *token;
    
    // Execute iw phy0 info command and read output
    fp = popen("iw phy0 info 2>/dev/null", "r");
    if (fp == NULL) {
        printf("Error: Cannot execute iw phy0 info command\n");
        return 0;
    }
    
    frequency_count = 0;
    int in_frequencies_section = 0;
    
    // Read output line by line
    while (fgets(line, sizeof(line), fp) != NULL && frequency_count < 256) {
        // Look for Frequencies section
        if (strstr(line, "Frequencies:") != NULL) {
            in_frequencies_section = 1;
            continue;
        }
        
        // If we found a new section, exit frequency parsing
        if (in_frequencies_section && line[0] != '\t' && line[0] != ' ') {
            break;
        }
        
        // Parse frequencies in Frequencies section
        if (in_frequencies_section) {
            // Look for lines with frequencies (contain "MHz")
            char *mhz_pos = strstr(line, "MHz");
            if (mhz_pos != NULL) {
                // Look for number before "MHz"
                char *start = mhz_pos;
                while (start > line && (start[-1] == ' ' || (start[-1] >= '0' && start[-1] <= '9'))) {
                    start--;
                }
                
                // Find start of number
                while (start > line && start[0] == ' ') {
                    start++;
                }
                
                // Extract frequency
                uint32_t freq = (uint32_t)atoi(start);
                if (freq > 0) {
                    available_frequencies[frequency_count].frequency = freq;
                    available_frequencies[frequency_count].channel = 0; // Will be set later
                    
                    // Try to extract channel number from the same line
                    char *channel_pos = strstr(line, "[");
                    if (channel_pos != NULL) {
                        channel_pos++; // Skip '['
                        uint32_t channel = (uint32_t)atoi(channel_pos);
                        if (channel > 0) {
                            available_frequencies[frequency_count].channel = channel;
                        }
                    }
                    available_frequencies[frequency_count].is_valid = true;
                    frequency_count++;
                    printf("Found frequency: %u MHz\n", freq);
                }
            }
        }
    }
    
    pclose(fp);
    frequencies_loaded = 1;
    
    printf("Total frequencies found: %d\n", frequency_count);
    return frequency_count;
}uint32_t wfb_ng_get_frequency()
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
    printf("Available frequencies:\n%s\n", frequencies_str);
    return frequencies_str;
}

uint16_t wfb_ng_get_current_frequency()
{
    FILE *fp;
    char line[256];
    uint32_t current_freq = 0;
    
    // Make sure frequencies array is loaded
    if (wfb_ng_get_frequencies() == 0) {
        printf("Error: No frequencies available\n");
        return 0;
    }
    
    // Execute iw dev wlan0 info command and read output
    fp = popen("iw dev wlan0 info 2>/dev/null", "r");
    if (fp == NULL) {
        printf("Error: Cannot execute iw dev wlan0 info command\n");
        return 0;
    }
    
    // Read output line by line
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Look for line with "channel" or "freq"
        if (strstr(line, "channel") != NULL) {
            // Format: "channel 6 (2437 MHz)"
            char *freq_start = strstr(line, "(");
            if (freq_start != NULL) {
                freq_start++; // Skip '('
                current_freq = (uint32_t)atoi(freq_start);
                break;
            }
        } else if (strstr(line, "freq") != NULL) {
            // Alternative format
            char *freq_pos = strstr(line, "freq");
            if (freq_pos != NULL) {
                // Look for number after "freq"
                char *start = freq_pos + 4; // Skip "freq"
                while (*start == ' ' || *start == ':') start++; // Skip spaces and colon
                current_freq = (uint32_t)atoi(start);
                break;
            }
        }
    }
    
    pclose(fp);
    
    if (current_freq == 0) {
        printf("Warning: Could not determine current frequency\n");
        return 0;
    }
    
    printf("Current frequency: %u MHz\n", current_freq);
    
    // Find index of current frequency in available frequencies array
    for (int i = 0; i < frequency_count; i++) {
        if (available_frequencies[i].frequency == current_freq) {
            printf("Current frequency index: %d\n", i);
            return (uint16_t)i;
        }
    }
    
    printf("Warning: Current frequency %u MHz not found in available frequencies\n", current_freq);
    return 0;
}

void wfb_ng_set_frequency(uint16_t frequency_idx)
{
    // Form command to set frequency
    char command[256];
    snprintf(command, sizeof(command), "iw dev wlan0 set freq %u 2>/dev/null", available_frequencies[frequency_idx].frequency);

    printf("Setting frequency to %u MHz\n", available_frequencies[frequency_idx].frequency);
    int result = system(command);
    
    if (result == 0) {
        printf("Frequency set successfully to %u MHz\n", available_frequencies[frequency_idx].frequency);
    } else {
        printf("Error: Failed to set frequency to %u MHz\n", available_frequencies[frequency_idx].frequency);
    }
}