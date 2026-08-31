---
status: draft
owner: Arusia
---

# Colmi R12 as a Mentra controller — feasibility

**Verdict: Yes, as a phone-paired BLE controller on the Keyfob/R1 path.
No, if “intercept media keys in the Mentra App” means OS HID / AVRCP /
`MediaSession` steal.**

The ring’s media panel does not talk to Android or iOS as a keyboard or
headset. It notifies a 16-byte Yawell GATT packet whose **leading opcode
on the R12 is `0x0B` (11)**. Older QRing docs call the same role command
29 (`0x1D`). QRing turns that notify into system media keys. Mentra should
skip QRing, consume `0x0B` (and `0x1D` if it also appears), then emit the
same `touch_event` strings miniapps already handle.

This is the same *role* as the XIAO nRF Keyfob (`origin/s3-watch`,
`notes/adding-a-controller.md`). It is **not** the same *protocol*: we do
not own R12 firmware.

Hardware capture is still required before writing the driver: we need the
bytes *after* `0x0B` (action / gesture) from a real R12. The leading opcode
is treated as known. Buy the ring; sniff with Android HCI snoop or nRF
Connect. A dedicated nRF USB sniffer is optional. See §9.

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

Advertised name (confirmed by PulseLoop’s pairing catalog, not a guess):
`COLMI R12_<hex serial>`, regex `^COLMI R12_.*`. Mentra’s scan matcher
should use that prefix. Do not use the older R02 `R0n_xxxx` regex.

Community note: the ring is often invisible if it is already bonded to
QRing or another central. Mentra should own the only GATT session.

---

## 3. PulseLoopAndroid — what it records, what we can reuse

Source: [foureight84/PulseLoopAndroid](https://github.com/foureight84/PulseLoopAndroid)
(Kotlin port of PulseLoop iOS). It is a **health companion**, not a
controller app. The Colmi R12 is a first-class catalog model
(`WearableModel.COLMI_R12`) on the **same** `ColmiDriver` as R02/R10.
There is no R12-specific opcode fork.

### Decoded outputs (typed `RingDecodedEvent` / `PulseEvent`)

These are what PulseLoop *understands*. None of them are taps, swipes,
or media keys.

| Wire | PulseLoop event | Mentra controller use |
| --- | --- | --- |
| `0x03` battery; `0x73 0x0C` battery notify | `Battery(percent)` | Yes — `controllerBatteryLevel` |
| `0x69` type 1 / `0x1E` | `HeartRateSample` | Ignore |
| `0x69` type 3 | `Spo2Result` | Ignore |
| `0x73 0x12` live activity (BE u24 steps/kcal/m) | `ActivityUpdate` | Ignore |
| `0x15` / `0x37` / `0x39` / `0x43` history | HR / stress / HRV / steps history | Ignore |
| `0xBC` big-data (sleep `0x27`, SpO₂ `0x2A`, temp) | `SleepTimeline`, history | Ignore |
| `0x3C` device support | `supportBlePair`, `supportIntervalTemp` | Read, do **not** bond (R12 is GATT-only in PulseLoop; only R09/R11 set `requiresOsBond`) |
| anything else with a valid checksum | `CommandAck(commandId)` | **This is where R12 media-panel `0x0B` (and legacy `0x1D`) would land** |

PulseLoop does **not** define Colmi opcodes for music (`0x0B` / `0x1C` /
`0x1D`), camera (`0x02`), or gestures. `DISPLAY_PREF` (`0x05`) is a
constant only; the encoder never sends it. `ColmiCoordinator.capabilities`
is HR / SpO₂ / steps / sleep / battery / stress / HRV / temp / find-device
— no input surface.

**Do not confuse PulseLoop `0x0B`.** In PulseLoop that opcode is
**jring / 56ff battery** (`RingDecoder.decodeBattery`), a different
20-byte KeepFit packet. Colmi frames are 16 bytes on the Yawell UART.
An R12 media notify that starts `0B …` will show up in PulseLoop’s Colmi
path as `command_ack` with `commandId: 11`.

So we cannot “use PulseLoop’s gesture values.” They never decoded any.

### Raw packet trace (this *is* usable)

Every GATT notify is also stored as `RawPacketEntity`:

```
commandId  = first byte
hexPayload = full 16-byte frame
decodedKind = e.g. "command_ack" for unknown opcodes
```

Debug console: Settings → About → tap version 7× → Developer.
Export: Settings → Privacy & Data → Export Diagnostics. Anonymize-on
keeps non-health frames **whole** (`command_ack` is not in
`HEALTH_KINDS`), so a media-panel tap would survive as hex starting
`0b…` (id 11) even in a privacy-safe export.

**Capture recipe (faster than nRF Connect if PulseLoop is already
installed):** forget QRing, pair the R12 in PulseLoop, open the raw
packet trace, use the OLED media panel, then grep the export for
`"commandId": 11` / hex prefix `0b` (R12 media panel) and also
`"commandId": 29` / `1d` (legacy QRing music). That log is the mapping
table we need. Mentra still must be its own GATT client later — PulseLoop
and Mentra cannot share the ring.

### Protocol values we should copy (not health metrics)

From `ColmiProtocol.kt` / `ColmiPacket.frame` / `ColmiEncoder` /
`ColmiSyncEngine.runStartup` — already cross-checked against QRing:

```
UUIDs:  6e40fff0-… / write 6e400002-… / notify 6e400003-…
Frame:  16 bytes, CRC = sum(bytes[0..14]) & 0xFF in byte[15]
Name:   ^COLMI R12_.*
Bond:   do not createBond() for R12
```

Minimum connect sequence PulseLoop sends (we can drop the health prefs):

1. `0x04` phone name (`02 0A 'P' 'L'` in PulseLoop; Mentra can send `"Mentra"`)
2. `0x01` set time, **including language byte** (0 = zh, 1 = en) — display
   rings show the wrong locale without it
3. `0x3C` device support (optional; tells us capability bits)
4. `0x03` battery

Do not enqueue PulseLoop’s auto-HR / stress / SpO₂ / HRV / temp / goals
/ big-data sync. Those keep the radio busy and are unrelated to being a
controller.

Do not vendor PulseLoop as a dependency. Reimplement the ~20-line
frame + those four commands in `ColmiR12Protocol.kt`.

---

## 4. The media panel is GATT, not HID

### What the ring sends (`0x0B` leading)

R12 media-panel notifies are expected to start with **`0x0B`** as the
Yawell command id (first of the 16 payload bytes, not an ATT opcode).

That slot is a hole in the published Oudmon/QRing enum
([ATC_RF03 #13](https://github.com/atc1441/ATC_RF03_Ring/issues/13)):
the dump jumps from `CMD_GET_TIME_SETTING = 10` (`0x0A`) to
`CMD_BP_TIMING_MONITOR_SWITCH = 12` (`0x0C`). colmi-docs has the same
gap (settings id 10 then 12). A later display-touch firmware adding
command 11 for the OLED media face is the straightforward explanation.

```c
struct R12MediaPanelNotify {    // leading opcode 0x0B
    uint8_t commandId;          // 0x0B
    uint8_t action;             // layout unconfirmed — see below
    char rest[13];
    uint8_t crc;
};
```

**Action byte is not yet captured.** Until a hex dump exists, try the
same `MediaAction` enum QRing uses on command 29, in `bytes[1]`:

```c
enum MediaAction {
    Pause       = 1,
    Previous    = 2,
    Next        = 3,
    VolumeUp    = 4,
    VolumeDown  = 5
};
```

Also accept **legacy command 29 (`0x1D`)** with that same action byte.
colmi-docs and the Oudmon list still document:

| Command | Direction | Role on R12 |
| --- | --- | --- |
| **`0x0B` (11)** | ring → phone | **Primary: R12 media-panel leading opcode** |
| `0x1D` (29) Music | ring → phone | Fallback if firmware still emits the old music cmd |
| `0x1C` (28) Music Switch | phone → ring | May still be needed to pin the music face |
| `0x02` Camera | both | Shutter (`TAKE_PHOTO = 2`) as another tap |
| `0x03` Battery | both | `controllerBatteryLevel` |

Not the same `0x0B`:

- ATT **Read Response** PDUs also start `0x0B` in a BTSnoop HCI view.
  Mentra’s GATT callback already delivers the *characteristic value*,
  which should start with the Yawell id, not the ATT opcode.
- PulseLoop **jring** `0x0B` is battery on a 20-byte KeepFit frame.

The phone can also **push** now-playing onto the ring (command 28:
playing / progress / volume / 10-char title). Try that if the OLED
leaves the media face without a dummy `"Mentra"` title.

### Why OS-level intercept fails

The idea “pair the ring, let it fire media keys, steal them in Mentra 3.0”
needs the ring (or QRing) to inject `KEYCODE_MEDIA_*` / AVRCP into the OS.

1. **The ring is not a HID device.** No HID-over-GATT (`1812`), no
   BLE HOGP, no classic AVRCP. nRF Connect and Gadgetbridge only show
   the Yawell services above.
2. **QRing is the HID synthesizer.** It holds the GATT session, reads
   `0x0B` / command 29, then dispatches media keys. Mentra cannot be that
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

## 5. Recommended mapping (v1)

Map `0x0B` (and fallback `0x1D`) onto the R1 gesture strings. Miniapps
already subscribe to these; new strings are invisible.

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

## 6. Mentra wiring (copy Keyfob, swap protocol)

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
- Unit tests for CRC, `0x0B` (and `0x1D`) decode, name prefix
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
5. Subscribe; log every 16-byte notify. Confirm leading `0x0B` on tap/swipe
   (also log `0x1D` if present).

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

## 7. Risks and open items (need a ring on the desk)

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| Music face silent unless QRing sent command 28 | v1 has no input | Bench: notify log with/without 28 |
| Media actions are 5 enums, not raw swipe/tap | Mapping is lossy | Freeze v1 table after watching the OLED |
| Exclusive GATT | QRing or system bond → Mentra scan finds nothing | Prep copy: forget in QRing and Android/iOS Bluetooth |
| Name prefix unknown | Scan filter wrong | **Resolved:** PulseLoop catalog `^COLMI R12_.*` |
| iOS background | Ring drops when Mentra is suspended | Copy R1/Keyfob UUID reconnect + `bluetooth-central` background mode (already in the app) |
| Vendor firmware change | Opcode could move off `0x0B` | Unofficial disclaimer; pin observed firmware version in logs; also match `0x1D` |
| Health noise | HR/step notifies on the same characteristic | Ignore non-`0x0B`/`0x1D`/`0x03` packets |
| Camera face vs music face | User must be on the right OLED screen | Prep guide: open the media panel |
| Wave / TikTok gesture | Marketing feature, poorly documented | Out of v1 |
| `dev` vs `s3-watch` | Keyfob identity plumbing not on `dev` | Implement R12 on top of that plumbing, or duplicate the ALL/`projectTargetReady` fixes |

---

## 8. What we will not do

- Flash or reverse-engineer RTL8762 firmware.
- Register a `MediaButtonReceiver` / `MPRemoteCommandCenter` steal.
- Keep QRing connected “and intercept.”
- Treat the ring as glasses (`sgcs/`, capabilities, store listing).
- Ship health/sleep/HR in v1 (easy later; unrelated to controller).
- Depend on accelerometer wave for navigation.

---

## 9. Bench checklist (do this before the driver PR)

Buy an R12. A dedicated sniffer is useful; it is not required.

**Fastest (recommended first):** nRF Connect. Scan `COLMI R12_*`, connect,
enable notify on `6e400003-…`, log every notification while using the
media panel. You get the 16-byte *value* with no ATT-opcode confusion.
PulseLoop Developer raw-packet trace also works (`commandId` 11 / hex
`0b`); it will label the frame `command_ack` because it does not parse
media.

**Also enough:** Android Developer Options → **Enable Bluetooth HCI snoop
log**. Reproduce the gestures, then:

```
adb bugreport /tmp/r12-snoop
```

Unzip the bugreport and open `btsnoop_hci.log` (often under
`FS/data/misc/bluetooth/logs/`). Older phones also write
`/sdcard/btsnoop_hci.log`. Wireshark filter:

```
btatt.handle_value_notification && bluetoothuuid == 6e400003-b5a3-f393-e0a9-e50e24dcca9e
```

You want the 16-byte *value*, not the ATT opcode. Leading `0x0B` here is
Yawell. Leading `0x0B` on the HCI ATT layer is a Read Response — ignore
those.

**Nice to have:** nRF52840 USB dongle running Nordic’s BLE sniffer
(Wireshark). Use it if you also want advertising name, scan responses,
reconnect, and empty-packet timing, or if a phone central is hiding
frames. Do not start with Ellisys-class gear.

### Isolation

1. Uninstall or force-stop QRing. Forget the ring in Android/iOS Bluetooth.
2. One central only (nRF Connect *or* PulseLoop *or* Mentra).
3. Wake the ring (off charger, tap the OLED).

### Labelled takes (one snoop file each, or a voiced timestamp)

Do each gesture three times. Write down wall-clock time next to the action.

| Take | What you do | What to record |
| --- | --- | --- |
| A | Scan only | Advertised name (`COLMI R12_<hex>`), RSSI |
| B | Connect, enable notify, send `0x03` | Battery frame |
| C | Open **media panel**, tap | Full 16-byte notify |
| D | Media panel swipe one way | Full 16-byte notify |
| E | Media panel swipe the other way | Full 16-byte notify |
| F | Media panel long-press / hold | Full 16-byte notify |
| G | Repeat C–F after writing command 28 (`isPlaying=1`, title Mentra) | Does `0x0B` start, or change? |
| H | Camera face shutter, if present | Command `0x02`? |
| I | Disconnect / reconnect by address | Still getting C–F? |

Fill:

```
gesture | time | hex (16 bytes) | leading | byte[1]
tap     |      |                | 0b?     |
swipe A |      |                |         |
swipe B |      |                |         |
hold    |      |                |         |
```

That table is the decoder. `byte[1]` is the remaining unknown. Paste it
back into this spec before starting the driver PR.

If C–F never yield leading `0x0B` (or `0x1D`), stop. Either the face is
display-only until something like command 28 enables it, or this SKU does
not emit media-panel commands. That is the only feasibility killer.

Optional later: same takes with glasses already connected to Mentra (radio
coexistence). PulseLoop cannot be connected at the same time.

---

## 10. Recommendation

| Question | Answer |
| --- | --- |
| Feasible as a Mentra controller? | **Yes**, Keyfob-shaped GATT driver |
| Feasible via OS media-key intercept? | **No** |
| Firmware work? | None |
| Miniapp API changes? | None if we stick to R1 gesture names |
| Blocked on? | One hex dump of `0x0B` + action byte from a real R12 media-panel tap |
| Suggested first PR after capture | Protocol object + Android driver + pairing row; iOS in the same PR if the capture is unambiguous |
| Product status | Unofficial, same disclaimer style as Keyfob |

Compared with the Keyfob: **less firmware work, more vendor-protocol
risk, same Mentra integration surface.** The Keyfob success transfers
to the phone side (identity, GATT client, `touch_event`, pairing UI).
It does not transfer to “pretend to be a media keyboard.”
