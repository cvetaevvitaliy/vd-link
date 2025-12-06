/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef MENU_LEFT_H
#define MENU_LEFT_H


/* Initialize left-side menu */
void menu_left_init(void);

/* Show menu with slide-in animation */
void menu_left_show(void);

/* Hide menu with slide-out animation */
void menu_left_hide(void);

/* Optional helper: toggle visibility */
void menu_left_toggle(void);

/* Refresh menu fields from connection API (drone list/active drone/RC state) */
void menu_left_refresh_from_api(void);


#endif //MENU_LEFT_H
