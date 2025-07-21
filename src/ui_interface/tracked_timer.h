#ifndef TRACKED_TIMER_H
#define TRACKED_TIMER_H

#include "lvgl.h"


void remove_tracked_timer(lv_timer_t *timer);
lv_timer_t* create_tracked_timer(lv_timer_cb_t timer_cb, uint32_t period, void *user_data);
void cleanup_tracked_timers(void);
void remove_all_tracked_timers(void);

#endif // TRACKED_TIMER_H