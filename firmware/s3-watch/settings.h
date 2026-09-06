#pragma once

// =============================================================================
// SETTINGS — identity and the BLE protocol spoken by firmware and S3Watch.kt
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
// Payload: unix seconds u32le, timezone offset minutes i16le (includes DST).
constexpr uint8_t CMD_TIME_SYNC = 0x05;
// G2-style glasses menu: [count]{running u8, namelen u8, name utf8}*
constexpr uint8_t CMD_MENU = 0x06;
constexpr uint8_t CMD_IMG_BEGIN = 0x10;
constexpr uint8_t CMD_IMG_END = 0x12;

constexpr uint8_t EVT_ACK = 0x80;
constexpr uint8_t EVT_BATTERY = 0x81;
constexpr uint8_t EVT_READY = 0x82;
constexpr uint8_t EVT_GESTURE = 0x83;
// Payload: selected menu index u8. Phone maps that to miniapp_selected.
constexpr uint8_t EVT_MENU_SELECT = 0x84;

constexpr uint8_t MENU_MAX_ITEMS = 10;
constexpr uint8_t MENU_NAME_MAX = 15;

// Gesture payload byte — Mentra miniapp names mapped in S3WatchProtocol.kt.
constexpr uint8_t GESTURE_SWIPE_UP = 0x01;
constexpr uint8_t GESTURE_SWIPE_DOWN = 0x02;
constexpr uint8_t GESTURE_SINGLE_TAP = 0x03;
constexpr uint8_t GESTURE_DOUBLE_TAP = 0x04;
constexpr uint8_t GESTURE_LONG_PRESS = 0x05;

constexpr uint16_t DISPLAY_WIDTH = 410;
constexpr uint16_t DISPLAY_HEIGHT = 502;
// CO5300 GRAM is wider than the glass. Visible 410 cols start at column 22
// (Waveshare 01_HelloWorld). Without this, text shifts left and a green
// leftover strip appears on the right.
constexpr uint8_t DISPLAY_COL_OFFSET = 22;
constexpr uint8_t DISPLAY_ROW_OFFSET = 0;
// Keep text below the AMOLED corner radius so the first line is not clipped.
constexpr uint16_t DISPLAY_TEXT_ORIGIN_X = 16;
constexpr uint16_t DISPLAY_TEXT_ORIGIN_Y = 64;
constexpr uint16_t DISPLAY_TEXT_LINE_H = 28;
constexpr uint16_t DISPLAY_TEXT_CHAR_W = 12;
// Mentra HUD green (#00FF88) in RGB565 — G1/G2 waveguide tint.
constexpr uint16_t DISPLAY_HUD_GREEN = 0x07F1;
// BOOT-hold cycles these. Firmware remaps JPEG luma onto the current hue.
constexpr uint8_t HUD_PALETTE_COUNT = 5;
constexpr uint16_t HUD_PALETTE[HUD_PALETTE_COUNT] = {
    0x07F1,  // green  #00FF88
    0xFD20,  // amber  #FFA000
    0xF924,  // red    #FF3333
    0xFFFF,  // white
    0x07FF,  // cyan   #00FFFF
};
constexpr uint16_t BOOT_COLOR_HOLD_MS = 700;

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
constexpr uint8_t PIN_I2S_DOUT = 40;  // ES8311; duplex so the S3 actually emits MCLK
constexpr uint8_t PIN_I2S_DIN = 42;

constexpr uint8_t ES7210_I2C_ADDR = 0x40;
constexpr uint8_t ES8311_I2C_ADDR = 0x18;
constexpr uint8_t AXP2101_I2C_ADDR = 0x34;
constexpr uint8_t AXP2101_STATUS1 = 0x00;
constexpr uint8_t AXP2101_CHARGE_GAUGE_WDT = 0x18;
constexpr uint8_t AXP2101_BAT_DET_CTRL = 0x68;
constexpr uint8_t AXP2101_BAT_PERCENT = 0xA4;
constexpr uint32_t BATTERY_POLL_MS = 5000;
constexpr uint32_t BATTERY_POLL_SLEEP_MS = 30000;
constexpr uint8_t PIN_PA_CTRL = 46;
// SYS_OUT: high while PWR is held. Hardware still shuts down at ~6s.
// Short tap opens/closes the app menu. Hold this long to sleep the panel.
constexpr uint8_t PIN_PWR_SENSE = 10;
constexpr uint16_t PWR_SLEEP_HOLD_MS = 1000;
// Top side button (BOOT). Active low. Hold ~0.7s cycles HUD color.
// Hold at power-on still enters download.
constexpr uint8_t PIN_BOOT = 0;
// Blank the AMOLED after this much idle. In-flight notification JPEGs wake it.
// Captions / mic do not keep it awake.
constexpr uint32_t DISPLAY_SLEEP_IDLE_MS = 30000;
// When the panel is asleep, yield this long so tickless idle can light-sleep.
// BLE stays connected; a GATT write (notification JPEG) wakes the radio.
constexpr uint32_t LIGHT_SLEEP_YIELD_MS = 50;

constexpr int8_t TX_POWER_DBM = 0;
