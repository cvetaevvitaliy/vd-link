#ifndef REMOTE_CLIENT_H
#define REMOTE_CLIENT_H

#include "common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int remote_client_init(const common_config_t* config);
int remote_client_start(void);
int remote_client_stop(void);
void remote_client_cleanup(void);
int remote_client_send_telemetry(const char* telemetry_json);
bool remote_client_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // REMOTE_CLIENT_H