---
status: archived
owner: Arusia
---

# Colmi R12 controller implementation plan — cancelled

> Archived. Hardware failed the Phase 0 gate. Do not execute this plan.

**Goal (abandoned):** Mentra App pairs a Colmi R12 as a phone-only BLE
controller and delivers R1-named `touch_event`s from the ring’s music face.

**Why cancelled:** A real R12 has **no swipe**, **no hold time**, and only a
**generic tap at ~1 Hz**. That cannot map onto `swipe_up` / `swipe_down` /
`hold` / `double_tap`. See the spec.

**Spec source of truth:**
`notes/superpowers/specs/2026-08-31-colmi-r12-controller-feasibility.md`

---

## Phase 0: Hardware capture (gate) — failed

- [x] Obtain an R12 and inspect media-panel / touch notifies
- [x] Result: single generic tap, ~1 Hz, no swipe, no hold
- [ ] ~~Record distinct tap / swipe / hold action bytes~~ — **no such bytes**
- [ ] ~~Start Phase 1~~ — **do not**

All later phases (identity, `ColmiR12Protocol`, Android/iOS drivers,
pairing UI, device verification) are **cancelled**. Do not add
`ControllerTypes.COLMI_R12`.
