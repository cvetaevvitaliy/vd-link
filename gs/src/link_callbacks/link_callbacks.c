#include "link_callbacks.h"
#include "log.h"
#include "ui_interface.h"

static const char* module_name_str = "LINK_CB";

void update_displayport_cb(const char *data, size_t size)
{
    if (size == 0 || data == NULL) {
        ERROR("Received empty displayport data");
        return;
    }

    // Forward the displayport data to the UI
    INFO("Received displayport data of size %zu bytes", size);
    //msp_process_data_pack((const uint8_t*)data, size);
    //ui_update_displayport(data, size);
}

void update_sys_telemetry(float cpu_temp, float cpu_usage)
{
    // Update the UI with system telemetry data
    ui_update_system_telemetry(cpu_temp, cpu_usage);
}

void update_detection_results(const link_detection_box_t* results, size_t count)
{
    if (count == 0 || results == NULL) {
        ERROR("Received empty detection results");
        return;
    }

    // Update the UI with detection results
    INFO("Received %zu detection results", count);
    // ui_update_detection(results, count);
}

void link_process_cmd(link_command_id_t cmd_id, link_subcommand_id_t sub_cmd_id, const void* data, size_t size)
{
    INFO("Received command: cmd_id=%d, sub_cmd_id=%d, size=%zu", cmd_id, sub_cmd_id, size);
    // Process the command as needed
    // For example, handle specific commands based on cmd_id and sub_cmd_id
}

void link_switch_cameras()
{
    static int cam_id = 0;
    cam_id = (cam_id + 1) % 2; // Toggle between 0 and 1
    INFO("Switching to camera ID: %d", cam_id);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_SWITCH_CAMERAS, &cam_id, sizeof(cam_id));
}