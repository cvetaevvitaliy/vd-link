#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t frequency;
    uint32_t channel;
    bool disabled;
} wifi_frequency_t;

uint32_t wifi_api_get_frequencies(char *iface, wifi_frequency_t *frequencies, size_t max_count);
uint32_t wifi_api_get_current_frequency(char *iface);
int wifi_api_set_current_frequency(char *iface, uint32_t freq);
size_t wifi_api_get_interfaces_for_phy(uint32_t phy_index, char **interfaces, size_t max_count);
int wifi_api_get_phy_index(const char *phy_name);
void wifi_api_cleanup(void);
uint32_t wifi_api_get_bandwidth(char *iface);
int wifi_api_set_bandwidth(char *iface, uint32_t bandwidth);
void wifi_api_get_ip_address(char *iface, char *ip_buffer, size_t buffer_size);
// int wifi_api_set_channel(char *iface, uint32_t freq, uint32_t bandwidth);
