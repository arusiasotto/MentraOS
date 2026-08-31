---
status: draft
owner: Arusia
---

# Colmi R12 as a Mentra controller — feasibility

**Verdict: Yes, as a phone-paired BLE controller on the Keyfob/R1 path.
No, if “intercept media keys in the Mentra App” means OS HID / AVRCP /
`MediaSession` steal.**

The ring’s media panel does not talk to Android or iOS as a keyboard or
headset. It notifies a 16-byte Yawell GATT packet (command id 29). QRing
turns that into system media keys. Mentra should skip QRing and consume
command 29 itself, then emit the same `touch_event` strings miniapps already
handle.

This is the same *role* as the XIAO nRF Keyfob (`origin/s3-watch`,
`notes/adding-a-controller.md`). It is **not** the same *protocol*: we do
not own R12 firmware.

Hardware capture is still required before writing the driver. The protocol
is public; whether the media face actually emits command 29 when Mentra is
the only central is the remaining unknown.

---

## 1. What the Keyfob work actually proved

On `origin/s3-watch` (not yet on `dev`):

| Piece | What it does |
| --- | --- |
| `ControllerTypes.KEYFOB = "XIAO Keyfob"` | Identity key, byte-identical across engine + Android + iOS |
| `controllers/Keyfob.kt` / `Keyfob.swift` | Phone GATT client: scan by name prefix, connect, CCCD, notify |
| `KeyfobProtocol.kt` + `firmware/keyfob/settings.h` | Framed `[opcode][seq][len][payload]` we defined |
| `Bridge.sendTouchEvent(...)` | `single_tap` / `double_tap` / `hold` / `swipe_up` / `swipe_down` |
| Pairing UI | Settings → Pair controller → model (not glasses, not Super mode) |
| Readiness | Set **both** `controllerConnected` and `controllerFullyBooted` |

Miniapps never see “Keyfob”. They see `session.input.onTouch("single_tap")`
because `DeviceEventRouter` forwards `touch_event` into
`LocalMiniappRuntime`.

On current `dev` the only controller is Even Realities R1
(`controllers/R1.kt` / `R1.swift`): custom service
`BAE80001-…`, gesture marker `0xFF`. `DeviceManager.initController` only
constructs `R1()`. `pairing.ts` `projectTargetReady` is still **R1-only**;
Keyfob generalized that to every `ControllerTypes` value.

An R12 driver should copy **Keyfob plumbing**, not R1’s vendor GATT, and
must land the Keyfob pairing-readiness generalization (or equivalent) or
the loading screen never completes.

---

## 2. What the Colmi R12 actually is

Public sources (QRing listings, Gadgetbridge, [colmi-docs](https://colmi.puxtril.com/commands/)):

| | R12 | XIAO Keyfob | Even Realities R1 |
| --- | --- | --- | --- |
| Chip | Realtek RTL8762 (Yawell / QRing family) | nRF52840 we flash | Vendor BLE ring |
| Firmware | Vendor, closed | Mentra fork in `firmware/keyfob/` | Vendor |
| Pairing | BLE GATT, no vendor app required (Gadgetbridge: no-vendor pair) | Mentra GATT only | Mentra GATT |
| Input surface | OLED + touchpad; music / camera faces | Three physical buttons | Touch gestures on the ring |
| How input leaves the device | Yawell UART-over-BLE notifies | Our `EVT_GESTURE` opcode | `0xFF` gesture frames |
| HID / media keys | **Not advertised.** QRing synthesizes them on the phone | None | None |
| Health stack | HR, SpO2, steps, sleep, … | None | None |

GATT (Yawell command channel):

```
Service:                6e40fff0-b5a3-f393-e0a9-e50e24dcca9e
Write (requests):       6e400002-b5a3-f393-e0a9-e50e24dcca9e
Notify (responses):     6e400003-b5a3-f393-e0a9-e50e24dcca9e
```

Every command is 16 bytes: `[id:u8][data:14][crc:u8]`, CRC = sum of the
first 15 bytes `& 0xFF`. There is a second “firmware / big data” service
(`de5bf728-…`); a controller does not need it.

Advertised names in the wild: `R12`, `R12_XXXX`, `Colmi`. Confirm on a
nRF Connect scan. Do not assume the R02 `R0n_xxxx` regex.

Community note: the ring is often invisible if it is already bonded to
QRing or another central. Mentra should own the only GATT session.

---

## 3. The media panel is GATT, not HID

### What the ring sends (command 29)

When the user uses the on-ring **music** UI, the ring notifies:

```c
struct MusicCommandResponse {   // commandId = 29
    uint8_t commandId;          // 29
    uint8_t action;             // MediaAction
    char unused[13];
    uint8_t crc;
};

enum MediaAction {
    Pause       = 1,
    Previous    = 2,
    Next        = 3,
    VolumeUp    = 4,
    VolumeDown  = 5
};
```

The phone can also **push** now-playing onto the ring (command 28:
playing / progress / volume / 10-char title). QRing does that so the
face looks like a media control panel. Mentra can send a dummy
“Mentra” title so the face stays in music mode without a real player.

Related, not media keys:

| Command | Direction | Use for a controller |
| --- | --- | --- |
| 28 Music Switch | phone → ring | Keep / enable the music face |
| 29 Music | ring → phone | **Primary input** |
| 2 Camera | both | Shutter (`TAKE_PHOTO = 2`) as another tap |
| 3 Battery | both | `controllerBatteryLevel` |
| 2 / `0x0204` wave-gesture | phone → ring | TikTok-style wave; Gadgetbridge still lists camera/wave as missing — do not depend on it for v1 |

### Why OS-level intercept fails

The idea “pair the ring, let it fire media keys, steal them in Mentra 3.0”
needs the ring (or QRing) to inject `KEYCODE_MEDIA_*` / AVRCP into the OS.

1. **The ring is not a HID device.** No HID-over-GATT (`1812`), no
   BLE HOGP, no classic AVRCP. nRF Connect and Gadgetbridge only show
   the Yawell services above.
2. **QRing is the HID synthesizer.** It holds the GATT session, reads
   command 29, then dispatches media keys. Mentra cannot be that
   central at the same time: these rings are single-central in practice
   (“cannot be paired to other devices or it cannot be discovered”).
3. **Android will not give Mentra those keys anyway.** From Android 8,
   media buttons go to the *current* `MediaSession`. Mentra is not a
   music player. Owning the session fights Spotify / YouTube / the
   glasses’ own playback (`audioPlaybackAssets.ts` already talks about
   media-control Play). A `MediaButtonReceiver` only fires when nothing
   else is playing.
4. **iOS is stricter.** `MPRemoteCommandCenter` only works when the app
   is Now Playing. Background Mentra cannot swallow lock-screen /
   headset events meant for Music. There is no public “intercept all
   media keys” API.
5. **Side effects.** Even a successful steal still changes volume and
   skips tracks unless Mentra also suppresses the system action — which
   again requires being the active session.

Accessibility / notification-listener hacks are Play-policy hostile and
do not exist on iOS. Do not use them.

**Do not implement HID intercept. Implement a Keyfob-shaped GATT
controller.**

---

## 4. Recommended mapping (v1)

Map command 29 onto the R1 gesture strings. Miniapps already subscribe
to these; new strings are invisible.

Proposed v1 (product can swap after a bench test of the actual face):

| Ring `MediaAction` | Likely touch on the OLED | Mentra `gestureName` |
| --- | --- | --- |
| Pause (1) | tap play/pause | `single_tap` |
| Previous (2) | prev / swipe one way | `swipe_down` |
| Next (3) | next / swipe the other way | `swipe_up` |
| VolumeUp (4) | volume + | ignore in v1 **or** `double_tap` |
| VolumeDown (5) | volume − | ignore in v1 **or** `hold` |
| Camera `TAKE_PHOTO` | shutter face | `single_tap` (optional second source) |

Volume vs skip is a collision if both are mapped to swipes. v1 should
either ignore volume or send it as `double_tap`/`hold` so navigation
miniapps keep `swipe_*`.

Deliver with:

```kotlin
Bridge.sendTouchEvent(ControllerTypes.COLMI_R12, gestureName, timestamp)
```

No new Miniapp SDK stream. No cloud capability file.

---

## 5. Mentra wiring (copy Keyfob, swap protocol)

Follow `notes/adding-a-controller.md` (on `origin/s3-watch`; the
checklist is the contract even if that branch is not merged).

### Identity

`ControllerTypes.COLMI_R12 = "Colmi R12"` in:

- `mobile/modules/engine/src/types/enums.ts`
- `mobile/modules/bluetooth-sdk/android/.../utils/Constants.kt` (`object` **and** `ALL`)
- `mobile/modules/bluetooth-sdk/ios/Source/utils/Constants.swift` (`struct` **and** `ALL`)
- `DeviceModels` maps (Android, iOS, `BluetoothSdk.types.ts`)

`cloud-v2` has no `ControllerTypes` today. Do not add the ring to
`DeviceTypes` / `sgcs/` / `select-glasses-model.tsx`.

### Driver shape

New files (mirror Keyfob):

- `controllers/ColmiR12.kt` / `ColmiR12.swift`
- `controllers/colmi/ColmiR12Protocol.kt` (UUIDs, CRC, command ids, name matcher, action → gesture)
- Unit tests for CRC, command 29 decode, name prefix
- Pairing UI: `select-controller.tsx`, `prep-controller.tsx`, `success.tsx`, i18n, image

`DeviceManager.initController`:

```kotlin
if (controllerType == ControllerTypes.R1) {
    controller = R1()
} else if (controllerType == ControllerTypes.COLMI_R12) {
    controller = ColmiR12()
}
```

On GATT + CCCD ready (phone-only remote, no glasses to boot the
controller):

```kotlin
DeviceStore.apply("glasses", "controllerConnected", true)
DeviceStore.apply("glasses", "controllerFullyBooted", true)
```

If only `controllerConnected` is set, pairing spins forever. R1 gets
away with that because G2 sets `controllerFullyBooted`. Keyfob does not.

Also land on `dev` (from Keyfob, or rewrite):

```ts
// pairing.ts projectTargetReady
if ((Object.values(ControllerTypes) as string[]).includes(deviceModel ?? "")) {
  // wait on controllerConnected && controllerFullyBooted
}
```

Current `dev` still special-cases `ControllerTypes.R1` only.

### Init sequence (phone → ring)

Minimum to try on the bench:

1. Connect, discover Yawell service, enable notify on `6e400003-…`.
2. Command 3 (battery) → prove the link.
3. Command 1 (set time) — many rings want this after connect.
4. Command 28 with `isPlaying=1`, dummy title `"Mentra"` — try to pin
   the music face.
5. Subscribe; log every 16-byte notify. Confirm id 29 on tap/swipe.

Heartbeat: R1/Keyfob ping; R12 may need periodic command 3 or 28 so the
ring does not drop the music face. Measure idle timeout on device.

Reconnect: persist BLE address (Android) / UUID (iOS), same as Keyfob.
Do **not** bond in system Settings.

### Dual connection with glasses

The phone already holds a GATT session to glasses (Live / G1 / G2 / …).
Adding the ring is a second central-role connection, which is what R1
and Keyfob already do. Android/iOS allow several concurrent GATT
centrals. Risk is radio contention and iOS background restoration with
two peripherals — already a known R1 class of bug, not new physics.

Do **not** tell the R12 to advertise to the glasses (R1’s `advStart` +
glasses MAC). The R12 has no G2 pairing role.

---

## 6. Risks and open items (need a ring on the desk)

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| Music face silent unless QRing sent command 28 | v1 has no input | Bench: notify log with/without 28 |
| Media actions are 5 enums, not raw swipe/tap | Mapping is lossy | Freeze v1 table after watching the OLED |
| Exclusive GATT | QRing or system bond → Mentra scan finds nothing | Prep copy: forget in QRing and Android/iOS Bluetooth |
| Name prefix unknown | Scan filter wrong | nRF Connect capture; matcher test |
| iOS background | Ring drops when Mentra is suspended | Copy R1/Keyfob UUID reconnect + `bluetooth-central` background mode (already in the app) |
| Vendor firmware change | Command 29 could move | Unofficial disclaimer; pin observed firmware version in logs |
| Health noise | HR/step notifies on the same characteristic | Ignore non-29/3 packets |
| Camera face vs music face | User must be on the right OLED screen | Prep guide: open the media panel |
| Wave / TikTok gesture | Marketing feature, poorly documented | Out of v1 |
| `dev` vs `s3-watch` | Keyfob identity plumbing not on `dev` | Implement R12 on top of that plumbing, or duplicate the ALL/`projectTargetReady` fixes |

---

## 7. What we will not do

- Flash or reverse-engineer RTL8762 firmware.
- Register a `MediaButtonReceiver` / `MPRemoteCommandCenter` steal.
- Keep QRing connected “and intercept.”
- Treat the ring as glasses (`sgcs/`, capabilities, store listing).
- Ship health/sleep/HR in v1 (easy later; unrelated to controller).
- Depend on accelerometer wave for navigation.

---

## 8. Bench checklist (do this before the driver PR)

Physical R12 + nRF Connect (or a 20-line Python `bleak` script):

1. Forget QRing. Confirm the ring advertises. Record the exact name.
2. Connect; list services. Confirm `6e40fff0-…` + notify char.
3. Enable CCCD. Send command 3. Log battery.
4. Navigate the OLED to the media panel. Tap / swipe / long-press.
   Save the notify hex. Expect `1d …` (29) with action 1–5.
5. Send command 28 (`isPlaying=1`, title Mentra). Repeat step 4. Note
   whether the face stays in music mode.
6. Optional: camera face → command 2 `TAKE_PHOTO`.
7. Disconnect, reconnect by address. Confirm notifies still flow.
8. Repeat with glasses already connected to Mentra (radio coexistence).

If step 4 never yields command 29, stop. Either the face is display-only
until QRing enables it (fixable with 28 / another enable byte) or this
SKU does not emit music commands. That is the only feasibility killer.

---

## 9. Recommendation

| Question | Answer |
| --- | --- |
| Feasible as a Mentra controller? | **Yes**, Keyfob-shaped GATT driver |
| Feasible via OS media-key intercept? | **No** |
| Firmware work? | None |
| Miniapp API changes? | None if we stick to R1 gesture names |
| Blocked on? | One hardware capture of command 29 |
| Suggested first PR after capture | Protocol object + Android driver + pairing row; iOS in the same PR if the capture is unambiguous |
| Product status | Unofficial, same disclaimer style as Keyfob |

Compared with the Keyfob: **less firmware work, more vendor-protocol
risk, same Mentra integration surface.** The Keyfob success transfers
to the phone side (identity, GATT client, `touch_event`, pairing UI).
It does not transfer to “pretend to be a media keyboard.”
