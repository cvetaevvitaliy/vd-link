#pragma once
/* drone_name.h
 *
 * Functions to generate drone name based on a pattern.
 * Pattern can include placeholders like <cpu_serial>, <fc_uid>, <craft_name>, <fc_variant>.
 * Only cpu_serial always available, others depend on flight controller connection.
 * For example: "Drone-<cpu_serial>-<fc_variant>" will be replaced with actual values.
 * End resulting name will be "Drone-cf17d5582095ad82-BTFL" (example).
 */
const char* get_drone_name(const char* pattern);