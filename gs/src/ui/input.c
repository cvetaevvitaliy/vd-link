#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/joystick.h>
#include <pthread.h>
#include <stdbool.h>

// Joystick handling
static int joystick_fd = -1;
static pthread_t joystick_thread;
static bool joystick_running = false;
static char current_button_text[32] = "none";

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
    
    printf("[ JOYSTICK ] Starting joystick reader thread\n");
    
    while (joystick_running) {
        if (joystick_fd < 0) {
            // Try to open joystick
            joystick_fd = open("/dev/input/js0", O_RDONLY | O_NONBLOCK);
            if (joystick_fd < 0) {
                usleep(1000000); // Wait 1 second before retry
                continue;
            }
            printf("[ JOYSTICK ] Connected to /dev/input/js0\n");
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
                    printf("[ JOYSTICK ] Button %d (%s) pressed\n", 
                           event.number, current_button_text);
                } else { // Button released
                    strcpy(current_button_text, "none");
                    printf("[ JOYSTICK ] Button %d released\n", event.number);
                }
            } else if (event.type == JS_EVENT_AXIS) {
                // Handle axis movement if needed
                printf("[ JOYSTICK ] Axis %d moved to %d\n", event.number, event.value);
                if (ui_elements.curr_button) {
                    lv_label_set_text_fmt(ui_elements.curr_button, "Axis: %d : %d", 
                                          event.number, event.value);
                }
            }
        } else if (bytes < 0) {
            // No data available or error
            if (errno == ENODEV) {
                printf("[ JOYSTICK ] Device disconnected\n");
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
    
    printf("[ JOYSTICK ] Joystick reader thread stopped\n");
    return NULL;
}

/**
 * Initialize joystick handling
 */
static int init_joystick(void)
{
    joystick_running = true;
    
    if (pthread_create(&joystick_thread, NULL, joystick_reader_thread, NULL) != 0) {
        fprintf(stderr, "[ JOYSTICK ] Failed to create joystick thread\n");
        joystick_running = false;
        return -1;
    }
    
    pthread_detach(joystick_thread);
    printf("[ JOYSTICK ] Joystick handling initialized\n");
    return 0;
}

/**
 * Cleanup joystick handling
 */
static void cleanup_joystick(void)
{
    joystick_running = false;
    
    if (joystick_fd >= 0) {
        close(joystick_fd);
        joystick_fd = -1;
    }
    
    printf("[ JOYSTICK ] Joystick handling cleaned up\n");
}