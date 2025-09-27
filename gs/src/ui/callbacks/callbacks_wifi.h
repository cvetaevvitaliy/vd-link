#ifndef CALLBACKS_H
#define CALLBACKS_H

#include "lvgl.h"
#include "wifi_api.h"
#include <stdint.h>
#include <stdbool.h>

void wifi_settings_click_handler();

// WiFi frequency functions
uint32_t wfb_ng_get_frequencies();        // Returns count of available frequencies
uint32_t wfb_ng_get_frequency();          // Returns first available frequency
uint32_t wfb_ng_get_frequency_by_index(int index);  // Returns frequency by index
wifi_frequency_t* wfb_ng_get_frequency_struct_by_index(int index); // Returns frequency structure by index
wifi_frequency_t* wfb_ng_get_frequencies_array(); // Returns pointer to frequencies array
const char* wfb_ng_get_frequencies_str(); // Returns string representation of frequencies
uint16_t wfb_ng_get_current_frequency();  // Returns index of current frequency in array
void wfb_ng_set_frequency(uint16_t frequency_idx); // Sets frequency by index
uint16_t wfb_ng_get_current_bandwidth(); // Returns current bandwidth in MHz
void wfb_ng_set_bandwidth(uint16_t bandwidth_idx); // Sets bandwidth in MHz

#endif // CALLBACKS_H