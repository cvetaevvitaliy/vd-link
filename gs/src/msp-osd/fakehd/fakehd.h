#pragma once
#include <stdint.h>

void load_fakehd_config();
void fakehd_disable();
void fakehd_enable();
int fakehd_is_enabled();
void fakehd_reset();
void fakehd_map_sd_character_map_to_hd(uint16_t sd_character_map[52][20], uint16_t hd_character_map[52][20]);