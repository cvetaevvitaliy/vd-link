#include "link.h"

void update_displayport_cb(const char *data, size_t size);
void update_sys_telemetry(float cpu_temp, float cpu_usage);
void update_detection_results(const link_detection_box_t* results, size_t count);