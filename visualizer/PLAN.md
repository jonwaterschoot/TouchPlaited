# TouchPlaited Visualizer — Plan

A web app that mirrors the physical device live while screen-recording: pads light up
when touched, knobs rotate, faders move, switches flip, the user LED blinks like the
real one, and labels (P0, S35, TIMBRE…) appear contextually. Built so the same
codebase can later become an interactive manual and a virtual TouchPlaited.

> **Status (2026-07-24): built and hardware-verified.** This file now serves
> as design/protocol reference (§1-4) rather than a forward-looking plan —
> the milestone list and feedback-round changelog that used to follow are
> archived at
> [`notesarchive/visualizer-plan-archive.md`](../notesarchive/visualizer-plan-archive.md).
> Still open / not built: **manual mode**, **virtual mode**, and a **Web
> Serial transport** (§4's "Future reuse" below) — no `modes/` folder or
> `transport/serial.ts` exist yet. Also note the physical 128×32 OLED
> hardware bring-up and the on-faceplate OLED screen (`oled-mini.ts` /
> `oled-wide.ts` in `src/panel/`) came after this plan was written and
> aren't described here; see notes.md.

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
| 0x01 | STATE       | pads lo7, pads hi5 · S30–S37 (8×7-bit) · swA, swB · LED (7-bit brightness) · model# · mode flags (bits0-1 mode, bit2 playing, bit3 Arp/Mel sound edit) · seq step · octave+3 · root semitone · rec slot (0x7F = idle) · NoteRec layers · NoteRec mute mask · clock src (0 int, 1 MIDI, 2 CV) · arp flags (bits0-1 latched Arp/Hold/Rec sub-state, bit2 Rec armed, bit3 melodic transport running) · hold kind (0 none, 1 P0+P2, 2 rec entry, 3 layer clear, 4 layer copy, 5 rec save, 6 rec cancel, 7 P0+P1 sound edit) · hold progress (7-bit, fraction toward that hold's threshold, per-stage for kind 1; stays 0 through the announce window at the head of each stage) · hold stage (confirms fired so far — edge-detect a rise for the confirm flash) · hold outcome (kind 3: 1 cleared, 2 empty · kind 7: 1 entered, 2 left) · seq pattern slot (0-based within the genre, the sequencer's own — S35 is behind a pickup) | 30 Hz while anything changed, 2 Hz keep-alive |
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
