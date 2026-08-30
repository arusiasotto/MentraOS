# ESP32-S3 Watch firmware (MentraOS fork)

Unofficial community firmware for the Waveshare **ESP32-S3-Touch-AMOLED-2.06**
board, advertised to the Mentra Android app as `ESP32-S3 Watch`.

**This is not a Waveshare product.** It is not affiliated with, endorsed by, or
supported by Waveshare Electronics. Use at your own risk.

The phone driver is
`mobile/modules/bluetooth-sdk/android/.../sgcs/S3Watch.kt`.
UUIDs and opcodes live in [`settings.h`](settings.h) and
`S3WatchProtocol.kt` — keep them identical.

## Identity

- BLE name prefix: `S3Watch` (MAC suffix appended when `DEVICE_ID` is empty)
- GATT service `c3a1b410-…d010`: control write, event notify, JPEG write, mic notify

## Flash

```bash
cd firmware/s3-watch
pio run -t upload
pio device monitor
```

Board package: ESP32-S3 with OPI PSRAM. If your Waveshare kit uses 32 MB flash,
set `board_upload.flash_size = 32MB` in `platformio.ini`.

## v1 features

- Full-screen UTF-8 text
- Full-screen JPEG blit (410x502)
- Brightness
- 16 kHz 16-bit mono PCM mic (ES7210 + I2S) when the phone sends `MIC_ENABLE`
- Capacitive gestures (FT3168): swipe up/down, tap, double tap, long press
- Battery notify is a stub (`100%`) until AXP2101 is wired
