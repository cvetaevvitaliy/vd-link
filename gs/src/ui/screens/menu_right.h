/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef MENU_RIGHT_H
#define MENU_RIGHT_H

void menu_right_init(void);

/* Show menu with slide-in animation */
void menu_right_show(void);

/* Hide menu with slide-out animation */
void menu_right_hide(void);

/* Optional helper: toggle visibility */
void menu_right_toggle(void);

/* Refresh menu fields from connection API (IP/login/password/flags) */
void menu_right_refresh_from_api(void);

/* Notify menu that connection status was changed externally */
void menu_right_on_connection_status_changed(void);

#endif //MENU_RIGHT_H
