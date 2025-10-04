#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/joystick.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include "lvgl.h"
#include "log.h"

#include "input.h"
#include "main_menu.h"

#include "link_callbacks.h"

/* Test powkiddy x55 joystick support to operate cursor */
#define USE_JOYSTICK 0
#define DEBUG_INPUT 0

static const char *module_name_str = "UI INPUT";

static int joystick_fd = -1;
static bool keypad_running = false;
lv_indev_t * indev = NULL;
static lv_group_t * input_group = NULL;

#if USE_JOYSTICK
// Mouse cursor position
static int mouse_x = 640;  // Start at center
static int mouse_y = 360;
#endif

// Joystick button names for display
static const char* button_names[] = {
    "B", "A", "X", "Y", "LB", "RB", "LT", "RT", 
    "Select", "Start", "??", "L3", "R3", "UP", "DOWN", "LEFT", "RIGHT"
};

static void keyboard_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    struct js_event event;

    // Initialize data to default state
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;

    ssize_t bytes = read(joystick_fd, &event, sizeof(event));
    if (bytes == sizeof(event)) {
        if (event.type == JS_EVENT_BUTTON) {
            switch (event.number) {
                case KEYPAD_BUTTON_UP:
                    data->key = LV_KEY_UP;
                    break;
                case KEYPAD_BUTTON_DOWN:
                    data->key = LV_KEY_DOWN;
                    break;
                case KEYPAD_BUTTON_LEFT:
                    data->key = LV_KEY_LEFT;
                    break;
                case KEYPAD_BUTTON_RIGHT:
                    data->key = LV_KEY_RIGHT;
                    break;
                case KEYPAD_BUTTON_A:
                    data->key = LV_KEY_ENTER;
                    break;
                case KEYPAD_BUTTON_B:
                    data->key = LV_KEY_ESC;
                    break;
                case KEYPAD_BUTTON_START:
                    data->key = LV_KEY_HOME;
                    break;
                case KEYPAD_BUTTON_Y:
                    /* Pass through to link switch cameras */
                    // TODO: call handler directly here later
                    break;
                default:
                    return; // Don't process unhandled buttons
                    break;
                }
            if (event.value == 1) { // Button pressed
                data->state = LV_INDEV_STATE_PRESSED;
                
                // Handle menu toggle with START button
                if (event.number == KEYPAD_BUTTON_START) {
                    main_menu_toggle();
                }
                if (event.number == KEYPAD_BUTTON_Y) {
                    link_switch_cameras();
                }
            } else { // Button released
                data->state = LV_INDEV_STATE_RELEASED;
            }
#if DEBUG_INPUT
            if (event.number < sizeof(button_names) / sizeof(button_names[0])) {
                DEBUG("Key %s, state: %s", 
                    button_names[event.number], 
                    data->state == LV_INDEV_STATE_PRESSED ? "PRESSED" : "RELEASED");
            }
#endif
        }
    }
}

#if USE_JOYSTICK
void mouse_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    struct js_event event;
    bool has_event = false;

    // Read all available events
    while (true) {
        ssize_t bytes = read(joystick_fd, &event, sizeof(event));
        if (bytes != sizeof(event)) {
            break; // No more events
        }
        if (event.type == JS_EVENT_AXIS) {
            has_event = true;
            if (event.number == 0) {
                // X axis movement: map from [-32767, 32767] to [0, 1280]
                mouse_x = ((event.value + 32767) * 1280) / 65534;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= 1280) mouse_x = 1279;
            } else if (event.number == 1) {
                // Y axis movement: map from [-32767, 32767] to [0, 720]
                mouse_y = ((event.value + 32767) * 720) / 65534;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= 720) mouse_y = 719;
            }
        }
    }
    // Always update position, even if no new events
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = LV_INDEV_STATE_RELEASED; // No button press for now
    
    if (has_event) {
        DEBUG("Mouse position: (%d, %d)", mouse_x, mouse_y);
    }
}
#endif

/**
 * Initialize joystick handling
 */
int ui_keypad_init(void)
{
    int attempts = 0;
    while (joystick_fd < 0) {
        // Try to open joystick
        joystick_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
        if (joystick_fd < 0) {
            usleep(1000000); // Wait 1 second before retry
            attempts++;
            if (attempts >= 5) {
                ERROR("Failed to open joystick after 5 attempts");
                return -1; // Give up after 5 attempts
            }
            continue;
        }
        INFO("Connected to /dev/input/js0");
    }

#if !(USE_JOYSTICK)
    keypad_running = true;
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, keyboard_read);
    
    // Create input group for keyboard navigation
    // input_group = lv_group_create();
    // if (input_group) {
    //     lv_indev_set_group(indev, input_group);
    //     INFO("Input group created and assigned to keypad");
    // } else {
    //     ERROR("Failed to create input group");
    // }
#else
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, mouse_read);
    
    // Create a simple cursor object instead of image
    lv_obj_t * cursor_obj = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cursor_obj, 10, 10);
    lv_obj_set_style_bg_color(cursor_obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(cursor_obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(cursor_obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cursor_obj, 5, LV_PART_MAIN);
    lv_indev_set_cursor(indev, cursor_obj);
#endif

    INFO("Joystick handling initialized");
    return 0;
}

/**
 * Cleanup joystick handling
 */
void ui_keypad_deinit(void)
{
    keypad_running = false;

    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }

    // Input group will be deleted by menu_destroy()

    INFO("Joystick handling cleaned up");
}

lv_group_t* ui_get_input_group(void)
{
    if (input_group) {
        DEBUG("Returning input group: %p", input_group);
    } else {
        DEBUG("Input group is NULL");
    }
    return input_group;
}

void ui_set_input_group(lv_group_t *group)
{
    input_group = group;
    if (indev) {
        lv_indev_set_group(indev, group);
    }
    DEBUG("Input group set to: %p", group);
}