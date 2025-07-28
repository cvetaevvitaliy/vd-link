#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#define ENABLE_DEBUG 0

#define LOG(level, module, x, ...) \
    do { printf("["level"][%s]: " x "\n", module, ##__VA_ARGS__); } while (0)

#define DEBUG(x, ...) \
    do { if (ENABLE_DEBUG) \
         printf("[DEBUG] [%s] %s:%d: " x "\n", module_name_str, __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define DEBUG_M(x, module, ...) \
    do { if (ENABLE_DEBUG) \
         printf("[DEBUG] [%s] %s:%d: " x "\n", module, __FILE__, __LINE__, ##__VA_ARGS__); } while (0)


#define INFO(x, ...) LOG("INFO", module_name_str, x, ##__VA_ARGS__)

#define INFO_M(x, module, ...) LOG("INFO", module, x, ##__VA_ARGS__)


#define WARN(x, ...) LOG("WARN", module_name_str, x, ##__VA_ARGS__)

#define WARN_M(x, module, ...) LOG("WARN", module, x, ##__VA_ARGS__)


#define ERROR(x, ...) \
    do { fprintf(stderr, "[ERROR] [%s]: " x "\n", module_name_str, ##__VA_ARGS__); } while (0)

#define ERROR_M(x, module, ...) \
    do { fprintf(stderr, "[ERROR] [%s]: " x "\n", module, ##__VA_ARGS__); } while (0)


#define PERROR(x, ...) \
    do { char buf[64]; \
         snprintf(buf, sizeof(buf), "[ERROR] [%s]: " x, module_name_str, ##__VA_ARGS__); \
         perror(buf); \
    } while (0)

#define PERROR_M(x, module, ...) \
    do { char buf[64]; \
         snprintf(buf, sizeof(buf), "[ERROR] [%s]: " x, module, ##__VA_ARGS__); \
         perror(buf); \
    } while (0)

#endif // LOG_H