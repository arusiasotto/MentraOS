# ESP32-S3 Watch — controls and features

Unofficial MentraOS firmware for the Waveshare **ESP32-S3-Touch-AMOLED-2.06**.
Use the **Watch** Mentra App (`com.mentra.mentra.watch`). Pair the device named
`S3Watch-XXXX`.

This is not a Waveshare product and is not affiliated with Waveshare.

After a USB flash: hold **PWR ~6s**, then tap **PWR** to boot.

---

## Hardware

| Control | Where |
|---|---|
| **PWR** | Bottom side button |
| **BOOT** | Top side button |
| **Touch** | AMOLED (FT3168) |
| **USB-C** | Power (and flash). This unit has no cell; battery reads **0%**. |

---

## PWR (bottom)

| Action | What it does |
|---|---|
| **Short tap** | Open or close the APPS menu. If the panel is asleep, wakes it first, then opens the menu. |
| **Hold ~1s** | Sleep: screen off. BLE stays connected. |
| **Hold ~6s** | Hardware power off (AXP cutoff, not firmware). |

## BOOT (top)

| Action | What it does |
|---|---|
| **Short tap** | Wake if asleep. Otherwise dismiss the APPS menu or a HUD card and return to the watch face. Does **not** stop the Mentra miniapp on the phone. |
| **Hold ~0.7s** | Cycle HUD color: green → amber → red → white → cyan. Face, menu, and on-screen HUD (including notification/Captions frames) remapped. |
| **Hold at power-on** | ESP32 download / flash mode. |

## Touch (when the panel is awake)

Gestures go to Mentra miniapps unless the APPS menu is open.

| Gesture | Mentra name | In APPS menu |
|---|---|---|
| Swipe up / down | `swipe_up` / `swipe_down` | Move selection |
| Tap | `single_tap` | Start or stop the highlighted miniapp |
| Double tap | `double_tap` | Same as tap |
| Long press (~400ms) | `long_press` | Close menu without selecting |

A tap on a sleeping panel only wakes it (the tap is not sent to Mentra).

---

## APPS menu

G2-style list. Mentra pushes the same **Glasses Menu** list the G2 uses
(Device settings → Glasses Menu in the Watch app). A `*` means that miniapp is
running. Tap an item to toggle it (same as G2 `miniapp_selected`).

There is no Sleep row in this list. Use **hold PWR ~1s** to blank the screen.

---

## Watch face

Shown when no HUD is up:

- Time + AM/PM on one line (phone time sync; `--:--` until paired)
- Date under the time
- Battery % at the bottom of the dial (`0%` with no cell)

---

## Sleep and wake

- Idle: panel sleeps after **30s** (mic on, or a JPEG/text in flight, keeps it awake).
- Manual sleep: **hold PWR ~1s**.
- While sleeping the ESP32 **light-sleeps** but **BLE stays up**. Mentra still
  sees the watch as connected. A notification JPEG, tap, PWR, or BOOT wakes it.
- Captions (mic on) will not stay asleep.

Deep sleep is not used. That would drop BLE and miss phone pushes.

---

## Mentra features this firmware supports

| Feature | Notes |
|---|---|
| HUD text | Full-screen UTF-8 (welcome `// MentraOS Connected` is ignored) |
| HUD images | Full-screen JPEG 410×502. Phone sends 16-level green; watch can recolor. |
| Brightness | Mentra brightness setting |
| Microphone | 16 kHz 16-bit mono PCM for Captions / STT |
| Notifications | Phone cards as JPEG; short beep when a card replaces the idle face (not Captions frames) |
| Glasses menu | Start/stop miniapps from the watch |
| Battery | AXP2101 % on the face and over BLE |
| Time | Unix seconds + timezone offset from the phone |

---

## Not wired (on purpose)

- Mentra speaker API (beep is firmware-only)
- Camera, Wi‑Fi, OTA, IMU / raise-to-wake
- Closing a miniapp except by picking it again in APPS (or stopping it in the Mentra App)
- iOS

---

## Flash

```bash
cd firmware/s3-watch
pio run -t upload --upload-port /dev/ttyACM0
```

Protocol and pin map: [`settings.h`](settings.h). Phone driver:
`mobile/modules/bluetooth-sdk/android/.../sgcs/S3Watch.kt`.
Developer notes: [`README.md`](README.md).
