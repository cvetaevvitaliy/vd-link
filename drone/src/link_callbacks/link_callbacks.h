#pragma once

#include "link.h"


void link_cmd_rx_callback(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);
void link_rc_rx_callback(const uint16_t* channel_values, size_t channel_count);
void link_stop_telemetry_thread();
int link_start_telemetry_thread();