#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"

#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
#include <VL53L0X.h>
static VL53L0X sensor;
#endif

static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

// ─── Median filter (DISTANCE only) ───────────────────────────────────────────
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE

static uint16_t distBuf[FILTER_WINDOW];
static int      distBufIdx = 0;

static void pushDist(uint16_t v) {
    distBuf[distBufIdx++ % FILTER_WINDOW] = v;
}

static uint16_t medianDist() {
    uint16_t s[FILTER_WINDOW];
    int n = (distBufIdx < FILTER_WINDOW) ? distBufIdx : FILTER_WINDOW;
    memcpy(s, distBuf, n * sizeof(uint16_t));
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - i - 1; j++)
            if (s[j] > s[j + 1]) { uint16_t t = s[j]; s[j] = s[j + 1]; s[j + 1] = t; }
    return (n > 0) ? s[n / 2] : DISTANCE_MAX_MM;
}

#endif

// ─── Battery ──────────────────────────────────────────────────────────────────

static uint8_t s_batPct = 0;

#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
static uint16_t s_displayVal = DISTANCE_MAX_MM;
#else
static uint16_t s_displayVal = 0;
#endif

static uint8_t readBatPct() {
    long sum = 0;
    for (int i = 0; i < BAT_SAMPLES; i++) { sum += analogRead(BAT_PIN); delay(1); }
    float v = ((float)(sum / BAT_SAMPLES) / 4095.0f) * 3.3f * 2.0f;
    int pct = map((int)(v * 100), 310, 415, 0, 100);
    return (uint8_t)constrain(pct, 0, 100);
}

// ─── Milk volume (DISTANCE only) ─────────────────────────────────────────────
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE

static float milkVolumeLiters(uint16_t distMm) {
    if (distMm >= DISTANCE_MAX_MM || (int)distMm >= BIDAN_HEIGHT_MM) return 0.0f;
    int depth = BIDAN_HEIGHT_MM - (int)distMm;
    return 3.14159f * BIDAN_RADIUS_MM * BIDAN_RADIUS_MM * (float)depth / 1000000.0f;
}

#endif

// ─── Reed switch (REED only) ──────────────────────────────────────────────────
#if SENSOR_TYPE == SENSOR_TYPE_REED

static volatile uint32_t s_pulseCount = 0;
static volatile bool     s_counting   = false;
static uint32_t          s_lastOpenMs = 0;  // when reed last opened

// Called immediately after light sleep returns — pin still LOW from reed closure
static void onWakeReed() {
    if (!s_counting || digitalRead(REED_PIN) != LOW) return;

    // Count only if reed was open for at least 1 second (float really moved away)
    if (s_lastOpenMs == 0 || millis() - s_lastOpenMs >= 1000) {
        s_pulseCount++;
    }

    // Wait for reed to open, record the open time
    uint32_t t = millis();
    while (digitalRead(REED_PIN) == LOW && millis() - t < 200) delay(2);
    s_lastOpenMs = millis();
    delay(REED_DEBOUNCE_MS);
}

#endif

// ─── VL53L0X (DISTANCE only) ─────────────────────────────────────────────────
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE

static bool sensorInit() {
    digitalWrite(XSHUT_PIN, HIGH);
    delay(10);
    sensor.setTimeout(500);
    return sensor.init();
}

static void sensorStandby() { digitalWrite(XSHUT_PIN, LOW); }

static uint16_t readFiltered(int nSamples) {
    distBufIdx = 0;
    for (int i = 0; i < nSamples; i++) {
        uint16_t raw = sensor.readRangeSingleMillimeters();
        if (sensor.timeoutOccurred() || raw > DISTANCE_MAX_MM) raw = DISTANCE_MAX_MM;
        pushDist(raw);
        delay(MEASURE_SAMPLE_INTERVAL_MS);
    }
    return medianDist();
}

#endif

// ─── Button / backlight ───────────────────────────────────────────────────────

static bool     s_backlightOn    = false;
static uint32_t s_backlightTimer = 0;
static bool     s_lastBtn        = HIGH;

static void handleButton() {
    bool btn = digitalRead(BUTTON_PIN);
    if (btn == LOW && s_lastBtn == HIGH) {
        s_batPct = readBatPct();
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
        if (sensorInit()) {
            uint16_t d = sensor.readRangeSingleMillimeters();
            s_displayVal = (!sensor.timeoutOccurred() && d < DISTANCE_MAX_MM) ? d : DISTANCE_MAX_MM;
            sensorStandby();
        }
#endif
        lcd.backlight();
        s_backlightOn    = true;
        s_backlightTimer = millis();
    }
    s_lastBtn = btn;
    if (s_backlightOn && millis() - s_backlightTimer >= BACKLIGHT_TIMEOUT_MS) {
        lcd.noBacklight();
        s_backlightOn = false;
    }
}

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static volatile bool      s_cmdPending = false;
static volatile CmdPacket s_pendingCmd = {};

static void onReceive(const uint8_t*, const uint8_t* data, int len) {
    if (len < (int)sizeof(CmdPacket)) return;
    if (data[0] != PKT_TYPE_CMD) return;
    CmdPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.target_sensor_id != SENSOR_ID) return;
    memcpy((void*)&s_pendingCmd, &pkt, sizeof(pkt));
    s_cmdPending = true;
}

static void sendResult(uint16_t value, uint8_t cmdResp) {
    SensorPacket pkt = {};
    pkt.type            = PKT_TYPE_SENSOR;
    pkt.target_slave_id = TARGET_SLAVE_ID;
    pkt.distance_mm     = value;
    pkt.battery_pct     = s_batPct;
    pkt.cmd_response    = cmdResp;
    esp_now_send(BROADCAST_MAC, (const uint8_t*)&pkt, sizeof(pkt));
}

static void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) { Serial.println("[ESP-NOW] Init failed"); return; }
    esp_now_register_recv_cb(onReceive);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.printf("[ESP-NOW] Ready on ch%d, sensor_id=%d → slave_id=%d\n",
                  ESPNOW_CHANNEL, SENSOR_ID, TARGET_SLAVE_ID);
}

// ─── Command processing ───────────────────────────────────────────────────────

static void processCmd(uint8_t cmd) {
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE

    if (!sensorInit()) { sensorStandby(); Serial.println("[Sensor] Init failed"); return; }
    uint16_t dist = DISTANCE_MAX_MM;

    if (cmd == CMD_MEASURE_INIT) {
        dist = readFiltered(FILTER_WINDOW);
        Serial.printf("[CMD] INIT → %u mm\n", dist);

    } else if (cmd == CMD_MEASURE_FINAL) {
        lcd.backlight();
        s_backlightOn    = true;
        s_backlightTimer = millis();

        uint32_t t    = millis();
        uint16_t last = DISTANCE_MAX_MM;
        while (millis() - t < MEASURE_FINAL_DURATION_MS) {
            uint16_t raw = sensor.readRangeSingleMillimeters();
            if (sensor.timeoutOccurred() || raw > DISTANCE_MAX_MM) raw = DISTANCE_MAX_MM;
            pushDist(raw);
            last = medianDist();

            lcd.setCursor(0, 0);
            if (last >= DISTANCE_MAX_MM) lcd.print("Dist: --- mm    ");
            else { lcd.print("Dist: "); lcd.print(last); lcd.print(" mm     "); }

            uint32_t elapsed = millis() - t;
            lcd.setCursor(0, 1);
            lcd.print("T: "); lcd.print(elapsed / 1000); lcd.print("s / 10s   ");

            delay(MEASURE_SAMPLE_INTERVAL_MS);
        }
        dist = last;
        Serial.printf("[CMD] FINAL → %u mm\n", dist);
    }

    s_displayVal = dist;
    sendResult(dist, cmd);
    sensorStandby();

#elif SENSOR_TYPE == SENSOR_TYPE_REED

    if (cmd == CMD_MEASURE_INIT) {
        s_pulseCount = 0;
        s_lastOpenMs = 0;
        s_counting   = true;
        Serial.println("[CMD] INIT → counting started");
        sendResult(0, cmd);

    } else if (cmd == CMD_MEASURE_FINAL) {
        s_counting   = false;
        uint16_t cnt = (s_pulseCount > 0xFFFF) ? 0xFFFF : (uint16_t)s_pulseCount;
        s_displayVal = cnt;
        lcd.backlight();
        s_backlightOn    = true;
        s_backlightTimer = millis();
        Serial.printf("[CMD] FINAL → %u pulses\n", cnt);
        sendResult(cnt, cmd);
    }

#endif
}

// ─── Sleep ────────────────────────────────────────────────────────────────────

static void goToLightSleep() {
    esp_sleep_enable_wifi_wakeup();
    gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
#if SENSOR_TYPE == SENSOR_TYPE_REED
    if (s_counting)
        gpio_wakeup_enable((gpio_num_t)REED_PIN, GPIO_INTR_LOW_LEVEL);
#endif
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();

#if SENSOR_TYPE == SENSOR_TYPE_REED
    onWakeReed();   // check pin immediately while still LOW
#endif

    delay(5);
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_PIN, ADC_11db);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
    pinMode(XSHUT_PIN, OUTPUT);
    digitalWrite(XSHUT_PIN, LOW);
#endif
#if SENSOR_TYPE == SENSOR_TYPE_REED
    pinMode(REED_PIN, INPUT_PULLUP);
#endif

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    lcd.init();
    lcd.backlight();
    s_backlightOn    = true;
    s_backlightTimer = millis();
    lcd.setCursor(0, 0);
    lcd.print("Sensor v"); lcd.print(FIRMWARE_VERSION);
    lcd.setCursor(0, 1);
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
    lcd.print("ID:"); lcd.print(SENSOR_ID); lcd.print(" Dist ch:"); lcd.print(ESPNOW_CHANNEL);
#else
    lcd.print("ID:"); lcd.print(SENSOR_ID); lcd.print(" Reed ch:"); lcd.print(ESPNOW_CHANNEL);
#endif

    s_batPct = readBatPct();

#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
    if (!sensorInit()) {
        lcd.clear();
        lcd.print("SENSOR ERROR!   ");
        lcd.setCursor(0, 1);
        lcd.print("Check wiring    ");
        while (1) delay(1000);
    }
    uint16_t testDist = sensor.readRangeSingleMillimeters();
    Serial.printf("[Init] VL53L0X dist: %u mm  bat: %u%%\n", testDist, s_batPct);
    sensorStandby();
#endif
#if SENSOR_TYPE == SENSOR_TYPE_REED
    s_counting = true;   // start counting pulses immediately
    Serial.printf("[Init] Reed pin: %d  bat: %u%%\n", REED_PIN, s_batPct);
#endif

    delay(1500);
    lcd.noBacklight();
    s_backlightOn = false;

    initEspNow();
    Serial.println("[Init] Entering light sleep — waiting for commands");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    if (s_cmdPending) {
        s_cmdPending = false;
        CmdPacket cmd;
        memcpy(&cmd, (const void*)&s_pendingCmd, sizeof(cmd));
        processCmd(cmd.cmd);
    }


    handleButton();

    if (s_backlightOn) {
        char row0[17], row1[17];
        snprintf(row0, sizeof(row0), "Bat: %u%%          ", s_batPct);
        lcd.setCursor(0, 0);
        lcd.print(row0);
        lcd.setCursor(0, 1);
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
        if (s_displayVal >= DISTANCE_MAX_MM)
            snprintf(row1, sizeof(row1), "Milk: no data   ");
        else
            snprintf(row1, sizeof(row1), "Milk: %.1f L     ", milkVolumeLiters(s_displayVal));
#else
        uint16_t showCnt = s_counting ? (uint16_t)s_pulseCount : s_displayVal;
        snprintf(row1, sizeof(row1), "Cnt: %u          ", showCnt);
#endif
        lcd.print(row1);
    }

    goToLightSleep();
}
