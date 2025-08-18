#ifndef CALLBACKS_H
#define CALLBACKS_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

// WiFi frequency structure
typedef struct {
    uint32_t frequency;
    uint32_t channel;
    bool is_valid;
} wifi_frequency_t;

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

#endif // CALLBACKS_H