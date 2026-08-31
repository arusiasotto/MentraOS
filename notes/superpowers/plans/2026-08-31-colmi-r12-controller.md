---
status: draft
owner: Arusia
---

# Colmi R12 controller implementation plan

> Execution checklist. Update checkboxes as work lands. Prefer editing this file over re-pasting plans into chat.

**Goal:** Mentra App pairs a Colmi R12 as a phone-only BLE controller and
delivers R1-named `touch_event`s from the ring’s music face (GATT `0x0B`
leading, with `0x1D` as fallback), without OS media-key intercept.

**Architecture:** Copy the XIAO Keyfob controller stack (`ControllerTypes` +
GATT driver + `Bridge.sendTouchEvent` + pairing UI). Speak Yawell UART-over-BLE
using PulseLoop/QRing constants (UUIDs, 16-byte CRC, name `^COLMI R12_.*`),
not the Keyfob frame. No firmware, no `sgcs/`, no Miniapp SDK change, no
PulseLoop dependency.

**Tech Stack:** Kotlin / Swift Bluetooth SDK, React Native pairing screens,
existing `touch_event` path through `DeviceEventRouter` →
`LocalMiniappRuntime`.

**Spec source of truth:** `notes/superpowers/specs/2026-08-31-colmi-r12-controller-feasibility.md`

**Depends on:** Keyfob identity plumbing on `origin/s3-watch`
(`ControllerTypes.ALL`, `projectTargetReady` for every controller, pairing
row not Super-mode-only). Land or cherry-pick that before or with this work.
Current `dev` still special-cases R1.

---

## File Map

| Path | Action | Responsibility |
|---|---|---|
| `mobile/modules/engine/src/types/enums.ts` | Modify | `ControllerTypes.COLMI_R12` |
| `mobile/modules/bluetooth-sdk/android/.../utils/Constants.kt` | Modify | type + `ALL` |
| `mobile/modules/bluetooth-sdk/ios/Source/utils/Constants.swift` | Modify | type + `ALL` |
| `mobile/modules/bluetooth-sdk/.../types/DeviceModels.*` + `BluetoothSdk.types.ts` | Modify | display name maps |
| `mobile/modules/bluetooth-sdk/android/.../controllers/colmi/ColmiR12Protocol.kt` | Create | UUIDs, CRC, cmd `0x0B`/`0x1D`/3/28, name matcher, action → gesture |
| `mobile/modules/bluetooth-sdk/android/.../controllers/ColmiR12.kt` | Create | scan / GATT / notify / battery / reconnect |
| `mobile/modules/bluetooth-sdk/ios/Source/controllers/ColmiR12.swift` | Create | iOS mirror |
| `mobile/modules/bluetooth-sdk/android/.../DeviceManager.kt` | Modify | `initController` branch |
| `mobile/modules/bluetooth-sdk/ios/Source/DeviceManager.swift` | Modify | `initController` branch |
| `mobile/modules/engine/src/facades/pairing.ts` | Modify | all `ControllerTypes` (if Keyfob not merged) |
| `mobile/src/app/pairing/select-controller.tsx` + prep/success/i18n/image | Modify | pairing list |
| `android/src/test/.../ColmiR12ProtocolTest.kt` | Create | CRC + decode + name matcher |

---

## Conventions

- Unofficial: disclaimer in driver comments, pairing copy, compatibility notes.
- Gesture strings must stay in the R1 set (`single_tap`, `double_tap`, `hold`,
  `swipe_up`, `swipe_down`).
- Phone-only: set both `controllerConnected` and `controllerFullyBooted`.
- Do not add HID / `MediaButtonReceiver` / `MPRemoteCommandCenter` intercept.

---

## Phase 0: Hardware capture (gate)

Do not start Phase 1 until a leading `0x0B` (or `0x1D`) notify is seen
with Mentra (or PulseLoop raw trace / nRF Connect / `bleak`) as the only
central, and `bytes[1]` is recorded per gesture.

- [ ] Forget QRing and system Bluetooth bond
- [ ] Record advertised name (`COLMI R12_<hex>`, PulseLoop regex `^COLMI R12_.*`)
- [ ] Confirm Yawell service + notify char
- [ ] Optional: PulseLoop Developer raw-packet trace while using the media panel; grep export for commandId 11 / hex `0b` (and 29 / `1d`)
- [ ] Battery command 3
- [ ] Media-panel taps/swipes → leading `0x0B`, log action byte
- [ ] Command 28 dummy now-playing: does the music face stay up?
- [ ] Optional camera `TAKE_PHOTO`
- [ ] Reconnect by address
- [ ] Coexist with glasses GATT

If `0x0B` never appears, stop and update the spec. That is the only
feasibility killer.

---

## Phase 1: Protocol + Android driver

### Task 1: Identity

**Files:** engine enums, Android/iOS `ControllerTypes` + `ALL`, DeviceModels

- [ ] Display name `"Colmi R12"` identical in every copy
- [ ] Not added to `DeviceTypes`

### Task 2: Protocol object + tests

**Files:** `ColmiR12Protocol.kt`, unit tests

- [ ] UUIDs, 16-byte CRC, cmd `0x0B` / `0x1D` / 3/28 (from PulseLoop `ColmiPacket` + colmi-docs + R12 `0x0B` leading)
- [ ] `matchesAdvertisedName` = `^COLMI R12_.*` (PulseLoop `WearableModel.COLMI_R12`)
- [ ] `MediaAction` / `bytes[1]` → R1 gesture map from the spec (v1 table)

### Task 3: Android driver

**Files:** `ColmiR12.kt`, `DeviceManager.initController`

- [ ] Scan, connect, CCCD, init (time + music switch + battery)
- [ ] Notify → `Bridge.sendTouchEvent`
- [ ] Both connected flags; persist address; reconnect
- [ ] Ignore non-29/3 packets on the notify char

---

## Phase 2: iOS + pairing UI

### Task 1: iOS driver

**Files:** `ColmiR12.swift`, iOS `DeviceManager`

- [ ] Same UUIDs, opcodes, flags, reconnect-by-UUID

### Task 2: Pairing UI

**Files:** `select-controller.tsx`, `prep-controller.tsx`, `success.tsx`,
`en.ts`, image, `getGlassesImage`

- [ ] Row on the normal controller list (iOS and Android)
- [ ] Prep: forget QRing; open the media panel
- [ ] Pairing tests for the new row

---

## Phase 3: Device verification

- [ ] Pair from Settings → Pair controller → Colmi R12
- [ ] Success screen (not infinite loading)
- [ ] Miniapp `onTouch` fires for tap / swipe
- [ ] Background reconnect
- [ ] Glasses still connected

`./scripts/check-android-compile.sh bluetooth-sdk` for the native module.
JS pairing tests: `cd mobile && bun test -- select-controller`.
