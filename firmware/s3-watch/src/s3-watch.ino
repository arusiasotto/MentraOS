// Unofficial MentraOS peripheral for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
// Not a Waveshare product; not affiliated with or endorsed by Waveshare.
// GATT, UUIDs, and opcodes: settings.h (must match S3WatchProtocol.kt).

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include "settings.h"

#ifndef RGB565_BLACK
#define RGB565_BLACK 0x0000
#endif
#ifndef RGB565_WHITE
#define RGB565_WHITE 0xFFFF
#endif

static Arduino_ESP32QSPI *bus =
    new Arduino_ESP32QSPI(PIN_QSPI_CS, PIN_QSPI_SCK, PIN_QSPI_D0, PIN_QSPI_D1, PIN_QSPI_D2, PIN_QSPI_D3);
static Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, PIN_PANEL_RST, 0 /* rotation */, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_COL_OFFSET,
    DISPLAY_ROW_OFFSET, 0, 0);
static JPEGDEC jpeg;
static I2SClass i2sBus;

static BLECharacteristic *ctrlChar = nullptr;
static BLECharacteristic *evtChar = nullptr;
static BLECharacteristic *imgChar = nullptr;
static BLECharacteristic *micChar = nullptr;

static bool deviceConnected = false;
static uint8_t txSeq = 1;
static uint8_t brightness = 80;
static volatile bool micEnabled = false;
static volatile bool micFlushPending = false;
static uint32_t micFramesSent = 0;
static volatile uint32_t micDrops = 0;
static uint32_t micLastLogMs = 0;
static QueueHandle_t micQueue = nullptr;

constexpr size_t MIC_NOTIFY_SAMPLES = 160;
struct MicFrame {
  int16_t samples[MIC_NOTIFY_SAMPLES];
};

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

static bool sendEvent(uint8_t opcode, const uint8_t *payload, uint16_t len) {
  if (!deviceConnected || evtChar == nullptr) return false;
  uint8_t pkt[4 + 16];
  if (len > 16) len = 16;
  pkt[0] = opcode;
  pkt[1] = nextSeq();
  pkt[2] = (uint8_t)(len & 0xFF);
  pkt[3] = (uint8_t)((len >> 8) & 0xFF);
  if (payload && len) memcpy(pkt + 4, payload, len);
  evtChar->setValue(pkt, 4 + len);
  evtChar->notify();
  return true;
}

static void sendAck(uint8_t cmd, uint8_t seq, uint8_t status) {
  const uint8_t payload[3] = {cmd, seq, status};
  sendEvent(EVT_ACK, payload, 3);
}

static void sanitizeAscii(const char *in, char *out, size_t outCap) {
  size_t o = 0;
  for (size_t i = 0; in[i] != 0 && o + 1 < outCap; ) {
    const unsigned char c = (unsigned char)in[i];
    if (c < 0x80) {
      if (c == '\r') {
        i++;
        continue;
      }
      if (c == '\t') {
        out[o++] = ' ';
        i++;
        continue;
      }
      if (c < 32 && c != '\n') {
        i++;
        continue;
      }
      out[o++] = (char)c;
      i++;
      continue;
    }
    // Drop UTF-8 sequences (curly quotes etc). Drawing them byte-wise
    // produced leftover glyphs like "rCI?".
    if ((c & 0xE0) == 0xC0) i += 2;
    else if ((c & 0xF0) == 0xE0) i += 3;
    else if ((c & 0xF8) == 0xF0) i += 4;
    else i++;
  }
  out[o] = 0;
}

static void newline(int *x, int *y) {
  *x = DISPLAY_TEXT_ORIGIN_X;
  *y += DISPLAY_TEXT_LINE_H;
  if (*y > (int)DISPLAY_HEIGHT - (int)DISPLAY_TEXT_ORIGIN_Y) {
    *y = DISPLAY_HEIGHT;
  }
  gfx->setCursor(*x, *y);
}

static void drawWord(const char *word, size_t n, int *x, int *y) {
  if (n == 0 || *y >= (int)DISPLAY_HEIGHT) return;
  const int maxX = (int)DISPLAY_WIDTH - (int)DISPLAY_TEXT_ORIGIN_X;
  const int wordW = (int)n * (int)DISPLAY_TEXT_CHAR_W;
  if (*x > DISPLAY_TEXT_ORIGIN_X && *x + wordW > maxX) {
    newline(x, y);
    if (*y >= (int)DISPLAY_HEIGHT) return;
  }
  for (size_t i = 0; i < n && *y < (int)DISPLAY_HEIGHT; i++) {
    if (*x + (int)DISPLAY_TEXT_CHAR_W > maxX) {
      newline(x, y);
      if (*y >= (int)DISPLAY_HEIGHT) return;
    }
    gfx->write(word[i]);
    *x += DISPLAY_TEXT_CHAR_W;
  }
}

static void drawTextWall(const char *text) {
  char buf[513];
  sanitizeAscii(text, buf, sizeof(buf));
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  int x = DISPLAY_TEXT_ORIGIN_X;
  int y = DISPLAY_TEXT_ORIGIN_Y;
  gfx->setCursor(x, y);

  const char *p = buf;
  while (*p && y < (int)DISPLAY_HEIGHT) {
    if (*p == '\n') {
      newline(&x, &y);
      p++;
      continue;
    }
    if (*p == ' ') {
      if (x > DISPLAY_TEXT_ORIGIN_X && x + (int)DISPLAY_TEXT_CHAR_W <= (int)DISPLAY_WIDTH - (int)DISPLAY_TEXT_ORIGIN_X) {
        gfx->write(' ');
        x += DISPLAY_TEXT_CHAR_W;
      } else if (x > DISPLAY_TEXT_ORIGIN_X) {
        newline(&x, &y);
      }
      p++;
      continue;
    }
    const char *start = p;
    while (*p && *p != ' ' && *p != '\n') p++;
    drawWord(start, (size_t)(p - start), &x, &y);
  }
}

static void drawIdle(const char *status) {
  char line[80];
  snprintf(line, sizeof(line), "MentraOS\nS3 Watch\n%s", status ? status : "Waiting...");
  drawTextWall(line);
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
  Serial.printf("ctrl op=0x%02X n=%u\n", opcode, (unsigned)n);

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
      // App start/stop sends clear. Do not paint the idle home screen here —
      // that made Captions look like it bounced back to boot.
      if (micEnabled) {
        drawTextWall("Listening...");
      } else {
        gfx->fillScreen(RGB565_BLACK);
      }
      sendAck(opcode, seq, 0);
      break;
    case CMD_BRIGHTNESS:
      if (n >= 1) applyBrightness(payload[0]);
      sendAck(opcode, seq, 0);
      break;
    case CMD_MIC_ENABLE:
      micEnabled = n >= 1 && payload[0] != 0;
      Serial.printf("mic %s\n", micEnabled ? "on" : "off");
      if (micEnabled) {
        micFlushPending = true;
        drawTextWall("Listening...");
      }
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

  // NimBLE (Arduino-ESP32 3.x) already creates CCCD 0x2902 for NOTIFY.
  // Adding a second BLE2902 makes the phone write the unused one, so
  // notify() thinks nobody is subscribed.
  evtChar = service->createCharacteristic(BLEUUID(S3_EVT_UUID), BLECharacteristic::PROPERTY_NOTIFY);

  imgChar = service->createCharacteristic(
      BLEUUID(S3_IMG_UUID), BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  imgChar->setCallbacks(new ImgCallbacks());

  micChar = service->createCharacteristic(BLEUUID(S3_MIC_UUID), BLECharacteristic::PROPERTY_NOTIFY);

  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID(S3_SERVICE_UUID));
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();
  Serial.printf("advertising %s\n", deviceName);
}

static uint8_t i2cReadReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return 0xFF;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0xFF;
  return (uint8_t)Wire.read();
}

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  for (int i = 0; i < 5; i++) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    if (Wire.endTransmission(true) == 0) return true;
    delay(5);
  }
  Serial.printf("i2c write fail 0x%02X reg 0x%02X\n", addr, reg);
  return false;
}

static void es7210Write(uint8_t reg, uint8_t value) {
  i2cWriteReg(ES7210_I2C_ADDR, reg, value);
}

static void es7210AnalogOn() {
  // 0x4B is active-high power-down on this silicon (0x00 = ADCs on).
  es7210Write(0x40, 0x43);
  es7210Write(0x41, 0x70);
  es7210Write(0x42, 0x70);
  delay(50);
  es7210Write(0x43, 0x1E);
  es7210Write(0x44, 0x1E);
  es7210Write(0x45, 0x00);
  es7210Write(0x46, 0x00);
  es7210Write(0x47, 0x00);
  es7210Write(0x48, 0x00);
  es7210Write(0x49, 0x00);
  es7210Write(0x4A, 0x00);
  es7210Write(0x4B, 0x00);
  es7210Write(0x4C, 0x00);
}

// Waveshare #20: analog rails live on AXP2101 ALDO1–4.
static void setupPmic() {
  const uint8_t id = i2cReadReg(AXP2101_I2C_ADDR, 0x03);
  if (id == 0xFF) {
    Serial.println("axp2101 not found");
    return;
  }
  const uint8_t on = i2cReadReg(AXP2101_I2C_ADDR, 0x90);
  i2cWriteReg(AXP2101_I2C_ADDR, 0x90, (uint8_t)(on | 0x0F));
  Serial.printf("axp2101 id=0x%02X ldo0=0x%02X\n", id, i2cReadReg(AXP2101_I2C_ADDR, 0x90));
}

// ES8311 shares MCLK/BCLK/LRCK with the ES7210. Bring it up as an I2S
// slave first so the mic ADC can lock. Keep the speaker amp off.
static void es8311Init() {
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, LOW);
  i2cWriteReg(ES8311_I2C_ADDR, 0x32, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x17, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0E, 0xFF);
  i2cWriteReg(ES8311_I2C_ADDR, 0x12, 0x02);
  i2cWriteReg(ES8311_I2C_ADDR, 0x14, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0D, 0xFA);
  i2cWriteReg(ES8311_I2C_ADDR, 0x15, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x02, 0x10);
  i2cWriteReg(ES8311_I2C_ADDR, 0x00, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x00, 0x1F);
  i2cWriteReg(ES8311_I2C_ADDR, 0x01, 0x30);
  i2cWriteReg(ES8311_I2C_ADDR, 0x01, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x45, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0D, 0xFC);
  i2cWriteReg(ES8311_I2C_ADDR, 0x02, 0x00);
  delay(20);
  i2cWriteReg(ES8311_I2C_ADDR, 0x00, 0x80);
  i2cWriteReg(ES8311_I2C_ADDR, 0x01, 0x3F);
  i2cWriteReg(ES8311_I2C_ADDR, 0x09, 0x0C);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0A, 0x4C);
  i2cWriteReg(ES8311_I2C_ADDR, 0x17, 0xBF);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0E, 0x02);
  i2cWriteReg(ES8311_I2C_ADDR, 0x12, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x14, 0x1A);
  i2cWriteReg(ES8311_I2C_ADDR, 0x0D, 0x01);
  i2cWriteReg(ES8311_I2C_ADDR, 0x15, 0x40);
  i2cWriteReg(ES8311_I2C_ADDR, 0x37, 0x08);
  i2cWriteReg(ES8311_I2C_ADDR, 0x31, 0x20);
  i2cWriteReg(ES8311_I2C_ADDR, 0x32, 0x00);
  Serial.printf("es8311 id=0x%02X\n", i2cReadReg(ES8311_I2C_ADDR, 0xFD));
}

static void probeI2c() {
  const uint8_t addrs[] = {0x18, 0x34, 0x38, 0x40, 0x51, 0x6B};
  Serial.print("i2c:");
  for (uint8_t addr : addrs) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
  }
  Serial.println();
}

static void recoverI2c() {
  Wire.end();
  delay(20);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  delay(20);
}

// 16-bit Philips I2S stereo. Shared MCLK with ES8311; keep the DLL on
// (0x06=0x00) and finish clocks with 0x01=0x14 or the ADC aliases.
static void es7210Init() {
  es7210Write(0x00, 0xFF);
  delay(20);
  es7210Write(0x00, 0x32);
  delay(10);
  es7210Write(0x01, 0x20);
  es7210Write(0x02, 0xC1);
  es7210Write(0x06, 0x00);
  es7210Write(0x07, 0x20);
  es7210Write(0x11, 0x60);
  es7210Write(0x09, 0x30);
  es7210Write(0x0A, 0x30);
  es7210AnalogOn();
  es7210Write(0x01, 0x14);
  delay(50);
  es7210Write(0x00, 0x71);
  delay(30);
  es7210Write(0x00, 0x41);
  delay(50);
  Serial.printf(
      "es7210 id=0x%02X%02X state=0x%02X clk=0x%02X analog 40=%02X 4B=%02X\n",
      i2cReadReg(ES7210_I2C_ADDR, 0x3D),
      i2cReadReg(ES7210_I2C_ADDR, 0x3E),
      i2cReadReg(ES7210_I2C_ADDR, 0x00),
      i2cReadReg(ES7210_I2C_ADDR, 0x16),
      i2cReadReg(ES7210_I2C_ADDR, 0x40),
      i2cReadReg(ES7210_I2C_ADDR, 0x4B));
}

static void logStereoLevels(const int16_t *stereo, size_t frames, const char *tag) {
  int32_t left = 0;
  int32_t right = 0;
  int16_t lMin = 32767;
  int16_t lMax = -32768;
  int16_t rMin = 32767;
  int16_t rMax = -32768;
  for (size_t i = 0; i < frames; i++) {
    const int16_t l = stereo[i * 2];
    const int16_t r = stereo[i * 2 + 1];
    left += l < 0 ? -l : l;
    right += r < 0 ? -r : r;
    if (l < lMin) lMin = l;
    if (l > lMax) lMax = l;
    if (r < rMin) rMin = r;
    if (r > rMax) rMax = r;
  }
  if (frames == 0) frames = 1;
  Serial.printf("%s n=%u L=%d [%d..%d] R=%d [%d..%d]\n", tag, (unsigned)frames,
                (int)(left / (int32_t)frames), (int)lMin, (int)lMax,
                (int)(right / (int32_t)frames), (int)rMin, (int)rMax);
}

static bool setupMic() {
  setupPmic();
  probeI2c();
  i2cWriteReg(ES8311_I2C_ADDR, 0x00, 0x1F);
  delay(10);

  i2sBus.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  i2sBus.setTimeout(20);
  if (!i2sBus.begin(I2S_MODE_STD, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                    I2S_STD_SLOT_BOTH)) {
    Serial.printf("i2s begin failed err=%d\n", i2sBus.lastError());
    return false;
  }
  delay(100);
  Serial.println("i2s mclk started");
  recoverI2c();
  es8311Init();
  delay(50);
  Serial.println("es7210 init");
  es7210Init();
  delay(50);

  static int16_t probe[MIC_FRAME_SAMPLES * 2];
  const size_t got = i2sBus.readBytes((char *)probe, sizeof(probe));
  logStereoLevels(probe, got / 4, "i2s probe");
  return true;
}

static void micCaptureTask(void *) {
  static int16_t stereo[MIC_NOTIFY_SAMPLES * 2];
  MicFrame frame;
  uint32_t lastLog = 0;
  for (;;) {
    if (!micEnabled && !micFlushPending) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (micFlushPending) {
      micFlushPending = false;
      i2sBus.setTimeout(0);
      while (i2sBus.readBytes((char *)stereo, sizeof(stereo)) > 0) {
      }
      i2sBus.setTimeout(20);
      if (micQueue) xQueueReset(micQueue);
      lastLog = millis();
      continue;
    }
    const size_t read = i2sBus.readBytes((char *)stereo, sizeof(stereo));
    if (read < 4) {
      continue;
    }
    const size_t frames = read / 4;
    const size_t n = frames < MIC_NOTIFY_SAMPLES ? frames : MIC_NOTIFY_SAMPLES;
    for (size_t i = 0; i < n; i++) {
      int32_t mixed = (int32_t)stereo[i * 2] + (int32_t)stereo[i * 2 + 1];
      mixed *= 2;
      if (mixed > 32767) mixed = 32767;
      if (mixed < -32768) mixed = -32768;
      frame.samples[i] = (int16_t)mixed;
    }
    if (n < MIC_NOTIFY_SAMPLES) {
      memset(frame.samples + n, 0, (MIC_NOTIFY_SAMPLES - n) * sizeof(int16_t));
    }
    if (micQueue != nullptr) {
      if (xQueueSend(micQueue, &frame, 0) != pdTRUE) {
        MicFrame dumped;
        xQueueReceive(micQueue, &dumped, 0);
        xQueueSend(micQueue, &frame, 0);
        micDrops = micDrops + 1;
      }
    }
    const uint32_t now = millis();
    if (now - lastLog >= 2000) {
      logStereoLevels(stereo, frames, "mic");
      Serial.printf("mic qdrops=%u\n", (unsigned)micDrops);
      lastLog = now;
    }
  }
}

static void pumpMic() {
  // Capture runs in micCaptureTask. Notify from loop() so NimBLE actually
  // transmits. One packet per loop tick — two back-to-back notifies overwrite
  // the TX buffer (crispy dropouts).
  if (!micEnabled || !deviceConnected || micChar == nullptr || micQueue == nullptr) {
    return;
  }
  static uint32_t lastNotifyMs = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastNotifyMs < 9) {
    return;
  }
  MicFrame frame;
  if (xQueueReceive(micQueue, &frame, 0) != pdTRUE) {
    return;
  }
  lastNotifyMs = nowMs;
  micChar->setValue((uint8_t *)frame.samples, sizeof(frame.samples));
  micChar->notify();
  micFramesSent++;
  const uint32_t now = millis();
  if (now - micLastLogMs >= 2000) {
    Serial.printf("mic tx=%u drops=%u\n", (unsigned)micFramesSent, (unsigned)micDrops);
    micLastLogMs = now;
  }
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

static void drawGestureHint(const char *name) {
  gfx->setTextSize(2);
  const int16_t textW = (int16_t)(strlen(name) * DISPLAY_TEXT_CHAR_W);
  const int16_t x = (int16_t)(((int)DISPLAY_WIDTH - textW) / 2);
  const int16_t y = (int16_t)(((int)DISPLAY_HEIGHT - 16) / 2);
  const int16_t boxX = (int16_t)((max(0, (int)x - 8)) & ~1);
  const int16_t boxY = (int16_t)((max(0, (int)y - 8)) & ~1);
  const int16_t boxW = (int16_t)((textW + 16) & ~1);
  gfx->fillRect(boxX, boxY, boxW, 32, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(max((int)DISPLAY_TEXT_ORIGIN_X, (int)x), y);
  gfx->print(name);
}

static const char *gestureName(uint8_t id) {
  switch (id) {
    case GESTURE_SWIPE_UP:
      return "swipe up";
    case GESTURE_SWIPE_DOWN:
      return "swipe down";
    case GESTURE_SINGLE_TAP:
      return "tap";
    case GESTURE_DOUBLE_TAP:
      return "double tap";
    case GESTURE_LONG_PRESS:
      return "hold";
    default:
      return "touch";
  }
}

static void emitGesture(uint8_t id) {
  const bool sent = sendEvent(EVT_GESTURE, &id, 1);
  char line[28];
  snprintf(
      line, sizeof(line), "%s%s", gestureName(id), sent ? " ok" : (deviceConnected ? " fail" : " n/c"));
  drawGestureHint(line);
  Serial.printf("gesture %u sent=%d connected=%d\n", id, sent ? 1 : 0, deviceConnected ? 1 : 0);
}

static void ft3168Write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(FT3168_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static bool ft3168Read(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(FT3168_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  const uint8_t n = Wire.requestFrom((int)FT3168_I2C_ADDR, (int)len);
  if (n < len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool readFt3168(bool *down, int16_t *x, int16_t *y) {
  uint8_t points = 0;
  if (!ft3168Read(0x02, &points, 1)) {
    *down = false;
    return false;
  }
  points &= 0x0F;
  if (points == 0) {
    *down = false;
    return true;
  }
  uint8_t buf[4];
  if (!ft3168Read(0x03, buf, 4)) {
    *down = false;
    return false;
  }
  *x = (int16_t)(((buf[0] & 0x0F) << 8) | buf[1]);
  *y = (int16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
  *down = true;
  return true;
}

static bool setupTouch() {
  pinMode(PIN_TP_RST, OUTPUT);
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(80);
  Wire.beginTransmission(FT3168_I2C_ADDR);
  const bool present = Wire.endTransmission() == 0;
  if (!present) {
    Serial.println("FT3168 not on I2C 0x38");
    return false;
  }
  ft3168Write(0x00, 0x00);  // working / normal mode
  delay(10);
  return true;
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
    drawGestureHint("touch");
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
  gfx->fillScreen(RGB565_BLACK);
  applyBrightness(80);
  drawIdle("Waiting...");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  if (!setupTouch()) {
    drawIdle("No touch IC");
  }
  setupMic();
  micQueue = xQueueCreate(12, sizeof(MicFrame));
  xTaskCreatePinnedToCore(micCaptureTask, "miccap", 4096, nullptr, 5, nullptr, 1);
  setupBle();
  es7210AnalogOn();
}

void loop() {
  static uint32_t lastTouchMs = 0;
  const uint32_t now = millis();
  if (!micEnabled || now - lastTouchMs >= 20) {
    pumpTouch();
    lastTouchMs = now;
  }
  pumpMic();
}
