#pragma once

#include <Arduino.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
} log_level_t;

void log_print(log_level_t level, const char *tag, const char *format, ...);
void log_flush(void);

#ifdef __cplusplus
}
#endif

#define LOG(tag, format, ...) log_print(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOG_E(tag, format, ...) log_print(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) log_print(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) log_print(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOG_D(tag, format, ...) log_print(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
