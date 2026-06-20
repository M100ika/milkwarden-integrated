#include <Arduino.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "config.h"

static VL53L0X           sensor;
static LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── Median filter ────────────────────────────────────────────────────────────

static uint16_t distBuf[FILTER_WINDOW];
static int      distBufIdx = 0;

static void pushDist(uint16_t v) {
    distBuf[distBufIdx % FILTER_WINDOW] = v;
    distBufIdx++;
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

// ─── Battery ──────────────────────────────────────────────────────────────────

static uint8_t  s_batPct     = 0;
static uint16_t s_displayDist = DISTANCE_MAX_MM;  // last known distance for info screen

static uint8_t readBatPct() {
    long sum = 0;
    for (int i = 0; i < BAT_SAMPLES; i++) { sum += analogRead(BAT_PIN); delay(1); }
    float v = ((float)(sum / BAT_SAMPLES) / 4095.0f) * 3.3f * 2.0f;
    int pct = map((int)(v * 100), 310, 415, 0, 100);
    return (uint8_t)constrain(pct, 0, 100);
}

// ─── Milk volume ──────────────────────────────────────────────────────────────

static float milkVolumeLiters(uint16_t distMm) {
    if (distMm >= DISTANCE_MAX_MM || (int)distMm >= BIDAN_HEIGHT_MM) return 0.0f;
    int depth = BIDAN_HEIGHT_MM - (int)distMm;
    return 3.14159f * BIDAN_RADIUS_MM * BIDAN_RADIUS_MM * (float)depth / 1000000.0f;
}

static bool     sensorInit();
static void     sensorStandby();

// ─── Button / backlight ───────────────────────────────────────────────────────

static bool     s_backlightOn    = false;
static uint32_t s_backlightTimer = 0;
static bool     s_lastBtn        = HIGH;

static void handleButton() {
    bool btn = digitalRead(BUTTON_PIN);
    if (btn == LOW && s_lastBtn == HIGH) {
        s_batPct = readBatPct();
        if (sensorInit()) {
            uint16_t d = sensor.readRangeSingleMillimeters();
            s_displayDist = (!sensor.timeoutOccurred() && d < DISTANCE_MAX_MM) ? d : DISTANCE_MAX_MM;
            sensorStandby();
        }
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

// ─── VL53L0X ─────────────────────────────────────────────────────────────────

static bool sensorInit() {
    digitalWrite(XSHUT_PIN, HIGH);
    delay(10);
    sensor.setTimeout(500);
    return sensor.init();
}

static void sensorStandby() {
    digitalWrite(XSHUT_PIN, LOW);
}

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

static void sendResult(uint16_t distMm, uint8_t cmdResp) {
    SensorPacket pkt = {};
    pkt.type            = PKT_TYPE_SENSOR;
    pkt.target_slave_id = TARGET_SLAVE_ID;
    pkt.distance_mm     = distMm;
    pkt.battery_pct     = s_batPct;
    pkt.cmd_response    = cmdResp;
    esp_now_send(BROADCAST_MAC, (const uint8_t*)&pkt, sizeof(pkt));
}

static void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed");
        return;
    }
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
    if (!sensorInit()) {
        Serial.println("[Sensor] Init failed");
        return;
    }

    uint16_t dist = DISTANCE_MAX_MM;

    if (cmd == CMD_MEASURE_INIT) {
        // Quick single measurement
        dist = readFiltered(FILTER_WINDOW);
        Serial.printf("[CMD] INIT → %u mm\n", dist);

    } else if (cmd == CMD_MEASURE_FINAL) {
        // Measure for MEASURE_FINAL_DURATION_MS
        lcd.backlight();
        s_backlightOn    = true;
        s_backlightTimer = millis();

        uint32_t t   = millis();
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

    s_displayDist = dist;
    sendResult(dist, cmd);
    sensorStandby();
}

// ─── Sleep ────────────────────────────────────────────────────────────────────

static void goToLightSleep() {
    // Wake on ESP-NOW packet
    esp_sleep_enable_wifi_wakeup();

    // Also wake on button press (GPIO LOW)
    gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    // Give WiFi stack a moment to complete the receive callback
    delay(5);
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_PIN, ADC_11db);
    pinMode(XSHUT_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    digitalWrite(XSHUT_PIN, LOW);

    Wire.begin(21, 22);
    Wire.setClock(100000);

    lcd.init();
    lcd.backlight();
    s_backlightOn    = true;
    s_backlightTimer = millis();
    lcd.setCursor(0, 0);
    lcd.print("Sensor v"); lcd.print(FIRMWARE_VERSION);
    lcd.setCursor(0, 1);
    lcd.print("ID:"); lcd.print(SENSOR_ID);
    lcd.print(" ch:"); lcd.print(ESPNOW_CHANNEL);

    s_batPct = readBatPct();

    // Verify sensor works before going to sleep
    if (!sensorInit()) {
        lcd.clear();
        lcd.print("SENSOR ERROR!   ");
        lcd.setCursor(0, 1);
        lcd.print("Check wiring    ");
        while (1) delay(1000);
    }
    uint16_t testDist = sensor.readRangeSingleMillimeters();
    Serial.printf("[Init] Test dist: %u mm  bat: %u%%\n", testDist, s_batPct);
    sensorStandby();

    delay(1500);
    lcd.noBacklight();
    s_backlightOn = false;

    initEspNow();
    Serial.println("[Init] Entering light sleep — waiting for commands");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    // Process any pending command FIRST (set by ESP-NOW callback before sleep returned)
    if (s_cmdPending) {
        s_cmdPending = false;
        CmdPacket cmd;
        memcpy(&cmd, (const void*)&s_pendingCmd, sizeof(cmd));
        processCmd(cmd.cmd);
    }

    // Handle button (woke from GPIO or already awake)
    handleButton();

    // Update LCD if backlight is on (scenarios: button press, after CMD_FINAL)
    if (s_backlightOn) {
        char row0[17], row1[17];
        snprintf(row0, sizeof(row0), "Bat: %u%%          ", s_batPct);
        lcd.setCursor(0, 0);
        lcd.print(row0);
        lcd.setCursor(0, 1);
        if (s_displayDist >= DISTANCE_MAX_MM)
            snprintf(row1, sizeof(row1), "Milk: no data   ");
        else
            snprintf(row1, sizeof(row1), "Milk: %.1f L     ", milkVolumeLiters(s_displayDist));
        lcd.print(row1);
    }

    // Go back to light sleep (wakes on ESP-NOW or button)
    goToLightSleep();
}
