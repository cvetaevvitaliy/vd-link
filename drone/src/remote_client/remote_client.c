#include "remote_client.h"
#include "drone_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static drone_client_handle_t* g_client = NULL;
static bool g_enabled = false;

static void on_status_change(const char* status, void* user_data) {
    (void)user_data;
    printf("[REMOTE_CLIENT] Status: %s\n", status);
}

static void on_error(int error_code, const char* message, void* user_data) {
    (void)user_data;
    printf("[REMOTE_CLIENT] Error %d: %s\n", error_code, message);
}

static void on_command(const char* command, const char* payload, void* user_data) {
    (void)user_data;
    printf("[REMOTE_CLIENT] Command: %s, payload: %s\n", command, payload);
}

int fill_server_config(server_connection_config_t* server_config,
                       const char* fc_variant,
                       const char* board_info,
                       const char* fc_version,
                       const char* drone_name,
                       const char* fc_uid,
                       const char* mcu_uid) {
    if (!server_config) {
        return -1;
    }

    if (fc_uid && fc_uid[0] != '\0') {
        strncpy(server_config->fc_serial, fc_uid, sizeof(server_config->fc_serial) - 1);
        server_config->fc_serial[sizeof(server_config->fc_serial) - 1] = '\0';
    }
    if (drone_name && drone_name[0] != '\0') {
        strncpy(server_config->name, drone_name, sizeof(server_config->name) - 1);
        server_config->name[sizeof(server_config->name) - 1] = '\0';
    }else {
        snprintf(server_config->name, sizeof(server_config->name),"Drone-%s", mcu_uid ? mcu_uid : "unknown");
    }
    if (fc_version && fc_version[0] != '\0') {
        strncpy(server_config->firmware_version, fc_version, sizeof(server_config->firmware_version) - 1);
        server_config->firmware_version[sizeof(server_config->firmware_version) - 1] = '\0';
    }
    if (board_info && board_info[0] != '\0') {
        strncpy(server_config->hardware_version, board_info, sizeof(server_config->hardware_version) - 1);
        server_config->hardware_version[sizeof(server_config->hardware_version) - 1] = '\0';
    }
    if (fc_variant && fc_variant[0] != '\0') {
        strncpy(server_config->fc_variant, fc_variant, sizeof(server_config->fc_variant) - 1);
        server_config->fc_variant[sizeof(server_config->fc_variant) - 1] = '\0';
    }
    if (mcu_uid && mcu_uid[0] != '\0') {
        strncpy(server_config->mcu_serial, mcu_uid, sizeof(server_config->mcu_serial) - 1);
        server_config->mcu_serial[sizeof(server_config->mcu_serial) - 1] = '\0';
    }

    return 0;
}

int remote_client_init(const common_config_t* config) {
    if (!config) {
        printf("[REMOTE_CLIENT] Invalid configuration\n");
        return -1;
    }
    
    if (!config->server_config.enabled) {
        printf("[REMOTE_CLIENT] Server connection disabled in config\n");
        g_enabled = false;
        return 0;
    }
    
    if (g_client) {
        printf("[REMOTE_CLIENT] Already initialized\n");
        return 0;
    }
    
    printf("[REMOTE_CLIENT] Initializing connection to %s:%d (drone: %s)\n",
           config->server_config.server_host,
           config->server_config.server_port,
           config->server_config.name);
    
    drone_client_config_t client_config;
    drone_client_config_init_default(&client_config);
    
    // Server name
    strncpy(client_config.server_host, config->server_config.server_host, sizeof(client_config.server_host) - 1);
    // Server port
    client_config.server_port = config->server_config.server_port;
    // Drone ID - use MCU serial as unique identifier
    strncpy(client_config.drone_id, config->server_config.mcu_serial, sizeof(client_config.drone_id) - 1);
    // FC variant - from flight controller (BTFL, ARDU, INAV, etc.)
    strncpy(client_config.fc_variant, config->server_config.fc_variant, sizeof(client_config.fc_variant) - 1);
    // HTTP Heartbeat and timeout settings
    client_config.heartbeat_interval = config->server_config.heartbeat_interval;
    client_config.max_retries = config->server_config.server_connect_max_retries;
    client_config.timeout_seconds = config->server_config.server_connect_retry_delay;
    // Drone name - will be visible in table on server
    strncpy(client_config.name, config->server_config.name, sizeof(client_config.name) - 1);
    // Firmware version - from flight controller
    strncpy(client_config.firmware_version, config->server_config.firmware_version, sizeof(client_config.firmware_version) - 1);
    // Hardware version - from flight controller
    strncpy(client_config.hardware_version, config->server_config.hardware_version, sizeof(client_config.hardware_version) - 1);
    // Owner ID - from config (e.g., default, user123)
    strncpy(client_config.owner_id, config->server_config.owner_id, sizeof(client_config.owner_id) - 1);

    // Capabilities, for now assume all enabled
    client_config.video_capable = true;
    client_config.telemetry_capable = true;
    client_config.commands_capable = true;
    
    g_client = drone_client_create(&client_config);
    if (!g_client) {
        printf("[REMOTE_CLIENT] Failed to create drone client\n");
        return -1;
    }
    
    drone_client_set_status_callback(g_client, on_status_change, NULL);
    drone_client_set_error_callback(g_client, on_error, NULL);
    drone_client_set_command_callback(g_client, on_command, NULL);
    
    g_enabled = true;
    printf("[REMOTE_CLIENT] Initialized successfully\n");
    
    return 0;
}

int remote_client_start(void) {
    if (!g_enabled || !g_client) {
        return 0;
    }
    
    printf("[REMOTE_CLIENT] Starting connection...\n");
    
    if (drone_client_start(g_client) != DRONE_CLIENT_SUCCESS) {
        printf("[REMOTE_CLIENT] Failed to start: %s\n", drone_client_get_last_error(g_client));
        return -1;
    }
    
    printf("[REMOTE_CLIENT] Connected, session: %s\n", drone_client_get_session_id(g_client));
    
    drone_client_send_status(g_client, "online");
    
    printf("[REMOTE_CLIENT] Started successfully\n");
    return 0;
}

int remote_client_stop(void) {
    if (!g_enabled || !g_client) {
        return 0;
    }
    
    printf("[REMOTE_CLIENT] Stopping...\n");
    
    if (drone_client_is_connected(g_client)) {
        drone_client_send_status(g_client, "offline");
    }
    
    drone_client_stop(g_client);
    drone_client_disconnect(g_client);
    
    printf("[REMOTE_CLIENT] Stopped\n");
    return 0;
}

void remote_client_cleanup(void) {
    if (g_client) {
        remote_client_stop();
        drone_client_destroy(g_client);
        g_client = NULL;
        g_enabled = false;
        printf("[REMOTE_CLIENT] Cleanup completed\n");
    }
}

int remote_client_send_telemetry(const char* telemetry_data)
{
    if (!g_enabled || !g_client) {
        return -1;
    }
    
    return drone_client_send_telemetry(g_client, telemetry_data);
}

int remote_client_get_stream_config(char* stream_ip, int* stream_port, int* telemetry_port, int* command_port, int* control_port)
{
    if (!g_enabled || !g_client) {
        return -1;
    }

    return drone_client_get_stream_config(g_client, stream_ip, stream_port, telemetry_port, command_port, control_port);
}

bool remote_client_is_active(void) {
    return g_enabled && g_client && drone_client_is_connected(g_client);
}