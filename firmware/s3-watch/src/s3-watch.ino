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
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>
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
static volatile bool audioReady = false;
static volatile bool audioStartPending = false;
static volatile bool audioStopPending = false;
static bool usbPadOff = false;
static QueueHandle_t micQueue = nullptr;
static bool ensureAudio();
static void releaseAudio();

constexpr size_t MIC_NOTIFY_SAMPLES = 160;
struct MicFrame {
  int16_t samples[MIC_NOTIFY_SAMPLES];
};

static uint8_t *jpegBuf = nullptr;
static size_t jpegCap = 0;
static size_t jpegLen = 0;
static size_t jpegExpected = 0;
static volatile bool jpegBlitPending = false;
static volatile bool jpegBusy = false;
static bool jpegReceiving = false;
static uint8_t jpegEndSeq = 0;
static bool hudFromJpeg = false;
static bool idleFace = true;
static uint32_t lastJpegMs = 0;
static bool clockSynced = false;
static int16_t tzOffsetMin = 0;
static bool faceDialDrawn = false;
static int lastShownSec = -1;
static uint8_t batteryPercent = 0;
static uint8_t lastShownBattery = 255;
static int16_t lastSecX = 0;
static int16_t lastSecY = 0;
static char pendingText[513];
static volatile bool textPending = false;
static volatile bool brightnessPending = false;
static uint8_t pendingBrightness = 80;
static bool displayAsleep = false;
static volatile bool wakePending = false;
static uint32_t lastActivityMs = 0;
static bool touchSwallow = false;
static bool pwrWasDown = false;
static bool bootWasDown = false;
static uint32_t pwrDownMs = 0;
static bool pwrHoldSleep = false;
static uint32_t bootDownMs = 0;
static bool bootHoldColor = false;
static uint8_t hudColorIndex = 0;
static size_t lastBlitLen = 0;

struct MenuItem {
  bool running;
  char name[MENU_NAME_MAX + 1];
};
static MenuItem menuItems[MENU_MAX_ITEMS];
static uint8_t menuCount = 0;
static uint8_t menuIndex = 0;
static bool menuOpen = false;
static volatile bool menuPending = false;
static esp_pm_lock_handle_t stayAwakeLock = nullptr;
static bool stayAwakeHeld = false;
static bool pmReady = false;

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

static void ft3168Write(uint8_t reg, uint8_t value);

static void sendAck(uint8_t cmd, uint8_t seq, uint8_t status) {
  const uint8_t payload[3] = {cmd, seq, status};
  sendEvent(EVT_ACK, payload, 3);
}

static uint16_t hudColor() {
  return HUD_PALETTE[hudColorIndex < HUD_PALETTE_COUNT ? hudColorIndex : 0];
}

static void recolorRgb565(uint16_t *px, int n) {
  const uint16_t dst = hudColor();
  if (dst == DISPLAY_HUD_GREEN) return;
  const uint8_t dr = (uint8_t)((dst >> 11) & 0x1F);
  const uint8_t dg = (uint8_t)((dst >> 5) & 0x3F);
  const uint8_t db = (uint8_t)(dst & 0x1F);
  for (int i = 0; i < n; i++) {
    const uint16_t p = px[i];
    if (p == 0) continue;
    const uint16_t r = (uint16_t)((p >> 11) & 0x1F) * 8;
    const uint16_t g = (uint16_t)((p >> 5) & 0x3F) * 4;
    const uint16_t b = (uint16_t)(p & 0x1F) * 8;
    const uint16_t lum = (uint16_t)((r * 30 + g * 59 + b * 11) / 100);
    if (lum < 8) {
      px[i] = 0;
      continue;
    }
    px[i] = (uint16_t)(((dr * lum / 255) << 11) | ((dg * lum / 255) << 5) | (db * lum / 255));
  }
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
  gfx->setTextColor(hudColor());
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

static bool localNow(struct tm *out) {
  time_t utc = time(nullptr);
  if (!clockSynced || utc < 1700000000) return false;
  const time_t local = utc + (time_t)tzOffsetMin * 60;
  gmtime_r(&local, out);
  return true;
}

static void applyTimeSync(const uint8_t *payload, uint16_t n) {
  if (n < 4) return;
  const uint32_t sec = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) | ((uint32_t)payload[2] << 16) |
                       ((uint32_t)payload[3] << 24);
  if (n >= 6) {
    tzOffsetMin = (int16_t)((uint16_t)payload[4] | ((uint16_t)payload[5] << 8));
  } else if (tzOffsetMin == 0) {
    tzOffsetMin = -240;
  }
  struct timeval tv;
  tv.tv_sec = (time_t)sec;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  clockSynced = true;
  lastShownSec = -1;
}

static void textBounds(const char *text, uint8_t size, uint16_t *w, uint16_t *h) {
  gfx->setTextSize(size);
  int16_t x1 = 0;
  int16_t y1 = 0;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, w, h);
}

static void drawCentered(const char *text, int16_t y, uint8_t size, uint16_t color) {
  uint16_t w = 0;
  uint16_t h = 0;
  textBounds(text, size, &w, &h);
  gfx->setTextColor(color);
  const int16_t x = (int16_t)((DISPLAY_WIDTH - (int)w) / 2);
  gfx->setCursor(x, y);
  gfx->print(text);
}

static void drawTimeWithAmPm(const char *timeText, const char *ampm, int16_t y) {
  uint16_t timeW = 0;
  uint16_t timeH = 0;
  uint16_t ampmW = 0;
  uint16_t ampmH = 0;
  textBounds(timeText, 6, &timeW, &timeH);
  if (ampm && ampm[0]) {
    textBounds(ampm, 2, &ampmW, &ampmH);
  }
  const int16_t gap = ampmW ? 10 : 0;
  const int16_t total = (int16_t)(timeW + gap + ampmW);
  const int16_t x0 = (int16_t)((DISPLAY_WIDTH - total) / 2);
  gfx->setTextColor(hudColor());
  gfx->setTextSize(6);
  gfx->setCursor(x0, y);
  gfx->print(timeText);
  if (ampmW) {
    gfx->setTextSize(2);
    gfx->setCursor((int16_t)(x0 + timeW + gap), (int16_t)(y + 8));
    gfx->print(ampm);
  }
}

static void drawWatchDial() {
  const int16_t cx = DISPLAY_WIDTH / 2;
  const int16_t cy = DISPLAY_HEIGHT / 2;
  const int16_t r = 168;
  gfx->fillScreen(RGB565_BLACK);
  yield();
  gfx->drawCircle(cx, cy, r, hudColor());
  gfx->drawCircle(cx, cy, r - 1, hudColor());
  for (int i = 0; i < 12; i++) {
    const float a = (float)i * 30.0f * DEG_TO_RAD - (float)M_PI / 2.0f;
    const int16_t x0 = (int16_t)(cx + (int)((r - 14) * cosf(a)));
    const int16_t y0 = (int16_t)(cy + (int)((r - 14) * sinf(a)));
    const int16_t x1 = (int16_t)(cx + (int)((r - 2) * cosf(a)));
    const int16_t y1 = (int16_t)(cy + (int)((r - 2) * sinf(a)));
    gfx->drawLine(x0, y0, x1, y1, hudColor());
  }
  faceDialDrawn = true;
}

static void drawWatchFace(bool force) {
  if (!idleFace || hudFromJpeg || jpegBusy || jpegReceiving || jpegBlitPending) return;

  struct tm t;
  const bool synced = localNow(&t);
  if (!force) {
    if (synced && t.tm_sec == lastShownSec && batteryPercent == lastShownBattery) return;
    if (!synced && lastShownSec == -2 && batteryPercent == lastShownBattery) return;
  }
  const uint8_t prevBattery = lastShownBattery;
  lastShownSec = synced ? t.tm_sec : -2;
  lastShownBattery = batteryPercent;

  const int16_t cx = DISPLAY_WIDTH / 2;
  const int16_t cy = DISPLAY_HEIGHT / 2;
  const int16_t r = 168;

  const bool redrewDial = !faceDialDrawn || force;
  if (redrewDial) {
    drawWatchDial();
    lastSecX = 0;
    lastSecY = 0;
  }

  if (lastSecX != 0 || lastSecY != 0) {
    gfx->fillCircle(lastSecX, lastSecY, 5, RGB565_BLACK);
  }

  char timeBuf[8];
  char dateBuf[20];
  char ampmBuf[4];
  char battBuf[8];
  snprintf(battBuf, sizeof(battBuf), "%u%%", (unsigned)batteryPercent);
  if (synced) {
    int hour = t.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour, t.tm_min);
    strftime(dateBuf, sizeof(dateBuf), "%a %b %d", &t);
    snprintf(ampmBuf, sizeof(ampmBuf), "%s", t.tm_hour >= 12 ? "PM" : "AM");
    const float a = (float)t.tm_sec * 6.0f * DEG_TO_RAD - (float)M_PI / 2.0f;
    lastSecX = (int16_t)(cx + (int)((r - 18) * cosf(a)));
    lastSecY = (int16_t)(cy + (int)((r - 18) * sinf(a)));
    gfx->fillCircle(lastSecX, lastSecY, 5, hudColor());
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "--:--");
    dateBuf[0] = 0;
    ampmBuf[0] = 0;
    lastSecX = 0;
    lastSecY = 0;
  }

  const int16_t boxX = (int16_t)(cx - 130);
  const int16_t boxY = (int16_t)(cy - 46);
  gfx->fillRect(boxX, boxY, 260, 92, RGB565_BLACK);
  drawTimeWithAmPm(timeBuf, ampmBuf, (int16_t)(cy - 38));
  if (dateBuf[0]) {
    drawCentered(dateBuf, (int16_t)(cy + 22), 2, hudColor());
  }
  if (redrewDial || prevBattery != batteryPercent) {
    const int16_t battY = (int16_t)(cy + r - 36);
    gfx->fillRect((int16_t)(cx - 40), (int16_t)(battY - 4), 80, 24, RGB565_BLACK);
    drawCentered(battBuf, battY, 2, hudColor());
  }
}

// BLE callbacks must not paint. QSPI from NimBLE's task watchdog-resets
// the chip (connect looked like a bootloop).
static void requestIdleFace() {
  idleFace = true;
  hudFromJpeg = false;
  if (!faceDialDrawn) {
    lastShownSec = -1;
  }
}

static void applyMenu(const uint8_t *payload, uint16_t n) {
  menuCount = 0;
  if (n < 1) {
    if (menuOpen) menuPending = true;
    return;
  }
  uint8_t count = payload[0];
  if (count > MENU_MAX_ITEMS) count = MENU_MAX_ITEMS;
  size_t i = 1;
  for (uint8_t k = 0; k < count && i + 2 <= n; k++) {
    const uint8_t running = payload[i++];
    const uint8_t len = payload[i++];
    if (i + len > n) break;
    const uint8_t copy = len < MENU_NAME_MAX ? len : MENU_NAME_MAX;
    memcpy(menuItems[menuCount].name, payload + i, copy);
    menuItems[menuCount].name[copy] = 0;
    menuItems[menuCount].running = running != 0;
    i += len;
    menuCount++;
  }
  if (menuIndex >= menuCount) menuIndex = 0;
  if (menuOpen) menuPending = true;
}

static void drawMenu() {
  gfx->fillScreen(RGB565_BLACK);
  drawCentered("APPS", 52, 3, hudColor());
  if (menuCount == 0) {
    drawCentered("No apps yet", 200, 2, hudColor());
    drawCentered("Mentra > Glasses menu", 240, 2, hudColor());
    return;
  }
  constexpr uint8_t visible = 7;
  constexpr int16_t rowH = 40;
  constexpr int16_t y0 = 108;
  uint8_t start = 0;
  if (menuCount > visible) {
    if (menuIndex >= visible / 2) {
      start = (uint8_t)(menuIndex - visible / 2);
    }
    if ((uint8_t)(start + visible) > menuCount) {
      start = (uint8_t)(menuCount - visible);
    }
  }
  const uint8_t end = (uint8_t)(start + visible < menuCount ? start + visible : menuCount);
  for (uint8_t i = start; i < end; i++) {
    const int16_t y = (int16_t)(y0 + (int16_t)(i - start) * rowH);
    char line[20];
    snprintf(line, sizeof(line), "%s%s", menuItems[i].running ? "* " : "  ", menuItems[i].name);
    if (i == menuIndex) {
      gfx->fillRect(24, y - 6, DISPLAY_WIDTH - 48, rowH - 6, hudColor());
      gfx->setTextColor(RGB565_BLACK);
    } else {
      gfx->setTextColor(hudColor());
    }
    gfx->setTextSize(2);
    gfx->setCursor(40, y);
    gfx->print(line);
  }
}

static void closeMenu() {
  menuOpen = false;
  menuPending = false;
  requestIdleFace();
  lastShownSec = -1;
}

static void openMenu() {
  menuOpen = true;
  menuPending = true;
  textPending = false;
  noteActivity();
}

static void handleMenuGesture(uint8_t id) {
  if (id == GESTURE_SWIPE_UP) {
    if (menuCount) {
      menuIndex = (uint8_t)((menuIndex + menuCount - 1) % menuCount);
      menuPending = true;
    }
  } else if (id == GESTURE_SWIPE_DOWN) {
    if (menuCount) {
      menuIndex = (uint8_t)((menuIndex + 1) % menuCount);
      menuPending = true;
    }
  } else if (id == GESTURE_SINGLE_TAP || id == GESTURE_DOUBLE_TAP) {
    if (menuCount) {
      sendEvent(EVT_MENU_SELECT, &menuIndex, 1);
    }
    closeMenu();
    drawWatchFace(true);
  } else if (id == GESTURE_LONG_PRESS) {
    closeMenu();
    drawWatchFace(true);
  }
}

static void pumpMic();

static int jpegDraw(JPEGDRAW *pDraw) {
  recolorRgb565(pDraw->pPixels, pDraw->iWidth * pDraw->iHeight);
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  // Captions can blit for hundreds of ms. Keep mic notifies moving or the
  // queue overflows and STT looks like the glasses mic never started.
  pumpMic();
  return 1;
}

static void blitJpeg() {
  if (jpegBuf == nullptr || jpegLen < 4) {
    Serial.printf("jpeg blit skip len=%u\n", (unsigned)jpegLen);
    return;
  }
  lastBlitLen = jpegLen;
  if (jpeg.openRAM(jpegBuf, (int)jpegLen, jpegDraw) == 1) {
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close();
    Serial.printf("jpeg blit ok %u\n", (unsigned)jpegLen);
  } else {
    Serial.println("jpeg open failed");
  }
}

static void applyBrightness(uint8_t level) {
  brightness = level;
  if (brightness > 100) brightness = 100;
  if (!displayAsleep) {
    gfx->setBrightness((uint8_t)((brightness * 255) / 100));
  }
}

static void noteActivity() {
  lastActivityMs = millis();
}

static bool displayBusy() {
  return jpegReceiving || jpegBusy || jpegBlitPending || textPending;
}

static bool shouldStayAwake() {
  return displayBusy() || menuOpen;
}

static void holdAwake() {
  if (!pmReady || stayAwakeHeld || stayAwakeLock == nullptr) return;
  esp_pm_lock_acquire(stayAwakeLock);
  stayAwakeHeld = true;
}

static void releaseAwake() {
  if (!pmReady || !stayAwakeHeld || stayAwakeLock == nullptr) return;
  esp_pm_lock_release(stayAwakeLock);
  stayAwakeHeld = false;
}

static void updateSleepLocks() {
  if (!displayAsleep || shouldStayAwake()) {
    holdAwake();
  } else {
    releaseAwake();
  }
}

static void setupPowerMgmt() {
  gpio_wakeup_enable((gpio_num_t)PIN_PWR_SENSE, GPIO_INTR_HIGH_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_BOOT, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_TP_INT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "hud", &stayAwakeLock) != ESP_OK) {
    Serial.println("pm lock create failed");
    return;
  }
  esp_pm_config_t cfg = {
      .max_freq_mhz = 240,
      .min_freq_mhz = 80,
      .light_sleep_enable = true,
  };
  const esp_err_t err = esp_pm_configure(&cfg);
  if (err != ESP_OK) {
    Serial.printf("pm light-sleep failed: %s\n", esp_err_to_name(err));
    return;
  }
  pmReady = true;
  holdAwake();
}

static void sleepDisplay() {
  if (displayAsleep || shouldStayAwake()) return;
  gfx->setBrightness(0);
  gfx->displayOff();
  releaseAudio();
  setUsbJtagPad(false);
  displayAsleep = true;
  // Interrupt on touch so a tap can wake without polling every loop.
  ft3168Write(0xA4, 0x01);
  updateSleepLocks();
}

static void wakeDisplay() {
  const bool wasAsleep = displayAsleep;
  displayAsleep = false;
  wakePending = false;
  holdAwake();
  setUsbJtagPad(true);
  ft3168Write(0xA4, 0x00);
  gfx->displayOn();
  gfx->setBrightness((uint8_t)((brightness * 255) / 100));
  noteActivity();
  if (wasAsleep && micEnabled) {
    audioStopPending = false;
    audioStartPending = true;
    micFlushPending = true;
  }
  if (wasAsleep && idleFace && !hudFromJpeg && !displayBusy()) {
    drawWatchFace(true);
  }
}

static void requestWake() {
  wakePending = true;
  lastActivityMs = millis();
}

static void handleControl(const uint8_t *data, size_t len) {
  if (len < HDR_LEN) return;
  const uint8_t opcode = data[0];
  const uint8_t seq = data[1];
  const uint16_t payloadLen = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
  const uint8_t *payload = data + HDR_LEN;
  const size_t available = len > HDR_LEN ? len - HDR_LEN : 0;
  const uint16_t n = payloadLen < available ? payloadLen : (uint16_t)available;

  Serial.printf("ctrl 0x%02X n=%u asleep=%u mic=%u\n", opcode, (unsigned)n,
                displayAsleep ? 1 : 0, micEnabled ? 1 : 0);
  switch (opcode) {
    case CMD_TEXT: {
      // Host still sends "// MentraOS Connected" on pair. The watch face
      // is the idle UI now — do not cover it with a text wall.
      if (n >= 11 && memcmp(payload, "// MentraOS", 11) == 0) {
        sendAck(opcode, seq, 0);
        break;
      }
      const uint16_t copy = n < 512 ? n : 512;
      memcpy(pendingText, payload, copy);
      pendingText[copy] = 0;
      idleFace = false;
      hudFromJpeg = false;
      faceDialDrawn = false;
      textPending = true;
      requestWake();
      sendAck(opcode, seq, 0);
      break;
    }
    case CMD_CLEAR:
      // Miniapp start/stop. Return to the watch face from loop(); a JPEG
      // HUD will replace it if Captions (etc) is still drawing.
      jpegReceiving = false;
      jpegLen = 0;
      textPending = false;
      if (!menuOpen) {
        requestIdleFace();
      }
      sendAck(opcode, seq, 0);
      break;
    case CMD_BRIGHTNESS:
      if (n >= 1) {
        pendingBrightness = payload[0];
        brightnessPending = true;
      }
      sendAck(opcode, seq, 0);
      break;
    case CMD_MIC_ENABLE:
      micEnabled = n >= 1 && payload[0] != 0;
      if (micEnabled) {
        audioStopPending = false;
        if (!displayAsleep) {
          audioStartPending = true;
          micFlushPending = true;
        }
      } else {
        audioStartPending = false;
        audioStopPending = true;
      }
      updateSleepLocks();
      sendAck(opcode, seq, 0);
      break;
    case CMD_TIME_SYNC:
      applyTimeSync(payload, n);
      sendAck(opcode, seq, 0);
      break;
    case CMD_MENU:
      applyMenu(payload, n);
      sendAck(opcode, seq, 0);
      break;
    case CMD_IMG_BEGIN: {
      if (jpegBlitPending || jpegBusy || jpegReceiving) {
        sendAck(opcode, seq, 1);
        break;
      }
      jpegReceiving = true;
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
      requestWake();
      Serial.printf("jpeg begin exp=%u buf=%u\n", (unsigned)jpegExpected, jpegBuf ? 1 : 0);
      sendAck(opcode, seq, jpegBuf || jpegExpected == 0 ? 0 : 1);
      break;
    }
    case CMD_IMG_END:
      Serial.printf("jpeg end recv=%u len=%u\n", jpegReceiving ? 1 : 0, (unsigned)jpegLen);
      if (!jpegReceiving) {
        sendAck(opcode, seq, 0);
        break;
      }
      jpegReceiving = false;
      jpegEndSeq = seq;
      jpegBlitPending = true;
      requestWake();
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
    if (!jpegReceiving || data == nullptr || len == 0 || jpegBuf == nullptr) return;
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
    sendEvent(EVT_BATTERY, &batteryPercent, 1);
  }
  void   onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    micEnabled = false;
    audioStartPending = false;
    audioStopPending = true;
    Serial.println("phone disconnected");
    menuOpen = false;
    menuPending = false;
    requestIdleFace();
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

static uint8_t readBatteryPercent() {
  const uint8_t status = i2cReadReg(AXP2101_I2C_ADDR, AXP2101_STATUS1);
  if (status == 0xFF) return 0;
  // STATUS1 bit 3: battery present. USB-only boards report absent → 0%.
  if ((status & 0x08) == 0) return 0;
  const uint8_t pct = i2cReadReg(AXP2101_I2C_ADDR, AXP2101_BAT_PERCENT);
  return pct > 100 ? 0 : pct;
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
  const uint8_t det = i2cReadReg(AXP2101_I2C_ADDR, AXP2101_BAT_DET_CTRL);
  if (det != 0xFF) {
    i2cWriteReg(AXP2101_I2C_ADDR, AXP2101_BAT_DET_CTRL, (uint8_t)(det | 0x01));
  }
  const uint8_t gauge = i2cReadReg(AXP2101_I2C_ADDR, AXP2101_CHARGE_GAUGE_WDT);
  if (gauge != 0xFF) {
    i2cWriteReg(AXP2101_I2C_ADDR, AXP2101_CHARGE_GAUGE_WDT, (uint8_t)(gauge | 0x08));
  }
  batteryPercent = readBatteryPercent();
}

static void pumpBattery() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const uint32_t period = displayAsleep ? BATTERY_POLL_SLEEP_MS : BATTERY_POLL_MS;
  if (lastMs != 0 && (uint32_t)(now - lastMs) < period) return;
  lastMs = now;
  const uint8_t pct = readBatteryPercent();
  if (pct != batteryPercent) {
    batteryPercent = pct;
    lastShownSec = -1;
  }
  if (deviceConnected) {
    sendEvent(EVT_BATTERY, &batteryPercent, 1);
  }
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
}

// I2S clocks block ESP32-S3 light sleep even when idle. Bring the path up
// only for Captions; tear it down afterward.
static bool ensureAudio() {
  if (audioReady) return true;
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, LOW);
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
  recoverI2c();
  es8311Init();
  delay(50);
  es7210Init();
  delay(50);
  audioReady = true;
  return true;
}

static void releaseAudio() {
  if (!audioReady) return;
  audioReady = false;
  delay(40);
  es7210Write(0x4B, 0x0F);
  i2cWriteReg(ES8311_I2C_ADDR, 0x32, 0x00);
  i2cWriteReg(ES8311_I2C_ADDR, 0x00, 0x1F);
  digitalWrite(PIN_PA_CTRL, LOW);
  i2sBus.end();
}

static void setUsbJtagPad(bool on) {
  if (on) {
    if (!usbPadOff) return;
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    usbPadOff = false;
    return;
  }
  if (usbPadOff || usb_serial_jtag_is_connected()) return;
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  usbPadOff = true;
}

static void micCaptureTask(void *) {
  static int16_t stereo[MIC_NOTIFY_SAMPLES * 2];
  MicFrame frame;
  for (;;) {
    if (!micEnabled && !micFlushPending) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!audioReady) {
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
      }
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
  if (touchSwallow) return;
  noteActivity();
  if (menuOpen) {
    handleMenuGesture(id);
    return;
  }
  sendEvent(EVT_GESTURE, &id, 1);
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
    if (displayAsleep || touchSwallow) {
      touchSwallow = true;
      if (displayAsleep) wakeDisplay();
      return;
    }
    noteActivity();
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
    if (touchSwallow) {
      touchSwallow = false;
      touchPhase = TP_IDLE;
      touchSecondTap = false;
      return;
    }
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
  pinMode(PIN_PWR_SENSE, INPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  bootWasDown = digitalRead(PIN_BOOT) == LOW;
  requestIdleFace();
  drawWatchFace(true);
  noteActivity();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  setupTouch();
  pinMode(PIN_PA_CTRL, OUTPUT);
  digitalWrite(PIN_PA_CTRL, LOW);
  setupPmic();
  if (idleFace) {
    drawWatchFace(true);
  }
  micQueue = xQueueCreate(24, sizeof(MicFrame));
  xTaskCreatePinnedToCore(micCaptureTask, "miccap", 4096, nullptr, 5, nullptr, 1);
  setupBle();
  setupPowerMgmt();
}

static void pumpPwr() {
  const bool down = digitalRead(PIN_PWR_SENSE) == HIGH;
  if (down && !pwrWasDown) {
    pwrDownMs = millis();
    pwrHoldSleep = false;
  }
  if (down && !pwrHoldSleep && (uint32_t)(millis() - pwrDownMs) >= PWR_SLEEP_HOLD_MS) {
    pwrHoldSleep = true;
    if (menuOpen) {
      closeMenu();
    }
    if (displayAsleep) {
      wakeDisplay();
    } else {
      sleepDisplay();
    }
  }
  if (!down && pwrWasDown && !pwrHoldSleep) {
    noteActivity();
    if (displayAsleep) {
      wakeDisplay();
      openMenu();
    } else if (menuOpen) {
      closeMenu();
      drawWatchFace(true);
    } else {
      openMenu();
    }
  }
  pwrWasDown = down;
}

static void cycleHudColor() {
  hudColorIndex = (uint8_t)((hudColorIndex + 1) % HUD_PALETTE_COUNT);
  if (menuOpen) {
    menuPending = true;
    return;
  }
  if (hudFromJpeg && jpegBuf != nullptr && lastBlitLen >= 4) {
    jpegLen = lastBlitLen;
    blitJpeg();
    return;
  }
  if (idleFace) {
    faceDialDrawn = false;
    lastShownSec = -1;
    drawWatchFace(true);
  }
}

static void pumpBoot() {
  const bool down = digitalRead(PIN_BOOT) == LOW;
  if (down && !bootWasDown) {
    bootDownMs = millis();
    bootHoldColor = false;
    noteActivity();
    if (displayAsleep) {
      wakeDisplay();
    }
  }
  if (down && !bootHoldColor && (uint32_t)(millis() - bootDownMs) >= BOOT_COLOR_HOLD_MS) {
    bootHoldColor = true;
    cycleHudColor();
  }
  if (!down && bootWasDown && !bootHoldColor) {
    if (menuOpen) {
      closeMenu();
      drawWatchFace(true);
    } else if (!idleFace || hudFromJpeg) {
      jpegReceiving = false;
      jpegLen = 0;
      textPending = false;
      requestIdleFace();
      drawWatchFace(true);
    }
  }
  bootWasDown = down;
}

static void pumpDisplaySleep() {
  if (wakePending) {
    wakeDisplay();
  }
  if (shouldStayAwake()) {
    if (displayAsleep) {
      wakeDisplay();
    } else {
      noteActivity();
    }
    return;
  }
  if (!displayAsleep && (uint32_t)(millis() - lastActivityMs) >= DISPLAY_SLEEP_IDLE_MS) {
    sleepDisplay();
  }
  updateSleepLocks();
}

void loop() {
  static uint32_t lastTouchMs = 0;
  const uint32_t now = millis();
  if (audioStartPending && !displayAsleep) {
    audioStartPending = false;
    ensureAudio();
  }
  if (audioStopPending && !micEnabled) {
    audioStopPending = false;
    releaseAudio();
  }
  pumpMic();
  pumpPwr();
  pumpBoot();
  pumpBattery();
  pumpDisplaySleep();
  if (brightnessPending) {
    brightnessPending = false;
    applyBrightness(pendingBrightness);
  }
  if (jpegBlitPending) {
    if (menuOpen) {
      jpegBlitPending = false;
      jpegLen = 0;
      jpegExpected = 0;
      sendAck(CMD_IMG_END, jpegEndSeq, 0);
    } else {
      if (displayAsleep) {
        wakeDisplay();
      }
      jpegBlitPending = false;
      jpegBusy = true;
      blitJpeg();
      idleFace = false;
      faceDialDrawn = false;
      hudFromJpeg = true;
      jpegLen = 0;
      jpegExpected = 0;
      jpegBusy = false;
      sendAck(CMD_IMG_END, jpegEndSeq, 0);
      noteActivity();
      lastJpegMs = millis();
    }
  }
  if (menuOpen) {
    if (menuPending) {
      menuPending = false;
      if (displayAsleep) {
        wakeDisplay();
      }
      drawMenu();
      noteActivity();
    }
  } else if (textPending) {
    if (displayAsleep) {
      wakeDisplay();
    }
    textPending = false;
    drawTextWall(pendingText);
    noteActivity();
  } else if (!displayAsleep) {
    drawWatchFace(false);
  }
  if (displayAsleep && !shouldStayAwake()) {
    if (digitalRead(PIN_TP_INT) == LOW) {
      touchSwallow = true;
      wakeDisplay();
    }
  } else if (!micEnabled || now - lastTouchMs >= 20) {
    pumpTouch();
    lastTouchMs = now;
  }
  pumpMic();
  if (displayAsleep && !shouldStayAwake()) {
    delay(LIGHT_SLEEP_YIELD_MS);
  }
}
