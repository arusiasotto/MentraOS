# Adding a Mentra controller (remote / ring / fob)

This is a contributor how-to for a **phone-paired BLE controller**: a device that
sends R1-style touch gestures to miniapps, not a pair of glasses.

Worked example: **XIAO Keyfob** (`ControllerTypes.KEYFOB` = `"XIAO Keyfob"`),
same role as the Even Realities R1 ring. Copy that stack, not the glasses
(`DeviceTypes` / `sgcs/`) stack.

**Reference implementations**

| Layer | First-party | Community example |
| --- | --- | --- |
| Android driver | `mobile/modules/bluetooth-sdk/android/.../controllers/R1.kt` | `.../controllers/Keyfob.kt` |
| Protocol constants | (R1 vendor protocol) | `.../controllers/keyfob/KeyfobProtocol.kt` + `firmware/keyfob/settings.h` |
| iOS driver | `mobile/modules/bluetooth-sdk/ios/Source/controllers/R1.swift` | `.../controllers/Keyfob.swift` |
| Firmware | (vendor) | `firmware/keyfob/` |

A controller is **not** glasses. Do not add it to `DeviceTypes`,
`hardware-capabilities.ts`, `select-glasses-model.tsx`, or `sgcs/`.

---

## 1. Pick a stable display name

The string users see is also the identity key. It must be **byte-for-byte
identical** in every copy:

- `ControllerTypes.<ID> = "<Display Name>"` in
  - `cloud/packages/types/src/enums.ts`
  - `mobile/modules/engine/src/types/enums.ts`
  - `mobile/modules/bluetooth-sdk/android/.../utils/Constants.kt` (`object ControllerTypes` **and** `ALL`)
  - `mobile/modules/bluetooth-sdk/ios/Source/utils/Constants.swift` (`struct ControllerTypes` **and** `ALL`)
- `DeviceModel` / `DeviceModels` entries:
  - `mobile/modules/bluetooth-sdk/android/.../types/DeviceModels.kt`
  - `mobile/modules/bluetooth-sdk/ios/Source/types/DeviceModels.swift` (`fromDeviceType` + `deviceType`)
  - `mobile/modules/bluetooth-sdk/src/BluetoothSdk.types.ts` (`DeviceModels`)

Example: `"XIAO Keyfob"`, not `"XIAO keyfob"` in one file and `"Keyfob"` in another.
`ControllerTypes.ALL` is how `DeviceManager` decides “this connect is a
controller, not glasses.” If you forget `ALL`, scan/connect goes down the glasses
path and pairing never completes.

---

## 2. BLE identity and protocol

Do **not** reuse the R1 GATT or Nordic UART unless you are actually speaking
that vendor protocol.

The Keyfob pattern (recommended for a new remote):

1. Unique BLE advertisement **name prefix** (Keyfob uses `Keyfob`, then a MAC
   suffix like `Keyfob-XXXX`).
2. Your own 128-bit GATT service + write characteristic + notify characteristic.
3. A tiny framed packet both sides share:

   `[opcode:u8][seq:u8][len:u16le][payload]`

4. Keep firmware and the phone protocol object in lockstep:
   - `firmware/<name>/settings.h`
   - `.../controllers/<name>/<Name>Protocol.kt`

Map button events to **R1 gesture names**. Miniapps subscribe to these strings;
new names will be invisible to existing apps.

| Gesture string | Typical input |
| --- | --- |
| `single_tap` | short primary press |
| `double_tap` | two short presses |
| `hold` | long primary press |
| `swipe_up` / `swipe_down` | dedicated up/down buttons or equivalent |

Deliver with `Bridge.sendTouchEvent(controllerType, gestureName, timestamp, source?)`.

---

## 3. Firmware

Put community firmware under `firmware/<name>/` (PlatformIO + `settings.h` +
`README.md` + `.gitignore` for `.pio`).

Requirements that the Mentra App actually cares about:

- Advertise the agreed name prefix.
- After connect, notify `EVT_READY` (or equivalent) so the phone can mark the
  controller booted.
- Notify battery percent if you have a divider; USB-only can report 100%.
- Buttons: document pin, polarity, and debounce. Keyfob is **active-low** to
  GND with internal pull-ups (D0 primary, D1 up, D2 down).

Do not expect users to pair in the phone’s system Bluetooth list. Mentra opens
its own GATT session.

---

## 4. Android driver

New files:

- `mobile/modules/bluetooth-sdk/android/.../controllers/<Name>.kt`
- `.../controllers/<name>/<Name>Protocol.kt`
- Unit tests under `android/src/test/java/...` (encode/decode, name matcher,
  gesture string map).

Wire `DeviceManager.initController`:

```kotlin
if (controllerType == ControllerTypes.R1) {
    controller = R1()
} else if (controllerType == ControllerTypes.KEYFOB) {
    controller = Keyfob()
} // add your type here
```

`DeviceManager` already routes `ControllerTypes.ALL` to `initController` +
`connectById`. Adding the type to `ALL` is what turns that on.

On GATT ready:

```kotlin
DeviceStore.apply("glasses", "controllerConnected", true)
DeviceStore.apply("glasses", "controllerFullyBooted", true)
```

Set **both**. R1 leaves `controllerFullyBooted` to G2 glasses. A phone-only
remote has no glasses to flip that bit. Pairing waits on
`engine/src/facades/pairing.ts` (`controllerConnected` **and**
`controllerFullyBooted`). If you only set connected, the UI sits on the loading
screen forever.

On disconnect, set both back to `false`. Persist `controller_device_name` and
`controllerMacAddress` so reconnect can find the same board.

Reconnect is by Bluetooth address when you saved it; scan match is by name
prefix (`matchesAdvertisedName`).

---

## 5. iOS driver

Mirror Android:

- `mobile/modules/bluetooth-sdk/ios/Source/controllers/<Name>.swift`
- Same display name, UUIDs, opcodes, gesture strings.
- `DeviceManager.initController` + `ControllerTypes.ALL`.
- Same `controllerConnected` / `controllerFullyBooted` behavior.

`connectByName` / `connectDevice` must call `initController` for
`ControllerTypes.ALL` (already true after the Keyfob work). Do not route a
controller into `initSGC`.

---

## 6. Pairing UI (normal list, not Super mode)

User path:

**Settings → Pair controller → \<your display name\>**

That screen is `mobile/src/app/pairing/select-controller.tsx`. Add a row there
(iOS and Android). Do **not** put the device on
`select-glasses-model.tsx`.

`ConnectControllerButton` must push `/pairing/select-controller`, not the
glasses picker.

Keep **Pair controller** visible without Super mode
(`DeviceSettingsSection`). Super-mode-only remotes never get used.

Then:

| File | What to add |
| --- | --- |
| `prep-controller.tsx` | Guide copy + image; `switch (deviceModel)` case |
| `success.tsx` | Connected subtitle |
| `src/i18n/en.ts` | `pairingGuides:<ID>` steps + `onboarding:<id>Connected` |
| `src/utils/getGlassesImage.tsx` | Display name + slug → PNG |
| `mobile/assets/glasses/<slug>.png` | List image (~180×220 is enough) |
| `glasses-compatibility.md` | Short identity + unofficial disclaimer if needed |

Tests: `mobile/src/__tests__/app/pairing/select-controller.test.tsx` (row +
navigates to prep). Prep/success tests if you add new copy keys.

---

## 7. Pairing readiness

`mobile/modules/engine/src/facades/pairing.ts` already treats **every**
`ControllerTypes` value as a controller (not R1-only). Adding the enum member
is enough **if** the native driver sets both connected flags.

Do not set `SETTINGS.default_wearable` for a controller. Use
`default_controller` / `controller_device_name` (the BLE layer already writes
these).

---

## 8. What miniapps see

No new cloud capability file is required for a button remote. Gestures arrive
as `touch_event` with the R1 names above (`session.input.onTouch('single_tap',
…)` in the Miniapp SDK).

If you invent a new gesture string, existing miniapps will ignore it. Prefer
the R1 set.

---

## Checklist

Copy this and tick it:

- [ ] Display name identical in cloud types, engine types, Android/iOS
      `ControllerTypes` + `ALL`, all three `DeviceModel` maps
- [ ] Custom GATT UUIDs + `settings.h` / `*Protocol.kt` in lockstep
- [ ] BLE name prefix unique (`matchesAdvertisedName`)
- [ ] Gestures: `single_tap` / `double_tap` / `hold` / `swipe_up` / `swipe_down`
- [ ] Android `initController` branch + driver + protocol unit tests
- [ ] iOS `initController` branch + driver
- [ ] Phone-only: set **both** `controllerConnected` and `controllerFullyBooted`
- [ ] Row on `select-controller.tsx` (not glasses list, not Super mode)
- [ ] Prep + success + i18n + image + `getGlassesImage`
- [ ] `ConnectControllerButton` → `/pairing/select-controller`
- [ ] `glasses-compatibility.md` (and firmware README if you ship firmware)
- [ ] Unofficial disclaimer if this is not a vendor product

Verify on device:

1. Flash firmware; LED/advertise as `<Prefix>-XXXX`.
2. Mentra App → Settings → Pair controller → your model.
3. Grant Bluetooth (and location on Android).
4. Connected + fully booted (success screen, not infinite loading).
5. Primary / up / down inputs fire in a miniapp that listens for R1 touches.

JS-only pairing UI: reload Metro. Native driver/protocol: rebuild the app
(`bun android` / `bun ios`). Bluetooth SDK Android compile:
`./scripts/check-android-compile.sh bluetooth-sdk`.

---

## Pitfalls (from Keyfob)

| Symptom | Cause |
| --- | --- |
| Pairing spinner never finishes | Set `controllerConnected` but not `controllerFullyBooted` |
| Scan finds nothing | Name prefix mismatch, or still advertising as a stock Nordic UART name |
| “Pair controller” missing | Hidden behind Super mode, or `DeviceSettingsSection` returned null (fixed: that row always shows) |
| Picker shows glasses (Live, G1, …) | `ConnectControllerButton` still pushes `select-glasses-model` |
| Miniapps ignore buttons | Gesture string is not an R1 name |
| Stock PlatformIO `UnknownBoard` | Board IDs like `xiaoble` are not in stock `nordicnrf52`; use the vendor board package (see `firmware/keyfob/platformio.ini`) |
| System Bluetooth “paired” but Mentra is not | User paired in Android/iOS settings; Mentra needs its own GATT connect |

---

## Out of scope (different guide)

- **Glasses** (including unofficial S3 Watch): `DeviceTypes`, `sgcs/`,
  `select-glasses-model.tsx`, cloud `hardware-capabilities.ts`.
- **R1 + G2 as a pair**: G2 owns `controllerFullyBooted`; do not copy that
  split onto a standalone remote.
