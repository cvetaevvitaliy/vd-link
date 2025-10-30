#include "link_callbacks.h"
#include "log.h"
#include "ui_interface.h"
#include "msp-osd/msp-osd.h"

#include <string.h>

static const char* module_name_str = "LINK_CB";

void update_displayport_cb(const unsigned char *data, size_t size)
{
    if (size == 0 || data == NULL) {
        ERROR("Received empty displayport data");
        return;
    }

    // Forward the displayport data to the UI
    msp_process_data_pack((const uint8_t*)data, size);
    //ui_update_displayport(data, size);
}

void update_sys_telemetry(link_sys_telemetry_t const *telemetry)
{
    if (telemetry == NULL) {
        ERROR("Received NULL telemetry data");
        return;
    }

    float cpu_temp = telemetry->cpu_temperature;
    float cpu_usage = telemetry->cpu_usage_percent;

    INFO("System Telemetry - CPU Temp: %.2f, CPU Usage: %.2f%%", cpu_temp, cpu_usage);

    // Forward telemetry data to the UI
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
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_CAMERA, &cam_id, sizeof(cam_id));
}

void link_set_focus_mode(bool enabled)
{
    uint32_t focus_quality = enabled ? 50 : 100;
    INFO("Setting focus mode to quality: %u", focus_quality);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_FOCUS_MODE, &focus_quality, sizeof(focus_quality));
}

void link_get_focus_mode(uint32_t *focus_quality)
{
    if (focus_quality == NULL) {
        ERROR("focus_quality pointer is NULL");
        return;
    }
    link_send_cmd(LINK_CMD_GET, LINK_SUBCMD_FOCUS_MODE, NULL, 0);
    *focus_quality = 1; // e.g., 0 = low, 1 = medium, 2 = high
    INFO("Getting focus mode, current quality: %u", *focus_quality);
}

void link_set_detection_enabled(bool enabled)
{
    uint32_t value = enabled ? 1 : 0;
    INFO("Setting detection enabled: %s", enabled ? "true" : "false");
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_DETECTION, &value, sizeof(value));
}

void link_set_fps(uint32_t fps)
{
    INFO("Setting FPS to: %u", fps);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_FPS, &fps, sizeof(fps));
}

void link_set_bitrate(uint32_t bitrate)
{
    INFO("Setting bitrate to: %u", bitrate);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_BITRATE, &bitrate, sizeof(bitrate));
}

void link_set_hdr_enabled(bool enabled)
{
    uint32_t value = enabled ? 1 : 0;
    INFO("Setting HDR enabled: %s", enabled ? "true" : "false");
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_HDR, &value, sizeof(value));
}

void link_set_gop(uint32_t gop)
{
    INFO("Setting GOP to: %u", gop);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_GOP, &gop, sizeof(gop));
}

void link_set_payload_size(uint32_t payload_size)
{
    INFO("Setting payload size to: %u", payload_size);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_PAYLOAD_SIZE, &payload_size, sizeof(payload_size));
}

void link_set_vbr_enabled(bool enabled)
{
    uint32_t value = enabled ? 1 : 0;
    INFO("Setting VBR enabled: %s", enabled ? "true" : "false");
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_VBR, &value, sizeof(value));
}

void link_set_codec(codec_type_t codec)
{
    INFO("Setting codec to: %u", codec);
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_CODEC, &codec, sizeof(codec));
}

void link_set_wfb_key(const char* wfb_key)
{
    if (wfb_key == NULL) {
        ERROR("WFB key is NULL");
        return;
    }
    INFO("Setting WFB key");
    link_send_cmd(LINK_CMD_SET, LINK_SUBCMD_WFB_KEY, wfb_key, strlen(wfb_key) + 1);
}

