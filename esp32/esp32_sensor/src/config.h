#pragma once
#include <stdint.h>

#define FIRMWARE_VERSION            "2.0.0"

// ─── Sensor type — change before flashing ─────────────────────────────────────
#define SENSOR_TYPE_DISTANCE        1   // VL53L0X (бидон)
#define SENSOR_TYPE_REED            2   // геркон (импульсы)

#define SENSOR_TYPE                 SENSOR_TYPE_REED

// ─── Identity ─────────────────────────────────────────────────────────────────
#define SENSOR_ID                   2   // ID this sensor reports to the slave
#define TARGET_SLAVE_ID             2   // slave that sends commands to us

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
// Must match the WiFi channel of the router the slave is connected to.
// Check via Telnet on slave: command "wifi" → Channel field.
#define ESPNOW_CHANNEL              1

// ─── Packet types (must match esp32_milkwarden config.h) ──────────────────────
#define PKT_TYPE_SENSOR             0x03
#define PKT_TYPE_CMD                0x04

#define CMD_MEASURE_INIT            0x01   // take one quick measurement
#define CMD_MEASURE_FINAL           0x02   // measure for 10 s then report

struct __attribute__((packed)) CmdPacket {
    uint8_t type;               // PKT_TYPE_CMD
    uint8_t target_sensor_id;   // which sensor should respond
    uint8_t cmd;                // CMD_MEASURE_INIT / CMD_MEASURE_FINAL
};

struct __attribute__((packed)) SensorPacket {
    uint8_t  type;              // PKT_TYPE_SENSOR
    uint8_t  target_slave_id;   // which slave should process this
    uint16_t distance_mm;       // VL53L0X reading (2000 = out of range)
    uint8_t  battery_pct;       // 0–100
    uint8_t  cmd_response;      // CMD_MEASURE_INIT or CMD_MEASURE_FINAL
};

// ─── LCD ──────────────────────────────────────────────────────────────────────
#if SENSOR_TYPE == SENSOR_TYPE_DISTANCE
#  define LCD_I2C_ADDR              0x27
#  define I2C_SDA                   21
#  define I2C_SCL                   22
#else
#  define LCD_I2C_ADDR              0x3F
#  define I2C_SDA                   21
#  define I2C_SCL                   22
#endif

// ─── Hardware pins ────────────────────────────────────────────────────────────
#define XSHUT_PIN                   4    // VL53L0X standby (DISTANCE only)
#define BUTTON_PIN                  18
#define BAT_PIN                     34
#define REED_PIN                    19   // геркон (REED only)

// ─── Reed switch ──────────────────────────────────────────────────────────────
#define REED_DEBOUNCE_MS            50   // ждём после размыкания (мс)

// ─── Sensor ───────────────────────────────────────────────────────────────────
#define FILTER_WINDOW               7
#define DISTANCE_MAX_MM             2000

// ─── Bidan geometry ───────────────────────────────────────────────────────────
#define BIDAN_RADIUS_MM             150   // 30 cm diameter / 2
#define BIDAN_HEIGHT_MM             300   // distance from sensor (lid) to empty bottom (mm)

// ─── Battery ──────────────────────────────────────────────────────────────────
#define BAT_SAMPLES                 15

// ─── Timing ───────────────────────────────────────────────────────────────────
#define MEASURE_FINAL_DURATION_MS   10000U   // how long to measure on CMD_MEASURE_FINAL
#define MEASURE_SAMPLE_INTERVAL_MS  100U     // ms between samples during measurement
#define BACKLIGHT_TIMEOUT_MS        10000U
