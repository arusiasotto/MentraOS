// Unofficial MentraOS controller for Seeed Studio XIAO nRF52840 Plus.
// Not a Seeed product; not affiliated with or endorsed by Seeed Studio.
// GATT, UUIDs, and opcodes: settings.h (must match KeyfobProtocol.kt).

#include <Arduino.h>
#include <bluefruit.h>
#include "settings.h"

static BLEService kfService(BLEUuid(KF_SERVICE_UUID));
static BLECharacteristic kfCtrl(BLEUuid(KF_CTRL_UUID));
static BLECharacteristic kfEvt(BLEUuid(KF_EVT_UUID));

static bool deviceConnected = false;
static uint8_t txSeq = 1;
static char deviceName[24];
static uint32_t lastBatteryMs = 0;
static uint8_t ledR = 0;
static uint8_t ledG = 0;
static uint8_t ledB = 0;
static bool ledFromPhone = false;

struct ButtonState {
  uint8_t pin;
  uint8_t id;
  bool lastLevel;
  bool pressed;
  uint32_t lastChangeMs;
  uint32_t pressStartMs;
  uint32_t lastReleaseMs;
  uint8_t tapCount;
  bool longSent;
};

static ButtonState buttons[BUTTON_COUNT];

static uint8_t nextSeq() {
  const uint8_t s = txSeq;
  txSeq = txSeq == 255 ? 1 : (uint8_t)(txSeq + 1);
  return s;
}

static void setRgb(uint8_t r, uint8_t g, uint8_t b) {
  digitalWrite(LED_RED, r ? LOW : HIGH);
  digitalWrite(LED_GREEN, g ? LOW : HIGH);
  digitalWrite(LED_BLUE, b ? LOW : HIGH);
}

static void sendEvent(uint8_t opcode, const uint8_t *payload, uint16_t len) {
  if (!deviceConnected) return;
  uint8_t pkt[4 + 16];
  if (len > 16) len = 16;
  pkt[0] = opcode;
  pkt[1] = nextSeq();
  pkt[2] = (uint8_t)(len & 0xFF);
  pkt[3] = (uint8_t)((len >> 8) & 0xFF);
  if (payload && len) memcpy(pkt + 4, payload, len);
  kfEvt.notify(pkt, 4 + len);
}

static void sendAck(uint8_t cmd, uint8_t seq, uint8_t status) {
  const uint8_t payload[3] = {cmd, seq, status};
  sendEvent(EVT_ACK, payload, 3);
}

static uint8_t readBatteryPercent() {
  pinMode(PIN_VBAT_ENABLE, OUTPUT);
  digitalWrite(PIN_VBAT_ENABLE, LOW);
  delay(5);
  const int adc = analogRead(PIN_VBAT);
  digitalWrite(PIN_VBAT_ENABLE, HIGH);
  if (adc < 20) return 100;
  const float volts = (adc * 3.6f / 1023.0f) * 2.0f;
  if (volts >= 4.15f) return 100;
  if (volts <= 3.30f) return 0;
  return (uint8_t)((volts - 3.30f) * 100.0f / 0.85f);
}

static void sendBattery() {
  const uint8_t pct = readBatteryPercent();
  sendEvent(EVT_BATTERY, &pct, 1);
}

static void sendGesture(uint8_t gesture, uint8_t buttonId) {
  const uint8_t payload[2] = {gesture, buttonId};
  sendEvent(EVT_GESTURE, payload, 2);
}

static uint8_t gestureFor(uint8_t buttonId) {
  if (buttonId == BUTTON_UP) return GESTURE_SWIPE_UP;
  if (buttonId == BUTTON_DOWN) return GESTURE_SWIPE_DOWN;
  return GESTURE_SINGLE_TAP;
}

static void pollButtons() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    ButtonState &b = buttons[i];
    const bool level = digitalRead(b.pin) == LOW;
    if (level != b.lastLevel) {
      b.lastChangeMs = now;
      b.lastLevel = level;
    }
    if ((now - b.lastChangeMs) < DEBOUNCE_MS) continue;
    if (level && !b.pressed) {
      b.pressed = true;
      b.pressStartMs = now;
      b.longSent = false;
    } else if (!level && b.pressed) {
      b.pressed = false;
      if (!b.longSent) {
        if (b.id == BUTTON_PRIMARY) {
          b.tapCount = (uint8_t)(b.tapCount + 1);
          b.lastReleaseMs = now;
          if (b.tapCount >= 2) {
            sendGesture(GESTURE_DOUBLE_TAP, b.id);
            b.tapCount = 0;
          }
        } else {
          sendGesture(gestureFor(b.id), b.id);
        }
      }
    }
    if (b.pressed && !b.longSent && (now - b.pressStartMs) >= LONG_PRESS_MS) {
      b.longSent = true;
      b.tapCount = 0;
      sendGesture(b.id == BUTTON_PRIMARY ? GESTURE_HOLD : gestureFor(b.id), b.id);
    }
    if (!b.pressed && b.tapCount == 1 && (now - b.lastReleaseMs) >= DOUBLE_TAP_MS) {
      sendGesture(GESTURE_SINGLE_TAP, b.id);
      b.tapCount = 0;
    }
  }
}

static void updateStatusLed() {
  if (ledFromPhone) {
    setRgb(ledR, ledG, ledB);
    return;
  }
  if (deviceConnected) {
    setRgb(0, 40, 0);
    return;
  }
  const bool on = ((millis() / 400) % 2) == 0;
  setRgb(0, 0, on ? 40 : 0);
}

static void handleControl(const uint8_t *data, uint16_t len) {
  if (len < HDR_LEN) return;
  const uint8_t opcode = data[0];
  const uint8_t seq = data[1];
  const uint16_t plen = (uint16_t)(data[2] | (data[3] << 8));
  const uint8_t *payload = data + HDR_LEN;
  if (HDR_LEN + plen > len) return;
  if (opcode == CMD_LED) {
    ledFromPhone = true;
    ledR = plen > 0 ? payload[0] : 0;
    ledG = plen > 1 ? payload[1] : 0;
    ledB = plen > 2 ? payload[2] : 0;
    if (ledR == 0 && ledG == 0 && ledB == 0) ledFromPhone = false;
    sendAck(opcode, seq, 0);
    return;
  }
  if (opcode == CMD_PING) {
    sendBattery();
    sendAck(opcode, seq, 0);
  }
}

static void ctrlWriteCallback(uint16_t, BLECharacteristic *, uint8_t *data, uint16_t len) {
  handleControl(data, len);
}

static void connectCallback(uint16_t) {
  deviceConnected = true;
  ledFromPhone = false;
  sendEvent(EVT_READY, nullptr, 0);
  sendBattery();
  lastBatteryMs = millis();
}

static void disconnectCallback(uint16_t, uint8_t) {
  deviceConnected = false;
  ledFromPhone = false;
}

static void buildDeviceName() {
  if (DEVICE_ID[0] != '\0') {
    snprintf(deviceName, sizeof(deviceName), "%s-%s", ADV_NAME_PREFIX, DEVICE_ID);
    return;
  }
  uint8_t mac[6] = {0};
  Bluefruit.getAddr(mac);
  snprintf(
      deviceName,
      sizeof(deviceName),
      "%s-%02X%02X%02X",
      ADV_NAME_PREFIX,
      mac[2],
      mac[1],
      mac[0]);
}

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  setRgb(0, 0, 0);

  pinMode(PIN_VBAT_ENABLE, OUTPUT);
  digitalWrite(PIN_VBAT_ENABLE, HIGH);
  analogReadResolution(10);

  const uint8_t pins[BUTTON_COUNT] = {PIN_BTN_PRIMARY, PIN_BTN_UP, PIN_BTN_DOWN};
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    buttons[i].pin = pins[i];
    buttons[i].id = i;
    buttons[i].lastLevel = false;
    buttons[i].pressed = false;
  }

  Bluefruit.begin();
  Bluefruit.setTxPower(TX_POWER_DBM);
  buildDeviceName();
  Bluefruit.setName(deviceName);
  Bluefruit.Periph.setConnectCallback(connectCallback);
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback);

  kfService.begin();

  kfCtrl.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  kfCtrl.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  kfCtrl.setMaxLen(20);
  kfCtrl.setWriteCallback(ctrlWriteCallback);
  kfCtrl.begin();

  kfEvt.setProperties(CHR_PROPS_NOTIFY);
  kfEvt.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  kfEvt.setMaxLen(20);
  kfEvt.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(kfService);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void loop() {
  pollButtons();
  updateStatusLed();
  if (deviceConnected && (millis() - lastBatteryMs) >= BATTERY_PERIOD_MS) {
    lastBatteryMs = millis();
    sendBattery();
  }
  waitForEvent();
  delay(5);
}
