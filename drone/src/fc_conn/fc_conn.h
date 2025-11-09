#include "msp.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef int (*msp_displayport_cb_t)(const char* buffer, size_t size);

int connect_to_fc(const char *device, int baudrate);
void register_msp_displayport_cb(msp_displayport_cb_t cb);
int request_fc_info(void);
void msp_send_update_rssi(int rssi); 

bool is_all_fc_properties_ready(void);

const char* get_device_uid(void);
const char* get_craft_name(void);
const char* get_fc_variant(void);
const char* get_fc_version(void);
const char* get_board_info(void);

void disconnect_from_fc();

// Telemetry functions
void update_telemetry_stats(uint8_t uplink_rssi_1, uint8_t uplink_rssi_2, 
                           uint8_t uplink_quality, int8_t uplink_snr,
                           uint8_t downlink_rssi, uint8_t downlink_quality, 
                           int8_t downlink_snr, uint8_t active_antenna, 
                           uint8_t rf_mode, uint8_t tx_power);
void send_telemetry_to_fc(void);
