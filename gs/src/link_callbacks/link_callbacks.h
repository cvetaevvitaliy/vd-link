#include "link.h"
#include <stdbool.h>

void update_displayport_cb(const unsigned char *data, size_t size);
void update_sys_telemetry(link_sys_telemetry_t const *telemetry);
void update_detection_results(const link_detection_box_t* results, size_t count);
void link_process_cmd(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);
void link_switch_cameras();
void link_set_focus_mode(bool focus_quality);
void link_set_detection_enabled(bool enabled);
void link_set_fps(uint32_t fps);
void link_set_payload_size(uint32_t payload_size);
void link_set_bitrate(uint32_t bitrate);
void link_set_hdr_enabled(bool hdr_enabled);
void link_set_gop_size(uint32_t gop_size);
void link_set_vbr_enabled(bool vbr_enabled);