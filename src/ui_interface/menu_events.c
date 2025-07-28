#include "menu_events.h"
#include "stdlib.h"

extern lv_indev_t* kb_indev;

void tabView_event_cb(lv_event_t* event)
{
    // lv_key_t key = lv_event_get_key(event);
    // lv_obj_t* target = lv_event_get_target(event);
    // uint32_t curr_tab_id = lv_tabview_get_tab_act(target);
    // lv_group_t** groups = get_obj_tab_groups();
    // lv_group_t* group = get_current_tab_group();
    // switch (key) {
    // case LV_KEY_RIGHT:
    //     lv_event_stop_processing(event);
    //     lv_event_stop_bubbling(event);
    //     if (curr_tab_id == TABVEW_TABS_NUM - 1) {
    //         return;
    //     }
    //     lv_indev_set_group(kb_indev, groups[curr_tab_id + 1]);
    //     lv_group_remove_obj(target);
    //     lv_group_add_obj(groups[curr_tab_id + 1], target);
    //     lv_group_focus_obj(target);
    //     lv_tabview_set_act(target, lv_tabview_get_tab_act(target) + 1, LV_ANIM_OFF);
    //     break;
    // case LV_KEY_LEFT:
    //     lv_event_stop_processing(event);
    //     lv_event_stop_bubbling(event);
    //     if (curr_tab_id == 0) {
    //         return;
    //     }
    //     lv_indev_set_group(kb_indev, groups[curr_tab_id - 1]);
    //     lv_group_remove_obj(target);
    //     lv_group_add_obj(groups[curr_tab_id - 1], target);
    //     lv_group_focus_obj(target);
    //     lv_tabview_set_act(target, lv_tabview_get_tab_act(target) - 1, LV_ANIM_OFF);
    //     break;
    // case LV_KEY_DOWN: {
    //     lv_group_set_editing(group, false);
    //     lv_group_focus_next(group);
    //     lv_obj_t* keys = lv_tabview_get_tab_btns(target);
    //     lv_group_remove_obj(keys);
    //     break;
    // }
    // case LV_KEY_UP: {
    //     lv_group_focus_prev(group);
    //     lv_obj_t* keys = lv_tabview_get_tab_btns(target);
    //     lv_group_remove_obj(keys);
    //     break;
    // }
    // }
}

void dropdown_event_cb(lv_event_t* event)
{
    lv_key_t key = lv_event_get_key(event);
    lv_group_t* group;// = get_current_tab_group();
    lv_obj_t* target = lv_event_get_target(event);
    switch (key) {
    case LV_KEY_RIGHT: {
        if (lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_event_send(target, LV_EVENT_RELEASED, NULL);
            lv_group_set_editing(group, false);
        } else {
            lv_event_stop_processing(event);
            lv_group_set_editing(group, true);
            lv_key_t k = LV_KEY_DOWN;
            lv_event_send(target, LV_EVENT_KEY, &k);
        }
        break;
    }
    case LV_KEY_LEFT: {
        if (lv_group_get_editing(group)) {
            lv_group_set_editing(group, false);
            lv_key_t k = LV_KEY_ESC;
            lv_event_stop_processing(event);
            lv_event_send(target, LV_EVENT_KEY, &k);
            lv_dropdown_close(target);
        } else {
            lv_event_stop_processing(event);
           // lv_group_focus_obj(get_main_tabview());
        }
        break;
    }
    case LV_KEY_DOWN: {
        if (!lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_group_focus_next(group);
        }
        break;
    }
    case LV_KEY_UP: {
        if (!lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_group_focus_prev(group);
        }
        break;
    }
    }
}

void button_event_cb(lv_event_t* event)
{
    lv_key_t key = lv_event_get_key(event);
    lv_group_t* group ;//= get_current_tab_group();
    lv_obj_t* target = lv_event_get_target(event);
    switch (key) {
    case LV_KEY_RIGHT: {
        lv_event_stop_processing(event);
        lv_obj_add_state(target, LV_STATE_PRESSED);
        lv_event_send(target, LV_EVENT_CLICKED, NULL);
        lv_obj_clear_state(target, LV_STATE_PRESSED);
        break;
    }
    case LV_KEY_LEFT: {
        lv_event_stop_processing(event);
        //lv_group_focus_obj(get_main_tabview());
        break;
    }
    case LV_KEY_DOWN: {
        lv_event_stop_processing(event);
        lv_group_focus_next(group);
        break;
    }
    case LV_KEY_UP: {
        lv_event_stop_processing(event);
        lv_group_focus_prev(group);
        break;
    }
    }
}

void switch_event_cb(lv_event_t* event)
{
    lv_key_t key = lv_event_get_key(event);
    lv_group_t* group ;//= get_current_tab_group();

    switch (key) {
    case LV_KEY_RIGHT:
    case LV_KEY_LEFT:
        break;
    case LV_KEY_DOWN: {
        lv_event_stop_processing(event);
        lv_group_focus_next(group);
        break;
    }
    case LV_KEY_UP: {
        lv_event_stop_processing(event);
        lv_group_focus_prev(group);
        break;
    }
    }
}

void roller_event_cb(lv_event_t* event)
{
    lv_key_t key = lv_event_get_key(event);
    lv_group_t* group;// = get_current_tab_group();
    lv_obj_t* target = lv_event_get_target(event);
    switch (key) {
    case LV_KEY_RIGHT: {
        if (lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_event_send(target, LV_EVENT_RELEASED, NULL);
            lv_group_set_editing(group, false);
        } else {
            lv_event_stop_processing(event);
            lv_group_set_editing(group, true);
        }
        break;
    }
    case LV_KEY_LEFT: {
        if (lv_group_get_editing(group)) {
            lv_group_set_editing(group, false);
            lv_key_t k = LV_KEY_ESC;
            lv_event_stop_processing(event);
            lv_event_send(target, LV_EVENT_KEY, &k);
        } else {
            lv_event_stop_processing(event);
            //lv_group_focus_obj(get_main_tabview());
        }
        break;
    }
    case LV_KEY_DOWN: {
        if (!lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_group_focus_next(group);
        }
        break;
    }
    case LV_KEY_UP: {
        if (!lv_group_get_editing(group)) {
            lv_event_stop_processing(event);
            lv_group_focus_prev(group);
        }
        break;
    }
    }
}

void label_event_cb(lv_event_t* event)
{
    lv_key_t key = lv_event_get_key(event);
    lv_group_t* group;// = get_current_tab_group();

    switch (key) {
    case LV_KEY_RIGHT:
        break;
    case LV_KEY_LEFT:
        lv_event_stop_processing(event);
        //lv_group_focus_obj(get_main_tabview());
        break;
    case LV_KEY_DOWN: {
        lv_event_stop_processing(event);
        lv_group_focus_next(group);
        break;
    }
    case LV_KEY_UP: {
        lv_event_stop_processing(event);
        lv_group_focus_prev(group);
        break;
    }
    }
}
