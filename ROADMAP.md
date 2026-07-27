# TouchPlaited — Roadmap

**v1 stable — 2026-07-08.** The numbered implementation steps that built it
(Steps 1–15: SW2 mode restructure → recording redesign → soft-clip output →
mode memory → background drum seq → voice expansion + sleep + shed guard →
MIDI notes/CC/clock) are archived verbatim in
`notesarchive/roadmap_v1_archive.md`. The workflow story since the very first
prompt is in `notesarchive/readme.md`.

Design decisions, budget analyses, and implementation write-ups live in
`notes.md`, with resolved/historical material moved to `notesarchive/` once
it's superseded (most recently `notesarchive/notes_archive_2026-07.md`).
**This file is the single owner of future work** — ideas and priorities live
here and nowhere else; a roadmap item links to its analysis when one exists.

**Six-Op gate fix — complete, hardware-verified 2026-07-24.** Real gate
semantics, per-note level pad, anti-click, LED blink, unified Decay routing,
and three app UI/telemetry rounds — full step-by-step write-up archived in
`notesarchive/notes_archive_2026-07.md` → "Priority 1 — Six-Op gate fix,
full write-up". Outstanding follow-ups moved to Priority 3 below.

---

## Housekeeping

- [ ] **File the SSD1306 driver fix upstream.** Two bugs found during OLED
      bring-up (wrong column-start address on 128×32 panels; one I2C
      transaction per byte instead of per page) are fixed locally inside the
      `lib/libDaisy` submodule (commit `62ab175`), but that submodule tracks
      `Synthux-Academy/libDaisy`, which we don't have write access to — so the
      fix isn't pushed anywhere upstream yet. Open a PR against
      `Synthux-Academy/libDaisy` with the diff (`git log -p 62ab175` inside
      the submodule). Affects any 128×32 SSD1306 use on their fork, not just
      this project. Full writeup: `notesarchive/notes_archive_2026-07.md` →
      "SSD1306 128×32 OLED".

## Priority 2 — Seq patterns: authoring & a less-repetitive feel

- [ ] **Manually author more patterns with `tools/pattern_editor.html`.** S35
      already selects a variant within the SW1 genre (`Sequencer::SetVariant`,
      `synth/sequencer.h`); the bottleneck is content, not plumbing. Current
      patterns on disk (`synth/patterns/<genre>/`), track progress here:
      - Electro (6): `00_classic`, `01_brute`, `02_offbeat`, `03_claptrap`,
        `04_sizzle`, `05_burst`
      - IDM (3): `00_glitch`, `01_fullrandom`, `02_stomp`
      - Techno (2): `00_fourfloor`, `01_rolling`
      - [ ] Add more Techno variants (thinnest genre right now, 2 vs. 6/3)
      - [ ] Add more IDM variants
      - [ ] Add more Electro variants
      - [ ] Re-run `tools/gen_patterns.py` after each addition, hardware-test
            the new variant via S35 sweep
- [ ] **Idea to track/iterate — a less repetitive feel, and freeing up S34.**
      The original point of per-track pattern variants was to break up
      repetition; good use of the existing per-step **chance** feature can
      get most of that without hand-authoring dozens of variants. Separately,
      S34 in Seq mode is currently **Kick punch** (`seq_punch_lk`,
      `TouchPlaited.cpp:1849` — a timbre boost applied only to the kick slot,
      `TouchPlaited.cpp:346`) and doesn't feel very impactful in practice.
      Better fix for "kick needs more punch": lean on the drum-kit
      selector/curation to offer more kick models/presets, which frees S34
      for pattern-feel duty instead. Open problem: S34 shouldn't just be a
      single global chance multiplier stacked on top of each step's existing
      per-note chance — need a mapping that actually reads as varying the
      pattern (e.g. scaling mutation depth, or biasing which steps are
      eligible) rather than a flat "more/less random" fader. Not designed
      yet — needs iteration before implementation.
- [ ] **Density + chance axis** — S32 split: 0–0.45 = less density, 0.5 = normal, 0.55–1 = chance/mutation per step. Changes seq tick logic.
- [x] **LED blink on model load** — already implemented (`LedEvent::MODEL` fires in `process_model_select` on every engine change; discovered while adding the Six-Op patch-index blink (Six-Op gate fix Step F, `notesarchive/notes_archive_2026-07.md`), which reuses the same blink).

## Priority 3 — feature additions

- [ ] **MIDI drum pitch phase 2** — ch10 notes within ±6 of a slot's GM anchor play that slot transposed.
  **What ch10 does today** (MANUAL.md:471-483, "Channel 10 — drums" table): each of the 7 kit slots accepts a small fixed set of GM note numbers (e.g. Tom accepts 41, 43, 45, 47, 48, 50). Any accepted note triggers that slot at its stored pitch — "exactly like tapping the pad." Notes outside the table are ignored entirely. There's no transposition logic at all right now; it's a lookup table, not a pitch mapping.

  **What "phase 2" would add**: actual transposition — a note within ±6 semitones of a slot's GM anchor (the bold note, e.g. 45 for Tom) would play that slot pitched up/down by the offset, instead of only the handful of literal notes in the table always sounding at one fixed pitch. That's a real, non-trivial feature (touches the MIDI-in note handler and the per-slot pitch path), not just documentation cleanup — it isn't implemented or partially implemented anywhere, and MANUAL.md's table explicitly documents current behavior as fixed-pitch.

- [ ] **Chiptune engine (7) — bring into manual selection** (decided 2026-07-24, resolves the old "Open decisions" item — the roadmap text there was stale: it's not just excluded from the random pools, `process_model_select` skips it out of the P0/P2+S35 quantizer bank entirely — `TouchPlaited.cpp:1337-1339` maps bank-0 index 7 straight to engine 8). Fix: stop the skip so it's reachable at position 7. Map its knobs to the standard layout, same convention as every other engine — S30 drive, S31 decay, S32 harmonics (chord select), S33 timbre (arpeggiator pattern/range), S34 morph (waveform shape). Caveat found while scoping: `ChiptuneEngine::Render` (`thirdparty/plaits/dsp/engine2/chiptune_engine.cc`) always takes the "clocked" single-arp-note path here — the host always patches the trigger (`plaits_voice.cpp`: `trigger_patched = true`) — and the engine never calls `set_envelope_shape`, so `already_enveloped` stays true and S31 Decay currently has no audible effect there; decide during implementation whether Decay should gate the arp note's length, or whether the (currently unreachable) chord-pluck path should be exposed instead. Still stays out of the random pools — self-running, no gate response.

 ## OLED screen
— done, see Housekeeping (upstream driver PR still open) and `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED".

**Parity with the visualizer — status (2026-07-24):**

- Text content (label/value strings): `display/oled_ui.cpp`'s `describe_control()`/
  `describe_pad()` is a line-by-line port of `visualizer/src/panel/labels.ts`'s
  `describeControl()`/`describePad()`, operating on the same `TelemetryState`
  wire fields the visualizer decodes — verified by inspection, not just belief.
  Three **deliberate** differences, documented in `display/oled_ui.h` and
  `oled_ui.cpp` at the point each happens — not bugs, don't "fix" without
  deciding the UX first:
  - Rec drum-slot editing uses flatter "Slot X" labels vs. the web's full
    per-knob nesting (128×32 char budget).
  - The dead-knob message is "no effect" instead of "no effect on \<model>".
  - **Hardware has no idle/status fallback** — it holds the last-touched
    control indefinitely once the screen has shown anything. The visualizer's
    `oled-mini.ts` reverts to a status row (model/mode/transport) after
    `IDLE_MS` (2.2s) of no input. This is the one open behavioral gap: either
    port the no-fallback behavior into the visualizer, or accept the
    visualizer as intentionally friendlier here — pick one and note it.
- Rendering (fonts/pixels): fixed 2026-07-24 — `oled-mini.ts` now blits the
  actual firmware bitmap fonts (`visualizer/src/panel/oled-font-data.ts`,
  ported from `lib/libDaisy/src/util/oled_fonts.c`) with the same discrete
  Font_11x18 → Font_7x10 → Font_6x8 stepping `display/oled_screen.cpp`'s
  `ShowLine()` uses, instead of a system font rasterized then thresholded.
  Small text was illegible before this — see
  `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED" for the
  bring-up bugs that motivated the overhaul.

- [ ] **Manually check on real hardware** once it's in hand — code-level parity
  above is as far as inspection can verify; this is what's left to confirm
  against an actual panel. Go mode by mode, trigger each row below, and
  compare the physical screen to the visualizer's OLED side by side:
  - [ ] Seq: idle (hardware: last-touched only), knob turn (incl. tempo as
    BPM, ext-clock "ext"), pad = drum note, SW1 (IDM/Techno/Electro), model
    select (P0/P2+S35), rec-slot editing (flatter labels)
  - [ ] Arp/Mel: knob turn (incl. dead-knob "no effect"), pad = pitched note,
    SW1 (Hold/Arp/Rec), Rec sub-mode knobs (Speed/Shift/Chance/Order), layer
    record/mute messages, model select
  - [ ] Pitch: knob turn (engine-specific labels via `kEngineKnobs`), pad =
    pitched note, SW1 (Minor/Chromatic/Major), model select
  - [ ] Cross-mode: SW2 flip, P1+FX layer (reverb/delay, drums vs. pitched
    wording), P0+S37 stereo width, external clock (MIDI vs. CV wording)
  - [ ] Held-combo progress bar + confirm flash (added 2026-07-24, needs the
    same real-hardware check as everything above): hold P0+P2 (Seq:
    re-randomize; Arp/Mel: vary sound — 2 stages, or 3 in Pitch mode, each
    with its own "Stage N" flash), hold a drum pad P3–P9 (rec entry →
    "Recording"), P2+drum-pad hold in Rec (layer clear → "Cleared"/"Empty"),
    and the copy-confirm hold while recording ("Copied") — bar should track
    the LED's blink rate, confirm flash should land the same instant the LED
    flashes/switches pattern.

**Held-combo progress bar — implemented 2026-07-24, confirm flash added same
day.** `TelemetryState` gained `hold_kind`/`hold_progress`/`hold_stage`/
`hold_outcome` (`midi/telemetry.h`, SysEx STATE payload bytes 23-26,
`visualizer/PLAN.md` §2 + `protocol.ts` updated in lockstep).
`TouchPlaited.cpp`'s `compute_hold_telemetry()` mirrors the LED loop's own
precedence exactly (P0+P2 hold > rec entry > layer clear > layer copy) so the
number on the wire never disagrees with what the LED is already blinking.
`OledScreen::ShowProgress()` / `oled-mini.ts`'s `showProgress()` draw the same
outlined-then-filled bar geometry on both sides, and a hold owns the screen
unconditionally while active (`OledUi::Service` returns early on `hold_kind
!= 0`; `oled-mini.ts`'s `show()` no-ops while `progressMode` is set) — no
knob/pad callout can steal the bar mid-hold on either side.

Each threshold crossing gets a distinct confirm: `hold_stage` counts
confirms fired so far for the current hold (0 while building, then
increments and stays there for as long as the gesture stays held) — both
sides edge-detect a rise against the previous frame and swap the bar for
text (`ShowLine`/`confirmFlash()`, held open ~220ms — `kConfirmFlashMs` /
`CONFIRM_FLASH_MS`) before releasing back to the bar or the normal chain.
P0+P2 is genuinely multi-stage (2 segments normally, 3 in Pitch mode) — its
progress is rescaled *per stage* so every segment fills its own 0..100%
independently, fixing a real bug where it used to stall around 67% in the
common 2-stage case (was scaled against the 3-stage-only 750-block ceiling).
Each stage flashes "Stage N"; the other three gestures are single-stage
("Recording", "Copied", "Cleared"/"Empty"). Layer clear's `hold_outcome`
distinguishes a real clear from a no-op (holding a pad with nothing
recorded) — the LED already tells these apart (NUMBERED vs LIMIT blink) so
showing "Empty" as if it were a success would have been actively
misleading. `entry_just_confirmed`/`copy_just_confirmed`/`p2layer_outcome[]`
(`TouchPlaited.cpp`) exist because rec-entry and layer-copy reset their hold
counters in the same ISR call that fires them — a level-polling read would
never observe the exact completion frame without a latched flag.

Deliberately **not covered**: the rec-pad confirm hold (`rec_hold_count`) and
the P0+P1 sound-edit toggle (`se_hold_count`) — unlike the four gestures
above, neither has an existing LED build-up animation to key off (both fire
a single confirm blink with no preceding countdown), so there's no
established "this is what building-up looks like" to extend. Give them one
first if they're worth a bar too.


## Parking Lot - performance 

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
