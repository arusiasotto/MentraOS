// Unofficial MentraOS peripheral for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
// Not a Waveshare product; not affiliated with or endorsed by Waveshare.
// GATT, UUIDs, and opcodes: settings.h (must match S3WatchProtocol.kt).

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include "settings.h"

static Arduino_ESP32QSPI *bus =
    new Arduino_ESP32QSPI(PIN_QSPI_CS, PIN_QSPI_SCK, PIN_QSPI_D0, PIN_QSPI_D1, PIN_QSPI_D2, PIN_QSPI_D3);
static Arduino_CO5300 *gfx = new Arduino_CO5300(bus, PIN_PANEL_RST, 0 /* rotation */, DISPLAY_WIDTH, DISPLAY_HEIGHT);
static JPEGDEC jpeg;

static BLECharacteristic *ctrlChar = nullptr;
static BLECharacteristic *evtChar = nullptr;
static BLECharacteristic *imgChar = nullptr;
static BLECharacteristic *micChar = nullptr;

static bool deviceConnected = false;
static uint8_t txSeq = 1;
static uint8_t brightness = 80;
static volatile bool micEnabled = false;

static uint8_t *jpegBuf = nullptr;
static size_t jpegCap = 0;
static size_t jpegLen = 0;
static size_t jpegExpected = 0;

static char deviceName[24];

static uint8_t nextSeq() {
  const uint8_t s = txSeq;
  txSeq = txSeq == 255 ? 1 : (uint8_t)(txSeq + 1);
  return s;
}

static void sendEvent(uint8_t opcode, const uint8_t *payload, uint16_t len) {
  if (!deviceConnected || evtChar == nullptr) return;
  uint8_t pkt[4 + 16];
  if (len > 16) len = 16;
  pkt[0] = opcode;
  pkt[1] = nextSeq();
  pkt[2] = (uint8_t)(len & 0xFF);
  pkt[3] = (uint8_t)((len >> 8) & 0xFF);
  if (payload && len) memcpy(pkt + 4, payload, len);
  evtChar->setValue(pkt, 4 + len);
  evtChar->notify();
}

static void sendAck(uint8_t cmd, uint8_t seq, uint8_t status) {
  const uint8_t payload[3] = {cmd, seq, status};
  sendEvent(EVT_ACK, payload, 3);
}

static void drawTextWall(const char *text) {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(12, 24);
  const size_t n = strlen(text);
  int x = 12;
  int y = 24;
  for (size_t i = 0; i < n; i++) {
    if (text[i] == '\n' || x > DISPLAY_WIDTH - 24) {
      x = 12;
      y += 28;
      gfx->setCursor(x, y);
      if (text[i] == '\n') continue;
    }
    gfx->write(text[i]);
    x += 12;
  }
}

static int jpegDraw(JPEGDRAW *pDraw) {
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  return 1;
}

static void blitJpeg() {
  if (jpegBuf == nullptr || jpegLen < 4) return;
  if (jpeg.openRAM(jpegBuf, (int)jpegLen, jpegDraw) == 1) {
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close();
  } else {
    Serial.println("jpeg open failed");
  }
}

static void applyBrightness(uint8_t level) {
  brightness = level;
  if (brightness > 100) brightness = 100;
  gfx->setBrightness((uint8_t)((brightness * 255) / 100));
}

static void handleControl(const uint8_t *data, size_t len) {
  if (len < HDR_LEN) return;
  const uint8_t opcode = data[0];
  const uint8_t seq = data[1];
  const uint16_t payloadLen = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
  const uint8_t *payload = data + HDR_LEN;
  const size_t available = len > HDR_LEN ? len - HDR_LEN : 0;
  const uint16_t n = payloadLen < available ? payloadLen : (uint16_t)available;

  switch (opcode) {
    case CMD_TEXT: {
      char text[513];
      const uint16_t copy = n < 512 ? n : 512;
      memcpy(text, payload, copy);
      text[copy] = 0;
      drawTextWall(text);
      sendAck(opcode, seq, 0);
      break;
    }
    case CMD_CLEAR:
      gfx->fillScreen(BLACK);
      sendAck(opcode, seq, 0);
      break;
    case CMD_BRIGHTNESS:
      if (n >= 1) applyBrightness(payload[0]);
      sendAck(opcode, seq, 0);
      break;
    case CMD_MIC_ENABLE:
      micEnabled = n >= 1 && payload[0] != 0;
      sendAck(opcode, seq, 0);
      break;
    case CMD_TIME_SYNC:
      sendAck(opcode, seq, 0);
      break;
    case CMD_IMG_BEGIN: {
      jpegExpected = 0;
      jpegLen = 0;
      if (n >= 4) {
        jpegExpected = (size_t)payload[0] | ((size_t)payload[1] << 8) | ((size_t)payload[2] << 16) |
                       ((size_t)payload[3] << 24);
      }
      if (jpegExpected > 400000) jpegExpected = 0;
      if (jpegExpected > jpegCap) {
        free(jpegBuf);
        jpegBuf = (uint8_t *)heap_caps_malloc(jpegExpected ? jpegExpected : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (jpegBuf == nullptr) jpegBuf = (uint8_t *)malloc(jpegExpected ? jpegExpected : 1);
        jpegCap = jpegBuf ? jpegExpected : 0;
      }
      sendAck(opcode, seq, jpegBuf || jpegExpected == 0 ? 0 : 1);
      break;
    }
    case CMD_IMG_END:
      blitJpeg();
      jpegLen = 0;
      jpegExpected = 0;
      sendAck(opcode, seq, 0);
      break;
    default:
      sendAck(opcode, seq, 2);
      break;
  }
}

class CtrlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    uint8_t *data = characteristic->getData();
    size_t len = characteristic->getLength();
    if (data && len) handleControl(data, len);
  }
};

class ImgCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    uint8_t *data = characteristic->getData();
    size_t len = characteristic->getLength();
    if (data == nullptr || len == 0 || jpegBuf == nullptr) return;
    const size_t room = jpegCap > jpegLen ? jpegCap - jpegLen : 0;
    const size_t n = len < room ? len : room;
    if (n) {
      memcpy(jpegBuf + jpegLen, data, n);
      jpegLen += n;
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
    Serial.println("phone connected");
    sendEvent(EVT_READY, nullptr, 0);
    const uint8_t battery = 100;
    sendEvent(EVT_BATTERY, &battery, 1);
  }
  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    micEnabled = false;
    Serial.println("phone disconnected");
    server->startAdvertising();
  }
};

static void buildDeviceName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  if (DEVICE_ID[0] != '\0') {
    snprintf(deviceName, sizeof(deviceName), "%s-%s", ADV_NAME_PREFIX, DEVICE_ID);
  } else {
    snprintf(deviceName, sizeof(deviceName), "%s-%02X%02X%02X", ADV_NAME_PREFIX, mac[3], mac[4], mac[5]);
  }
}

static void setupBle() {
  buildDeviceName();
  BLEDevice::init(deviceName);
  BLEDevice::setMTU(517);
  BLEDevice::setPower((esp_power_level_t)ESP_PWR_LVL_P3);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(BLEUUID(S3_SERVICE_UUID));

  ctrlChar = service->createCharacteristic(
      BLEUUID(S3_CTRL_UUID), BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  ctrlChar->setCallbacks(new CtrlCallbacks());

  evtChar = service->createCharacteristic(BLEUUID(S3_EVT_UUID), BLECharacteristic::PROPERTY_NOTIFY);
  evtChar->addDescriptor(new BLE2902());

  imgChar = service->createCharacteristic(
      BLEUUID(S3_IMG_UUID), BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  imgChar->setCallbacks(new ImgCallbacks());

  micChar = service->createCharacteristic(BLEUUID(S3_MIC_UUID), BLECharacteristic::PROPERTY_NOTIFY);
  micChar->addDescriptor(new BLE2902());

  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID(S3_SERVICE_UUID));
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();
  Serial.printf("advertising %s\n", deviceName);
}

static void es7210Write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ES7210_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static void setupMic() {
  delay(20);
  es7210Write(0x00, 0xFF);  // reset
  delay(10);
  es7210Write(0x00, 0x41);
  es7210Write(0x01, 0x20);  // 16 kHz-ish clock
  es7210Write(0x02, 0x01);
  es7210Write(0x03, 0x04);
  es7210Write(0x04, 0x01);  // ADC1 on
  es7210Write(0x06, 0x00);
  es7210Write(0x07, 0x20);
  es7210Write(0x11, 0x60);

  i2s_config_t i2s = {};
  i2s.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s.sample_rate = MIC_SAMPLE_RATE;
  i2s.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s.dma_buf_count = 4;
  i2s.dma_buf_len = MIC_FRAME_SAMPLES;
  i2s.use_apll = false;
  i2s.tx_desc_auto_clear = false;
  i2s.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_I2S_MCLK;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_I2S_DIN;

  if (i2s_driver_install(I2S_NUM_0, &i2s, 0, nullptr) != ESP_OK) {
    Serial.println("i2s install failed");
    return;
  }
  i2s_set_pin(I2S_NUM_0, &pins);
}

static void pumpMic() {
  if (!deviceConnected || !micEnabled || micChar == nullptr) return;
  int16_t frame[MIC_FRAME_SAMPLES];
  size_t read = 0;
  const esp_err_t err =
      i2s_read(I2S_NUM_0, frame, sizeof(frame), &read, 0);
  if (err != ESP_OK || read == 0) return;
  micChar->setValue((uint8_t *)frame, read);
  micChar->notify();
}

enum TouchPhase : uint8_t { TP_IDLE, TP_DOWN, TP_WAIT_DOUBLE, TP_HOLD_FIRED };

static TouchPhase touchPhase = TP_IDLE;
static int16_t touchStartX = 0;
static int16_t touchStartY = 0;
static int16_t touchLastX = 0;
static int16_t touchLastY = 0;
static uint32_t touchT0 = 0;
static uint32_t touchReleaseMs = 0;
static bool touchDown = false;
static bool touchSecondTap = false;

static void emitGesture(uint8_t id) {
  sendEvent(EVT_GESTURE, &id, 1);
  Serial.printf("gesture %u\n", id);
}

static bool readFt3168(bool *down, int16_t *x, int16_t *y) {
  Wire.beginTransmission(FT3168_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) {
    *down = false;
    return false;
  }
  const uint8_t n = Wire.requestFrom((int)FT3168_I2C_ADDR, 5);
  if (n < 5) {
    *down = false;
    return false;
  }
  const uint8_t td = Wire.read();
  const uint8_t xh = Wire.read();
  const uint8_t xl = Wire.read();
  const uint8_t yh = Wire.read();
  const uint8_t yl = Wire.read();
  const uint8_t points = td & 0x0F;
  const uint8_t event = (xh >> 6) & 0x03;
  *x = (int16_t)(((xh & 0x0F) << 8) | xl);
  *y = (int16_t)(((yh & 0x0F) << 8) | yl);
  *down = points > 0 && event != 0x01;
  return true;
}

static void setupTouch() {
  pinMode(PIN_TP_RST, OUTPUT);
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(50);
}

static void pumpTouch() {
  int16_t x = 0;
  int16_t y = 0;
  bool down = false;
  if (!readFt3168(&down, &x, &y) && !touchDown && touchPhase != TP_WAIT_DOUBLE) {
    return;
  }
  const uint32_t now = millis();

  if (touchPhase == TP_WAIT_DOUBLE && (now - touchReleaseMs) >= GESTURE_DOUBLE_TAP_MS) {
    emitGesture(GESTURE_SINGLE_TAP);
    touchPhase = TP_IDLE;
    touchSecondTap = false;
  }

  if (down && !touchDown) {
    touchSecondTap = (touchPhase == TP_WAIT_DOUBLE && (now - touchReleaseMs) < GESTURE_DOUBLE_TAP_MS);
    touchPhase = TP_DOWN;
    touchStartX = x;
    touchStartY = y;
    touchLastX = x;
    touchLastY = y;
    touchT0 = now;
    touchDown = true;
    return;
  }

  if (down && touchDown && touchPhase == TP_DOWN) {
    touchLastX = x;
    touchLastY = y;
    if ((now - touchT0) >= GESTURE_LONG_PRESS_MS) {
      emitGesture(GESTURE_LONG_PRESS);
      touchPhase = TP_HOLD_FIRED;
      touchSecondTap = false;
    }
    return;
  }

  if (!down && touchDown) {
    touchDown = false;
    const int16_t dx = (int16_t)(touchLastX - touchStartX);
    const int16_t dy = (int16_t)(touchLastY - touchStartY);
    const int16_t adx = dx < 0 ? (int16_t)-dx : dx;
    const int16_t ady = dy < 0 ? (int16_t)-dy : dy;

    if (touchPhase == TP_HOLD_FIRED) {
      touchPhase = TP_IDLE;
      touchSecondTap = false;
      return;
    }

    if (ady >= GESTURE_SWIPE_MIN_PX && ady > adx) {
      emitGesture(dy < 0 ? GESTURE_SWIPE_UP : GESTURE_SWIPE_DOWN);
      touchPhase = TP_IDLE;
      touchSecondTap = false;
      return;
    }

    if (touchSecondTap) {
      emitGesture(GESTURE_DOUBLE_TAP);
      touchPhase = TP_IDLE;
      touchSecondTap = false;
      return;
    }

    touchPhase = TP_WAIT_DOUBLE;
    touchReleaseMs = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  if (!gfx->begin()) {
    Serial.println("display begin failed");
  }
  gfx->fillScreen(BLACK);
  applyBrightness(80);
  drawTextWall("MentraOS\nS3 Watch\nWaiting...");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  setupTouch();
  setupMic();
  setupBle();
}

void loop() {
  pumpTouch();
  pumpMic();
  delay(20);
}
