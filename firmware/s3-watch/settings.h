#pragma once

// =============================================================================
// SETTINGS — identity and the BLE protocol spoken by firmware and S3Watch.java
// Unofficial MentraOS firmware for Waveshare ESP32-S3-Touch-AMOLED-2.06.
// Not a Waveshare product; not affiliated with or endorsed by Waveshare.
// =============================================================================
// Phone driver:
//   mobile/modules/bluetooth-sdk/android/.../sgcs/s3watch/S3WatchProtocol.kt
// Keep UUIDs, opcodes, and framing in lockstep with that file.
// =============================================================================

#define ADV_NAME_PREFIX "S3Watch"
#define DEVICE_ID ""

#define DIS_MANUFACTURER "Waveshare (unofficial)"
#define DIS_MODEL "ESP32-S3 Watch"

// Custom GATT (not AR99 / not Nordic UART).
#define S3_SERVICE_UUID "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d010"
#define S3_CTRL_UUID "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d011"
#define S3_EVT_UUID "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d012"
#define S3_IMG_UUID "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d013"
#define S3_MIC_UUID "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d014"

// Control / event frame: [opcode:u8][seq:u8][len:u16le][payload]
constexpr uint8_t HDR_LEN = 4;

constexpr uint8_t CMD_TEXT = 0x01;
constexpr uint8_t CMD_CLEAR = 0x02;
constexpr uint8_t CMD_BRIGHTNESS = 0x03;
constexpr uint8_t CMD_MIC_ENABLE = 0x04;
constexpr uint8_t CMD_TIME_SYNC = 0x05;
constexpr uint8_t CMD_IMG_BEGIN = 0x10;
constexpr uint8_t CMD_IMG_END = 0x12;

constexpr uint8_t EVT_ACK = 0x80;
constexpr uint8_t EVT_BATTERY = 0x81;
constexpr uint8_t EVT_READY = 0x82;
constexpr uint8_t EVT_GESTURE = 0x83;

// Gesture payload byte — Mentra miniapp names mapped in S3WatchProtocol.kt.
constexpr uint8_t GESTURE_SWIPE_UP = 0x01;
constexpr uint8_t GESTURE_SWIPE_DOWN = 0x02;
constexpr uint8_t GESTURE_SINGLE_TAP = 0x03;
constexpr uint8_t GESTURE_DOUBLE_TAP = 0x04;
constexpr uint8_t GESTURE_LONG_PRESS = 0x05;

constexpr uint16_t DISPLAY_WIDTH = 410;
constexpr uint16_t DISPLAY_HEIGHT = 502;

constexpr uint16_t MIC_SAMPLE_RATE = 16000;
constexpr uint8_t MIC_CHANNELS = 1;
constexpr uint8_t MIC_BITS = 16;
constexpr uint16_t MIC_FRAME_SAMPLES = 320;  // 20 ms at 16 kHz

// Waveshare ESP32-S3-Touch-AMOLED-2.06 pinout. Unofficial MentraOS firmware —
// not a Waveshare product and not affiliated with Waveshare.
constexpr uint8_t PIN_QSPI_CS = 12;
constexpr uint8_t PIN_QSPI_SCK = 11;
constexpr uint8_t PIN_QSPI_D0 = 4;
constexpr uint8_t PIN_QSPI_D1 = 5;
constexpr uint8_t PIN_QSPI_D2 = 6;
constexpr uint8_t PIN_QSPI_D3 = 7;
constexpr uint8_t PIN_PANEL_RST = 8;

constexpr uint8_t PIN_I2C_SCL = 14;
constexpr uint8_t PIN_I2C_SDA = 15;
constexpr uint8_t PIN_TP_RST = 9;
constexpr uint8_t PIN_TP_INT = 38;
constexpr uint8_t FT3168_I2C_ADDR = 0x38;

constexpr uint16_t GESTURE_LONG_PRESS_MS = 400;
constexpr uint16_t GESTURE_DOUBLE_TAP_MS = 300;
constexpr uint16_t GESTURE_SWIPE_MIN_PX = 48;
constexpr uint8_t PIN_I2S_MCLK = 16;
constexpr uint8_t PIN_I2S_BCLK = 41;
constexpr uint8_t PIN_I2S_LRCK = 45;
constexpr uint8_t PIN_I2S_DIN = 42;

constexpr uint8_t ES7210_I2C_ADDR = 0x40;

constexpr int8_t TX_POWER_DBM = 0;
