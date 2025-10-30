#include "camera/camera_manager.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include "camera/camera_csi.h"
#include "camera/camera_usb.h"

// Forward declarations
static camera_sensor_t get_sensor_from_names(const char *driver, const char *card, 
                                            camera_priority_t *priority, uint8_t *quality);

// Parse media-ctl output to find cameras
// Known camera sensors for CSI interface
static const struct {
    const char *name;
    camera_sensor_t sensor;
    const char *driver_pattern;
    const char *card_pattern;
    camera_priority_t priority;
    uint8_t quality_score;
} csi_sensors[] = {
    {"imx415", SENSOR_IMX415, "imx415", "imx415", CAMERA_PRIORITY_HIGH, 95},
    {"imx307", SENSOR_IMX307, "imx307", "imx307", CAMERA_PRIORITY_HIGH, 90},
    {"gc4663", SENSOR_GC4663, "gc4663", "gc4663", CAMERA_PRIORITY_MEDIUM, 75},
    {NULL, SENSOR_UNKNOWN, NULL, NULL, CAMERA_PRIORITY_FALLBACK, 0}
};
camera_info_t *current_camera = NULL;

// Check if device exists in device tree
// Check if device name matches ISP pipeline (should be filtered out)
static bool is_isp_pipeline_device(const char *name) {
    const char *isp_devices[] = {
        "rkisp_mainpath", "rkisp_selfpath", "rkisp_rawwr", 
        "rkisp_rawrd", "rkisp-statistics", "rkisp-input-params",
        "rkisp-mipi-luma", "rkispp_", "rkispp-stats", 
        "rkispp-input", "rkispp-scale", "rkispp-m_bypass",
        "rkispp-iqtool", NULL
    };
    
    for (int i = 0; isp_devices[i] != NULL; i++) {
        if (strstr(name, isp_devices[i])) {
            return true;
        }
    }
    return false;
}

// Scan subdev devices for camera sensors
// Check V4L2 subdev for sensor information
// Get sensor type from driver/card name
static camera_sensor_t get_sensor_from_names(const char *driver, const char *card, 
                                            camera_priority_t *priority, uint8_t *quality) {
    printf("Matching sensor: driver='%s', card='%s'\n", 
           driver ? driver : "NULL", card ? card : "NULL");
    
    for (int i = 0; csi_sensors[i].name != NULL; i++) {
        if ((driver && strstr(driver, csi_sensors[i].driver_pattern)) ||
            (card && strstr(card, csi_sensors[i].card_pattern))) {
            printf("Matched sensor: %s\n", csi_sensors[i].name);
            if (priority) *priority = csi_sensors[i].priority;
            if (quality) *quality = csi_sensors[i].quality_score;
            return csi_sensors[i].sensor;
        }
    }
    
    // Additional specific matching for known patterns
    if (card) {
        if (strstr(card, "m01_f_imx307") || strstr(card, "imx307")) {
            printf("Matched IMX307 by specific pattern\n");
            if (priority) *priority = CAMERA_PRIORITY_MEDIUM;
            if (quality) *quality = 80;
            return SENSOR_IMX307;
        }
    }
    
    printf("No sensor match found\n");
    if (priority) *priority = CAMERA_PRIORITY_FALLBACK;
    if (quality) *quality = 50; // Default quality for unknown sensors
    return SENSOR_UNKNOWN;
}

// Test V4L2 device and get detailed info
bool camera_test_v4l2_device(const char *device_path, camera_info_t *info) {
    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        return false;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        close(fd);
        return false;
    }

    // Skip ISP pipeline devices - they're not actual cameras
    if (is_isp_pipeline_device((char *)cap.card)) {
        close(fd);
        return false;
    }

    // Fill basic info
    strncpy(info->device_path, device_path, MAX_DEVICE_PATH_LEN - 1);
    strncpy(info->name, (char *)cap.card, MAX_CAMERA_NAME_LEN - 1);
    strncpy(info->driver_name, (char *)cap.driver, MAX_CAMERA_NAME_LEN - 1);
    strncpy(info->bus_info, (char *)cap.bus_info, MAX_CAMERA_NAME_LEN - 1);
    
    info->supports_streaming = (cap.capabilities & V4L2_CAP_STREAMING) != 0;
    info->is_available = true;

    // Determine camera type and sensor
    if (strstr((char *)cap.bus_info, "usb")) {
        info->type = CAMERA_USB;
        info->sensor = SENSOR_UVC_GENERIC;
        info->priority = CAMERA_PRIORITY_LOW;
        info->quality_score = 60; // USB cameras typically lower quality
        
        // Try to get vendor/product ID
        char usb_device_path[64] = {0};
        char vendor_id_str[8] = {0}, product_id_str[8] = {0};
        
        // Extract device path from bus_info like "usb-ffe00000.usb-1.3" -> "1-3"
        const char *usb_part = strstr((char *)cap.bus_info, "usb-");
        if (usb_part) {
            const char *last_dash = strrchr(usb_part, '-');
            if (last_dash) {
                strncpy(usb_device_path, last_dash + 1, sizeof(usb_device_path) - 1);
                
                char vendor_path[256], product_path[256];
                snprintf(vendor_path, sizeof(vendor_path), "/sys/bus/usb/devices/%s/idVendor", usb_device_path);
                snprintf(product_path, sizeof(product_path), "/sys/bus/usb/devices/%s/idProduct", usb_device_path);
                
                FILE *f = fopen(vendor_path, "r");
                if (f) {
                    if (fgets(vendor_id_str, sizeof(vendor_id_str), f)) {
                        vendor_id_str[strcspn(vendor_id_str, "\n")] = 0;
                        info->vendor_id = (uint32_t)strtol(vendor_id_str, NULL, 16);
                    }
                    fclose(f);
                }
                
                f = fopen(product_path, "r");
                if (f) {
                    if (fgets(product_id_str, sizeof(product_id_str), f)) {
                        product_id_str[strcspn(product_id_str, "\n")] = 0;
                        info->product_id = (uint32_t)strtol(product_id_str, NULL, 16);
                    }
                    fclose(f);
                }
            }
        }
        
        printf("USB Camera detected: card='%s', driver='%s', bus_info='%s', vendor=0x%04x, product=0x%04x\n", 
               cap.card, cap.driver, cap.bus_info, info->vendor_id, info->product_id);
        
        // Check for thermal camera by looking at vendor info and device patterns
        // Vendor ID 0x3474 is Thermal Cam Co.,Ltd (thermal cameras)
        if (strstr((char *)cap.card, "thermal") || 
            strstr((char *)cap.card, "Thermal") ||
            strstr((char *)cap.driver, "thermal") ||
            strstr((char *)cap.bus_info, "3474") ||  // String check for vendor ID
            info->vendor_id == 0x3474 ||             // Numeric check for vendor ID
            (strstr((char *)cap.card, "Camera") && strlen((char *)cap.card) <= 15)) { // Short generic names often thermal
            info->type = CAMERA_THERMAL;
            info->sensor = SENSOR_THERMAL;
            info->priority = CAMERA_PRIORITY_MEDIUM; // Thermal cameras are useful for drones
            info->quality_score = 75;
            printf("Detected as thermal camera\n");
        }
    } else if (strstr((char *)cap.bus_info, "platform")) {
        info->type = CAMERA_CSI;
        info->sensor = get_sensor_from_names((char *)cap.driver, (char *)cap.card, 
                                           &info->priority, &info->quality_score);
    } else {
        info->type = CAMERA_NOT_FOUND;
        info->sensor = SENSOR_UNKNOWN;
        info->priority = CAMERA_PRIORITY_FALLBACK;
        info->quality_score = 0;
    }

    // Get supported resolutions
    struct v4l2_frmsizeenum frmsize;
    info->num_resolutions = 0;
    
    // Try multiple pixel formats commonly used by different cameras
    uint32_t formats_to_try[] = {
        V4L2_PIX_FMT_YUYV,    // Most common
        V4L2_PIX_FMT_MJPEG,   // Common for USB cameras
        V4L2_PIX_FMT_RGB24,   // Sometimes used
        V4L2_PIX_FMT_GREY,    // Thermal cameras often use this
        V4L2_PIX_FMT_Y16      // 16-bit thermal data
    };
    
    for (int fmt = 0; fmt < 5 && info->num_resolutions == 0; fmt++) {
        memset(&frmsize, 0, sizeof(frmsize));
        frmsize.pixel_format = formats_to_try[fmt];
        
        for (int i = 0; i < MAX_SUPPORTED_RESOLUTIONS; i++) {
            frmsize.index = i;
            if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
                if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    info->supported_resolutions[info->num_resolutions].width = frmsize.discrete.width;
                    info->supported_resolutions[info->num_resolutions].height = frmsize.discrete.height;
                    info->supported_resolutions[info->num_resolutions].fps = 30; // Default
                    info->num_resolutions++;
                }
            } else {
                break;
            }
        }
    }
    
    // If still no resolutions found and it's a thermal camera, add common thermal resolutions
    // Leave empty if no resolutions detected - don't add fallback values

    close(fd);
    return true;
}

// Detect CSI cameras
int camera_detect_csi(camera_info_t *cameras, int max_cameras) {
    int found = 0;
    
    printf("Detecting CSI cameras by scanning subdevices...\n");
    
    // Scan v4l-subdev devices for camera sensors
    for (int i = 0; i < 20 && found < max_cameras; i++) {
        char subdev_path[32];
        char name_path[256];
        char sensor_name[MAX_CAMERA_NAME_LEN];
        
        snprintf(subdev_path, sizeof(subdev_path), "/dev/v4l-subdev%d", i);
        snprintf(name_path, sizeof(name_path), "/sys/class/video4linux/v4l-subdev%d/name", i);
        
        // Check if subdev exists
        if (access(subdev_path, F_OK) != 0) {
            continue;
        }
        
        // Read sensor name from sysfs
        FILE *name_file = fopen(name_path, "r");
        if (!name_file) {
            continue;
        }
        
        if (fgets(sensor_name, sizeof(sensor_name), name_file) != NULL) {
            // Remove newline
            sensor_name[strcspn(sensor_name, "\n")] = '\0';
            
            // Check if it's a known camera sensor
            if (strstr(sensor_name, "imx307") || strstr(sensor_name, "imx219") || 
                strstr(sensor_name, "ov5647") || strstr(sensor_name, "ov4689")) {
                
                camera_info_t *cam = &cameras[found];
                memset(cam, 0, sizeof(camera_info_t));
                
                // Set sensor info
                strncpy(cam->name, sensor_name, sizeof(cam->name) - 1);
                snprintf(cam->device_path, sizeof(cam->device_path), "/dev/video0"); // Main ISP output
                cam->device_id = 0;  // Main video device
                cam->type = CAMERA_CSI;
                cam->priority = CAMERA_PRIORITY_HIGH;
                cam->quality_score = 90;
                cam->is_available = true;
                cam->supports_streaming = true;
                
                // Set sensor type based on name
                if (strstr(sensor_name, "imx415")) {
                    cam->sensor = SENSOR_IMX415;
                } else if (strstr(sensor_name, "imx307")) {
                    cam->sensor = SENSOR_IMX307;
                } else if (strstr(sensor_name, "gc4663")) {
                    cam->sensor = SENSOR_GC4663;
                } else {
                    cam->sensor = SENSOR_UNKNOWN;
                }
                
                // Add some resolution info for IMX307
                if (cam->sensor == SENSOR_IMX307) {
                    cam->supported_resolutions[0] = (camera_resolution_t){1920, 1080, 30};
                    cam->supported_resolutions[1] = (camera_resolution_t){1945, 1097, 60};
                    cam->num_resolutions = 2;
                }
                
                printf("CSI Camera detected: %s (subdev: %s, video: %s)\n",
                       sensor_name, subdev_path, cam->device_path);
                found++;
            }
        }
        
        fclose(name_file);
    }
    
    if (found == 0) {
        printf("No CSI camera sensors found in subdevices\n");
    }
    
    return found;
}

// Detect USB cameras
static int camera_detect_usb(camera_info_t cameras[], int max_cameras) {
    int count = 0;
    DIR *dir = opendir("/dev");
    if (!dir) {
        return 0;
    }

    printf("Scanning for USB cameras...\n");
    
    // First, collect all video devices with their info
    camera_info_t all_devices[32] = {0};
    int device_count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && device_count < 32) {
        if (strncmp(entry->d_name, "video", 5) == 0) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
            
            camera_info_t info = {0};
            if (camera_test_v4l2_device(path, &info)) {
                strncpy(info.device_path, path, sizeof(info.device_path) - 1);
                // Extract device number from path like "/dev/video48" -> 48
                sscanf(info.device_path, "/dev/video%d", &info.device_id);
                all_devices[device_count] = info;
                device_count++;
            }
        }
    }
    closedir(dir);
    
    // Now group devices by bus_info and select the best one for each camera
    for (int i = 0; i < device_count && count < max_cameras; i++) {
        if (strlen(all_devices[i].bus_info) == 0) continue; // Already processed
        
        // Find the best device for this bus_info (camera)
        camera_info_t *best_device = &all_devices[i];
        int best_score = all_devices[i].num_resolutions;
        int best_device_num = 999;
        
        // sscanf(all_devices[i].device_path, "/dev/video%d", &best_device_num);
        best_device_num = all_devices[i].device_id;
        printf("Evaluating camera: %s (%s) - device id %d \n",
               all_devices[i].name, all_devices[i].device_path, all_devices[i].device_id);
        
        // Check other devices with same bus_info
        for (int j = i + 1; j < device_count; j++) {
            if (strcmp(all_devices[i].bus_info, all_devices[j].bus_info) == 0) {
                // This is the same camera, check if it's better
                int score = all_devices[j].num_resolutions;
                int device_num = 999;
                sscanf(all_devices[j].device_path, "/dev/video%d", &device_num);
                
                // Prefer device with more resolutions, or if equal resolutions, prefer lower device number
                if (score > best_score || 
                    (score == best_score && device_num < best_device_num)) {
                    best_device = &all_devices[j];
                    best_score = score;
                    best_device_num = device_num;
                }
                // Mark as processed
                all_devices[j].bus_info[0] = '\0';
            }
        }
        
        // Add the best device for this camera
        cameras[count] = *best_device;
        count++;
        
        if (best_device->type == CAMERA_THERMAL) {
            printf("Thermal USB Camera detected: %s (%s) - %d resolutions\n",
                   best_device->name, best_device->device_path, best_device->num_resolutions);
        } else {
            printf("USB Camera detected: %s (%s)\n", best_device->name, best_device->device_path);
        }
        
        // Mark as processed
        all_devices[i].bus_info[0] = '\0';
    }
    
    return count;
}

// Detect thermal cameras  
// Camera manager functions

// Detect all available cameras
int camera_detect_all(camera_info_t *cameras, int max_cameras) {
    int total_found = 0;
    
    // Detect CSI cameras first (highest priority)
    int csi_found = camera_detect_csi(cameras + total_found, max_cameras - total_found);
    total_found += csi_found;
    
    // Detect USB cameras (including thermal)
    if (total_found < max_cameras) {
        int usb_found = camera_detect_usb(cameras + total_found, max_cameras - total_found);
        total_found += usb_found;
    }
    
    return total_found;
}

// Legacy function for compatibility
// Camera manager functions
int camera_manager_init(camera_manager_t *manager) {
    if (!manager) {
        return -1;
    }
    
    memset(manager, 0, sizeof(*manager));
    manager->primary_camera_index = -1;
    manager->secondary_camera_index = -1;
    
    // Detect all available cameras
    manager->count = camera_detect_all(manager->cameras, MAX_CAMERAS);
    
    printf("Camera Manager: Found %d cameras\n", manager->count);
    
    // Auto-select best cameras
    camera_manager_select_best(manager, CAMERA_CSI);

    return manager->count;
}

// Sort cameras by priority and quality
static void sort_cameras_by_priority(camera_info_t *cameras, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            // First sort by priority (lower number = higher priority)
            if (cameras[j].priority > cameras[j + 1].priority ||
                (cameras[j].priority == cameras[j + 1].priority && 
                 cameras[j].quality_score < cameras[j + 1].quality_score)) {
                
                camera_info_t temp = cameras[j];
                cameras[j] = cameras[j + 1];
                cameras[j + 1] = temp;
            }
        }
    }
}

int camera_manager_select_best(camera_manager_t *manager, camera_type_t preferred_type) {
    if (!manager || manager->count == 0) {
        return -1;
    }
    
    // Sort cameras by priority and quality
    sort_cameras_by_priority(manager->cameras, manager->count);
    
    // Reset selection
    manager->primary_camera_index = -1;
    manager->secondary_camera_index = -1;
    
    // First pass: look for preferred type with highest priority
    for (int i = 0; i < manager->count; i++) {
        if (manager->cameras[i].type == preferred_type && 
            manager->cameras[i].is_available &&
            manager->cameras[i].supports_streaming) {
            
            if (manager->primary_camera_index == -1) {
                manager->primary_camera_index = i;
                printf("Selected primary camera: %s (priority=%d, quality=%d)\n",
                       manager->cameras[i].name, 
                       manager->cameras[i].priority,
                       manager->cameras[i].quality_score);
            } else if (manager->secondary_camera_index == -1) {
                manager->secondary_camera_index = i;
                printf("Selected secondary camera: %s (priority=%d, quality=%d)\n",
                       manager->cameras[i].name,
                       manager->cameras[i].priority,
                       manager->cameras[i].quality_score);
                break;
            }
        }
    }
    
    // Second pass: if no primary camera found, select any available camera
    if (manager->primary_camera_index == -1) {
        for (int i = 0; i < manager->count; i++) {
            if (manager->cameras[i].is_available &&
                manager->cameras[i].supports_streaming) {
                
                manager->primary_camera_index = i;
                printf("Selected fallback primary camera: %s (priority=%d, quality=%d)\n",
                       manager->cameras[i].name,
                       manager->cameras[i].priority,
                       manager->cameras[i].quality_score);
                break;
            }
        }
    }
    
    // Third pass: select secondary camera from remaining
    if (manager->secondary_camera_index == -1) {
        for (int i = 0; i < manager->count; i++) {
            if (i != manager->primary_camera_index &&
                manager->cameras[i].is_available &&
                manager->cameras[i].supports_streaming) {
                
                manager->secondary_camera_index = i;
                printf("Selected fallback secondary camera: %s (priority=%d, quality=%d)\n",
                       manager->cameras[i].name,
                       manager->cameras[i].priority,
                       manager->cameras[i].quality_score);
                break;
            }
        }
    }
    
    return (manager->primary_camera_index >= 0) ? 1 : 0;
}

camera_info_t* camera_manager_get_primary(camera_manager_t *manager) {
    if (!manager || manager->primary_camera_index < 0) {
        return NULL;
    }
    return &manager->cameras[manager->primary_camera_index];
}

camera_info_t* camera_manager_get_secondary(camera_manager_t *manager) {
    if (!manager || manager->secondary_camera_index < 0) {
        return NULL;
    }
    return &manager->cameras[manager->secondary_camera_index];
}

camera_info_t* camera_manager_get_next_available(camera_manager_t *manager, camera_info_t *current) {
    if (!manager || !current) {
        return NULL;
    }
    
    for (int i = 0; i < manager->count; i++) {
        if (&manager->cameras[i] == current) {
            // Found current camera, return next available
            for (int j = i + 1; j < manager->count; j++) {
                if (manager->cameras[j].is_available) {
                    return &manager->cameras[j];
                }
            }
            break;
        }
    }
    return NULL;
}

camera_info_t* camera_manager_get_by_type(camera_manager_t *manager, camera_type_t type) {
    if (!manager) {
        return NULL;
    }
    
    for (int i = 0; i < manager->count; i++) {
        if (manager->cameras[i].type == type && 
            manager->cameras[i].is_available) {
            return &manager->cameras[i];
        }
    }
    return NULL;
}

camera_info_t* camera_manager_get_by_sensor(camera_manager_t *manager, camera_sensor_t sensor) {
    if (!manager) {
        return NULL;
    }
    
    for (int i = 0; i < manager->count; i++) {
        if (manager->cameras[i].sensor == sensor && 
            manager->cameras[i].is_available) {
            return &manager->cameras[i];
        }
    }
    return NULL;
}

void camera_manager_print_all(camera_manager_t *manager) {
    if (!manager) {
        printf("Camera manager is NULL\n");
        return;
    }
    
    printf("\n=== Camera Manager Status ===\n");
    printf("Total cameras found: %d\n", manager->count);
    
    if (manager->primary_camera_index >= 0) {
        camera_info_t *primary = &manager->cameras[manager->primary_camera_index];
        printf("Primary camera: %s (%s)\n", primary->name, primary->device_path);
    } else {
        printf("Primary camera: None selected\n");
    }
    
    if (manager->secondary_camera_index >= 0) {
        camera_info_t *secondary = &manager->cameras[manager->secondary_camera_index];
        printf("Secondary camera: %s (%s)\n", secondary->name, secondary->device_path);
    } else {
        printf("Secondary camera: None selected\n");
    }
    
    printf("\nAll detected cameras:\n");
    for (int i = 0; i < manager->count; i++) {
        camera_info_t *cam = &manager->cameras[i];
        char status_marker = ' ';
        if (i == manager->primary_camera_index) status_marker = '*';
        else if (i == manager->secondary_camera_index) status_marker = '+';
        
        printf("%c [%d] %s (%s)\n", status_marker, i, cam->name, cam->device_path);
        printf("    Type: %s, Sensor: %s\n", 
               camera_type_to_string(cam->type), 
               sensor_type_to_string(cam->sensor));
        printf("    Priority: %s, Quality: %d\n", 
               priority_to_string(cam->priority), cam->quality_score);
        printf("    Available: %s, Streaming: %s\n",
               cam->is_available ? "Yes" : "No",
               cam->supports_streaming ? "Yes" : "No");
        
        if (cam->num_resolutions > 0) {
            printf("    Resolutions: ");
            for (int j = 0; j < cam->num_resolutions && j < 3; j++) {
                printf("%ux%u", cam->supported_resolutions[j].width,
                               cam->supported_resolutions[j].height);
                if (j < cam->num_resolutions - 1 && j < 2) printf(", ");
            }
            if (cam->num_resolutions > 3) printf("...");
            printf("\n");
        }
        printf("\n");
    }
    
    printf("Legend: * = Primary camera, + = Secondary camera\n");
}

int camera_select_camera_by_idx(camera_manager_t *manager, common_config_t *config, int index)
{
    if (!manager || manager->count == 0 || index < 0 || index >= manager->count) {
        return -1;
    }

    camera_info_t *next_camera = &manager->cameras[index];
    return camera_select_camera(manager, config, next_camera);
}

int camera_get_current_camera_index(camera_manager_t *manager)
{
    if (!manager || !current_camera) {
        return -1;
    }

    for (int i = 0; i < manager->count; i++) {
        if (&manager->cameras[i] == current_camera) {
            return i;
        }
    }
    return -1;
}

int camera_select_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *next_camera)
{
    if (!manager || !next_camera) {
        return -1;
    }

    if (!next_camera->is_available || !next_camera->supports_streaming) {
        return -1;
    }

    if (current_camera) {
        printf("Current camera before switch: %s (type %s)\n", 
            current_camera ? current_camera->name : "None", 
            current_camera ? camera_type_to_string(current_camera->type) : "N/A");

        camera_manager_unbind_camera(manager, config, current_camera);
        camera_manager_deinit_camera(manager, config, current_camera);
    }
    printf("Switching to camera: %s (type: %s)\n", next_camera->name, camera_type_to_string(next_camera->type));
    camera_manager_init_camera(manager, config, next_camera);
    camera_manager_bind_camera(manager, config, next_camera);

    current_camera = next_camera;

    return 0;
}

int camera_manager_init_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera)
{
    if (!manager || manager->count == 0 || !config || !camera) {
        return -1;
    }

    if (camera->type == CAMERA_CSI) {
        camera_csi_init(&config->camera_csi_config);
    } else if (camera->type == CAMERA_USB || camera->type == CAMERA_THERMAL) {
        if (config->camera_usb_config.height == 0 || config->camera_usb_config.width == 0) {
            config->camera_usb_config.height = camera->supported_resolutions[0].height;
            config->camera_usb_config.width = camera->supported_resolutions[0].width;
        }
        if (config->camera_usb_config.device_index <= 0) {
            config->camera_usb_config.device_index = camera->device_id;
        }
        camera_usb_init(config);
    }
    printf("Initialized camera: %s (type: %s)\n", camera->name, camera_type_to_string(camera->type));

    return 0;
}

void camera_manager_deinit_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera)
{
    if (!manager || manager->count == 0 || !config || !camera) {
        return;
    }

    if (camera->type == CAMERA_CSI) {
        camera_csi_unbind_encoder(config->camera_csi_config.cam_id, 0 /* encoder id */);
        camera_csi_deinit(&config->camera_csi_config);
    } else if (camera->type == CAMERA_USB || camera->type == CAMERA_THERMAL) {
        camera_usb_unbind_encoder(config->camera_usb_config.device_index, 0 /* encoder id */);
        camera_usb_deinit();
    }
}

int camera_manager_bind_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera)
{
    if (!manager || manager->count == 0 || !config || !camera) {
        return -1;
    }

    if (camera->type == CAMERA_CSI) {
        camera_csi_bind_encoder(config->camera_csi_config.cam_id, 0 /* encoder id */);
    } else if (camera->type == CAMERA_USB || camera->type == CAMERA_THERMAL) {
        camera_usb_bind_encoder(config->camera_usb_config.device_index, 0 /* encoder id */);
    }

    return 0;
}

int camera_manager_unbind_camera(camera_manager_t *manager, common_config_t *config, camera_info_t *camera)
{
    if (!manager || manager->count == 0 || !config || !camera) {
        return -1;
    }

    if (camera->type == CAMERA_CSI) {
        camera_csi_unbind_encoder(config->camera_csi_config.cam_id, 0 /* encoder id */);
    } else if (camera->type == CAMERA_USB || camera->type == CAMERA_THERMAL) {
        camera_usb_unbind_encoder(config->camera_usb_config.device_index, 0 /* encoder id */);
    }

    return 0;
}

camera_info_t* camera_manager_get_current_camera(camera_manager_t *manager)
{
    if (!manager || !current_camera) {
        return NULL;
    }
    return current_camera;
}