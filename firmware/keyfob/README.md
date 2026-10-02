# XIAO Keyfob firmware (MentraOS fork)

Unofficial community firmware for the **Seeed Studio XIAO nRF52840 Plus**,
advertised to the Mentra App as a controller (`XIAO Keyfob`) in the same role
as the Even Realities R1 ring.

**This is not a Seeed product.** It is not affiliated with, endorsed by, or
supported by Seeed Studio. Use at your own risk.

The phone driver is
`mobile/modules/bluetooth-sdk/android/.../controllers/Keyfob.kt`.
UUIDs and opcodes live in [`settings.h`](settings.h) and
`KeyfobProtocol.kt` — keep them identical.

Adding another remote? Follow
[`notes/adding-a-controller.md`](../../notes/adding-a-controller.md).

## Identity

- BLE name prefix: `Keyfob` (MAC suffix appended when `DEVICE_ID` is empty)
- GATT service `d4b2c520-…e020`: control write, event notify

## Pair

In the Mentra App: **Settings → Pair controller → XIAO Keyfob**.
The LED blinks blue while advertising and turns green when connected.

## Flash

```bash
cd firmware/keyfob
pio run -t upload
pio device monitor
```

Board package: Seeed Adafruit nRF52 Arduino via
`seeed-xiao-afruitnrf52-nrf52840` (stock PlatformIO has no `xiaoble` board).
The Plus variant is pin-compatible on D0–D2; extra castellated GPIOs are unused.

Double-tap reset (or hold RESET) to enter the UF2 bootloader if the serial
port is missing.

## Buttons (active-low, internal pull-up)

| Pin | Role | Mentra gesture |
| --- | --- | --- |
| D0 | Primary | `single_tap` / `double_tap` / `hold` |
| D1 | Up | `swipe_up` |
| D2 | Down | `swipe_down` |

Wire each switch from the pin to GND. The RGB LED (active-low) blinks blue
while advertising and turns green when the phone is connected.

## v1 features

- R1-compatible touch events over BLE notify
- Battery percent from the XIAO VBAT divider (USB-only reports 100%)
- Phone can set the RGB LED (`CMD_LED`)
- No display, mic, camera, or glasses-side pairing
