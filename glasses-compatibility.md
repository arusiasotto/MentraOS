# Smart Glasses Compatibility

## Supported Devices

MentraOS supports smart glasses through explicit device and project identifiers. A
Bluetooth name or manufacturer-data prefix is a compatibility claim; do not add a
prefix unless the hardware is validated and listed here.

## Feature Compatibility Matrix

| Model | Display (Text) | Display (Images) | Microphone | Speaker | Camera |
| --- | --- | --- | --- | --- | --- |
| Even Realities G1 | Full | Full | Full | Not available | Not available |
| Mentra Live | Not available | Not available | Full | Full | Full |
| Mentra Mach 1 | Full | Not available | Partial* | Not available | Not available |
| Vuzix Z100 | Full | Not available | Partial* | Not available | Not available |
| Xingyi AR99 | Full | Not available | Full | Not available | Not available |
| ESP32-S3 Watch | Full | Full | Full | Not available | Not available |

* Microphone support via connected phone's microphone.

## AR99 Compatibility Matrix

The Mentra App exposes validated Xingyi AR99 hardware as `DeviceTypes.AR99`.
Only the exact BLE project identifier listed below is supported.

| Manufacturer | Display model | Device type | BLE project identifier |
| --- | --- | --- | --- |
| Xingyi Intelligent | Xingyi AR99 | `AR99` | `AR99` |

`AF98`, `AF99`, `HVXM`, and `HVXF` are not supported project identifiers and
must be rejected by scanning and advertisement parsing. AR99 pairing must fail
closed when a scan result has no project identifier or has a project identifier
outside the matrix above.

## ESP32-S3 Watch

This fork adds unofficial MentraOS support for the Waveshare ESP32-S3-Touch-AMOLED-2.06
as `DeviceTypes.S3_WATCH` (`ESP32-S3 Watch`). **This is not a Waveshare product and is
not affiliated with, endorsed by, or supported by Waveshare.**

The watch advertises BLE name prefix `S3Watch` and uses the custom GATT in
`firmware/s3-watch/settings.h`. Pairing reconnects by Bluetooth address.
Capabilities: 410x502 color AMOLED, JPEG bitmaps, 16 kHz PCM microphone.
No OTA, camera, or speaker path in v1.

## XIAO Keyfob

This fork adds unofficial MentraOS support for the Seeed Studio XIAO nRF52840 Plus
as `ControllerTypes.KEYFOB` (`XIAO Keyfob`). **This is not a Seeed product and is
not affiliated with, endorsed by, or supported by Seeed Studio.**

The fob pairs as a controller (same role as the Even Realities R1 ring), not as
glasses. Pair from Settings → Pair controller → XIAO Keyfob. It advertises BLE
name prefix `Keyfob` and uses the custom GATT in
`firmware/keyfob/settings.h`. Pairing reconnects by Bluetooth address.
Buttons: D0 primary (`single_tap` / `double_tap` / `hold`), D1 `swipe_up`,
D2 `swipe_down`. RGB LED and battery percent are reported to the Mentra App.

Contributor how-to for another controller:
[`notes/adding-a-controller.md`](notes/adding-a-controller.md).


## Getting Started

1. Download the Mentra App from the [App Store](https://apps.apple.com/us/app/mentra-the-smart-glasses-app/id6747363193) or [Google Play](https://play.google.com/store/apps/details?id=com.mentra.mentra).
2. Connect your smart glasses via Bluetooth.
3. Start using miniapps from the [Mentra Miniapp Store](https://apps.mentra.glass).

## Need Help?

If you are having trouble connecting your smart glasses or want to confirm compatibility, please:

- Check our [documentation](https://docs.mentra.glass)
- Join our [Discord community](https://mentra.glass/discord)
- Contact us at [team@mentra.glass](mailto:team@mentra.glass)
