#pragma once

#include <Arduino.h>
#include <HX711.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// ─── HX711 object and ring buffer ─────────────────────────────────────────────
extern HX711             scale;
extern SemaphoreHandle_t scaleMutex;
extern TaskHandle_t      hx711TaskHandle;
extern portMUX_TYPE      scaleMux;

extern volatile long rawBuf[];
extern volatile int  rawHead;
extern volatile int  rawCount;

// ─── Calibration settings (persisted by nvs_manager) ─────────────────────────
extern float calibration_factor;
extern long  calibration_offset;
extern int   num_samples;

// ─── Public API ───────────────────────────────────────────────────────────────
void  initLoadCell();
long  safeRead();
long  safeReadAvg(int n);
void  safeTare(int n = 15);
float getFilteredWeight(int n);
void  hx711Pause();
void  hx711Resume();
void  hx711Task(void* pv);
