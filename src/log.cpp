#include "log.h"
#include <cstring>

static constexpr int kMaxInlineItems = 6;
static constexpr uint32_t kLogTimeoutMs = 1000;
static constexpr size_t kTagBufferSize = 32;

static char g_last_tag[kTagBufferSize] = "";
static log_level_t g_last_level = LOG_LEVEL_INFO;
static bool g_line_open = false;
static uint32_t g_last_log_time = 0;
static int g_inline_count = 0;

static const char *const kLevelColors[] = {
    "\033[0;31m", // ERROR: Đỏ
    "\033[0;33m", // WARN:  Vàng
    "\033[0;32m", // INFO:  Xanh lá
    "\033[0;36m"  // DEBUG: Xanh dương (Cyan)
};
static const char *const kLevelIcons[] = {"🛑", "⚠️ ", "ℹ️", "🔍"};
static const char *const kLevelNames[] = {"E", "W", "I", "D"};

static bool can_inline(log_level_t level) {
    return level == LOG_LEVEL_INFO || level == LOG_LEVEL_DEBUG;
}

static void write_prefix(log_level_t level, const char *tag, const char *message) {
    Serial.printf("%s%s [%s][%s]\033[0m %s", kLevelColors[level], kLevelIcons[level],
                  kLevelNames[level], tag, message);
}

void log_print(log_level_t level, const char *tag, const char *format, ...) {
    if (tag == nullptr) {
        tag = "UNKNOWN";
    }
    if (format == nullptr) {
        return;
    }

    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    const uint32_t now = millis();
    const bool should_inline = can_inline(level);
    const bool tag_changed = strcmp(g_last_tag, tag) != 0;
    const bool level_changed = g_last_level != level;
    const bool timeout_reached = now - g_last_log_time > kLogTimeoutMs;
    const bool line_is_full = g_inline_count >= kMaxInlineItems;

    if (g_line_open &&
        (!should_inline || tag_changed || level_changed || timeout_reached || line_is_full)) {
        Serial.print("\n");
        g_line_open = false;
        g_inline_count = 0;
    }

    if (!g_line_open) {
        write_prefix(level, tag, buffer);
        g_line_open = should_inline;
        g_inline_count = should_inline ? 1 : 0;
    } else {
        Serial.printf(" -> %s", buffer);
        g_inline_count++;
    }

    strncpy(g_last_tag, tag, sizeof(g_last_tag) - 1);
    g_last_tag[sizeof(g_last_tag) - 1] = '\0';
    g_last_level = level;
    g_last_log_time = now;
}

void log_flush(void) {
    if (!g_line_open) {
        return;
    }

    Serial.print("\n");
    g_line_open = false;
    g_inline_count = 0;
    g_last_tag[0] = '\0';
}
