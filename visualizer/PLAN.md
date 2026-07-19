# TouchPlaited Visualizer — Plan

A web app that mirrors the physical device live while screen-recording: pads light up
when touched, knobs rotate, faders move, switches flip, the user LED blinks like the
real one, and labels (P0, S35, TIMBRE…) appear contextually. Built so the same
codebase can later become an interactive manual and a virtual TouchPlaited.

## 1. Transport decision: USB MIDI + SysEx telemetry

**Recommendation: use USB MIDI as the only transport, with device→host telemetry
encoded as SysEx.**

Why, given the alternatives:

- The firmware's USB port runs **either** USB MIDI (`-DUSB_MIDI`, the current default
  build) **or** USB serial logging / CPU meter — never both ([Makefile](../Makefile)
  line 14, [midi_io.h](../midi/midi_io.h)). Choosing serial for telemetry would mean
  no USB MIDI during recording, killing the "demonstrate MIDI features" scenario
  (TRS MIDI exists but only on hardware-modded boards).
- With MIDI-only, one cable does everything: telemetry out (SysEx), MIDI demo in
  (the browser or a DAW can send notes/CC to the device), and the device's own
  note/clock output is visible to the visualizer for free — sequencer events can be
  displayed without any extra telemetry.
- The browser side needs no native helper: **Web MIDI API** (Chrome/Edge, SysEx
  permission prompt on first use) works today. Web Serial exists too, so a serial
  transport can be added later without redesign — the app treats transports as
  plugins (§4).
- Bandwidth is a non-issue: a full state frame is ~25 bytes; at 30 Hz that is
  <1 KB/s against USB MIDI's ~3 MB/s.

**Windows caveat**: classic WinMM MIDI is single-client — if the visualizer holds the
device's MIDI port, a DAW can't open it simultaneously. Mitigations, in order of
preference:
1. Demo MIDI *from the browser app itself* (virtual keyboard / CC sliders in the
   visualizer — also the seed of the future "virtual device" mode).
2. [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) + a small
   forwarding toggle in the app.
3. TRS MIDI input on a modded board.

**Fallback** (if SysEx proves troublesome): telemetry over USB serial CDC with the
same frame payload, MIDI demos via TRS. The transport abstraction makes this a
swap, not a rewrite.

## 2. Telemetry protocol (SysEx)

Manufacturer ID `0x7D` (educational/non-commercial). Frame layout, all payload
bytes 7-bit:

```
F0 7D 54 50 <ver> <type> <payload…> F7        ("TP" = 0x54 0x50)
```

| type | name        | payload                                                        | when |
|------|-------------|----------------------------------------------------------------|------|
| 0x01 | STATE       | pads lo7, pads hi5 · S30–S37 (8×7-bit) · swA, swB · LED (7-bit brightness) · model# · mode flags (bits0-1 mode, bit2 playing, bit3 Arp/Mel sound edit) · seq step · octave+3 · root semitone · rec slot (0x7F = idle) | 30 Hz while anything changed, 2 Hz keep-alive |
| 0x02 | EVENT       | event id + arg (pad down/up, patch blink start, mode change)   | on occurrence |
| 0x03 | HELLO/CAPS  | firmware version, feature bits                                 | on host request `0x7E` |
| 0x04 | FX          | drive · reverb · delay · nTrims (trims reserved)               | ≤10 Hz on change |
| 0x05 | KIT         | nSlots(7), then per drum slot: engine · harmonics · timbre · morph · decay · MIDI note | ≤10 Hz on change, 2 s keep-alive |

Design rules:
- **Full-state frames, not deltas**, as the baseline — the visualizer can join
  mid-stream and OBS restarts are harmless. EVENT frames only for things that need
  sample-accurate feel (pad touch flashes, LED blink starts).
- Versioned (`<ver>`) so the manual/virtual-device modes can extend it later.
- Host→device: reuse the *same* framing for future virtual-mode control
  (`0x11 SET_STATE` etc.) — nothing to design now, just don't preclude it.

### Firmware work required

- [`MidiIO`](../midi/midi_io.h) TX queue holds max-3-byte messages; add a SysEx
  path (either a second byte-stream queue or chunked OutMsg). TX happens in
  `Service()` on the main loop, so a ~25-byte frame is fine.
- New `midi/telemetry.{h,cpp}`: snapshots pads bitmask, `Knobs` S30–S37 values,
  `Switches` A/B, LED brightness, model/mode/seq state; rate-limits to 30 Hz on
  change + 2 Hz heartbeat. Read-only observer — no audio-path impact. Compile
  it unconditionally with `USB_MIDI`; cost is negligible.

## 3. Panel graphic: the SVG is usable — no Illustrator relayering needed

[img/simpletouchdrawing_blank.svg](../img/simpletouchdrawing_blank.svg) (26 KB)
already has one object per interactive part. Rather than renaming layers in the
.ai file, we make a **one-time cleaned copy** `visualizer/assets/panel.svg` with
stable IDs, and treat the .ai as the visual source of truth. If the artwork
changes later, re-export and re-apply IDs with a small script (match by geometry).

Inventory found in the current export:

| Element | In SVG today | Target ID |
|---|---|---|
| 6 knobs | anonymous `<g>` pairs (circle r=9.92 + pointer line); top row y≈126: x=49.8, 94.0, 138.3, 182.5 → S31–S34; bottom y≈175: x=49.8 → S30, x=182.5 → S35 | `knob-s30` … `knob-s35` |
| 2 faders | handle rects at x=13.1 (S36 VOL) and x=214.7 (S37 WIDTH/MODMIX); tracks are cut into `Faceplate_main_shape` | `fader-s36`, `fader-s37` |
| 10 pad shapes | inside `<g id="PADS">`, anonymous paths (plus a few degenerate stray paths to delete) | `pad-p0`, `pad-p1fx`, `pad-p2` … `pad-p9` |
| P10 / P11 | **not drawn** — they're the top corners; add two simple shapes | `pad-p10`, `pad-p11` |
| user LED | `<g id="userLED">` ✔ (second, unnamed LED group nearby = the R LED) | `led-user`, `led-r` |
| switches | red rects + arrow groups `SW2` and `SW21` (note: `SW21` is mislabeled `data-name="SW2"` but sits over SW1) | `sw1`, `sw2` |
| faceplate | `Faceplate_main_shape` ✔ | keep |

Rendering approach: inline the SVG into the DOM (fetch + inject), then bind by ID:
- **Pads**: toggle a CSS class → fill/glow highlight; label callout appears near
  the shape.
- **Knobs**: `transform: rotate()` on the knob group around its circle center
  (−150°…+150° for 0…1). Centers are known from the geometry above.
- **Faders**: `translateY` of the handle rect along its track (track extent from
  the faceplate slot: y≈122–193).
- **LED**: fill opacity/color from the telemetry brightness byte — this reproduces
  blink patterns exactly instead of re-implementing them.
- **Labels**: an overlay `<g>` the app draws itself (name + current value + panel
  label like TIMBRE/HARMONICS), so artwork stays clean.

## 4. App architecture (reusable)

Vite + TypeScript, no framework (or Preact if UI grows). Everything hangs off a
single observable `DeviceState`; transports and views are plugins around it.

```
visualizer/
  PLAN.md               ← this file
  index.html
  src/
    core/
      state.ts          DeviceState: pads[12], controls S30–S37, swA/B, led,
                        model, mode, seqStep + subscribe()
      protocol.ts       SysEx frame encode/decode (shared with future virtual mode)
      controls-meta.ts  names/labels/CC map per control (P0…P11, S30…S37, panel
                        legends TEMPO/DECAY etc.) — single source for all modes
    transport/
      transport.ts      interface: connect(), onFrame, send()
      midi.ts           Web MIDI implementation (SysEx in, notes/CC both ways)
      mock.ts           scripted/random state for development without hardware
      (serial.ts)       later, Web Serial — same interface
    panel/
      panel.ts          SVG loader + ID binding
      bindings.ts       state→visual (rotate/translate/highlight/LED)
      labels.ts         callout overlay
    modes/
      visualize.ts      tutorial overlay mode (default)
      (manual.ts)       later: hover/click a control → doc popup from MANUAL.md
      (virtual.ts)      later: drag knobs / click pads → send MIDI or SET_STATE
    ui/
      toolbar.ts        connect button, transport picker, overlay options
  assets/
    panel.svg           cleaned+ID'd copy of img/simpletouchdrawing_blank.svg
```

### Screen-recording integration

- Serve locally (`npm run dev`), capture the browser window — works everywhere.
- **Better: OBS Browser Source.** Add a `?transparent` query flag that removes the
  page background so the panel floats over the footage as an overlay layer. OBS
  browser sources support Web MIDI (Chromium), so no visible browser window at all.
- A `?zoom=pads|knobs|all` flag to frame just the region being taught.

### Future reuse (kept cheap by the above)

- **Interactive manual**: `modes/manual.ts` + anchor map from control ID →
  MANUAL.md section. Static hosting (GitHub Pages) since Web MIDI is optional there.
- **Virtual device**: `modes/virtual.ts` makes panel elements input sources;
  `protocol.ts` already encodes both directions. Could drive the real device over
  MIDI, or later a WASM Plaits for a fully virtual unit.

## 5. Milestones

1. ✅ **Panel asset** — `assets/panel.svg` generated by `tools/build-panel-svg.mjs`
   (stable IDs, strays removed, SW groups fixed, P10/P11 shapes added).
2. ✅ **App skeleton** — Vite+TS scaffold, `DeviceState`, mock transport (Demo
   button / `?demo`), panel bindings, label overlay, status chip. Verified
   visually via headless screenshots.
3. ✅ **Firmware telemetry** — `MidiIO::SendSysexUsb`, `midi/telemetry.{h,cpp}`,
   STATE (30 Hz on change + 500 ms heartbeat) and FX frames; LED shadow via
   `set_led()`. Compiles clean. EVENT/HELLO/REQUEST reserved for v2 — the
   heartbeat makes the connect round-trip unnecessary.
4. **Live link** — `transport/midi.ts` is written (auto-pick, reconnect via
   statechange); needs the first end-to-end test with the flashed device:
   verify pad/knob mapping, switch orientation (flip in `controls-meta.ts` if
   mirrored), and LED blink fidelity.
5. **Recording polish** — transparent/OBS mode, zoom presets, label styling,
   LED fidelity pass.
6. *(later)* Manual mode · virtual mode · Web Serial transport.

## 6a. Feedback round 2 (queued 2026-07-14 — ✅ built same day, pending hardware re-test)

All five items implemented; app verified against the scripted demo via headless
screenshots (note attach, condensed model line, FX result labels, drawer close,
fixed info box). Item 3 needs a firmware reflash: the STATE frame grew 16 → 18
payload bytes (octave+3, root semitone — append-only, old frames still parse).
Original feedback with diagnoses:

1. ✅ **Pad note info lags one touch behind** (shows on the *next* touch while
   still holding the previous pad). Cause: the device's NoteOn goes out
   immediately (audio ISR → queue) but the pad-down arrives via the STATE
   frame, rate-limited to 33 ms — so when the note reaches `labels.ts`,
   `heldPads` is often still empty and the note is dropped; it only attaches
   when a *pad is already held*. Fix in `labels.ts`: buffer the last
   unattached ch1/ch10 note (+timestamp); on a pad-down event within ~300 ms,
   attach the buffered note to that pad. Keep the existing note-after-pad
   path too.

2. ✅ **Info screen ("the screen") cleanups:**
   - Model-select shows double info: the `P0 + S35 · Model select · bank 0 ·
     <name>` line is followed by a separate `Model → <name> #n` line. Fix:
     when the newest log entry is a model-select combo, let the `model` event
     *update that entry* (append `#n`) instead of adding its own line — one
     condensed line.
   - Fixed box size: the panel currently grows/shrinks with content on every
     update. Give it a fixed width (~30ch, still font-scaled by the grip);
     remove `white-space: nowrap` from `.action-line` so long lines wrap;
     `min-height` reserving ~4 log lines so height is stable too.
   - Remove the persistent FX chip entirely — obsolete now that FX moves
     appear as `P1 + S30/S35` lines in the log and callouts. (Keep decoding
     the FX frame: its values feed item 4's display and the fx-layer label
     context.)

3. ✅ **On-screen pad MIDI must follow the device mode** — currently always
   sends ch10 drum notes, even in Basic Pitch. In pitched modes clicks should
   send ch1 notes matching what the *device* would play. The app lacks
   `root_semitone` and `octave_offset` (not in telemetry), so: extend the
   STATE frame from 16 → 18 payload bytes with `octave_offset + 3` (0..6) and
   `root_semitone` (0..11) — append-only, guarded by `p.length >= 18` in
   `protocol.ts`, no version bump needed. Then `interact.ts` computes, per
   TouchPlaited.cpp `compute_note()`: `note = kPitchBase + root + kScales
   [scale][pad-3] + octave*12`, with the scale from SW1 (`swA`) and kScales
   copied into `controls-meta.ts` (minor {0,2,3,5,7,8,10} / chromatic
   {0,1,…,6} / major {0,2,4,5,7,9,11}; check `kPitchBase` in the firmware
   when implementing). Seq mode keeps the GM drum notes.

4. ✅ **Reverb/delay values should show the *result*, not the %.** These use the
   center-off encoding (MANUAL "MIDI in"): 64 ≈ off; below = character A,
   above = character B, wet grows with distance from center. Display, e.g.:
   reverb → `Off` (near center) / `Room 45%` / `Hall 60%`; delay → `Slapback
   30%` / `Dotted ⅛ 70%` (wet = |v−center|×2). Apply in three places: the
   P1+S30/S35 callouts and action-log lines (`labels.ts`, using the FX-frame
   values), and the CC 85–88 value readouts in the drawer (`ccpanel.ts`).

5. ✅ **MIDI drawer has no collapse control once open** — the drawer overlays
   the topbar toggle. Add a header row inside the drawer with an `×` close
   button (and keep the toolbar button as a toggle).

## 6c. Feedback round 3 — mobile & touch (2026-07-14, ✅ built same day)

1. ✅ **Touch-screen quirks** (laptop touch test): tap-highlight rectangles
   around the SVG shapes and long-press → context menu on the pads. Fixed via
   `-webkit-tap-highlight-color: transparent`, `user-select: none`,
   `touch-action: none` on the panel SVG, and `contextmenu` preventDefault on
   the panel + piano.
2. ✅ **Pad callouts pushed content off-screen in portrait** (e.g. holding P7
   shoved the MIDI drawer out of the viewport). Callouts are now clamped
   inside the overlay and `#stage` clips overflow, so nothing can widen the
   page/visual viewport anymore.
3. ✅ **Mobile space use**: on phone-sized viewports (`max-width: 820px` or
   `max-height: 500px`) the info panel auto-places into the letterbox space —
   below the device in portrait, beside it in landscape (drag/double-click
   still override; desktop default unchanged). The panel SVG now fills the
   stage both ways and letterboxes via preserveAspectRatio; topbar wraps on
   narrow screens; drawer width caps at 88vw.
4. ✅ **Fullscreen button** in the toolbar (hidden where the API is missing,
   i.e. iPhone Safari).

Verified via headless screenshots at phone portrait/landscape and desktop
sizes. Heads-up for future testing: Edge headless silently enforces a
~500 px minimum window width — screenshots requested narrower are a crop of
a 500 px viewport, which looks like (but is not) a layout overflow.

## 6d. Feedback round 4 — layout & jitter (2026-07-14, ✅ built same day)

1. ✅ **Movable/resizable device**: drag the drawing with one pointer (pads
   still play), pinch with two, mouse-wheel to zoom, double-click to reset —
   persisted like the info panel (`panel/layout.ts`).
2. ✅ **Topbar → hamburger menu**: one ☰ button opens a dropdown with
   Connect MIDI / Demo / Fullscreen / MIDI panel and the link status — no
   more overflow when the MIDI port name is long. Future entries (manual
   mode, repo link…) hook in via `Toolbar.addMenuItem()`. `?menu` opens it
   for screenshots. The topbar floats over the stage (no layout row), so the
   stage owns the full window and a dragged-up device isn't clipped.
3. ✅ **Knob jitter**: pot noise is ±1 LSB after libDaisy AnalogControl's
   one-pole, which kept STATE frames + callouts + the action log churning.
   Fix at the source, not with more smoothing (that only adds lag): the
   firmware telemetry reports a knob only when it moves ≥2 LSB from the last
   reported value (rails exempt so 0/127 stay reachable) — audio paths are
   untouched. The app store's CONTROL_EPS was raised to 1.6 LSB as a second
   line of defense (oscillation never accumulates, deliberate turns do).
   **Needs the same firmware reflash as §6a item 3.**

## 6b. Decisions (answered 2026-07-13)

- Boards: both exist, but the **non-TRS Simple Touch is the demo unit** — so the
  Windows single-client workaround is the in-app MIDI demo (browser sends the
  MIDI), with loopMIDI only if a DAW must be on screen too.
- FX values (sends / per-slot trims) **are shown when appropriate**: they travel
  in a separate `FX_STATE` (0x04) frame sent on change, and the UI surfaces them
  contextually rather than permanently.
- LED: raw brightness at 30 Hz in v1; revisit with EVENT blink-ids only if
  recordings show aliasing.


