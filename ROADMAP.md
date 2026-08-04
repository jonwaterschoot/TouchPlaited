# TouchPlaited — Roadmap

**v1 stable — 2026-07-08.** The numbered implementation steps that built it
(Steps 1–15: SW2 mode restructure → recording redesign → soft-clip output →
mode memory → background drum seq → voice expansion + sleep + shed guard →
MIDI notes/CC/clock) are archived verbatim in
`notesarchive/roadmap_v1_archive.md`. The workflow story since the very first
prompt is in `notesarchive/readme.md`.

Design decisions, budget analyses, and implementation write-ups live in
`notes.md`, with resolved/historical material moved to `notesarchive/` once
it's superseded (most recently `notesarchive/notes_archive_2026-08.md`).
**This file is the single owner of future work** — ideas and priorities live
here and nowhere else; a roadmap item links to its analysis when one exists.

**Six-Op gate fix — complete, hardware-verified 2026-07-24.** Real gate
semantics, per-note level pad, anti-click, LED blink, unified Decay routing,
and three app UI/telemetry rounds — full step-by-step write-up archived in
`notesarchive/notes_archive_2026-07.md` → "Priority 1 — Six-Op gate fix,
full write-up". Outstanding follow-ups moved to Priority 3 below.

---

## Housekeeping

- [ ] **File the SSD1306 driver fix upstream, against `electro-smith/libDaisy`
      (not the `Synthux-Academy` fork).** Two bugs found during OLED
      bring-up (wrong column-start address on 128×32 panels; one I2C
      transaction per byte instead of per page) are fixed locally inside the
      `lib/libDaisy` submodule (commit `62ab175`) — still just pinned there,
      not pushed anywhere. Rechecked 2026-08-04: nothing was lost in the
      libDaisy cleanup, and the bug is confirmed upstream at
      [electro-smith/libDaisy#634](https://github.com/electro-smith/libDaisy/issues/634)
      (open since 2025-08-13, reproduced independently by a maintainer and
      two other users — same root cause, same class, just via SPI instead of
      I2C). `Synthux-Academy/libDaisy` only inherited the bug from a routine
      merge and isn't the real source, so a PR there wouldn't close #634 or
      reach the other affected users. A branch with the fix as two commits
      (column-address fix referencing #634; the I2C batching perf change
      kept separate) is ready at `fix/ssd1306-128x32-column-address` in the
      submodule, based on `upstream/master` — needs pushing to a personal
      fork of `electro-smith/libDaisy` and opening as a PR (or comment) on
      #634. Note: write access to the target repo was never actually
      required to open a PR — fork-and-PR is the standard flow. Full
      writeup: `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32
      OLED".

## Priority 2 — Seq patterns: authoring & a less-repetitive feel

- [ ] **Manually author more patterns with `tools/pattern_editor.html`.** S35
      already selects a variant within the SW1 genre (`Sequencer::SetVariant`,
      `synth/sequencer.h`); the bottleneck is content, not plumbing. Current
      patterns on disk (`synth/patterns/<genre>/`), track progress here:
      - Electro (6): `00_classic`, `01_brute`, `02_offbeat`, `03_claptrap`,
        `04_sizzle`, `05_burst`
      - Techno (4): `00_fourfloor`, `01_rolling`, `03_minmal`,
        `04_tropicalsunset` (`03`/`04` added 2026-07-27, alongside groove
        reworks of `00`/`01`)
      - IDM (3): `00_glitch`, `01_fullrandom`, `02_stomp` — thinnest genre
        now that Techno's caught up
      - [ ] Add more IDM variants
      - [ ] Add more Techno variants
      - [ ] Add more Electro variants
      - [ ] Re-run `tools/gen_patterns.py` after each addition, hardware-test
            the new variant via S35 sweep
- [ ] **Idea to track/iterate — a less repetitive feel, and freeing up S34.**
      The original point of per-track pattern variants was to break up
      repetition; good use of the existing per-step **chance** feature can
      get most of that without hand-authoring dozens of variants. Separately,
      S34 in Seq mode is currently **Kick punch** (`seq_punch_lk`, read via
      pickup at `TouchPlaited.cpp:2003`, applied as a timbre boost to only
      the kick slot at `TouchPlaited.cpp:362`) and doesn't feel very
      impactful in practice. Better fix for "kick needs more punch": lean on
      the drum-kit selector/curation to offer more kick models/presets,
      which frees S34 for pattern-feel duty instead. Open problem: S34
      shouldn't just be a single global chance multiplier stacked on top of
      each step's existing per-note chance — need a mapping that actually
      reads as varying the pattern (e.g. scaling mutation depth, or biasing
      which steps are eligible) rather than a flat "more/less random" fader.
      Not designed yet — needs iteration before implementation. (A separate
      "reuse S32 for a density/chance split" proposal was considered here
      and dropped — Seq's knob layout already gives density its own knob,
      S33; see `notesarchive/notes_archive_2026-08.md`.)

## Priority 3 — feature additions

- [ ] **MIDI drum pitch phase 2** — ch10 notes within ±6 of a slot's GM anchor play that slot transposed.
  **What ch10 does today** (MANUAL.md:471-483, "Channel 10 — drums" table): each of the 7 kit slots accepts a small fixed set of GM note numbers (e.g. Tom accepts 41, 43, 45, 47, 48, 50). Any accepted note triggers that slot at its stored pitch — "exactly like tapping the pad." Notes outside the table are ignored entirely. There's no transposition logic at all right now; it's a lookup table, not a pitch mapping.

  **What "phase 2" would add**: actual transposition — a note within ±6 semitones of a slot's GM anchor (the bold note, e.g. 45 for Tom) would play that slot pitched up/down by the offset, instead of only the handful of literal notes in the table always sounding at one fixed pitch. That's a real, non-trivial feature (touches the MIDI-in note handler and the per-slot pitch path), not just documentation cleanup — it isn't implemented or partially implemented anywhere, and MANUAL.md's table explicitly documents current behavior as fixed-pitch.

- [ ] **Chiptune engine (7) — bring into manual selection** (decided 2026-07-24, resolves the old "Open decisions" item — the roadmap text there was stale: it's not just excluded from the random pools, `process_model_select` skips it out of the P0/P2+S35 quantizer bank entirely — `TouchPlaited.cpp:1383-1385` maps bank-0 index 7 straight to engine 8). Fix: stop the skip so it's reachable at position 7. Map its knobs to the standard layout, same convention as every other engine — S30 drive, S31 decay, S32 harmonics (chord select), S33 timbre (arpeggiator pattern/range), S34 morph (waveform shape). Caveat found while scoping: `ChiptuneEngine::Render` (`thirdparty/plaits/dsp/engine2/chiptune_engine.cc`) always takes the "clocked" single-arp-note path here — the host always patches the trigger (`plaits_voice.cpp`: `trigger_patched = true`) — and the engine never calls `set_envelope_shape`, so `already_enveloped` stays true and S31 Decay currently has no audible effect there; decide during implementation whether Decay should gate the arp note's length, or whether the (currently unreachable) chord-pluck path should be exposed instead. Still stays out of the random pools — self-running, no gate response.

## OLED screen
— done, see Housekeeping (upstream driver PR still open) and `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED".

Text/rendering parity with the visualizer (2026-07-24) and the held-combo
progress bar + confirm flash (2026-07-24) are implemented and verified by
code inspection — full write-up moved to
`notesarchive/notes_archive_2026-08.md` → "OLED screen — parity + held-combo
progress bar". Two things survive from that write-up as still-open:

- **Hardware has no idle/status fallback** — it holds the last-touched
  control indefinitely once the screen has shown anything. The visualizer's
  `oled-mini.ts` reverts to a status row (model/mode/transport) after
  `IDLE_MS` (2.2s) of no input. Either port the no-fallback behavior into the
  visualizer, or accept the visualizer as intentionally friendlier here —
  pick one and note it.
- **Deliberately not covered**: the rec-pad confirm hold (`rec_hold_count`)
  and the P0+P1 sound-edit toggle (`rec_snd_edit`, `TouchPlaited.cpp:437,
  2221-2227`) have no build-up bar or confirm flash — neither has an
  existing LED build-up animation to key off. Tracked as "Missing
  confirmations" in the per-mode list below.

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

**Next round — OLED per-mode improvement list (captured 2026-08-04, not
scoped yet — pick apart into concrete items before starting):**

- [ ] **Missing confirmations.** Echoes "Deliberately not covered" above:
      the rec-pad confirm hold and the P0+P1 sound-edit toggle both fire
      with no build-up bar or confirm flash on the screen. Give them one, or
      explicitly decide they don't need it.
- [ ] **Long-press stages need to say what they do.** P0+P2's "Stage N"
      confirm (`hold_stage`, `compute_hold_telemetry()`) tells you a
      threshold was crossed but not what changed — e.g. Pitch mode's 1s
      soft-tight → 2s soft-wide → 3s restore-live-knobs
      (`TouchPlaited.cpp:2134-2138`) reads identically to Seq's 1s
      param-variance → 2s new-kit on the screen. Worth a short label per
      stage instead of just the number.
- [ ] **Rec mode: surface entering sound edit.** P0+P1 already toggles
      `rec_snd_edit` while SW1=Rec (`TouchPlaited.cpp:437,2221-2227`) — same
      gap as the confirmations item above, called out again because it's
      Rec-specific and easy to lose inside the general note.
- [ ] **Basic Pitch is underusing the screen.** Live/idle currently only
      shows the last pad/note touched (or nothing, per the no-idle-fallback
      gap above). Show more at a glance instead — active engine/model, and
      multiple values at once while idle — rather than one transient
      last-touched callout.
- [ ] **Broader per-mode audit.** The four items above came out of one
      conversation, not a systematic pass — before implementing, walk
      Seq/Arp-Mel/Pitch/Rec each on their own and ask the same "is the
      128×32 budget earning its keep here" question Basic Pitch got above.

Before touching any of this: re-read the whole OLED screen section top to
bottom first, plus the archived write-up it points to — the real-hardware
checklist and the per-mode list above already overlap with parts of it.

## Parking Lot - performance 

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
