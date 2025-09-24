#pragma once

#include "link.h"


void link_cmd_rx_callback(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);