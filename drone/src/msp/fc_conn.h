#include "msp.h"
#include <stdlib.h>

typedef int (*msp_displayport_cb_t)(const char* buffer, size_t size);

int connect_to_fc(const char *device, int baudrate);
void register_msp_displayport_cb(msp_displayport_cb_t cb);

void disconnect_from_fc();