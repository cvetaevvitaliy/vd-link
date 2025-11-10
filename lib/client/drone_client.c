#define _GNU_SOURCE
#include "drone_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include <json-c/json.h>

#define BUFFER_SIZE 4096

struct drone_client_handle {
    drone_client_config_t config;
    char session_id[128];
    char last_error[512];
    bool connected;
    bool running;
    pthread_t worker_thread;
    pthread_mutex_t mutex;
    
    // Reconnection state
    bool reconnect_enabled;
    int reconnect_attempts;
    int reconnect_delay_seconds;
    time_t last_connection_attempt;
    bool registration_valid;
    
    drone_client_status_callback_t status_callback;
    void* status_callback_data;
    drone_client_error_callback_t error_callback;
    void* error_callback_data;
    drone_client_command_callback_t command_callback;
    void* command_callback_data;
};

static int send_http_request(drone_client_handle_t* client, const char* method, 
                           const char* path, const char* body, char* response);
static int register_drone(drone_client_handle_t* client);
static int send_heartbeat_internal(drone_client_handle_t* client);
static void* worker_thread_function(void* arg);
static void set_error(drone_client_handle_t* client, const char* format, ...);
static int test_tcp_connection(drone_client_handle_t* client);
static int attempt_reconnection(drone_client_handle_t* client);

static void set_error(drone_client_handle_t* client, const char* format, ...) {
    if (!client) return;
    
    va_list args;
    va_start(args, format);
    vsnprintf(client->last_error, sizeof(client->last_error), format, args);
    va_end(args);
    
    if (client->error_callback) {
        client->error_callback(DRONE_CLIENT_ERROR, client->last_error, client->error_callback_data);
    }
}

// Test TCP connection to server without sending full HTTP request
static int test_tcp_connection(drone_client_handle_t* client) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    
    if (!client) return DRONE_CLIENT_ERROR;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return DRONE_CLIENT_NET_ERROR;
    }
    
    // Set socket timeout for non-blocking connect test
    struct timeval timeout;
    timeout.tv_sec = 5;  // 5 second timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    server = gethostbyname(client->config.server_host);
    if (server == NULL) {
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(client->config.server_port);
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    close(sockfd);
    return DRONE_CLIENT_SUCCESS;
}

// Attempt to reconnect to server
static int attempt_reconnection(drone_client_handle_t* client) {
    if (!client || !client->reconnect_enabled) {
        return DRONE_CLIENT_ERROR;
    }
    
    time_t current_time = time(NULL);
    
    // Check if enough time has passed since last attempt
    if (current_time - client->last_connection_attempt < client->reconnect_delay_seconds) {
        return DRONE_CLIENT_ERROR; // Too soon to retry
    }
    
    client->last_connection_attempt = current_time;
    client->reconnect_attempts++;
    
    // Test basic TCP connection first
    if (test_tcp_connection(client) != DRONE_CLIENT_SUCCESS) {
        set_error(client, "Reconnection attempt %d failed - TCP connection failed", 
                 client->reconnect_attempts);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    // TCP connection works, now try to re-register if needed
    if (!client->registration_valid) {
        if (register_drone(client) != DRONE_CLIENT_SUCCESS) {
            set_error(client, "Reconnection attempt %d failed - registration failed", 
                     client->reconnect_attempts);
            return DRONE_CLIENT_ERROR;
        }
        client->registration_valid = true;
    }
    
    // Test with a heartbeat
    if (send_heartbeat_internal(client) != DRONE_CLIENT_SUCCESS) {
        client->registration_valid = false; // Mark registration as invalid
        set_error(client, "Reconnection attempt %d failed - heartbeat failed", 
                 client->reconnect_attempts);
        return DRONE_CLIENT_ERROR;
    }
    
    // Successful reconnection
    client->connected = true;
    client->reconnect_attempts = 0; // Reset counter on success
    
    if (client->status_callback) {
        client->status_callback("reconnected", client->status_callback_data);
    }
    
    return DRONE_CLIENT_SUCCESS;
}

static int send_http_request(drone_client_handle_t* client, const char* method, 
                           const char* path, const char* body, char* response) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    char request[BUFFER_SIZE];
    int bytes_sent, bytes_received;
    
    if (!client || !method || !path || !response) {
        return DRONE_CLIENT_ERROR;
    }
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        set_error(client, "Error opening socket: %s", strerror(errno));
        return DRONE_CLIENT_NET_ERROR;
    }
    
    server = gethostbyname(client->config.server_host);
    if (server == NULL) {
        set_error(client, "Error: no such host %s", client->config.server_host);
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    server_addr.sin_port = htons(client->config.server_port);
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        set_error(client, "Error connecting to server: %s", strerror(errno));
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    int content_length = body ? strlen(body) : 0;
    snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        method, path, client->config.server_host, client->config.server_port, content_length,
        body ? body : "");
    
    bytes_sent = send(sockfd, request, strlen(request), 0);
    if (bytes_sent < 0) {
        set_error(client, "Error sending request: %s", strerror(errno));
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    memset(response, 0, BUFFER_SIZE);
    
    /* Read response in a loop until connection closes or buffer full */
    int total_received = 0;
    int max_attempts = 100; /* Prevent infinite loops */
    int attempts = 0;
    
    while (total_received < BUFFER_SIZE - 1 && attempts < max_attempts) {
        bytes_received = recv(sockfd, response + total_received, 
                             BUFFER_SIZE - 1 - total_received, 0);
        
        if (bytes_received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Non-blocking socket would block, try again */
                attempts++;
                usleep(10000); /* Wait 10ms */
                continue;
            }
            set_error(client, "Error receiving response: %s", strerror(errno));
            close(sockfd);
            return DRONE_CLIENT_NET_ERROR;
        } else if (bytes_received == 0) {
            /* Connection closed by server */
            break;
        }
        
        total_received += bytes_received;
        attempts++;
        
        /* Check if we have a complete HTTP response */
        if (total_received >= 4 && strstr(response, "\r\n\r\n")) {
            /* We have headers, check if we need to read more body */
            char* content_length_header = strstr(response, "Content-Length:");
            if (content_length_header) {
                int content_length = atoi(content_length_header + 15);
                char* body_start = strstr(response, "\r\n\r\n");
                if (body_start) {
                    body_start += 4;
                    int headers_length = body_start - response;
                    int body_received = total_received - headers_length;
                    
                    if (body_received >= content_length) {
                        /* We have the complete response */
                        break;
                    }
                }
            } else {
                /* No Content-Length header, assume connection close indicates end */
                /* Continue reading until connection closes */
            }
        }
    }
    
    /* Ensure null termination */
    response[total_received] = '\0';
    
    close(sockfd);
    return total_received;
}

static int register_drone(drone_client_handle_t* client) {
    char body[1024];
    char response[BUFFER_SIZE];
    char *response_body;
    int result;
    
    if (!client) return DRONE_CLIENT_ERROR;
    
    printf("Registering drone %s with server...\n", client->config.drone_id);
    
    snprintf(body, sizeof(body),
        "{"
        "\"drone_id\": \"%s\","
        "\"name\": \"%s\","
        "\"firmware_version\": \"%s\","
        "\"hardware_version\": \"%s\","
        "\"fc_variant\": \"%s\","
        "\"owner_id\": \"%s\","
        "\"capabilities\": {"
            "\"video\": %s,"
            "\"telemetry\": %s,"
            "\"commands\": %s"
        "}"
        "}",
        client->config.drone_id,
        client->config.name,
        client->config.firmware_version,
        client->config.hardware_version,
        client->config.fc_variant,
        client->config.owner_id,
        client->config.video_capable ? "true" : "false",
        client->config.telemetry_capable ? "true" : "false",
        client->config.commands_capable ? "true" : "false");
        printf("Registration body: %s\n", body);
    
    result = send_http_request(client, "POST", "/api/drones/register", body, response);
    if (result < 0) {
        set_error(client, "Failed to send registration request");
        return result;
    }
    
    response_body = strstr(response, "\r\n\r\n");
    if (response_body) {
        response_body += 4;
        
        if (strstr(response, "HTTP/1.1 20") != NULL) {
            printf("Registration successful!\n");
            
            client->connected = true;
            client->registration_valid = true;
            if (client->status_callback) {
                client->status_callback("connected", client->status_callback_data);
            }
            
            return DRONE_CLIENT_SUCCESS;
        } else {
            set_error(client, "Registration failed: %s", response_body);
            return DRONE_CLIENT_AUTH_ERROR;
        }
    }
    
    set_error(client, "Invalid response format");
    return DRONE_CLIENT_ERROR;
}

// Send heartbeat to server
static int send_heartbeat_internal(drone_client_handle_t* client) {
    char path[256];
    char response[BUFFER_SIZE];
    int result;
    
    if (!client) return DRONE_CLIENT_ERROR;
    
    printf("Sending heartbeat for drone %s...\n", client->config.drone_id);
    
    // Build heartbeat URL
    snprintf(path, sizeof(path), "/api/drones/%s/heartbeat", client->config.drone_id);
    
    // Send heartbeat request (POST with empty body)
    result = send_http_request(client, "POST", path, "{}", response);
    if (result < 0) {
        set_error(client, "Failed to send heartbeat");
        return result;
    }
    
    // Check response
    if (strstr(response, "HTTP/1.1 20") != NULL) {
        printf("Heartbeat sent successfully\n");
        return DRONE_CLIENT_SUCCESS;
    } else {
        char *response_body = strstr(response, "\r\n\r\n");
        if (response_body) {
            response_body += 4;
            set_error(client, "Heartbeat failed: %s", response_body);
        }
        return DRONE_CLIENT_ERROR;
    }
}

// Worker thread function with reconnection logic
static void* worker_thread_function(void* arg) {
    drone_client_handle_t* client = (drone_client_handle_t*)arg;
    time_t last_heartbeat = 0;
    int consecutive_failures = 0;
    
    if (!client) return NULL;
    
    while (client->running) {
        time_t current_time = time(NULL);
        
        // If not connected, try to reconnect
        if (!client->connected && client->reconnect_enabled) {
            if (attempt_reconnection(client) == DRONE_CLIENT_SUCCESS) {
                printf("Successfully reconnected to server\n");
                consecutive_failures = 0;
                last_heartbeat = current_time; // Reset heartbeat timer after reconnection
            } else {
                // Wait before next reconnection attempt
                sleep(client->reconnect_delay_seconds);
                continue;
            }
        }
        
        // If connected, check if it's time for heartbeat
        if (client->connected && (current_time - last_heartbeat >= client->config.heartbeat_interval)) {
            int heartbeat_result = send_heartbeat_internal(client);
            
            if (heartbeat_result == DRONE_CLIENT_SUCCESS) {
                last_heartbeat = current_time;
                consecutive_failures = 0;  // Reset failure count on success
            } else {
                consecutive_failures++;
                
                // Mark as disconnected after network errors
                if (heartbeat_result == DRONE_CLIENT_NET_ERROR) {
                    client->connected = false;
                    client->registration_valid = false;
                    
                    if (client->status_callback) {
                        client->status_callback("disconnected", client->status_callback_data);
                    }
                    
                    printf("Network error detected, will attempt reconnection\n");
                }
                
                // If too many consecutive failures and reconnection is disabled, stop trying
                if (consecutive_failures >= client->config.max_retries && !client->reconnect_enabled) {
                    set_error(client, "Too many heartbeat failures, stopping (reconnection disabled)");
                    client->running = false;
                    client->connected = false;
                    
                    if (client->status_callback) {
                        client->status_callback("disconnected", client->status_callback_data);
                    }
                    break;
                }
            }
        }
        
        // Sleep for 1 second before checking again
        sleep(1);
    }
    
    return NULL;
}

// Public API implementations

void drone_client_config_init_default(drone_client_config_t* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(*config));
    strncpy(config->server_host, DRONE_CLIENT_DEFAULT_HOST, sizeof(config->server_host) - 1);
    config->server_port = DRONE_CLIENT_DEFAULT_PORT;
    config->heartbeat_interval = DRONE_CLIENT_DEFAULT_TIMEOUT;
    config->max_retries = DRONE_CLIENT_MAX_RETRIES;
    config->timeout_seconds = 10;
    
    // Generate default drone ID
    srand(time(NULL));
    snprintf(config->drone_id, sizeof(config->drone_id), "drone-%04d", rand() % 10000);
    
    strncpy(config->name, "VD Link Drone", sizeof(config->name) - 1);
    strncpy(config->firmware_version, "1.0.0", sizeof(config->firmware_version) - 1);
    strncpy(config->hardware_version, "Generic", sizeof(config->hardware_version) - 1);
    strncpy(config->owner_id, "owner-unknown", sizeof(config->owner_id) - 1);
    strncpy(config->fc_variant, "N/A", sizeof(config->fc_variant) - 1);
    
    config->video_capable = true;
    config->telemetry_capable = true;
    config->commands_capable = true;
}

drone_client_handle_t* drone_client_create(const drone_client_config_t* config) {
    if (!config) return NULL;
    
    drone_client_handle_t* client = calloc(1, sizeof(drone_client_handle_t));
    if (!client) return NULL;
    
    memcpy(&client->config, config, sizeof(client->config));
    client->connected = false;
    client->running = false;
    
    // Initialize reconnection parameters with defaults
    client->reconnect_enabled = true;
    client->reconnect_attempts = 0;
    client->reconnect_delay_seconds = 5;  // Default: wait 5 seconds between attempts
    client->last_connection_attempt = 0;
    client->registration_valid = false;
    
    if (pthread_mutex_init(&client->mutex, NULL) != 0) {
        free(client);
        return NULL;
    }
    
    return client;
}

void drone_client_destroy(drone_client_handle_t* client) {
    if (!client) return;
    
    drone_client_stop(client);
    pthread_mutex_destroy(&client->mutex);
    free(client);
}

int drone_client_set_status_callback(drone_client_handle_t* client, 
                                    drone_client_status_callback_t callback, 
                                    void* user_data) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    pthread_mutex_lock(&client->mutex);
    client->status_callback = callback;
    client->status_callback_data = user_data;
    pthread_mutex_unlock(&client->mutex);
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_set_error_callback(drone_client_handle_t* client, 
                                   drone_client_error_callback_t callback, 
                                   void* user_data) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    pthread_mutex_lock(&client->mutex);
    client->error_callback = callback;
    client->error_callback_data = user_data;
    pthread_mutex_unlock(&client->mutex);
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_set_command_callback(drone_client_handle_t* client, 
                                     drone_client_command_callback_t callback, 
                                     void* user_data) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    pthread_mutex_lock(&client->mutex);
    client->command_callback = callback;
    client->command_callback_data = user_data;
    pthread_mutex_unlock(&client->mutex);
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_connect(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    if (client->connected) return DRONE_CLIENT_SUCCESS;
    
    return register_drone(client);
}

int drone_client_disconnect(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    if (client->connected) {
        char path[256];
        char response[BUFFER_SIZE];
        
        // Set drone status to offline
        snprintf(path, sizeof(path), "/api/drones/%s/status", client->config.drone_id);
        char body[] = "{\"status\": \"offline\"}";
        
        send_http_request(client, "POST", path, body, response);
        
        client->connected = false;
        if (client->status_callback) {
            client->status_callback("disconnected", client->status_callback_data);
        }
    }
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_start(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    if (client->running) return DRONE_CLIENT_SUCCESS;
    
    // Connect first if not connected
    int max_retries = client->config.max_retries;
    int attempt = 0;
    int delay = client->config.timeout_seconds > 0 ? client->config.timeout_seconds : 2; // Fixed delay
    bool infinite_retries = (max_retries == 0);
    
    while (!client->connected) {
        attempt++;
        
        if (infinite_retries) {
            printf("[DRONE_CLIENT] Connection attempt %d (infinite retries)...\n", attempt);
        } else {
            printf("[DRONE_CLIENT] Connection attempt %d/%d...\n", attempt, max_retries);
        }
        
        int ret = drone_client_connect(client);
        if (ret == DRONE_CLIENT_SUCCESS) {
            printf("[DRONE_CLIENT] Connected on attempt %d\n", attempt);
            break;
        }
        
        printf("[DRONE_CLIENT] Connection attempt %d failed: %s\n", attempt, client->last_error);
        
        // Check if we should stop trying (only for non-infinite retries)
        if (!infinite_retries && attempt >= max_retries) {
            printf("[DRONE_CLIENT] Reached maximum retry attempts (%d)\n", max_retries);
            break;
        }
        
        printf("[DRONE_CLIENT] Retrying in %d seconds...\n", delay);
        sleep(delay);
    }

    client->running = true;
    
    if (pthread_create(&client->worker_thread, NULL, worker_thread_function, client) != 0) {
        client->running = false;
        set_error(client, "Failed to create worker thread");
        return DRONE_CLIENT_ERROR;
    }
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_stop(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    if (!client->running) return DRONE_CLIENT_SUCCESS;
    
    client->running = false;
    
    // Wait for worker thread to finish
    if (pthread_join(client->worker_thread, NULL) != 0) {
        // Thread join failed, but we'll continue cleanup
    }
    
    drone_client_disconnect(client);
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_send_heartbeat(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    if (!client->connected) return DRONE_CLIENT_ERROR;
    
    return send_heartbeat_internal(client);
}

int drone_client_send_telemetry(drone_client_handle_t* client, const char* telemetry_json) {
    if (!client || !telemetry_json) return DRONE_CLIENT_ERROR;
    if (!client->connected) return DRONE_CLIENT_ERROR;
    
    char path[256];
    char response[BUFFER_SIZE];
    
    snprintf(path, sizeof(path), "/api/drones/%s/telemetry", client->config.drone_id);
    
    int result = send_http_request(client, "POST", path, telemetry_json, response);
    if (result < 0) {
        return result;
    }
    
    if (strstr(response, "HTTP/1.1 20") != NULL) {
        return DRONE_CLIENT_SUCCESS;
    } else {
        set_error(client, "Failed to send telemetry");
        return DRONE_CLIENT_ERROR;
    }
}

int drone_client_send_status(drone_client_handle_t* client, const char* status) {
    if (!client || !status) return DRONE_CLIENT_ERROR;
    if (!client->connected) return DRONE_CLIENT_ERROR;
    
    char path[256];
    char body[256];
    char response[BUFFER_SIZE];
    
    snprintf(path, sizeof(path), "/api/drones/%s/status", client->config.drone_id);
    snprintf(body, sizeof(body), "{\"status\": \"%s\"}", status);
    
    int result = send_http_request(client, "POST", path, body, response);
    if (result < 0) {
        return result;
    }
    
    if (strstr(response, "HTTP/1.1 20") != NULL) {
        if (client->status_callback) {
            client->status_callback(status, client->status_callback_data);
        }
        return DRONE_CLIENT_SUCCESS;
    } else {
        set_error(client, "Failed to send status");
        return DRONE_CLIENT_ERROR;
    }
}

int drone_client_get_stream_config(drone_client_handle_t* client, char* stream_ip, int* stream_port, int* telemetry_port, int* command_port, int* control_port) {
    if (!client || !stream_ip || !stream_port || !telemetry_port || !command_port || !control_port) return DRONE_CLIENT_ERROR;
    if (!client->connected) return DRONE_CLIENT_ERROR;
    
    char path[256];
    char response[BUFFER_SIZE];
    
    snprintf(path, sizeof(path), "/api/drones/%s/drone-ports-config", client->config.drone_id);
    
    int result = send_http_request(client, "GET", path, NULL, response);
    if (result < 0) {
        return result;
    }
    
    if (strstr(response, "HTTP/1.1 200") != NULL) {
        char* json_start = strstr(response, "\r\n\r\n");
        if (json_start) {
            json_start += 4;
            
            /* Safety checks for JSON string before parsing */
            if (!json_start || strlen(json_start) == 0) {
                set_error(client, "Empty JSON response");
                return DRONE_CLIENT_ERROR;
            }
            
            /* Basic JSON sanity check */
            if (json_start[0] != '{' && json_start[0] != '[') {
                set_error(client, "Response doesn't look like JSON");
                return DRONE_CLIENT_ERROR;
            }
            
            /* Ensure string is reasonably terminated */
            size_t json_len = strlen(json_start);
            if (json_len > 10000) { /* Sanity check - config shouldn't be huge */
                set_error(client, "JSON response too large");
                return DRONE_CLIENT_ERROR;
            }
            
            struct json_object *root_json = json_tokener_parse(json_start);
            if (root_json == NULL) {
                set_error(client, "Failed to parse JSON response");
                return DRONE_CLIENT_ERROR;
            }

            struct json_object *server_ip_obj = NULL;
            struct json_object *video_send_port_obj = NULL;
            struct json_object *telemetry_send_port_obj = NULL;
            struct json_object *command_listen_port_obj = NULL;
            struct json_object *control_listen_port_obj = NULL;
            
            if (!json_object_object_get_ex(root_json, "server_ip", &server_ip_obj) ||
                !json_object_object_get_ex(root_json, "video_send_port", &video_send_port_obj) ||
                !json_object_object_get_ex(root_json, "telemetry_send_port", &telemetry_send_port_obj) ||
                !json_object_object_get_ex(root_json, "command_listen_port", &command_listen_port_obj) ||
                !json_object_object_get_ex(root_json, "control_listen_port", &control_listen_port_obj)) {
                json_object_put(root_json);
                set_error(client, "Missing required fields in JSON response");
                return DRONE_CLIENT_ERROR;
            }
            
            const char* ip = json_object_get_string(server_ip_obj);
            int stream_port_val = json_object_get_int(video_send_port_obj);
            int telemetry_port_val = json_object_get_int(telemetry_send_port_obj);
            int command_port_val = json_object_get_int(command_listen_port_obj);
            int control_port_val = json_object_get_int(control_listen_port_obj);
            
            /* Additional safety checks for parsed values */
            if (ip != NULL && strlen(ip) > 0 && strlen(ip) < 256 && 
                stream_port_val > 0 && stream_port_val < 65536 && 
                telemetry_port_val > 0 && telemetry_port_val < 65536 &&
                command_port_val > 0 && command_port_val < 65536 &&
                control_port_val > 0 && control_port_val < 65536) {
                strncpy(stream_ip, ip, 255);
                stream_ip[255] = '\0';
                *stream_port = stream_port_val;
                *telemetry_port = telemetry_port_val;
                *command_port = command_port_val;
                *control_port = control_port_val;
                
                json_object_put(root_json);
                return DRONE_CLIENT_SUCCESS;
            } else {
                json_object_put(root_json);
                set_error(client, "Missing required fields in stream config response");
                return DRONE_CLIENT_ERROR;
            }
        }
        set_error(client, "Invalid HTTP response format");
        return DRONE_CLIENT_ERROR;
    } else {
        set_error(client, "Failed to get stream config");
        return DRONE_CLIENT_ERROR;
    }
}

bool drone_client_is_connected(const drone_client_handle_t* client) {
    return client ? client->connected : false;
}

bool drone_client_is_running(const drone_client_handle_t* client) {
    return client ? client->running : false;
}

const char* drone_client_get_last_error(const drone_client_handle_t* client) {
    return client ? client->last_error : "Invalid client handle";
}

const char* drone_client_get_session_id(const drone_client_handle_t* client) {
    return client ? client->session_id : NULL;
}

const char* drone_client_get_drone_id(const drone_client_handle_t* client) {
    return client ? client->config.drone_id : NULL;
}

// Reconnection management functions
int drone_client_set_reconnect_enabled(drone_client_handle_t* client, bool enabled) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    pthread_mutex_lock(&client->mutex);
    client->reconnect_enabled = enabled;
    if (!enabled) {
        // Reset reconnection state when disabling
        client->reconnect_attempts = 0;
    }
    pthread_mutex_unlock(&client->mutex);
    
    return DRONE_CLIENT_SUCCESS;
}

int drone_client_set_reconnect_delay(drone_client_handle_t* client, 
                                   int delay_seconds) {
    if (!client || delay_seconds < 1) {
        return DRONE_CLIENT_ERROR;
    }
    
    pthread_mutex_lock(&client->mutex);
    client->reconnect_delay_seconds = delay_seconds;
    pthread_mutex_unlock(&client->mutex);
    
    return DRONE_CLIENT_SUCCESS;
}

bool drone_client_get_reconnect_enabled(const drone_client_handle_t* client) {
    return client ? client->reconnect_enabled : false;
}

int drone_client_get_reconnect_attempts(const drone_client_handle_t* client) {
    return client ? client->reconnect_attempts : -1;
}

int drone_client_force_reconnect(drone_client_handle_t* client) {
    if (!client) return DRONE_CLIENT_ERROR;
    
    pthread_mutex_lock(&client->mutex);
    
    // Mark as disconnected to trigger reconnection
    client->connected = false;
    client->registration_valid = false;
    client->reconnect_attempts = 0; // Reset attempt counter for forced reconnect
    client->last_connection_attempt = 0; // Allow immediate reconnection
    
    pthread_mutex_unlock(&client->mutex);
    
    if (client->status_callback) {
        client->status_callback("reconnecting", client->status_callback_data);
    }
    
    return DRONE_CLIENT_SUCCESS;
}