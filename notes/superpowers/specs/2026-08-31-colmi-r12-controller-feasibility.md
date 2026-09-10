---
status: archived
owner: Arusia
---

# Colmi R12 as a Mentra controller — failed path

**Verdict: Failed. Do not implement a Colmi R12 Mentra controller.**

Hardware on a real R12 does not expose a Mentra-usable gesture set. The
ring can fire a **single generic tap** over GATT, at about **1 Hz**, with
**no hold duration** and **no swipe** (no previous/next, no directional
touch, no long-press). That cannot drive R1-named `touch_event`s
(`swipe_up` / `swipe_down` / `hold` / `double_tap`) that miniapps already
subscribe to. Mapping one 1 Hz tap onto `single_tap` is not a controller.

Earlier drafts of this spec (on
`origin/cursor/colmi-r12-controller-feasibility-9c7b`) guessed that the
OLED media panel would emit QRing `MediaAction` values (pause / prev /
next / volume) as Yawell notifies. Bench disproved that. Leave the
protocol notes below so the investigation is not repeated. Do not write
`ColmiR12.kt` / `ColmiR12.swift`, do not add `ControllerTypes.COLMI_R12`,
and do not reopen this unless vendor firmware actually grows swipe/hold.

OS HID / AVRCP / `MediaSession` intercept was already a **No**. That
part still stands: the ring is not a HID device; QRing synthesizes media
keys on the phone. Mentra should not steal those either.

---

## Why it fails as a Mentra controller

Mentra controllers (Even Realities R1; XIAO Keyfob on `origin/s3-watch`)
deliver this set:

| Gesture string | What miniapps need it for |
| --- | --- |
| `single_tap` | confirm / expand / shutter |
| `double_tap` | secondary confirm |
| `hold` | long primary |
| `swipe_up` / `swipe_down` | navigation, lists, HUD |

What the R12 actually sends:

| Hoped-for input | Observed |
| --- | --- |
| Swipe either direction | **None** |
| Hold / long-press | **None** (no hold time on the tap) |
| Distinct media actions (pause / prev / next) | **One generic tap** |
| Repeat rate | **~1 Hz** — too slow for HUD / list navigation |

A Keyfob-shaped GATT driver would still compile. It would only ever emit
`single_tap` once per second. That is not worth pairing UI, a
`ControllerTypes` slot, or radio contention with glasses.

---

## What we already know (do not re-sniff for gestures)

Public / PulseLoop facts that remain true and are **not** a reason to
build a driver:

| | R12 |
| --- | --- |
| Chip | Realtek RTL8762 (Yawell / QRing family) |
| Advertised name | `COLMI R12_<hex>`, regex `^COLMI R12_.*` |
| GATT | Service `6e40fff0-b5a3-f393-e0a9-e50e24dcca9e`; write `6e400002-…`; notify `6e400003-…` |
| Frame | 16 bytes, CRC = sum of first 15 `& 0xFF` |
| Bond | GATT-only; do not `createBond()`; exclusive central (QRing or Mentra, not both) |
| Health | Battery / HR / SpO2 / steps — ignore for a controller |
| HID | Not advertised. QRing turns notifies into phone media keys |

PulseLoop ([foureight84/PulseLoopAndroid](https://github.com/foureight84/PulseLoopAndroid))
never decoded taps, swipes, or music opcodes. Unknown 16-byte frames show
up as `CommandAck`. That is consistent with a single opaque tap, not a
gesture vocabulary.

---

## What we will not do

- Implement `ColmiR12` on the Keyfob/R1 controller path.
- Pretend the generic tap is `swipe_up` / `swipe_down` / `hold`.
- Flash or reverse-engineer RTL8762 firmware to invent swipes.
- Register a `MediaButtonReceiver` / `MPRemoteCommandCenter` steal.
- Keep QRing connected “and intercept.”
- Treat the ring as glasses (`sgcs/`, capabilities, store listing).

Phone-paired BLE remotes that **do** have distinct up / down / tap (or
firmware we control) still follow `notes/adding-a-controller.md` on
`origin/s3-watch`. Colmi R12 is not one of those remotes.

**Plan (cancelled):**
[`plans/archive/2026-08-31-colmi-r12-controller.md`](../plans/archive/2026-08-31-colmi-r12-controller.md)
