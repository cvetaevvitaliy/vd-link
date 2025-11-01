#include "drone_client.h"
#include "parson.h"
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

#define BUFFER_SIZE 4096

struct drone_client_handle {
    drone_client_config_t config;
    char session_id[128];
    char last_error[512];
    bool connected;
    bool running;
    pthread_t worker_thread;
    pthread_mutex_t mutex;
    
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
    bytes_received = recv(sockfd, response, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0) {
        set_error(client, "Error receiving response: %s", strerror(errno));
        close(sockfd);
        return DRONE_CLIENT_NET_ERROR;
    }
    
    close(sockfd);
    return bytes_received;
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
        client->config.video_capable ? "true" : "false",
        client->config.telemetry_capable ? "true" : "false",
        client->config.commands_capable ? "true" : "false");
    
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
            
            char *session_start = strstr(response_body, "\"current_session_id\":");
            if (session_start) {
                session_start = strchr(session_start, '"');
                if (session_start) {
                    session_start++;
                    char *session_end = strchr(session_start, '"');
                    if (session_end) {
                        size_t len = session_end - session_start;
                        if (len < sizeof(client->session_id) - 1) {
                            strncpy(client->session_id, session_start, len);
                            client->session_id[len] = '\0';
                            printf("Session ID: %s\n", client->session_id);
                        }
                    }
                }
            }
            
            client->connected = true;
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

// Worker thread function
static void* worker_thread_function(void* arg) {
    drone_client_handle_t* client = (drone_client_handle_t*)arg;
    time_t last_heartbeat = 0;
    int retry_count = 0;
    
    if (!client) return NULL;
    
    while (client->running) {
        time_t current_time = time(NULL);
        
        // Check if it's time for heartbeat
        if (current_time - last_heartbeat >= client->config.heartbeat_interval) {
            if (send_heartbeat_internal(client) == DRONE_CLIENT_SUCCESS) {
                last_heartbeat = current_time;
                retry_count = 0;  // Reset retry count on success
            } else {
                retry_count++;
                if (retry_count >= client->config.max_retries) {
                    set_error(client, "Too many heartbeat failures, stopping");
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
    if (!client->connected) {
        int ret = drone_client_connect(client);
        if (ret != DRONE_CLIENT_SUCCESS) {
            return ret;
        }
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

int drone_client_get_stream_config(drone_client_handle_t* client, char* stream_ip, int* stream_port, int* telemetry_port) {
    if (!client || !stream_ip || !stream_port || !telemetry_port) return DRONE_CLIENT_ERROR;
    if (!client->connected) return DRONE_CLIENT_ERROR;
    
    char path[256];
    char response[BUFFER_SIZE];
    
    snprintf(path, sizeof(path), "/api/drones/%s/stream-config", client->config.drone_id);
    
    int result = send_http_request(client, "GET", path, NULL, response);
    if (result < 0) {
        return result;
    }
    
    if (strstr(response, "HTTP/1.1 200") != NULL) {
        char* json_start = strstr(response, "\r\n\r\n");
        if (json_start) {
            json_start += 4;
            
            JSON_Value *root_value = json_parse_string(json_start);
            if (root_value == NULL) {
                set_error(client, "Failed to parse JSON response");
                return DRONE_CLIENT_ERROR;
            }
            
            JSON_Object *root_object = json_value_get_object(root_value);
            if (root_object == NULL) {
                json_value_free(root_value);
                set_error(client, "Invalid JSON response format");
                return DRONE_CLIENT_ERROR;
            }
            
            const char* ip = json_object_get_string(root_object, "stream_ip");
            double stream_port_double = json_object_get_number(root_object, "stream_port");
            double telemetry_port_double = json_object_get_number(root_object, "telemetry_port");
            
            if (ip != NULL && stream_port_double > 0 && telemetry_port_double > 0) {
                strncpy(stream_ip, ip, 255);
                stream_ip[255] = '\0';
                *stream_port = (int)stream_port_double;
                *telemetry_port = (int)telemetry_port_double;
                
                json_value_free(root_value);
                return DRONE_CLIENT_SUCCESS;
            } else {
                json_value_free(root_value);
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