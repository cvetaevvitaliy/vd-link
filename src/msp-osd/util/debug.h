// #pragma once
#include <stdio.h>

#define DEBUG_OSD
#ifdef DEBUG_OSD
#define DEBUG_PRINT(fmt, args...)    fprintf(stderr, "[ MSP-OSD ] " fmt, ## args)
#else
#define DEBUG_PRINT(fmt, args...)
#endif