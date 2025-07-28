
#include <linux/joystick.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "log.h"
#include "joystick.h"
#include "menu.h"
#include "ui_interface.h"

// Joystick handling
static const char* module_name_str = "JOYSTICK";
static int joystick_fd = -1;
static pthread_t joystick_thread;
static bool joystick_running = false;
static char current_button_text[32] = "none";
lv_indev_t* kb_indev;

// Joystick button names for display
static const char* button_names[] = {
    "B", "A", "X", "Y", "LB", "RB", "LT", "RT", 
    "Select", "Start", "??", "L3", "R3", "UP", "DOWN", "LEFT", "RIGHT"
};

/**
 * Joystick reading thread
 */
static void* joystick_reader_thread(void* arg)
{
    struct js_event event;
    
    INFO("Starting joystick reader thread");
    
    while (joystick_running) {
        if (joystick_fd < 0) {
            // Try to open joystick
            joystick_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
            if (joystick_fd < 0) {
                usleep(1000000); // Wait 1 second before retry
                continue;
            }
            INFO("Connected to /dev/input/js0");
        }
        
        ssize_t bytes = read(joystick_fd, &event, sizeof(event));
        if (bytes == sizeof(event)) {
            // Process joystick event
            if (event.type == JS_EVENT_BUTTON) {
                if (event.value == 1) { // Button pressed
                    if (event.number < sizeof(button_names)/sizeof(button_names[0])) {
                        snprintf(current_button_text, sizeof(current_button_text), 
                                "%s", button_names[event.number]);
                    } else {
                        snprintf(current_button_text, sizeof(current_button_text), 
                                "BTN%d", event.number);
                    }
                    DEBUG("Button %d (%s) pressed", 
                           event.number, current_button_text);
                    
                    // Handle menu navigation
                    menu_handle_navigation(event.number);
                    // show_notification_with_timeout(current_button_text);
                } else { // Button released
                    strcpy(current_button_text, "none");
                    DEBUG("Button %d released", event.number);
                }
                // Debug output current button state
            } else if (event.type == JS_EVENT_AXIS) {
                // Handle axis movement for menu navigation
                // Update UI with axis info
                char current_axis_text[64];
                snprintf(current_axis_text, sizeof(current_axis_text), 
                         "Axis %d: %d", event.number, event.value);
                // show_notification_with_timeout(current_axis_text);
            }
        } else if (bytes < 0) {
            // No data available or error
            if (errno == ENODEV) {
                INFO("Device disconnected");
                close(joystick_fd);
                joystick_fd = -1;
            }
            usleep(10000); // Wait 10ms
        }
    }
    
    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }

    INFO("Joystick reader thread stopped");
    return NULL;
}

static void joystick_reader(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
    struct js_event event;

    ssize_t bytes = read(joystick_fd, &event, sizeof(event));
    if (bytes == sizeof(event)) {
        // Process joystick event
        if (event.type == JS_EVENT_BUTTON) {
            switch (event.number) {
                    case JOYSTICK_BUTTON_UP:
                        data->key = LV_KEY_UP;
                        break;
                    case JOYSTICK_BUTTON_DOWN:
                        data->key = LV_KEY_DOWN;
                        break;
                    case JOYSTICK_BUTTON_LEFT:
                        data->key = LV_KEY_LEFT;
                        break;
                    case JOYSTICK_BUTTON_RIGHT:
                        data->key = LV_KEY_RIGHT;
                        break;
                    case JOYSTICK_BUTTON_A:
                        data->key = LV_KEY_ENTER;
                        break;
                    case JOYSTICK_BUTTON_B:
                        data->key = LV_KEY_ESC;
                        break;
                    default:
                        break;
                }
            if (event.value == 1) { // Button pressed
                if (event.number < sizeof(button_names)/sizeof(button_names[0])) {
                    snprintf(current_button_text, sizeof(current_button_text), 
                            "%s", button_names[event.number]);
                } else {
                    snprintf(current_button_text, sizeof(current_button_text), 
                            "BTN%d", event.number);
                }
                INFO("Button %d (%s) pressed", 
                        event.number, current_button_text);
                data->state = LV_INDEV_STATE_PRESSED;

                // Handle menu navigation
                // menu_handle_navigation(event.number);
                // show_notification_with_timeout(current_button_text);
            } else { // Button released
                strcpy(current_button_text, "none");
                INFO("Button %d released", event.number);
                data->state = LV_INDEV_STATE_RELEASED;
            }
            // Debug output current button state
        } else if (event.type == JS_EVENT_AXIS) {
            // Handle axis movement for menu navigation
            // Update UI with axis info
            char current_axis_text[64];
            snprintf(current_axis_text, sizeof(current_axis_text), 
                        "Axis %d: %d", event.number, event.value);
            // show_notification_with_timeout(current_axis_text);
        }
    }

}

/**
 * Initialize joystick handling
 */
int init_joystick(void)
{
    while (joystick_fd < 0) {
        // Try to open joystick
        joystick_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
        if (joystick_fd < 0) {
            usleep(1000000); // Wait 1 second before retry
            continue;
        }
        INFO("Connected to /dev/input/js0");
    }

    joystick_running = true;
    static lv_indev_drv_t joystick_indev_drv;
    lv_indev_drv_init(&joystick_indev_drv);
    joystick_indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    joystick_indev_drv.read_cb = joystick_reader; // Use our thread as read callback
    kb_indev = lv_indev_drv_register(&joystick_indev_drv);
    lv_group_t* g = lv_group_create();
    lv_indev_set_group(kb_indev, g);
    lv_group_set_default(g);
    
    // if (pthread_create(&joystick_thread, NULL, joystick_reader_thread, NULL) != 0) {
    //     ERROR("Failed to create joystick thread");
    //     joystick_running = false;
    //     return -1;
    // }
    
    // pthread_detach(joystick_thread);
    INFO("Joystick handling initialized");
    return 0;
}

/**
 * Cleanup joystick handling
 */
void cleanup_joystick(void)
{
    joystick_running = false;
    
    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }

    INFO("Joystick handling cleaned up");
}
