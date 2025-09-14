/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#include "config/config_parser.h"

/**
 * Example config file
 *
 *    [protocol]               # Protocol configuration
 *    version=6                # IPv6
 *
 *    [user]
 *    name = Bob Smith         # Spaces around '=' are stripped
 *    email = bob@smith.com    # And comments (like this) ignored
 *    active = true            # Test a boolean
 *    pi = 3.14159             # Test a floating point number
 *    trillion = 1000000000000 # Test 64-bit integers
 *
 * example https://github.com/benhoyt/inih/blob/master/examples/ini_example.c
 */
int config_parser_handler(void* user, const char* section, const char* name, const char* value)
{

    return 0;
}

