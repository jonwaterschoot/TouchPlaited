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

**Manually author more patterns with `tools/pattern_editor.html`** — done,
2026-08-04. All three genres now have a healthy variant count (Electro 6,
Techno 6, IDM 6); S35 selects among them (`Sequencer::SetVariant`,
`synth/sequencer.h`). Re-run `tools/gen_patterns.py` and hardware-test via
S35 sweep before shipping new additions, same as always — just no longer
tracked as an open item.

**S34 is no longer Kick punch** — as of 2026-08-04 it scales each step's
authored chance nibble (`Sequencer::SetChance`, `synth/sequencer.h`), which
was the "free up S34 for pattern-feel duty" half of the old item here. Punch
now rides S30 Drive on the models where it applies (`drum_params()`,
`TouchPlaited.cpp:362`). What's left of that idea:

- [ ] **Does chance-on-S34 actually break up the repetition?** The mapping
      shipped (below 0.5 → probabilistic steps drift toward always-fires,
      0.5 → exactly as authored, above → miss rate scaled up to
      `kChanceExtraMax`×) is deliberately *not* a flat global multiplier, but
      it's only ever been reasoned about, not played with for an hour. If it
      still reads as "more/less random" rather than "the pattern varies",
      the alternatives already considered were scaling mutation depth or
      biasing which steps are eligible. Its **display** is a separate,
      already-filed problem — see OLED item C1.
- [ ] **Kick curation** — the other half of the old item: offer more/better
      kick models rather than a punch knob. Blocked-adjacent finding from
      the 2026-08-04 hardware pass (moved here out of the OLED list, it's
      not a display bug): **P0+P2 stage 1 keeps whatever engine a slot
      already has.** `mutate_drum_soft()` (`TouchPlaited.cpp:394-401`) only
      jitters harmonics/timbre/decay; only stage 2's `generate_drum_random()`
      re-picks engines from `kDrumKick`/`kDrumSnare`/… So once a slot has
      been pointed at an off-pool engine by hand (Rec mode S35 reaches all
      24), a stage-1 re-randomize never brings it back to a kick-shaped
      model — which is exactly the "randomize should stick to actual kick
      models" complaint. Decide: (a) leave stage 1 as the deliberate
      "same kit, new variation" step and document it, (b) have stage 1 snap
      an off-pool engine back into the slot's pool while keeping in-pool
      engines put, or (c) widen `kDrumKick` (currently only 21 Bass drum,
      10 FM 2-op) so there's more to land on in the first place.

## Priority 3 — feature additions

- [ ] **MIDI drum pitch phase 2** — ch10 notes within ±6 of a slot's GM anchor play that slot transposed.
  **What ch10 does today** (MANUAL.md:471-483, "Channel 10 — drums" table): each of the 7 kit slots accepts a small fixed set of GM note numbers (e.g. Tom accepts 41, 43, 45, 47, 48, 50). Any accepted note triggers that slot at its stored pitch — "exactly like tapping the pad." Notes outside the table are ignored entirely. There's no transposition logic at all right now; it's a lookup table, not a pitch mapping.

  **What "phase 2" would add**: actual transposition — a note within ±6 semitones of a slot's GM anchor (the bold note, e.g. 45 for Tom) would play that slot pitched up/down by the offset, instead of only the handful of literal notes in the table always sounding at one fixed pitch. That's a real, non-trivial feature (touches the MIDI-in note handler and the per-slot pitch path), not just documentation cleanup — it isn't implemented or partially implemented anywhere, and MANUAL.md's table explicitly documents current behavior as fixed-pitch.

- [ ] **Chiptune engine (7) — bring into manual selection** (decided 2026-07-24, resolves the old "Open decisions" item — the roadmap text there was stale: it's not just excluded from the random pools, `process_model_select` skips it out of the P0/P2+S35 quantizer bank entirely — `TouchPlaited.cpp:1383-1385` maps bank-0 index 7 straight to engine 8). Fix: stop the skip so it's reachable at position 7. Map its knobs to the standard layout, same convention as every other engine — S30 drive, S31 decay, S32 harmonics (chord select), S33 timbre (arpeggiator pattern/range), S34 morph (waveform shape). Caveat found while scoping: `ChiptuneEngine::Render` (`thirdparty/plaits/dsp/engine2/chiptune_engine.cc`) always takes the "clocked" single-arp-note path here — the host always patches the trigger (`plaits_voice.cpp`: `trigger_patched = true`) — and the engine never calls `set_envelope_shape`, so `already_enveloped` stays true and S31 Decay currently has no audible effect there; decide during implementation whether Decay should gate the arp note's length, or whether the (currently unreachable) chord-pluck path should be exposed instead. Still stays out of the random pools — self-running, no gate response.

- [ ] **Root note wrap instead of clamp** (moved out of the OLED list
      2026-08-04 — it's a control-behavior question, nothing to do with the
      screen). P0+P10 / P0+P11 shift `root_semitone` but clamp at the ends
      (`TouchPlaited.cpp:3196` `if (root_semitone > 0)` / `:3238`
      `if (root_semitone < 11)`), so you can't step "below C" — the control
      just goes dead there. Root is octave-less by design (the octave lives
      in `active_octave()`), so wrapping 11→0 and 0→11 is two lines and
      loses nothing. Decide: wrap (then say so in MANUAL.md), or keep the
      clamp and explain *why* in MANUAL.md — right now it does neither.
      Note `t.root` is published in telemetry, so the visualizer's root
      readout follows either way with no protocol change.

## OLED screen
— driver bring-up done, see Housekeeping (upstream driver PR still open) and `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED".

Text/rendering parity with the visualizer (2026-07-24) and the held-combo
progress bar + confirm flash (2026-07-24) are implemented and verified by
code inspection — full write-up moved to
`notesarchive/notes_archive_2026-08.md` → "OLED screen — parity + held-combo
progress bar".

**The 2026-08-04 hardware pass has started**, and everything below is either
its findings or was already open before it. Two structural constraints to
keep in mind while reading, because most items collide with one or the
other:

- ~~**The screen has no idle/status fallback.**~~ It has one as of
  2026-08-04 (**A4** below) — a per-mode status row after 2.2 s, matching
  the visualizer. Items that were waiting on it ("go back to the model
  after a while", the Rec top line) are done or unblocked; "replace the
  value with hold-to-save" still needs **B2**.
- **Value-row font is chosen per string length** (`ShowLine()`,
  `display/oled_screen.cpp:108-123`, mirroring the emulator's `fitFont()`),
  so text size jumps around as you scroll a list. Item **A3** — and it
  constrains the exact wording chosen in C1/C2, so do A3's decision first or
  pick strings that don't straddle a font boundary.

Ordering: **A → C → B**, roughly. A1/A2 are a real bug plus cheap geometry
work that several other items sit on; C is pure copy once A3's rule is
settled; B is the one protocol bump, worth batching so the visualizer and
`visualizer/PLAN.md` §2 only move once.

### A. Cross-mode display plumbing (blocks much of C)

- [x] **A1 — one-shot confirms get dropped; the bar freezes at ~98%.**
      *Fixed 2026-08-04.* Both halves: the ISR now posts confirms through
      `fire_confirm()` and the main loop latches them for `kConfirmLatchMs`
      (120 ms — longer than either consumer's throttle, shorter than the
      OLED's own 220 ms flash so the bar can't reappear behind it), and
      `OledUi::Service` forces a redraw on the `hold_kind != 0 → 0` edge so a
      finished bar hands the screen back to the status row. All four hold
      kinds go through the latch now, including P0+P2's stages, which used to
      vanish if you released right after a threshold. **Needs hardware
      re-test** (section D).
      *Root cause found 2026-08-04, this is a bug not a wish.*
      `entry_just_confirmed` is consumed inside `compute_hold_telemetry()`
      the first time it's read (`TouchPlaited.cpp:1670-1673`), which happens
      in `capture_state()` at `:1174` — but `OledUi::Service()` (`:1190`)
      returns early whenever it's inside its 80 ms redraw throttle
      (`display/oled_ui.cpp:565`). If the confirm frame lands in a throttled
      window, the flash is gone for good, which is exactly the reported
      "sometimes it shows RECORDING, sometimes not". `Telemetry::SendState`
      is separately rate-limited to 33 ms, so the visualizer can lose it the
      same way. Second half of the same symptom: once rec entry fires,
      `rec_mode != IDLE` so `hold_kind` drops to 0, nothing in the priority
      chain has changed, and **no redraw happens at all** — the last drawn
      frame (the bar at whatever fraction it reached, ~98 %) just stays on
      screen. Fix both: latch a confirm with a deadline instead of a
      consume-once flag (or ack per consumer — there are two), and force one
      redraw on the `hold_kind != 0 → 0` edge so the bar is guaranteed to be
      replaced. Prerequisite for every new confirm in B2.
- [x] **A2 — progress bar: make room for text.** *Done 2026-08-04.* The bar
      moved to rows 12-21 and the bottom row became a Font_6x8 note saying
      what crossing the next threshold does (that's C3's content);
      `oled-mini.ts`'s `layoutProgress()` matches. **Still open**, split out
      because it's a separate behavior: in Basic Pitch `p0p2_all_done` pins
      `progress = 127` while the pads stay held (`TouchPlaited.cpp`), so
      after the 3 s stage the full bar sits there until release. A1's
      hold-end redraw doesn't cover it — `hold_kind` is still 1. Decide
      whether the bar should clear at `all_done` or keep showing "nothing
      more to reach".
- [ ] **A3 — stop the value row changing size, scroll instead.** Decide the
      rule: one fixed font per context (probably `Font_11x18` for values,
      `Font_6x8` for the label row as now), and when the string overflows,
      hold it still for a beat, then marquee horizontally rather than
      shrinking. Two things this runs into: (i) a marquee needs *continuous*
      redraws, and with no idle fallback (A4) the screen would scroll the
      same string forever — give it a stop condition (scroll once or twice,
      then park at the end, or park truncated); (ii) every redraw is an I2C
      transfer on the bus shared with the MPR121 touch controller
      (`i2c1_lock.h`), which is why the throttle is 80 ms — a marquee
      stepping at that rate is ~12 fps, fine, but don't let it run when
      nothing is being scrolled. Emulator parity: `fitFont()` in
      `oled-mini.ts` implements the same shrinking rule and has to change
      with it.
- [x] **A4 — idle/status fallback.** *Done 2026-08-04*, decided in favour and
      implemented on both sides. `status_row()` (`display/oled_ui.cpp`) is
      per-mode and built only from slow-moving fields, which is what killed
      the first attempt (it included `seq_step`, which changes every block):
      Seq → genre + ext-clock + Play/Stop, Rec → `Rec P5 Clap` + the slot's
      model, Arp/Mel → sub-state + sound-edit + model, Pitch → scale +
      model. Takes over after `kIdleMs` (2200, matching the visualizer's
      `IDLE_MS`), immediately when a hold ends, and redraws itself when its
      own content changes while idle. The visualizer's `setStatus()` call is
      now a 1:1 mirror of it rather than the old model+mode+step row — one
      fewer deliberate divergence.

- [x] **A5 — hold pacing: the bar is too fast and appears to start halfway.**
      *Done 2026-08-04, needs a hardware feel-check.* Every build-up now
      opens with an **announce window** — `hold_progress_of()` reports 0
      while the screen holds an empty bar and names what the stage does,
      then the bar fills over the rest. At 600 ms it comfortably swallows
      the 220 ms confirm flash, so a new stage's bar always visibly starts
      from empty. Stages doubled to 2 s (`kStageAnnounceBlocks` 150 +
      `kStageFillBlocks` 350), so P0+P2 now fires at 2/4/6 s — **those two
      constants are the pacing dial**, change them and everything (bar, LED,
      thresholds) follows. Rec entry keeps its 2 s total with a 600 ms
      announce; the 1.2 s holds get 300 ms (`kShortAnnounceBlocks`).
      MANUAL.md's hold tables are updated to the new times.
      *From the 2026-08-04 hardware pass, and the most important item in
      this section.* Two separate causes, one fix:
      The two causes it fixed, for the record: (i) a confirm flash owns the
      screen for 220 ms while the *next* stage's counter keeps running
      underneath, and at 1 s per stage that was ~22 % of the bar spent
      hidden; (ii) 1 s left no time to read the note row and stop there.
- [x] **A6 — the same build-up in LED language, for units with no screen.**
      *Done 2026-08-04.* The LED now mirrors A5's two phases: three slow
      pulses (130 ms on / 70 ms off) through the announce window, then the
      existing acceleration (150 → 40 ms) as the bar fills. Applied to the
      P0+P2 stages and to rec entry — the two long build-ups. **Left as
      is**: the 1.2 s holds (layer clear, copy, and the new save) keep a
      plain accelerating blink; their announce window is only 300 ms, which
      isn't enough for three readable pulses. Revisit if they feel
      inconsistent in the hand.

### B. Telemetry the screen doesn't have yet

The state frame appends fields at the end behind length guards
(`midi/telemetry.cpp` `SendState`, `visualizer/src/core/protocol.ts`), so
adding one is backward-compatible — but each still touches both sides plus
`visualizer/PLAN.md` §2. Batch what you can.

- [ ] **B1 — show that a knob is waiting for pickup.** Nothing in
      `TelemetryState` exposes pickup state today, so the screen prints the
      raw pot position (`t.controls[i]`) whether or not it's actually doing
      anything — you can sweep a knob through its whole travel, watch the
      number change, and hear nothing. `KnobPickup::caught`
      (`TouchPlaited.cpp:645-676`) already holds the answer; what's missing
      is a mapping from "current mode/layer" to the eight live pickups
      (there are per-layer sets: `seq_pu*`, `arp_pu*`, `rec_pu*`, `arp_se*`,
      `rec_se*`, the BP volume/blend pair, plus the CC pickups) and one
      published byte — an 8-bit "armed" mask over S30..S37. Then the value
      row can read e.g. `42% -> 78%` or mark the target, instead of lying.
      Cross-mode by construction: every playmode has a pickup layer.
- [x] **B2 — the confirms that have no telemetry at all.** *Mostly done
      2026-08-04.* Two new hold kinds, both riding A1's latch so they can't
      be dropped: **5 — rec save**, the hold the hardware pass asked for by
      name, drawing `HOLD P5 TO SAVE` over a filling bar and flashing
      `SAVED`; and **6 — rec cancel**, which has no build-up (it's a tap) so
      it only ever flashes `CANCELLED`. `rec_hold_count` became volatile —
      the main loop reads it now. Completed **2026-08-05** with **7 — P0+P1
      sound edit**, which turned out to deserve more than the "short-lived
      message" originally filed: it's a real 1 s hold, so it gets the same
      bar, announce window and confirm as every other build-up, and the
      first LED animation it has ever had (it used to give nothing at all
      until it fired, while silently reassigning every knob in the mode).
      The note row names the direction — `KNOBS EDIT THE SOUND` /
      `BACK TO ARP KNOBS` — since the same combo does both; the confirm
      reads `SOUND EDIT` / `ARP KNOBS` off `hold_outcome`, because
      `snd_edit` has already flipped by the time the latched flash draws.
      `hold_outcome` is therefore no longer kind-3-only; its doc comment
      says so in all four places now.
- [x] **B3 — publish the active Seq pattern.** *Done 2026-08-04.*
      `Sequencer::VariantSlot()` → `t.seq_pattern`, appended as state byte
      27 behind the usual length guard, and resolved to a name through
      `patterns_gen.h` on both sides (`pattern_slot_value()` /
      `patternSlotValue()`). Deliberately not derived from `t.controls[5]`:
      S35 sits behind a pickup, so the pot can be parked anywhere.
- [x] **B4 — publish the melodic transport (`arp_run_on`).** *Done
      2026-08-04*, as `arp_flags` bit 3 — no new field. Pre-v8 frames
      default it to running rather than stopped, since the bit was always 0
      before it meant anything.

### C. Wording and labels (no new plumbing, except where noted)

- [x] **C1 — Seq S34 chance reads backwards.** *Done 2026-08-04*, option (a)
      as chosen: no percentage at all, since the knob is a three-zone
      control — `always fire` / `fuller 60%` / `as authored` / `sparse 2.4x`
      (`chance_value()`, mirrored by `chanceValue()` in the visualizer), top
      line now `S34 STEP CHANCE`. The curve itself is untouched, so centre
      still plays patterns exactly as authored. All four strings are ≤11
      chars, so the value row keeps one font across the sweep. Lockstep done
      in `synth/sequencer.h`, `display/oled_ui.cpp`,
      `visualizer/src/core/controls-meta.ts`, `visualizer/src/panel/labels.ts`
      and MANUAL.md.
- [x] **C2 — Seq S33 density wording, round 2.** *Done 2026-08-04.* Top line
      is now `S33 PATTERN DENSITY` (19 chars, inside the 21-char label
      budget) and the values name the layers as asked: `layer 4` /
      `layers 3-4` / `layers 2-4` / `layers 1-4`. Two judgement calls made
      while implementing, both worth knowing:
      (i) the `1/4` … `4/4` prefix was dropped. With it the strings run
      11-19 chars, which straddles two font sizes, so the value row would
      resize mid-sweep — exactly what A3 is meant to stop. Without it all
      four are ≤11 chars and hold one font, and the ordering still reads
      off the layer numbers themselves.
      (ii) **"layer" is now overloaded** — in Arp/Mel Rec it means a NoteRec
      take (`rec_layers`, the P2+pad "Clear layer" and "Copy layer" holds).
      Taken deliberately, since it's the word the pattern format actually
      uses for weight tiers; flag it if it bites in practice.
- [x] **C3 — long-press stages should say what they do.** *Done 2026-08-04*,
      in both directions. While the bar builds, the note row under it says
      what the *next* threshold does (`hold_note()`: `1s vary kit` /
      `2s new kit` in Seq, `1s vary sound` / `2s vary more` /
      `3s back to live knobs` in the pitched modes, plus one line each for
      rec entry, layer clear and copy). When one fires, the flash names the
      change instead of the old `STAGE N` (`confirm_text()`: `Kit varied`,
      `New kit`, `Varied more`, `Live knobs`, …). Mirrored in
      `visualizer/src/panel/labels.ts`.
- [x] **C4 — list the combos when a modifier pad is touched.** *Done
      2026-08-05.* Holding P0/P1/P2 now lists what it unlocks in the current
      mode (`ShowList()`, four rows of Font_6x8 filling the whole 32 px),
      instead of printing `P0 modifier` — the name of the pad already under
      your finger. The list owns the screen until the modifier is released,
      so the idle timeout can't pull a reference away mid-read.

      **The counts, which is what made this tractable.** Enumerated from the
      handlers rather than from MANUAL, 15 lists (3 modifiers × 5 contexts:
      Seq, Seq Recording, Basic Pitch, Arp/Hold, Arp Rec):

      | | Seq | Seq Rec | Pitch | Arp/Hold | Arp Rec |
      |---|---|---|---|---|---|
      | **P0** | 2 | 2 | 4 | 4 | **5** |
      | **P1** | 2 | 2 | 2 | 4 | 4 |
      | **P2** | 3 | 1 | 4 | 4 | **5** |

      Two things kept this from needing real paging. Pairing the ± combos
      onto one row each (`P10/P11 root -/+`, `P10/P11 arp octaves`) cut most
      lists from 5 to 4; and making each row self-labelling (`P1+S30 …`)
      meant no header row had to be spent naming the modifier. So **13 of 15
      lists fit one screen exactly**, and the two that don't overflow by
      exactly one row — handled by scrolling the window down one row every
      `kListScrollMs`, which always keeps the screen full. No dependency on
      A3 after all (that's horizontal marquee for over-long values; this is
      a vertical window over short rows).

      **Two mislabels the enumeration turned up**, both now fixed:
      `process_model_select()` returns immediately on `seq_mode_on`, so
      **P0/P2 + S35 does nothing in Seq** — `describe_control()` was
      labelling it `Model select bank0/1` anyway. And with that combo absent
      in Seq, S35 keeps its pattern role under P0/P2 there, so the value row
      shows the pattern name instead of falling through to a bare %.
- [ ] **C5 — Rec mode's own status row.** Partly done 2026-08-04: A4's
      status row covers Rec, so the screen now settles on
      `REC P5 CLAP` + the slot's model instead of the stale
      `P3-P9 REC ENTRY` hold label left over from entry. Still open, both
      needing B2's message mechanism: replace the last-changed value with
      `Hold P5 to save` after a few idle seconds, and give cancel its own
      message. A blinking marker on the edited pad is also still open — the
      status row is static text today.
- [x] **C10 — Rec needs a live capture indicator and per-layer state.**
      *Done 2026-08-05.* A persistent indicator block on the label row, the
      first thing on this screen that outlives the callout showing under it:
      a blinking circle while capture is live, and five dots for the layer
      stack (filled = committed, hollow = muted, pulsing outline = the take
      being recorded into, tick = free). `StatusIcons` in
      `display/oled_screen.h`, built by `icons_for()`. It is also the only
      self-generated redraw on the screen — everything else is
      change-driven — so the blink is gated on capture actually being live
      (`kBlinkMs` 400, one redraw per phase).
- [x] **C11 — transport combos announced the wrong pad.** *Done 2026-08-05.*
      P2+P10 and P2+P11 fire on the P10/P11 press edge, and the pad-down
      branch sat above the state-change branch in the priority chain — so
      the screen said `P10 OCT-/PITCH-1`, the modifier's meaning never
      reaching it. State changes now outrank the pad-down that caused them,
      and `t.playing` got the same treatment (`P2+P11 DRUM SEQ` ·
      `Play`/`Stop`, which also covers MIDI Start/Stop and the seq's own
      first-entry auto-start). Separately, `describe_pad()` now resolves
      P10/P11 against the held modifier the way the visualizer's
      `describePad()` already did — five different jobs on those two pads,
      and the firmware named none of them.
- [x] **C12 — Rec's transport was unreachable (control change, not display).**
      *Done 2026-08-05.* Filed here because it surfaced as "I'm not even
      sure what starts and stops what", but no amount of labelling could fix
      it: while SW1 was in Rec, P2+P10 toggled capture arm **only**. Arming
      force-starts the clock and disarming deliberately doesn't stop it, so
      stopping playback meant flicking SW1 out of Rec, pressing P2+P10
      there, and flicking back — three of the four states you'd name were
      reachable and the fourth wasn't a state at all. P2+P10 in Rec now
      cycles **capturing → looping, punched out → stopped**, which is
      exactly the three states `melodic_state()` already names. The
      punch-out step commits the open take, so the stop step can't lose
      one. LED counts down with the state: 3 blinks, 2, 1. MANUAL's
      *Arming and transport*, the transport section and the blink-code
      table are updated.
- [ ] **C6 — broader per-mode audit.** Standing item. The list above came
      out of one hardware session on Seq, Rec and Basic Pitch. Walk
      **Arp/Mel** (Hold/Arp/Rec sub-states, layer record/mute messages) the
      same way and ask the same "is the 128×32 budget earning its keep"
      question before calling this section done. C7 is the first result of
      starting that walk.
- [x] **C7 — Arp/Mel: say what state the loop is in, and when it changes.**
      *Done 2026-08-04* on B4. The Rec status row reports loop state instead
      of the model, and **P2+P10** gets a callout on either of its meanings —
      riding the normal priority chain on an `arp_flags` change, since the
      combo's pads are usually already held and so never produce a pad-down
      edge to catch. The **wording** it shipped with reported one flag at a
      time and was replaced same-day by C9 below.
- [x] **C9 — P2+P10 should name the state it lands in, not the flag that
      moved.** *Done 2026-08-04, second hardware note on the same combo.*
      C7 published both flags but still reported them one at a time —
      `Play`/`Stop` for the transport, `Armed`/`Punched out` for capture —
      and neither half describes what the device is actually doing, because
      the two are independent. `melodic_state()` now names the combination:
      `Rec + play` · `Play no rec` · `Rec stopped` in Rec, `Arp play` /
      `Arp stopped` in Arp and Hold (Hold is the same arpeggiator with
      latched input, and the label row already distinguishes them),
      `Mel play` / `Mel stopped` when the combo is hit from Seq or Basic
      Pitch. The label still names which of the combo's two meanings fired
      (`P2+P10 TRANSPORT` vs `P2+P10 REC CAPTURE`). Same strings feed the
      idle status row, so a press and the row it settles back to agree —
      and the row now surfaces a stopped Arp/Hold instead of showing the
      model, that being the one state where the mode looks broken rather
      than merely quiet. All ≤11 chars, one font throughout.
- [x] **C8 — Seq idle row names the pattern.** *Done 2026-08-04* on B3:
      the value row reads e.g. `fourfloor 1/6` and the transport moved up
      beside the genre (`SEQ TECHNO` / `SEQ STOP TECHNO`), so both fit
      without the pattern losing the big font.

### D. Per-mode hardware verification checklist

Code-level parity is as far as inspection can go; this is the side-by-side
pass against the visualizer's OLED. **Walked 2026-08-04** — Pitch and
cross-mode passed; everything the rest turned up (A5, A6, B2, B3, B4, C7,
C8) is implemented but only code-verified, so those rows need a **second
walk** before they tick. `[O]` = walked once, findings filed and fixed.

- [x] Seq: idle (status row after 2.2 s — now `SEQ TECHNO` + the playing
  pattern name), knob turn (incl. tempo as BPM, ext-clock "ext", the new
  S33/S34 wording), pad = drum note, SW1 (IDM/Techno/Electro), model select
  (P0/P2+S35), rec-slot editing (flatter labels, status row `REC P5 CLAP`,
  and the new hold-to-save bar → `SAVED` / tap-to-cancel → `CANCELLED`)
- [O] Arp/Mel: knob turn (incl. dead-knob "no effect"), pad = pitched note,
  SW1 (Hold/Arp/Rec), Rec sub-mode knobs (Speed/Shift/Chance/Order), layer
  record/mute messages, model select, and P2+P10's callout + status row now
  naming the combined state (**C9**)
- [x] Pitch: knob turn (engine-specific labels via `kEngineKnobs`), pad =
  pitched note, SW1 (Minor/Chromatic/Major), model select
- [x] Cross-mode: SW2 flip, P1+FX layer (reverb/delay, drums vs. pitched
  wording), P0+S37 stereo width, external clock (MIDI vs. CV wording)
- [x] Held-combo progress bar + confirm flash: hold P0+P2 (Seq:
  re-randomize; Arp/Mel: vary sound — 2 stages, or 3 in Pitch mode), hold a
  drum pad P3–P9 (rec entry → "Recording"), P0+P1 in Arp/Mel (sound edit,
  both directions), P2+drum-pad hold in Rec (layer
  clear → "Cleared"/"Empty"), and the copy-confirm hold while recording
  ("Copied") — bar should track the LED's blink rate, confirm flash should
  land the same instant the LED flashes/switches pattern. A1's reliability
  fix held up on the first walk; the pacing didn't, and A5/A6 rewrote it —
  **the feel of the new 2 s stages and the three-pulse announce is the main
  thing to judge on the second walk.** Both dials are named constants
  (`kStageAnnounceBlocks`, `kStageFillBlocks`).

### Deliberate differences from the visualizer — don't "fix" without deciding

- Rec drum-slot editing uses flatter "Slot X" labels vs. the web's full
  per-knob nesting (128×32 char budget) — `display/oled_ui.h`.
- The dead-knob message is `no effect`, not `no effect on <model>`.
- ~~No idle/status fallback~~ — resolved by A4 (2026-08-04): both sides now
  show the same per-mode status row after the same 2.2 s timeout.

## Parking Lot - performance 

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
