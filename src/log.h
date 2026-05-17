/**
 * @file log.h
 * @brief Logging utility for ESP with log levels and simple inline logging.
 */

#pragma once

#include <Arduino.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum log_level_t
 * @brief Supported log levels.
 */
typedef enum {
    /** Critical error message. */
    LOG_LEVEL_ERROR = 0,
    /** Warning message. */
    LOG_LEVEL_WARN,
    /** Informational message. */
    LOG_LEVEL_INFO,
    /** Detailed debug message. */
    LOG_LEVEL_DEBUG,
} log_level_t;

/**
 * @brief Print a log message to serial.
 *
 * The log is printed with a tag and printf-style formatting. Some log levels
 * may be combined inline when the tag and level remain unchanged.
 *
 * @param level Log level (ERROR/WARN/INFO/DEBUG).
 * @param tag Short tag to categorize the log message.
 * @param format printf-style format string.
 * @param ... Arguments for the format string.
 */
void log_print(log_level_t level, const char *tag, const char *format, ...);
void log_print_inline(log_level_t level, const char *tag, const char *format, ...);

/**
 * @brief Flush the current open log line and reset internal state.
 *
 * If a log line is currently being combined inline, this prints a newline so the
 * next log begins on a new line.
 */
void log_flush(void);

#ifdef __cplusplus
}
#endif

/**
 * @brief Log an informational message.
 *
 * This macro prints a full log line and flushes immediately.
 */
#define LOG(tag, format, ...) log_print(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief Log an error message.
 *
 * This macro prints a full log line and flushes immediately.
 */
#define LOG_E(tag, format, ...) log_print(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)

/**
 * @brief Log a warning message.
 *
 * This macro prints a full log line and flushes immediately.
 */
#define LOG_W(tag, format, ...) log_print(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)

/**
 * @brief Log an informational message (alias of LOG).
 *
 * This macro prints a full log line and flushes immediately.
 */
#define LOG_I(tag, format, ...) log_print(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief Log a debug message.
 *
 * This macro prints a full log line and flushes immediately.
 */
#define LOG_D(tag, format, ...) log_print(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)

/**
 * @brief Log an informational message inline.
 *
 * Inline logs may be combined on the same line when the level/tag is unchanged.
 */
#define LOG_INLINE(tag, format, ...) log_print_inline(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief Log an error message inline.
 */
#define LOG_E_INLINE(tag, format, ...) log_print_inline(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)

/**
 * @brief Log a warning message inline.
 */
#define LOG_W_INLINE(tag, format, ...) log_print_inline(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)

/**
 * @brief Log an informational message inline (alias of LOG_INLINE).
 */
#define LOG_I_INLINE(tag, format, ...) log_print_inline(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)

/**
 * @brief Log a debug message inline.
 */
#define LOG_D_INLINE(tag, format, ...) log_print_inline(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
