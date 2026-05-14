#pragma once

#include <ESPTelnet.h>
#include <stdarg.h>

void tlogInit();
void tlogConnect(ESPTelnet* t);
void tlogDump(ESPTelnet* t);
void tlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
