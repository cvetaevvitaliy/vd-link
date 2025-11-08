#ifndef DRONE_CLIENT_H
#define DRONE_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DRONE_CLIENT_SUCCESS       0
#define DRONE_CLIENT_ERROR        -1
#define DRONE_CLIENT_NET_ERROR    -2
#define DRONE_CLIENT_AUTH_ERROR   -3
#define DRONE_CLIENT_TIMEOUT      -4

#define DRONE_CLIENT_DEFAULT_HOST     "localhost"
#define DRONE_CLIENT_DEFAULT_PORT     8000
#define DRONE_CLIENT_DEFAULT_TIMEOUT  30
#define DRONE_CLIENT_MAX_RETRIES      3

typedef struct drone_client_config drone_client_config_t;
typedef struct drone_client_handle drone_client_handle_t;

struct drone_client_config {
    char server_host[256];
    int server_port;
    int heartbeat_interval;
    int max_retries;
    int timeout_seconds;
    char drone_id[64];
    char name[128];
    char firmware_version[32];
    char hardware_version[32];
    char owner_id[32];
    char fc_variant[5];
    bool video_capable;
    bool telemetry_capable;
    bool commands_capable;
};

typedef void (*drone_client_status_callback_t)(const char* status, void* user_data);
typedef void (*drone_client_error_callback_t)(int error_code, const char* message, void* user_data);
typedef void (*drone_client_command_callback_t)(const char* command, const char* payload, void* user_data);

void drone_client_config_init_default(drone_client_config_t* config);
drone_client_handle_t* drone_client_create(const drone_client_config_t* config);
void drone_client_destroy(drone_client_handle_t* client);

int drone_client_set_status_callback(drone_client_handle_t* client, 
                                    drone_client_status_callback_t callback, 
                                    void* user_data);
int drone_client_set_error_callback(drone_client_handle_t* client, 
                                   drone_client_error_callback_t callback, 
                                   void* user_data);
int drone_client_set_command_callback(drone_client_handle_t* client, 
                                     drone_client_command_callback_t callback, 
                                     void* user_data);

int drone_client_connect(drone_client_handle_t* client);
int drone_client_disconnect(drone_client_handle_t* client);
int drone_client_start(drone_client_handle_t* client);
int drone_client_stop(drone_client_handle_t* client);
int drone_client_send_heartbeat(drone_client_handle_t* client);
int drone_client_send_telemetry(drone_client_handle_t* client, const char* telemetry_json);
int drone_client_send_status(drone_client_handle_t* client, const char* status);
int drone_client_get_stream_config(drone_client_handle_t* client, char* stream_ip, int* stream_port, int* telemetry_port, int* command_port, int* control_port);

bool drone_client_is_connected(const drone_client_handle_t* client);
bool drone_client_is_running(const drone_client_handle_t* client);
const char* drone_client_get_last_error(const drone_client_handle_t* client);
const char* drone_client_get_session_id(const drone_client_handle_t* client);
const char* drone_client_get_drone_id(const drone_client_handle_t* client);

// Reconnection management functions
int drone_client_set_reconnect_enabled(drone_client_handle_t* client, bool enabled);
int drone_client_set_reconnect_delay(drone_client_handle_t* client, int delay_seconds);
bool drone_client_get_reconnect_enabled(const drone_client_handle_t* client);
int drone_client_get_reconnect_attempts(const drone_client_handle_t* client);
int drone_client_force_reconnect(drone_client_handle_t* client);

#ifdef __cplusplus
}
#endif

#endif // DRONE_CLIENT_H