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
int remote_client_send_telemetry(const char* telemetry_data);
int remote_client_get_stream_config(char* stream_ip, int* stream_port, int* telemetry_port, int* command_port, int* control_port);
bool remote_client_is_active(void);
int fill_server_config(server_connection_config_t* server_config,
                       const char* fc_variant,
                       const char* board_info,
                       const char* fc_version,
                       const char* drone_name,
                       const char* fc_uid,
                       const char* mcu_uid);


#ifdef __cplusplus
}
#endif

#endif // REMOTE_CLIENT_H