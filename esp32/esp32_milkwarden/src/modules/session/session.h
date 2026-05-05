#pragma once

#include <stdint.h>
#include "config.h"

enum SessionState : uint8_t {
    SESSION_IDLE        = 0,
    SESSION_COW_PRESENT = 1,
    SESSION_MILKING     = 2,
};

struct SessionInfo {
    SessionState state;
    char         rfid[25];
    float        weight_initial;
    float        weight_current;
    uint32_t     start_time;     // Unix time, 0 if not started
};

void         initSession();
void         sessionForceReset();          // Telnet: force back to IDLE
void         getSessionInfo(SessionInfo* out);

// FreeRTOS task (launched by startAllTasks)
void sessionTask(void* pv);
