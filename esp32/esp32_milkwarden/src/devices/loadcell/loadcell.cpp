#include "loadcell.h"
#include "config.h"

// ─── Global state ─────────────────────────────────────────────────────────────

HX711             scale;
SemaphoreHandle_t scaleMutex      = NULL;
TaskHandle_t      hx711TaskHandle = NULL;
portMUX_TYPE      scaleMux        = portMUX_INITIALIZER_UNLOCKED;

volatile long rawBuf[RAW_BUF_SIZE];
volatile int  rawHead  = 0;
volatile int  rawCount = 0;

// Calibration settings (loaded from NVS at boot, updated via Telnet commands)
float calibration_factor = DEFAULT_CALIB_FACTOR;
long  calibration_offset = DEFAULT_CALIB_OFFSET;
int   num_samples        = DEFAULT_NUM_SAMPLES;

// ─── Init ─────────────────────────────────────────────────────────────────────

void initLoadCell() {
    scaleMutex = xSemaphoreCreateMutex();
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    scale.set_gain(128);
    scale.set_scale(calibration_factor);

    if (calibration_offset != 0) {
        scale.set_offset(calibration_offset);
        Serial.println("[Scale] Offset loaded from NVS, tare skipped.");
    } else {
        safeTare(15);
        calibration_offset = scale.get_offset();
    }
}

// ─── FreeRTOS task control ────────────────────────────────────────────────────

void hx711Pause()  { if (hx711TaskHandle) vTaskSuspend(hx711TaskHandle); }
void hx711Resume() { if (hx711TaskHandle) vTaskResume(hx711TaskHandle); }

// ─── Thread-safe HX711 reads ─────────────────────────────────────────────────

// Waits for DOUT=LOW outside the critical section, then reads in ~100-200 µs.
long safeRead() {
    while (!scale.is_ready()) vTaskDelay(pdMS_TO_TICKS(1));
    portENTER_CRITICAL(&scaleMux);
    long v = scale.read();
    portEXIT_CRITICAL(&scaleMux);
    return v;
}

// Returns the average of n raw reads. Suspends hx711Task during sampling.
long safeReadAvg(int n) {
    hx711Pause();
    long long sum = 0;
    for (int i = 0; i < n; i++) sum += safeRead();
    hx711Resume();
    return (long)(sum / n);
}

// Tares by computing offset manually (does not call scale.tare() to avoid blocking).
void safeTare(int n) {
    hx711Pause();
    long long sum = 0;
    for (int i = 0; i < n; i++) sum += safeRead();
    scale.set_offset((long)(sum / n));
    hx711Resume();
}

// ─── Filtered weight ─────────────────────────────────────────────────────────

static void insertionSort(long* arr, int n) {
    for (int i = 1; i < n; i++) {
        long key = arr[i];
        int  j   = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
}

// Reads last n samples from the ring buffer and applies a trimmed mean (middle 50%).
// Never calls scale.read() — does not block the main thread.
float getFilteredWeight(int n) {
    if (n < 5) n = 5;
    if (n > RAW_BUF_SIZE) n = RAW_BUF_SIZE;

    long snap[RAW_BUF_SIZE];
    int  snapN;

    xSemaphoreTake(scaleMutex, portMAX_DELAY);
    int avail = (rawCount < n) ? rawCount : n;
    for (int i = 0; i < avail; i++)
        snap[i] = rawBuf[(rawHead - 1 - i + RAW_BUF_SIZE) % RAW_BUF_SIZE];
    snapN = avail;
    xSemaphoreGive(scaleMutex);

    if (snapN == 0) return 0.0f;

    insertionSort(snap, snapN);

    int from = snapN / 4;
    int to   = snapN - snapN / 4;
    if (from >= to) { from = 0; to = snapN; }

    long long sum = 0;
    for (int i = from; i < to; i++) sum += snap[i];
    long avg = (long)(sum / (to - from));

    return (float)(avg - scale.get_offset()) / calibration_factor;
}

// ─── HX711 FreeRTOS task (Core 1) ────────────────────────────────────────────
// Continuously fills the ring buffer. Main thread reads without blocking.
void hx711Task(void* pv) {
    for (;;) {
        if (scale.is_ready()) {
            long v = safeRead();
            if (xSemaphoreTake(scaleMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                rawBuf[rawHead] = v;
                rawHead  = (rawHead + 1) % RAW_BUF_SIZE;
                if (rawCount < RAW_BUF_SIZE) rawCount++;
                xSemaphoreGive(scaleMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // ~200 Hz poll; HX711 outputs 10/80 Hz
    }
}
