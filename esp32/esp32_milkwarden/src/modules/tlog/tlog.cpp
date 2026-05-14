#include "tlog.h"
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdarg.h>

#define BOOT_BUF_SIZE 2048

static char         s_buf[BOOT_BUF_SIZE];
static uint16_t     s_pos = 0;
static ESPTelnet*   s_tel = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static const char* rstReason(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "POWER_ON";
        case ESP_RST_SW:       return "SOFTWARE";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default:               return "OTHER";
    }
}

static void bufWrite(const char* line) {
    uint16_t len = (uint16_t)strlen(line);
    taskENTER_CRITICAL(&s_mux);
    if (s_pos + len + 2 < BOOT_BUF_SIZE) {
        memcpy(s_buf + s_pos, line, len);
        s_pos += len;
        s_buf[s_pos++] = '\n';
        s_buf[s_pos]   = '\0';
    }
    taskEXIT_CRITICAL(&s_mux);
}

void tlogInit() {
    s_buf[0] = '\0';

    Preferences prefs;
    prefs.begin("tlog", false);
    uint32_t boots = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", boots);
    prefs.end();

    char hdr[96];
    snprintf(hdr, sizeof(hdr), "=== BOOT #%lu  reason=%s ===",
             (unsigned long)boots, rstReason(esp_reset_reason()));
    bufWrite(hdr);
}

void tlogConnect(ESPTelnet* t) {
    s_tel = t;
}

void tlogDump(ESPTelnet* t) {
    if (s_pos == 0) return;
    t->print(s_buf);
}

void tlog(const char* fmt, ...) {
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    // strip trailing newline so println/printf both work uniformly
    int len = (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    if (s_tel && s_tel->isConnected()) {
        s_tel->println(line);
    } else {
        bufWrite(line);
    }
}
