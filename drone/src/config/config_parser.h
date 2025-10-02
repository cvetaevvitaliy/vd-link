/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
#include "common.h"
#include <ini.h>

int config_parser_dumper(void* user, const char* section, const char* name, const char* value);
void config_init_defaults(common_config_t *cfg);
int config_load(const char *path, common_config_t *cfg);
void config_cleanup(common_config_t *cfg);

#endif //CONFIG_PARSER_H
