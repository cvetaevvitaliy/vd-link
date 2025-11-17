#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "common.h"

#define MAX_CAMERA_NAME_LEN 64
#define MAX_DEVICE_PATH_LEN 32
#define MAX_SUPPORTED_RESOLUTIONS 10
#define MAX_CAMERAS 16

typedef enum {
    CAMERA_NOT_FOUND = 0,
    CAMERA_CSI,
    CAMERA_USB,
    CAMERA_THERMAL,
    CAMERA_FAKE
} camera_type_t;

typedef enum {
    SENSOR_UNKNOWN = 0,
    SENSOR_IMX307,
    SENSOR_IMX415,
    SENSOR_GC4663,
    SENSOR_UVC_GENERIC,
    SENSOR_THERMAL
} camera_sensor_t;

typedef enum {
    CAMERA_PRIORITY_HIGH = 1,    // High priority (e.g. IMX415)
    CAMERA_PRIORITY_MEDIUM = 2,  // Medium priority (standard CSI cameras)
    CAMERA_PRIORITY_LOW = 3,     // Low priority (USB cameras)
    CAMERA_PRIORITY_FALLBACK = 4 // Fallback cameras
} camera_priority_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t pixel_format;
} camera_resolution_t;

typedef struct {
    camera_type_t type;
    camera_sensor_t sensor;
    char name[MAX_CAMERA_NAME_LEN];
    char device_path[MAX_DEVICE_PATH_LEN];
    char driver_name[MAX_CAMERA_NAME_LEN];
    char bus_info[MAX_CAMERA_NAME_LEN];
    uint32_t device_id;
    uint32_t vendor_id;
    uint32_t product_id;
    bool is_available;
    bool supports_streaming;
    camera_priority_t priority;
    uint8_t quality_score;  // Оцінка якості від 0 до 100
    camera_resolution_t supported_resolutions[MAX_SUPPORTED_RESOLUTIONS];
    int num_resolutions;
} camera_info_t;

typedef struct {
    camera_info_t cameras[MAX_CAMERAS];
    int count;
    int primary_camera_index;    // Індекс обраної основної камери
    int secondary_camera_index;  // Індекс резервної камери
} camera_manager_t;

// Main detection functions
int camera_detect_csi(camera_info_t *cameras, int max_cameras);

// System-level detection
bool camera_test_v4l2_device(const char *device_path, camera_info_t *info);

// Camera manager functions
int camera_manager_init(camera_manager_t *manager);
camera_info_t* camera_manager_get_primary(camera_manager_t *manager);
camera_info_t* camera_manager_get_secondary(camera_manager_t *manager);
camera_info_t* camera_manager_get_by_type(camera_manager_t *manager, camera_type_t type);
camera_info_t* camera_manager_get_by_sensor(camera_manager_t *manager, camera_sensor_t sensor);
camera_info_t* camera_manager_get_next_available(camera_manager_t *manager, camera_info_t *current);

void camera_manager_print_all(camera_manager_t *manager);
int camera_manager_select_best(camera_manager_t *manager, camera_type_t preferred_type);

int camera_select_camera_by_idx(camera_manager_t *manager, common_config_t *config, int index);
int camera_select_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *next_camera);

int camera_manager_init_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera);
void camera_manager_deinit_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera);
int camera_manager_bind_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera);
int camera_manager_unbind_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera);

camera_info_t* camera_manager_get_current_camera(camera_manager_t *manager);
int camera_get_current_camera_index(camera_manager_t *manager);


// Utility functions for string conversion
static inline const char* camera_type_to_string(camera_type_t type) {
    switch(type) {
        case CAMERA_CSI: return "CSI";
        case CAMERA_USB: return "USB";
        case CAMERA_THERMAL: return "Thermal";
        case CAMERA_FAKE: return "Fake";
        case CAMERA_NOT_FOUND: 
        default: return "Not Found";
    }
}
const char* pixel_format_to_string(uint32_t pixel_format);

static inline const char* sensor_type_to_string(camera_sensor_t sensor) {
    switch(sensor) {
        case SENSOR_IMX307: return "IMX307";
        case SENSOR_IMX415: return "IMX415";
        case SENSOR_GC4663: return "GC4663";
        case SENSOR_UVC_GENERIC: return "UVC Generic";
        case SENSOR_THERMAL: return "Thermal";
        case SENSOR_UNKNOWN:
        default: return "Unknown";
    }
}

static inline const char* priority_to_string(camera_priority_t priority) {
    switch(priority) {
        case CAMERA_PRIORITY_HIGH: return "High";
        case CAMERA_PRIORITY_MEDIUM: return "Medium";
        case CAMERA_PRIORITY_LOW: return "Low";
        case CAMERA_PRIORITY_FALLBACK: return "Fallback";
        default: return "Unknown";
    }
}