#include "link_callbacks.h"
#include "log.h"

static const char* module_name_str = "LINK_CB";

void update_displayport_cb(const char *data, size_t size)
{
    if (size == 0 || data == NULL) {
        ERROR("Received empty displayport data");
        return;
    }

    // Forward the displayport data to the UI
    DEBUG("Received displayport data of size %zu bytes", size);
    //msp_process_data_pack((const uint8_t*)data, size);
    //ui_update_displayport(data, size);
}

void update_sys_telemetry(float cpu_temp, float cpu_usage)
{
    // Update the UI with system telemetry data
    DEBUG("Drone CPU Temperature: %.2f °C, CPU Usage: %.2f%%", cpu_temp, cpu_usage);
    // ui_update_system_telemetry(cpu_temp, cpu_usage);
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
