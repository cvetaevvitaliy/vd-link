// #pragma once
#include <stdio.h>

#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, "[ MSP-OSD ] " fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif