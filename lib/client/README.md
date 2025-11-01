# VD Link Drone Client Library

A modular C library for connecting drones to the VD Link server system. This library provides a clean API for drone registration, heartbeat management, telemetry transmission, and command handling.

## Features

- **Thread-safe design** with mutex protection
- **Asynchronous operation** with background worker threads  
- **Callback-based event handling** for status changes, errors, and commands
- **Automatic heartbeat management** with configurable intervals
- **Robust error handling** with detailed error messages
- **Easy integration** as a static library

## API Overview

### Core Functions

```c
// Configuration and lifecycle
void drone_client_config_init_default(drone_client_config_t* config);
drone_client_handle_t* drone_client_create(const drone_client_config_t* config);
void drone_client_destroy(drone_client_handle_t* client);

// Connection management
int drone_client_connect(drone_client_handle_t* client);
int drone_client_disconnect(drone_client_handle_t* client);
int drone_client_start(drone_client_handle_t* client);
int drone_client_stop(drone_client_handle_t* client);

// Communication
int drone_client_send_telemetry(drone_client_handle_t* client, const char* telemetry_json);
int drone_client_send_status(drone_client_handle_t* client, const char* status);
int drone_client_send_heartbeat(drone_client_handle_t* client);

// State queries
bool drone_client_is_connected(const drone_client_handle_t* client);
bool drone_client_is_running(const drone_client_handle_t* client);
const char* drone_client_get_last_error(const drone_client_handle_t* client);
```

### Callback Functions

```c
// Set event handlers
int drone_client_set_status_callback(drone_client_handle_t* client, 
                                    drone_client_status_callback_t callback, 
                                    void* user_data);

int drone_client_set_error_callback(drone_client_handle_t* client, 
                                   drone_client_error_callback_t callback, 
                                   void* user_data);

int drone_client_set_command_callback(drone_client_handle_t* client, 
                                     drone_client_command_callback_t callback, 
                                     void* user_data);
```

## Usage Example

```c
#include "drone_client.h"

// Callback for status changes
void on_status_change(const char* status, void* user_data) {
    printf("Status: %s\n", status);
}

// Callback for errors
void on_error(int error_code, const char* message, void* user_data) {
    printf("Error %d: %s\n", error_code, message);
}

int main() {
    // Initialize configuration
    drone_client_config_t config;
    drone_client_config_init_default(&config);
    
    // Customize configuration
    strncpy(config.drone_id, "my-drone-001", sizeof(config.drone_id) - 1);
    strncpy(config.server_host, "192.168.1.100", sizeof(config.server_host) - 1);
    config.server_port = 8000;
    config.heartbeat_interval = 30;
    
    // Create client
    drone_client_handle_t* client = drone_client_create(&config);
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }
    
    // Set callbacks
    drone_client_set_status_callback(client, on_status_change, NULL);
    drone_client_set_error_callback(client, on_error, NULL);
    
    // Connect and start
    if (drone_client_connect(client) != DRONE_CLIENT_SUCCESS) {
        printf("Failed to connect: %s\n", drone_client_get_last_error(client));
        drone_client_destroy(client);
        return 1;
    }
    
    if (drone_client_start(client) != DRONE_CLIENT_SUCCESS) {
        printf("Failed to start: %s\n", drone_client_get_last_error(client));
        drone_client_destroy(client);
        return 1;
    }
    
    // Send telemetry
    char telemetry[] = "{\"battery\": 85, \"altitude\": 120.5}";
    drone_client_send_telemetry(client, telemetry);
    
    // Your application logic here...
    sleep(60);
    
    // Cleanup
    drone_client_destroy(client);
    return 0;
}
```

## Configuration

The `drone_client_config_t` structure contains all configuration options:

```c
typedef struct drone_client_config {
    char server_host[256];        // Server hostname/IP
    int server_port;              // Server port
    int heartbeat_interval;       // Heartbeat interval in seconds
    int max_retries;              // Max retry attempts
    int timeout_seconds;          // Network timeout
    char drone_id[64];            // Unique drone identifier
    char name[128];               // Human-readable drone name
    char firmware_version[32];    // Firmware version string
    char hardware_version[32];    // Hardware version string
    
    // Capabilities
    bool video_capable;           // Video streaming capability
    bool telemetry_capable;       // Telemetry transmission capability
    bool commands_capable;        // Command reception capability
} drone_client_config_t;
```

## Error Codes

```c
#define DRONE_CLIENT_SUCCESS       0   // Operation successful
#define DRONE_CLIENT_ERROR        -1   // General error
#define DRONE_CLIENT_NET_ERROR    -2   // Network error
#define DRONE_CLIENT_AUTH_ERROR   -3   // Authentication error
#define DRONE_CLIENT_TIMEOUT      -4   // Timeout error
```

## Building

### As part of the main project:

The library will be built automatically when building the main VD Link project.

### Standalone:

```bash
mkdir build
cd build
cmake ..
make
```

This will create:
- `libdrone_client.a` - Static library
- `drone_client_example` - Example executable

## Thread Safety

The library is designed to be thread-safe:
- All public API calls are protected by internal mutexes
- Background worker thread handles heartbeat automatically
- Callbacks are executed in the worker thread context

## Integration with VD Link Project

To use this library in other parts of the VD Link project:

1. **Include the header:**
   ```c
   #include "lib/client/drone_client.h"
   ```

2. **Link the library:**
   ```cmake
   target_link_libraries(your_target drone_client)
   ```

3. **Add include directory:**
   ```cmake
   target_include_directories(your_target PRIVATE lib/client)
   ```

## Protocol

The library communicates with the server using HTTP/JSON:

- **Registration:** `POST /api/drones/register`
- **Heartbeat:** `POST /api/drones/{drone_id}/heartbeat`
- **Telemetry:** `POST /api/drones/{drone_id}/telemetry`
- **Status:** `POST /api/drones/{drone_id}/status`

All requests include appropriate JSON payloads and expect HTTP 2xx responses for success.