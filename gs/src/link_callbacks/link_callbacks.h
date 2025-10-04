#include "link.h"

void update_displayport_cb(const char *data, size_t size);
void update_sys_telemetry(float cpu_temp, float cpu_usage);
void update_detection_results(const link_detection_box_t* results, size_t count);
void link_process_cmd(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size);
void link_switch_cameras();