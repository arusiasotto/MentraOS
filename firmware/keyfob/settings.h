#pragma once

// =============================================================================
// SETTINGS — identity and the BLE protocol spoken by firmware and Keyfob.kt
// Unofficial MentraOS firmware for Seeed Studio XIAO nRF52840 Plus.
// Not a Seeed product; not affiliated with or endorsed by Seeed Studio.
// =============================================================================
// Phone driver:
//   mobile/modules/bluetooth-sdk/android/.../controllers/keyfob/KeyfobProtocol.kt
// Keep UUIDs, opcodes, and framing in lockstep with that file.
// =============================================================================

#define ADV_NAME_PREFIX "Keyfob"
#define DEVICE_ID ""

#define DIS_MANUFACTURER "Seeed Studio (unofficial)"
#define DIS_MODEL "XIAO Keyfob"

// Custom GATT (not R1 / not Nordic UART).
#define KF_SERVICE_UUID "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e020"
#define KF_CTRL_UUID "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e021"
#define KF_EVT_UUID "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e022"

// Control / event frame: [opcode:u8][seq:u8][len:u16le][payload]
constexpr uint8_t HDR_LEN = 4;

constexpr uint8_t CMD_LED = 0x01;
constexpr uint8_t CMD_PING = 0x02;

constexpr uint8_t EVT_ACK = 0x80;
constexpr uint8_t EVT_BATTERY = 0x81;
constexpr uint8_t EVT_READY = 0x82;
constexpr uint8_t EVT_GESTURE = 0x83;

// Gesture payload byte 0 — Mentra R1-compatible names mapped in KeyfobProtocol.kt.
constexpr uint8_t GESTURE_HOLD = 0x01;
constexpr uint8_t GESTURE_SINGLE_TAP = 0x02;
constexpr uint8_t GESTURE_DOUBLE_TAP = 0x03;
constexpr uint8_t GESTURE_SWIPE_UP = 0x04;
constexpr uint8_t GESTURE_SWIPE_DOWN = 0x05;

// Seeed XIAO nRF52840 Plus — same D0–D2 as the non-Plus board.
// Active-low tactile buttons with internal pull-ups (to GND).
constexpr uint8_t PIN_BTN_PRIMARY = 0;    // D0 / P0.02
constexpr uint8_t PIN_BTN_UP = 1;         // D1 / P0.03
constexpr uint8_t PIN_BTN_DOWN = 2;       // D2 / P0.28

constexpr uint8_t BUTTON_PRIMARY = 0;
constexpr uint8_t BUTTON_UP = 1;
constexpr uint8_t BUTTON_DOWN = 2;
constexpr uint8_t BUTTON_COUNT = 3;

// User RGB is active-low. Charge LED is separate (P0.17).
constexpr uint8_t PIN_LED_RED = 11;    // P0.26
constexpr uint8_t PIN_LED_GREEN = 13;  // P0.30
constexpr uint8_t PIN_LED_BLUE = 12;   // P0.06

// Backwards-compatible aliases expected by firmware source
#define LED_RED PIN_LED_RED
#define LED_GREEN PIN_LED_GREEN
#define LED_BLUE PIN_LED_BLUE

// Battery: P0.14 LOW enables the read path, then ADC P0.31 (Arduino 32).
constexpr uint8_t PIN_VBAT_ENABLE = 14;
// The board variant may already define `PIN_VBAT` as a macro (Seeed variant).
// Avoid redefining the same identifier as a C++ variable which breaks when
// the macro is expanded. Only define the constexpr if the macro is missing.
#ifndef PIN_VBAT
constexpr uint8_t PIN_VBAT = 32;
#endif

constexpr uint16_t DEBOUNCE_MS = 30;
constexpr uint16_t LONG_PRESS_MS = 400;
constexpr uint16_t DOUBLE_TAP_MS = 300;
constexpr uint16_t BATTERY_PERIOD_MS = 30000;

constexpr int8_t TX_POWER_DBM = 4;
