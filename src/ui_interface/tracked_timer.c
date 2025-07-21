#include "tracked_timer.h"
#include "log.h"

// Store our timers for cleanup
#define MAX_TIMERS 20  // Initial value
static lv_timer_t *app_timers[MAX_TIMERS] = {0};
static int timer_count = 0;

/**
 * Helper function to remove a timer from tracking array
 */
void remove_tracked_timer(lv_timer_t *timer)
{
    if (!timer) return;
    
    for (int i = 0; i < timer_count; i++) {
        if (app_timers[i] == timer) {
            // Shift all subsequent timers down
            for (int j = i; j < timer_count - 1; j++) {
                app_timers[j] = app_timers[j + 1];
            }
            app_timers[timer_count - 1] = NULL;
            timer_count--;
            break;
        }
    }
}

/**
 * Helper function to create and track a timer
 */
lv_timer_t* create_tracked_timer(lv_timer_cb_t timer_cb, uint32_t period, void *user_data)
{
    // First check for any NULL entries in the array and reuse those slots
    for (int i = 0; i < timer_count; i++) {
        if (app_timers[i] == NULL) {
            lv_timer_t *timer = lv_timer_create(timer_cb, period, user_data);
            if (timer) {
                app_timers[i] = timer;
            }
            return timer;
        }
    }
    
    // No empty slots found, add to end if space available
    if (timer_count >= MAX_TIMERS) {
        WARN_M("LVGL", "Maximum number of timers reached");
        return NULL;
    }
    
    lv_timer_t *timer = lv_timer_create(timer_cb, period, user_data);
    if (timer) {
        app_timers[timer_count++] = timer;
    }
    return timer;
}


/**
 * Cleanup function to remove any NULL timers from the tracking array
 */
void cleanup_tracked_timers(void)
{
    int i = 0;
    while (i < timer_count) {
        if (app_timers[i] == NULL) {
            // Found a NULL timer, remove it by shifting all subsequent timers down
            for (int j = i; j < timer_count - 1; j++) {
                app_timers[j] = app_timers[j + 1];
            }
            app_timers[timer_count - 1] = NULL;
            timer_count--;
            // Don't increment i since we need to check the new element at this position
        } else {
            i++;
        }
    }
}

void remove_all_tracked_timers(void)
{
    for (int i = 0; i < timer_count; i++) {
        if (app_timers[i]) {
            lv_timer_del(app_timers[i]);
            app_timers[i] = NULL;
        }
    }
    timer_count = 0;
}
