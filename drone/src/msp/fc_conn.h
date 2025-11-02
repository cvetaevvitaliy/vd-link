#include "msp.h"
#include <stdlib.h>
#include <stdbool.h>

typedef int (*msp_displayport_cb_t)(const char* buffer, size_t size);

int connect_to_fc(const char *device, int baudrate);
void register_msp_displayport_cb(msp_displayport_cb_t cb);
const char* get_device_uid(void);
bool is_device_uid_ready(void);

void disconnect_from_fc();