/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef STATUS_H
#define STATUS_H

enum status_bar_element_e{
    STATUS_ELEMENT_LEFT,
    STATUS_ELEMENT_MID,
    STATUS_ELEMENT_RIGHT,
    STATUS_BAR,
    STATUS_ELEMENT_COUNT,
};

void screen_status_init(void);
void screen_status_update(enum status_bar_element_e element, int value);
void screen_status_show(void);
void screen_status_hide(void);
void screen_status_deinit(void);
#endif //STATUS_H
