/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
#include "common.h"
#include <ini.h>

int config_parser_handler(void* user, const char* section, const char* name, const char* value);

#endif //CONFIG_PARSER_H
