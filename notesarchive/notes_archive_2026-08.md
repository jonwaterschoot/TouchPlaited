# Notes archive — August 2026

Resolved/historical material moved out of `ROADMAP.md`/`notes.md` during
August 2026, per the archiving convention described at the top of
`ROADMAP.md`. Earlier material: `notesarchive/notes_archive_2026-07.md`.

---

## OLED screen — parity + held-combo progress bar (moved from `ROADMAP.md` 2026-08-04)

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
  - Hardware has no idle/status fallback (still open — tracked in
    `ROADMAP.md` "OLED screen").
- Rendering (fonts/pixels): fixed 2026-07-24 — `oled-mini.ts` now blits the
  actual firmware bitmap fonts (`visualizer/src/panel/oled-font-data.ts`,
  ported from `lib/libDaisy/src/util/oled_fonts.c`) with the same discrete
  Font_11x18 → Font_7x10 → Font_6x8 stepping `display/oled_screen.cpp`'s
  `ShowLine()` uses, instead of a system font rasterized then thresholded.
  Small text was illegible before this — see
  `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED" for the
  bring-up bugs that motivated the overhaul.

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
the P0+P1 sound-edit toggle (`rec_snd_edit`) — unlike the four gestures
above, neither has an existing LED build-up animation to key off (both fire
a single confirm blink with no preceding countdown), so there's no
established "this is what building-up looks like" to extend. Tracked as
"Missing confirmations" in `ROADMAP.md` → "OLED screen" → per-mode list.

---

## Density + chance axis via S32 — superseded, not implemented (moved from `ROADMAP.md` 2026-08-04)

The original idea (`ROADMAP.md` Priority 2) proposed splitting S32 into a
0–0.45 "less density" / 0.5 "normal" / 0.55–1 "chance/mutation per step"
range, changing seq tick logic. By the time this was revisited, Seq mode's
knob layout had already settled with S32 = shuffle and S33 = density as two
independent knobs (`TouchPlaited.cpp` "Seq layout" comment, ~line 1996) —
there's no free S32 range left to repurpose this way, and density already
has its own dedicated knob. The underlying goal (less repetitive patterns
without hand-authoring dozens of variants) is still tracked in `ROADMAP.md`
Priority 2 under "a less repetitive feel, and freeing up S34" — via
per-step chance and a rethought S34, not a knob split.

---

# The 2026-08-04/05 rounds — closed (moved from `ROADMAP.md` 2026-08-05)

Everything below is finished, and everything in the *2026-08-05* material is
hardware-verified (section D's checklists were walked twice; see the two
hardware-round sections at the end). It is kept verbatim because most items
record *why* a decision went the way it did, which is the part that would
otherwise be re-derived.

The strands that were still open when this was archived stayed behind in
`ROADMAP.md`: **A3** (value-row font stepping, deferred by decision),
**A2**'s `p0p2_all_done` bar question, **E2** and **E3**, the `kDrumKick`
pool width, and weight → audio level.

Item numbering (A1-A6, B1-B4, C1-C12, D, E1) is the OLED-round scheme from
`ROADMAP.md`; cross-references elsewhere in the repo still use it.

---

## Housekeeping — SSD1306 driver fix, filed upstream

- [x] **File the SSD1306 driver fix upstream, against `electro-smith/libDaisy`
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
      - PR's were added to both Synthux liDaisy fork and to libDaisy, though because these are LLM made it was perhaps not best practice to just add them as PR and not just as an issue / ticket.
      - Either way the small OLED fix was added and merged into Synthux libDaisy

---

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

- [x] **Does chance-on-S34 actually break up the repetition?** The mapping
      shipped (below 0.5 → probabilistic steps drift toward always-fires,
      0.5 → exactly as authored, above → miss rate scaled up to
      `kChanceExtraMax`×) is deliberately *not* a flat global multiplier, but
      it's only ever been reasoned about, not played with for an hour. If it
      still reads as "more/less random" rather than "the pattern varies",
      the alternatives already considered were scaling mutation depth or
      biasing which steps are eligible. Its **display** is a separate,
      already-filed problem — see OLED item C1.
- [x] **Kick curation** — *Done 2026-08-05*, option (b) plus the two Seq
      follow-ups filed with it. The pools became a table (`kDrumPools`,
      `TouchPlaited.cpp`) instead of seven hard-coded calls inside
      `generate_drum_random()`, which is what made the rest of this
      tractable:
      - **Stage 1 snaps off-pool slots back into role.** `mutate_drum_soft()`
        now tests `slot_engine_in_pool()` per slot: in-pool engines are
        jittered exactly as before ("same kit, new variation"), an off-pool
        one is re-picked from that slot's pool. So a kick pad pointed at a
        Speech engine by hand in Rec comes back to a kick on the first
        stage, which was the actual complaint.
      - **Randomizing no longer starts the sequencer.** Stage 2 called
        `seq.Start()` unconditionally, so you could not audition a new kit
        against a stopped seq — the kit change began playing. Guarded on
        `seq.IsActive()`; a running seq still restarts from bar 0 so the new
        kit lands on a downbeat, which is what that call was for.
      - **P0+P2 in Rec randomizes just the edited pad.** New hold kind 8,
        two stages on the same `kStageBlocks` pacing: 1 varies the sound the
        pad has, 2 re-picks it from the pad's own pool. Both re-arm the whole
        rec knob layer (`arm_rec_slot_pickups()`, factored out of
        `enter_rec_mode()`) — the pots were armed to values the slot no
        longer has. Full LED countdown, bar, note row and confirm, same as
        every other build-up; the bar names the pad (`P0+P2 P5 SOUND`).
      Option (c) — widening `kDrumKick` past its two engines — is untouched
      and still open as a taste call; nothing here depends on it.

---

## Priority 3 — feature additions

- [x] **MIDI drum pitch phase 2** — *Resolved 2026-08-05, and the transposing
      half was deliberately dropped.* The original plan (a ch10 note within
      ±6 of a slot's GM anchor plays that slot transposed) was decided
      against: the seven slots are drums, each already carries its own tuned
      pitch as part of the sound, and a transposing kick pad is a different
      instrument rather than a played one. **Velocity is the expressive axis
      instead** — it was already honoured on the way in, and now rides out
      too: `Sequencer::StepWeight()` exposes each firing step's authored
      weight tier and `drum_velocity()` maps it to 45/70/95/120 (hits with
      no step behind them — pad taps, the forced rec-slot trigger — still
      send 100). Previously every outgoing hit was a flat 100, so a ghost
      note and a downbeat kick left the device identical.
      The anchors also moved onto the standard 4×4 grid so a pad
      controller's bottom two rows drive the kit unmapped: `kDrumSlotGm` is
      `36/38/42/46/39/41/43` (Tom 45→41, Perc 37→43), and `gm_to_drum_slot()`
      moved 43 from the tom aliases to Perc to match. The wider GM aliases
      stay accepted. MANUAL's ch10 table and the visualizer's `DRUM_NOTES`
      are in lockstep.

- [x] **Chiptune engine (7) — skipped, not shipping** *(closed 2026-08-05)*.
      The 2026-07-24 decision to bring it into the bank is reversed: it
      stays out of the P0/P2+S35 quantizer bank and out of the random pools.
      The reason is the one the scoping already found and never resolved —
      `ChiptuneEngine::Render` always takes the clocked single-arp-note path
      here (the host patches the trigger unconditionally), and the engine
      never calls `set_envelope_shape`, so S31 Decay is inert on it. An
      engine where one of the five standard knobs does nothing and whose
      arpeggiator free-runs against the device's own doesn't earn a slot.
      `process_model_select`'s skip (`TouchPlaited.cpp`, bank-0 index 7 →
      engine 8) stays as-is; MANUAL already lists it as *Chiptune — skipped*
      with the reason.

- [x] **Root note: clamp stays, and now says so** *(2026-08-05, answering
      the "wrap instead of clamp" question with "no, and here's what was
      actually missing")*. Wrapping was rejected for the reason it exists:
      root is the one control with no audible landmark of its own, and
      without perfect pitch or a screen, the control going dead **is** how
      you know you have reached C (or B). Wrapping would take that away and
      give nothing back. `root_semitone`'s two clamps are unchanged.
      What was actually wrong was that nothing ever *named* the root:
      - `describe_pad()`'s P0+P10/P11 branch now reads
        `P0+P10 ROOT - D#` and spells the seven pads out on the value row
        (`scale_notes()` → `D# F F# G# A# B C#`).
      - the SW1 callout in Basic Pitch reads `SW1 MINOR - D#` with the same
        note row — a scale is a scale *from somewhere*, and "Minor" alone
        never said which minor.
      - the idle status row reads `PITCH MINOR D#`.
      **Scale application verified while doing it**: `compute_note()` adds
      `root_semitone` *before* the degree offsets
      (`kPitchBase + root + kScales[scale][degree] + octave*12`), so a root
      shift is a real transposition — SW1 on Minor with the root three up
      plays D♯ minor, same intervals in a new key. That was already correct;
      the note row is what makes it visible. Mirrored in the visualizer
      (`scaleNotes()`/`ROOT_NAMES` in `controls-meta.ts`); MANUAL's *Pitch
      controls* section documents both the clamp rationale and the readouts.

---

## OLED screen — the 2026-08-04/05 rounds

Driver bring-up: see `notesarchive/notes_archive_2026-07.md` → "SSD1306
128×32 OLED". Text/rendering parity with the visualizer and the held-combo
progress bar + confirm flash (both 2026-07-24) are written up earlier in this
same file → "OLED screen — parity + held-combo progress bar".

The two structural constraints the round worked under, for the record:

- **The screen had no idle/status fallback.** It got one on 2026-08-04
  (**A4**) — a per-mode status row after 2.2 s, matching the visualizer.
- **Value-row font is chosen per string length** (`ShowLine()`,
  `display/oled_screen.cpp:108-123`, mirroring the emulator's `fitFont()`),
  so text size jumps around as you scroll a list. That is **A3**, deferred —
  it is the one item from this section still live in `ROADMAP.md`. C1/C2
  sidestepped it by choosing strings that don't straddle a font boundary.

Ordering was **A → C → B**: A1/A2 a real bug plus cheap geometry work that
several other items sat on; C pure copy; B the one protocol bump, batched so
the visualizer and `visualizer/PLAN.md` §2 only moved once.

### A. Cross-mode display plumbing

- [x] **A1 — one-shot confirms get dropped; the bar freezes at ~98%.**
      *Fixed 2026-08-04.* Both halves: the ISR now posts confirms through
      `fire_confirm()` and the main loop latches them for `kConfirmLatchMs`
      (120 ms — longer than either consumer's throttle, shorter than the
      OLED's own 220 ms flash so the bar can't reappear behind it), and
      `OledUi::Service` forces a redraw on the `hold_kind != 0 → 0` edge so a
      finished bar hands the screen back to the status row. All four hold
      kinds go through the latch now, including P0+P2's stages, which used to
      vanish if you released right after a threshold.
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
      screen.
- [x] **A2 — progress bar: make room for text.** *Done 2026-08-04.* The bar
      moved to rows 12-21 and the bottom row became a Font_6x8 note saying
      what crossing the next threshold does (that's C3's content);
      `oled-mini.ts`'s `layoutProgress()` matches. The `p0p2_all_done`
      behaviour split out of this item stayed open — see `ROADMAP.md`.
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
      *Done 2026-08-04.* Every build-up now opens with an **announce
      window** — `hold_progress_of()` reports 0 while the screen holds an
      empty bar and names what the stage does, then the bar fills over the
      rest. At 600 ms it comfortably swallows the 220 ms confirm flash, so a
      new stage's bar always visibly starts from empty. Stages doubled to 2 s
      (`kStageAnnounceBlocks` 150 + `kStageFillBlocks` 350), so P0+P2 now
      fires at 2/4/6 s — **those two constants are the pacing dial**, change
      them and everything (bar, LED, thresholds) follows. Rec entry keeps its
      2 s total with a 600 ms announce; the 1.2 s holds get 300 ms
      (`kShortAnnounceBlocks`). MANUAL.md's hold tables are updated to the
      new times.
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

### B. Telemetry the screen didn't have

The state frame appends fields at the end behind length guards
(`midi/telemetry.cpp` `SendState`, `visualizer/src/core/protocol.ts`), so
adding one is backward-compatible — but each still touches both sides plus
`visualizer/PLAN.md` §2.

- [x] **B1 — show that a knob is waiting for pickup.** *Done 2026-08-05.*
      Nine new state bytes behind the usual length guard: an 8-bit `armed`
      mask over S30..S37 plus the eight targets. `capture_pickups()`
      (`TouchPlaited.cpp`) is the missing mapping — it mirrors the
      knob-application block's layer selection exactly (same order, same
      guards) across all six pickup sets plus the CC ones, so if a role
      moves there it moves here.
      **What it shows, which turned out to matter more than the mask.**
      Rather than a `42% -> 78%` string, the value row keeps showing the
      **stored value in its own units** — `describe_control()` formats from
      the target instead of the pot while armed, so a waiting tempo knob
      still reads `128 BPM` and a waiting density still reads `layers 3-4`
      rather than being demoted to a bare percentage the moment it's
      mid-handover. The pot's actual position is spatial instead:
      `OledScreen::ShowPickup()` draws a track with a tall post at the
      target and a slider block at the pot. Drive the block onto the post
      and the knob takes over; the track disappearing is the "you have it"
      signal. No arithmetic, works identically whether the value is a BPM, a
      note name or a word.
      **Deliberately not reported**: the movement-catches (P0+S37 width, the
      P1 FX mirror knobs, Rec's S35 bank select). They engage on any ~3%
      nudge, so there is no target to aim at and no dead travel to warn
      about — a marker there would be satisfied by the very next turn.
      Mirrored end to end in the visualizer (`setPickup` in `state.ts`,
      `layoutPickup()` in `oled-mini.ts`, same geometry) and in
      `visualizer/PLAN.md` §2.
- [x] **B2 — the confirms that had no telemetry at all.** *2026-08-04/05.*
      Two new hold kinds, both riding A1's latch so they can't be dropped:
      **5 — rec save**, the hold the hardware pass asked for by name, drawing
      `HOLD P5 TO SAVE` over a filling bar and flashing `SAVED`; and
      **6 — rec cancel**, which has no build-up (it's a tap) so it only ever
      flashes `CANCELLED`. `rec_hold_count` became volatile — the main loop
      reads it now. Completed **2026-08-05** with **7 — P0+P1 sound edit**,
      which turned out to deserve more than the "short-lived message"
      originally filed: it's a real 1 s hold, so it gets the same bar,
      announce window and confirm as every other build-up, and the first LED
      animation it has ever had (it used to give nothing at all until it
      fired, while silently reassigning every knob in the mode). The note row
      names the direction — `KNOBS EDIT THE SOUND` / `BACK TO ARP KNOBS` —
      since the same combo does both; the confirm reads `SOUND EDIT` /
      `ARP KNOBS` off `hold_outcome`, because `snd_edit` has already flipped
      by the time the latched flash draws. `hold_outcome` is therefore no
      longer kind-3-only; its doc comment says so in all four places now.
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

### C. Wording and labels

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

      Counts as of 2026-08-05, after the kick-curation rows and the
      P0/P1 octave-range↔undo swap (the Arp columns moved with it):

      | | Seq | Seq Rec | Pitch | Arp/Hold | Arp Rec |
      |---|---|---|---|---|---|
      | **P0** | 2 | 3 | 4 | **5** | **5** |
      | **P1** | 2 | 2 | 2 | 3 | 4 |
      | **P2** | 3 | 2 | 4 | 4 | **5** |

      Two things kept this from needing real paging. Pairing the ± combos
      onto one row each (`P10/P11 root -/+`, `P10/P11 arp octaves`) cut most
      lists from 5 to 4; and making each row self-labelling (`P1+S30 …`)
      meant no header row had to be spent naming the modifier. So **12 of 15
      lists fit one screen exactly**, and the three that don't overflow by
      exactly one row — handled by scrolling the window down one row every
      `kListScrollMs`, which always keeps the screen full. No dependency on
      A3 after all (that's horizontal marquee for over-long values; this is
      a vertical window over short rows). **P0 in Arp and in Arp Rec are now
      the same list** — since undo moved to P1, Rec's differences all live on
      P1 and P2, so the two contexts collapsed to one table.

      **Two mislabels the enumeration turned up**, both now fixed:
      `process_model_select()` returns immediately on `seq_mode_on`, so
      **P0/P2 + S35 does nothing in Seq** — `describe_control()` was
      labelling it `Model select bank0/1` anyway. And with that combo absent
      in Seq, S35 keeps its pattern role under P0/P2 there, so the value row
      shows the pattern name instead of falling through to a bare %.
- [x] **C5 — Rec mode's own status row.** *Done 2026-08-05*, closing the
      half A4 left. The value row **cycles** instead of sitting on the model
      — `kRecCycleMs` (2.6 s) between the slot's model, `Hold P5 save` and
      `+pad copies`. No new mechanism was needed after all: the status row
      already repaints whenever its own text changes, so the cycle drives
      itself and costs one redraw per phase. (The visualizer needed a timer,
      since nothing there repaints without a state event.) Rec is the one
      mode you can be *stuck* in and both ways out are holds on pads you
      aren't otherwise touching, which is what makes this worth the row.
      The blinking marker landed too: `icons_for()` now returns the C10
      capture circle for Seq slot editing as well — no layer stack to show
      there, but it's the other state that looks idle while the device is
      waiting for a deliberate exit.
- [x] **C6 — broader per-mode audit.** The list came out of one hardware
      session on Seq, Rec and Basic Pitch; **Arp/Mel** (Hold/Arp/Rec
      sub-states, layer record/mute messages) was then walked the same way.
      C7 is the first result of that walk; section E (in `ROADMAP.md`) is the
      rest of it.
- [x] **C7 — Arp/Mel: say what state the loop is in, and when it changes.**
      *Done 2026-08-04* on B4. The Rec status row reports loop state instead
      of the model, and **P2+P10** gets a callout on either of its meanings —
      riding the normal priority chain on an `arp_flags` change, since the
      combo's pads are usually already held and so never produce a pad-down
      edge to catch. The **wording** it shipped with reported one flag at a
      time and was replaced same-day by C9 below.
- [x] **C8 — Seq idle row names the pattern.** *Done 2026-08-04* on B3:
      the value row reads e.g. `fourfloor 1/6` and the transport moved up
      beside the genre (`SEQ TECHNO` / `SEQ STOP TECHNO`), so both fit
      without the pattern losing the big font.
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

### D. Per-mode hardware verification checklist — walked

Code-level parity is as far as inspection can go; this was the side-by-side
pass against the visualizer's OLED. Walked 2026-08-04 and again 2026-08-05;
every row below ticked.

- [x] Seq: idle (status row after 2.2 s — `SEQ TECHNO` + the playing
  pattern name), knob turn (incl. tempo as BPM, ext-clock "ext", the new
  S33/S34 wording), pad = drum note, SW1 (IDM/Techno/Electro), model select
  (P0/P2+S35), rec-slot editing (flatter labels, status row `REC P5 CLAP`,
  and the hold-to-save bar → `SAVED` / tap-to-cancel → `CANCELLED`)
- [x] Arp/Mel: knob turn (incl. dead-knob "no effect"), pad = pitched note,
  SW1 (Hold/Arp/Rec), Rec sub-mode knobs (Speed/Shift/Chance/Order), layer
  record/mute messages, model select, and P2+P10's callout + status row
  naming the combined state (**C9**)
- [x] Pitch: knob turn (engine-specific labels via `kEngineKnobs`), pad =
  pitched note, SW1 (Minor/Chromatic/Major), model select
- [x] Cross-mode: SW2 flip, P1+FX layer (reverb/delay, drums vs. pitched
  wording), P0+S37 stereo width, external clock (MIDI vs. CV wording)
- [x] Held-combo progress bar + confirm flash: hold P0+P2 (Seq:
  re-randomize; Arp/Mel: vary sound — 2 stages, or 3 in Pitch mode), hold a
  drum pad P3–P9 (rec entry → "Recording"), P0+P1 in Arp/Mel (sound edit,
  both directions), P2+drum-pad hold in Rec (layer clear → "Cleared"/
  "Empty"), and the copy-confirm hold while recording ("Copied") — bar
  tracks the LED's blink rate, confirm flash lands the same instant the LED
  flashes/switches pattern. A1's reliability fix held up on the first walk;
  the pacing didn't, and A5/A6 rewrote it. Both pacing dials are named
  constants (`kStageAnnounceBlocks`, `kStageFillBlocks`).
- [x] **The 2026-08-05 additions:**
  - [x] **Knob pickup (B1)** — in every mode, grab a pot that's armed: the
    value row reads the *stored* value in its right units, and the track
    makes the direction obvious without thinking. The track vanishes on
    catch rather than lingering; the rails (a target at 0 or 127 catching on
    movement, not crossing) were hit specifically.
  - [x] **Rec P0+P2 per-pad randomize** — pacing in the hand, stage 2 landing
    on role-appropriate sounds, and no pot jumping afterwards (both stages
    re-arm the layer).
  - [x] **Randomize no longer starts the seq** — stage 2 with the transport
    stopped stays stopped; with it running restarts at bar 0.
  - [x] **Rec status cycle (C5)** + the blinking circle in Seq slot editing.
  - [x] **Root readouts** — `P0+P10 ROOT - D#` with the note row,
    `SW1 MINOR - D#`, `PITCH MINOR D#`; the pads really play D♯ minor.
  - [x] **ch10 velocity out** — ghosts vs. downbeats arrive as visibly
    different velocities, and a 4×4 pad controller's bottom two rows drive
    the kit unmapped.

---

## Hardware test round — 2026-08-05

First walk of the 2026-08-05 pass. **Passed and closed: B1's pickup screen
and C5's Rec status-row cycle** — both confirmed in the hand, no changes
needed. Four findings, all fixed same-day:

- [x] **Arp: `P0+P10 ROOT (PITCH ON`.** "Root (Pitch only)" ran to 24 chars
      behind the combo and truncated mid-word, reading as a fault rather than
      a restriction — with the value row sitting empty underneath it. Split
      across the two rows: `P0+P10 ROOT` over `Pitch mode only`. The
      visualizer had the same combo labelled as a working root shift in
      *every* mode, which was worse; it now gates on Basic Pitch too.
- [x] **Seq: silent randomize against a stopped transport.** Only reachable
      because this pass stopped stage 2 force-starting the seq — the gesture
      fired into silence. Each stage now auditions the kick when the seq is
      stopped (a running one is its own confirmation). Slot 0, because it is
      the sound the curation is about.
- [x] **Seq/Pitch: the screen named the lever, not what was loaded.** SW1 is
      change-latched per role, so flicking it in Basic Pitch and returning to
      Seq left the screen naming a genre that was not playing — and picking
      pattern names out of that wrong genre's table, which is the part that
      made it a bug rather than a cosmetic slip. The old code comment called
      reading the lever "a deliberate simplification"; it wasn't. Both latched
      roles are now published (`t.sw1_latch`, state byte 37, both normalised
      to panel order) and every genre/scale/note-name read goes through them.
      A trailing `*` marks the divergence, as suggested — `SEQ TECHNO*`.
      Fixed in the same place: pitched **pad note names** were computed off
      the lever too, so the visualizer's pad legends could show the wrong
      scale entirely.
- [x] **Octave: direction without a destination.** P10/P11 said only
      `Octave +`. Now the value row carries `+1 D#5` — the offset within
      −3…+3 and the note the root lands on — and, in Seq slot editing where
      the same pads retune one drum, that slot's own note.

**Second walk, same day — all four confirmed OK.** One question came out of
it, and became the fifth change:

- [x] **Arp octave range moved from P1+P10/P11 to P0+P10/P11**, swapping with
      Rec's undo (now `P1+P10`). Raised as "why is this on P1?", and the
      answer was that it shouldn't be: the panel's modifier grammar is
      **P0 = sound & pitch · P1 = FX · P2 = transport**, and octave range was
      the only P1 combo that wasn't FX. It was parked there because undo held
      `P0+P10` in Rec, so binding range to P0 would have made one combo mean
      two unrelated things depending on SW1 — the exact fault C11/C12 spent
      this round removing. Swapping fixes both ends at once: `P0+P10/P11` is
      octave range in **all three** sub-states, and undo takes `P1+P10`,
      which nothing used. Deliberately *not* extended into "P1 owns all of
      Rec": Rec's other two gestures are the layer ops and the capture cycle,
      both on P2, and `P2+P10`/`P2+P11` being the melodic/drum transport pair
      is load-bearing symmetry that C9/C12 just verified. Side effects worth
      knowing: the P0 Arp and P0 Arp Rec combo lists became identical and
      collapsed into one, and `P1+P10/P11` outside Rec now falls through to a
      plain octave shift — which is what `P1+P10` already did in Basic Pitch,
      so it is at least uniform.

**Nothing outstanding — the whole pass is walked and ticked.** That covers
section D's 2026-08-05 block (pickup, Rec per-pad randomize, no-autostart, Rec
cycle, root readouts, ch10 velocity), A5/A6's pacing which had been carried
unwalked since 08-04, the four first-walk fixes, the `*` marker, the
octave-range/undo swap, and **E1**'s span readout. The swap was exercised
incidentally — using `P0+P10/P11` is what turned up E1's empty value row.

### E1 — `P0+P10/P11 ARP OCTAVES` leaves the value row empty

*Done 2026-08-05, hardware-verified the same day.* The value row now names
the **span**, not the range — `+1..+3`, or `+1 only` at range 0. Rec reports
`+2 extra` instead, because `t.octave` there is Rec's own octave while the
range governs the arp's climb from `arp_octave`; pairing them would be a
confident lie. `arp_flags` bits 4-5 carry the range, so no new field and no
length guard. Every variant is ≤ 11 chars, so the value row holds one font
across the sweep.

*Original framing:* unfinished business from the same round that gave plain
P10/P11 a value row — the combo version got the label and nothing under it.
What belongs there isn't the range on its own: **base octave and range
compose**, so the useful readout is the span the arp will actually cover —
base `+1` with range 2 climbs to `+3`.

---

## notes.md sweep (2026-08-06) — the v1-era reference tables

`notes.md` still carried its original control reference, which described a
device that no longer exists: SW2 center as **Random mode**, S34 as Kick
punch, S35 disabled in Seq, rec entry at 1200 ms, P0+P2 stages at 1/2/3 s,
Decay on S37, P1 unused. Its header claimed those sections "reflect current
code". `MANUAL.md` has been the real control reference for a long time, and
the drum-pool tables below have been superseded by `kDrumPools` in
`TouchPlaited.cpp` — the single source of truth since the 2026-08-05 kick
curation. Moved here verbatim rather than deleted, because the *shape* of the
old mode layout explains a lot of the naming still in the code.

What stayed in `notes.md`: the hardware/pin reference, the deliberate-decision
rationale, the per-engine knob table, the drum-engine parameter notes (they
feed the open `kDrumKick` question), and the pattern-system format.

### Control reference — as of the Random-mode era

#### Normal modes (SW2: down=Basic Pitch / center=Random / up=Seq)

| Control | Function | In Seq mode |
|---------|----------|-------------|
| **S30** | Drive (soft-clip distortion 0..1) | Drive (overall; per-slot in rec mode) |
| **S31** | Decay, unified — LPG env + model decay (morph) for engines 19–23. LPG colour retired (fixed 0.5) | Tempo (60–180 BPM) |
| **S32** | Harmonics — live in Basic Pitch, per-slot in Random | Shuffle (0–50% swing) |
| **S33** | Timbre — live in Basic Pitch, per-slot in Random | Density (0–4 weight threshold) |
| **S34** | Morph — live in Basic Pitch, per-slot in Random | Kick punch (timbre boost) |
| **S35** | Model select (P0=bank 0–11 / P2=bank 12–23, pickup) | — disabled |
| **S36** | Pitched volume (pickup). Per-slot volume in recording mode | Seq/drum volume (pickup) |
| **S37** | Blend: OUT↔AUX mono mix (0=OUT, 1=AUX). Per-slot blend in recording. **P0 held → stereo width** (0 = mono; group × per-slot multiply, MoveCatch) | Tightness (morph-decay scale, engines 19–23). **P0 held → drum-group stereo width** |
| **SW1** | Scale: Center=Chromatic / Left=Major / Right=Minor | Genre: Center=Techno / Left=Electro / Right=Ambient |
| **SW2** | Playmode: Down=Basic Pitch / Center=Random / Up=Seq | — |
| **P0** | Modifier A — hold + S35 → model 0–11; hold + P10/P11 → root note; hold + S37 → stereo width | hold + S37 → drum-group stereo width |
| **P2** | Modifier B — hold + S35 → model 12–23 | — |
| **P0 + P2** | Re-randomize — hold stages 1s/2s (Basic Pitch: soft tight/wide same engine; Random: full random) | Re-randomize drum sounds — stages 1s/2s (soft variance / full new models) |
| **P3–P9** | Note pads / drum triggers | Drum pads (play kit; manual triggers on top of running seq) |
| **P10** | Octave down (cycles −1, range −3 to +3). P0 held → root note −1 | — |
| **P11** | Octave up (cycles +1). P0 held → root note +1. Disabled while P2 held | — |
| **P2 + P11** | Drum seq play/pause (P2 first, then P11) — works in all 3 playmodes | Same |
| **P1** | Unused. Future: trigger seq patterns against current mode (melodic seq) | Unused |
| **LED** | Blink feedback: SW1/SW2 blinks; root limit; rec confirm; beat pulse | Beat pulse suppressed in recording mode |

**Note:** S30 was previously FM amount (`frequency_modulation_amount`). Without a CV input, `modulations.frequency` is always 0, so FM amount has no effect on any engine. Repurposed as drive.

#### Basic Pitch mode — P0+P2 hold stages

SW2 down = Basic Pitch. P0+P2 hold stages apply here (moved from old Random mode stages 1 & 2).

| Hold duration | Stage | What happens |
|--------------|-------|-------------|
| 1s | 1 — Soft tight | Same engine as current selection. Each pad gets random params within ±0.25 spread. All pads play scale pitches. |
| 2s | 2 — Soft wide | Same engine, larger variance ±0.45. Still scale pitches. |
| 3s | 3 — Clean | Drops the snapshots and restores the clean live-knob sound (audition confirms). |

Stages are cumulative: 3s passes through 1 and 2 first. (Doubled to 2/4/6 s by
the A5 hold-pacing work, 2026-08-04.)

After stage 1 or 2 fires, pads play the randomized snapshots (`bp_slots`) instead of the live knobs. **Escape back to live mode:** hold to stage 3, move any timbral knob (S32/S33/S34/S37) more than 5%, or pick a model with P0/P2+S35.

#### Random mode — P0+P2 hold stages

SW2 center = Random mode. Random is now the true full-random selector — no soft/same-engine stages here.
Drum mode is **not accessible from Random** — it lives in Seq only (SW2 up).

| Hold duration | Stage | What happens |
|--------------|-------|-------------|
| 1s | 1 — Full random | Each slot gets a random engine + params from the full pool (all engines except Chiptune; drum engines 21–23 included, played at scale pitches). Decay locked to current S37 value. |
| 2s | 2 — Full random spread | Each slot gets a random engine + params. Decay spread ±0.25 around current S37 value. |

Stages are cumulative: 2s passes through 1s first.
P0+P2 in Seq mode: staged drum randomize — see Seq mode section below.

#### Recording mode — the unified-entry era

**Unified flow** — same gesture and thresholds in Seq and Random: hold a pad P3–P9 for **1200ms** to enter. The long threshold exists so holding a sustained note in Random doesn't trip recording by accident. (The old P0+pad entry in Random is removed; P0+pad now just plays the note.)

All rec knobs use **true pickup**: each pot takes effect only when it reaches the slot value it edits — no jump, works from either direction (including targets at 0.0/1.0).

| Action | Effect |
|--------|--------|
| Hold pad 1200ms (Seq or Random) | Enter recording; AllNotesOff; audition voice starts (skipped when seq is running — it fires the slot) |
| S32–S34, S37 in rec mode | Edit harmonics / timbre / morph / decay; pot picks up at the slot's value; audition updates live |
| S30 in rec mode | Per-slot drive (Seq: ratio of overall drive, which stays frozen during rec) |
| S36 in rec mode | Per-slot volume |
| P0/P2 + S35 in rec mode | Change that slot's engine only (separate pickup) |
| Release source pad | Fine — recording stays active, hands free to edit knobs |
| Re-hold source pad ≥ 1200ms | Confirm / store; 3 rapid blinks; exit recording mode |
| Different pad touched < 50ms | Ignored (ghost touch / accidental) |
| Different pad held 50ms–1199ms, no source held | Cancel: restore backup, exit recording |
| Source pad held + different pad held ≥ 1200ms | Copy: source params → target slot; **audible confirmation** (the copied sound fires on the target) + 3 blinks; keep holding source to copy to more pads |
| SW2 flip | Cancel recording; restore backup |
| Seq running + in rec mode | Rec slot is force-triggered every *other* step (8th notes — 16ths were overwhelming). BPM beat blink suppressed. To solo the sound: pause the seq (P2+P11) *before* entering recording — the combo is unavailable while recording (P11 = drum pitch there). |

*(That last row is the behaviour filed as ROADMAP **F2** on 2026-08-06 — it
was a known consequence here, and never revisited after C12 fixed the same
class of problem in Arp/Mel Rec.)*

#### Seq mode (SW2 up) — the pre-Arp/Mel description

**Drum editing/controls are exclusively here**, but the sequencer itself runs independently of SW2:

- First SW2-Up entry (or boot with SW2 Up): kit generated + seq auto-starts, knobs live immediately. Later entries keep the last play/pause state.
- Flipping SW2 away from Up does **not** stop the seq — drums keep playing behind Basic Pitch / Random with all seq settings (tempo, shuffle, density, punch, tightness, drive, genre, pattern) locked at their last SW2-Up values.
- **Pickup on re-entry:** every seq pot (S30–S34, S36, S37) must reach its stored value before it takes effect again — using a pot in another mode (or during recording, which borrows most of them) never jumps a seq setting. Pickups re-arm on Seq entry and on recording exit. Values at the pot extremes (0.0/1.0) are reachable — inclusive comparison + near-window.
- **P2+P11** (P2 first) toggles play/pause from any playmode. While P2 is held, P11's octave function is disabled.
- Seq-triggered voices are param-locked in the voice pool (slot ids 16+i), so pitched-mode global knobs (live Basic Pitch params, drive, LPG) can't stomp drum sounds mid-decay.
- Pads P3–P9 in Seq mode play the drum kit directly (also while paused).

| Control | Sequencer function |
|---------|-------------------|
| **P2 + P11** | Play/pause (2 blinks = paused, 3 = playing) |
| **SW1** | Genre: Center=Techno / Left=Electro / Right=Ambient |
| **S30** | Drive (overall; per-slot drive settable in rec mode as percentage of overall) |
| **S31** | Tempo (60–180 BPM) |
| **S32** | Shuffle (0–50% swing) |
| **S33** | Density (0–4) |
| **S34** | Kick punch (timbre boost for kick slot) |
| **S35** | Pattern select within the SW1 genre (pickup; range splits across the genre's pattern count) |
| **S36** | Seq volume — drum group only; pitched-mode volume untouched |
| **S37** | Tightness (scales morph of drum engine slots 21–23 globally; lower = shorter tail) |
| **P3–P9** | Manual trigger on top of running seq |
| **P0 + P2** | Re-randomize drum sounds — staged (see below) |
| **LED** | Beat pulse on step 0 (suppressed when in recording mode) |

Sequencer weight logic: step fires when `weight + density ≥ 5`.

**Mode memory:** switching between playmodes restores the last state for that mode — no re-randomize on mode switch. Only P0+P2 forces a re-randomize. First entry into Seq (or after a full restart) always generates a fresh drum kit.

### Current state by feature — the v1 build-out table

Every row here is long since shipped and hardware-verified; the "needs
hardware test" notes date from the steps that added them.

| Feature | Status |
|---|---|
| Basic Pitch (SW2 down) | Working |
| Basic Pitch P0+P2 randomize — stages 1s/2s (soft tight / soft wide, same engine) | Implemented — needs hardware test (knob-grab escape back to live mode) |
| Random mode — 2 full-random stages (SW2 center) | Implemented — needs hardware test (stage 1 decay locked to S37, stage 2 spread) |
| Drum mode (Seq-exclusive — SW2 up) | Working (SW2 Up only; not reachable from Random) |
| Seq mode (SW2 up, starts immediately) | Working (SW2 Up enters; P1 = play/pause; SW2 flip exits) |
| Voice pool (6 voices, internal SRAM, voice sleep, load-shed guard) | Working — hardware-confirmed, no audible crackling |
| SW1/SW2 blinks on change | Working |
| P0+P2 hold animation + audio preview | Working |
| Model select (P0+S35, P2+S35) | Working |
| Root note (P0+P10/P11) | Working |
| Octave shift (P10/P11) | Working |
| Recording mode — confirm (pad-alone 800ms) | Working |
| Recording mode — entry (hold 800ms drum / P0+pad pitched) | Working |
| Recording mode — cancel/copy (50ms/800ms gesture) | Working |
| Per-slot volume (S36 in rec mode) | Struct field + drum defaults done; S36 pickup in rec mode = Step 5 |
| Per-slot drive (S30 in rec mode) | Struct field done; S30 routing + rec mode pickup = Steps 3 + 5 |
| S30 = drive globally (including Seq: overall × per-slot ratio) | Implemented — needs hardware test (Seq remapped: S31=Tempo S32=Shuffle S33=Density S34=Punch) |
| Seq P0+P2 staged re-randomize (1s=soft variance / 2s=full new kit) | Implemented — needs hardware test |
| Mode memory — per-mode slot arrays (`bp_slots`/`pad_slots`/`drum_slots`) | Implemented — needs hardware test |
| Seq pads play drums (fixed: were playing Random-mode slots) | Implemented — needs hardware test |
| Background drum seq in Basic Pitch / Random (settings locked) | Implemented — voice stealing much reduced at 6 voices + sleep |
| P2+P11 seq play/pause combo (all modes; replaces P1 tap) | Implemented — needs hardware test |
| Seq knob pickup on re-entry / after recording | Implemented — needs hardware test |
| Basic Pitch stage 3 (3s) = restore clean live sound | Implemented — needs hardware test |
| VoicePool global-param cache (NoteOn/Audition rehydrate reused voices) | Implemented — fixes stale drum engine on Basic Pitch pads |
| CPU load meter (serial print every 2s) | Working — baseline measured: idle 44/49, playing up to 95–100 max |
| Voice memory in internal SRAM (was SDRAM) | Working — measured: idle 44%→3% |
| Voice sleep (silent voices skip render) | Working — hardware-confirmed |
| -O3 build (project + Plaits sources) | Working |
| Load-shed guard (escalating; `shed N` in serial line) | Working — hardware-confirmed, no crackling |
| Unified rec entry — hold pad 1200ms in Seq *and* Random | Implemented — needs hardware test |
| Rec knobs true pickup (arm to slot values; extremes reachable) | Implemented — fixes dead S30/S34 + one-directional rec knobs |
| Copy gesture audible confirmation | Implemented — copied sound fires on target |
| Rec force-fire halved to 8th notes | Implemented |
| Seq volume separate from pitched volume (S36 per group, pickup) | Implemented — needs hardware test |
| Drive loudness makeup (−6dB at full drive) | Implemented — tune slope by ear |
| Six-Op random generation anchored to audible presets | Implemented |
| Unified decay (S37→morph for engines 21–23) | Working (slot.decay→patch.morph at seq trigger; tightness on all engine 21–23 slots) |
| Output soft-clip limiter | Working |
| Seq: rec slot force-triggered per step regardless of density | Implemented — was firing every audio block (250×/s); now gated on `StepFired()` |

### Full-Random stage columns

The FR S1/S2/S3 columns of the models table described which engines each
*Random mode* stage could draw — that mode is gone, and the drum pools are
now `kDrumPools`. The engine list itself (with the knob meanings) stays in
`notes.md`; only the FR columns are archived:

FR = Full Random / Drum mode stages. Chiptune (7) excluded from all FR stages.
grp0 = bank-0 engines (0–11), grp1 = bank-1 (12–23); S3 was drum-only
(21–23).

### Per-role drum pools — superseded by `kDrumPools`

These tables were the source of truth until the 2026-08-05 kick curation made
the code's `kDrumPools` table the single owner. Kept for the tuning history
(the ranges themselves are unchanged, and the reasoning notes moved into code
comments beside the table).

**GM ref (as of this table):** 36=Kick · 38=Snare · 42=CHH · 46=OHH · 39=Clap
· 41=Low Tom. *(Perc's anchor became 43 on 2026-08-05.)*

**P3 — Kick**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 21 BassDrum | 0.05–0.30 | 0.20–0.65 | 0.20–0.55 | 36–48 |
| 10 Two-Op FM | 0.10–0.30 | 0.00–0.30 | 0.10–0.40 | 36–48 |

**P4 — Snare**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 22 SnareDrum | 0.10–0.60 | 0.30–0.80 | 0.30–0.70 | 48–60 |
| 17 Noise | 0.05–0.20 | 0.55–0.90 | 0.30–0.70 | 48–60 |

**P5 — Closed Hi-Hat**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 23 HiHat | 0.02–0.12 | 0.50–0.90 | 0.40–0.80 | 84–100 |
| 17 Noise | 0.02–0.10 | 0.65–0.95 | 0.45–0.75 | 84–100 |

Hats moved out of the melodic register (was 60–84): note is the pitch center (23) / filter center (17), and low centers read as tonal noise instead of metal. Timbre floor raised — engine 23's timbre is the metallic ratio.

**P6 — Open Hi-Hat**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 23 HiHat | 0.35–0.60 | 0.45–0.85 | 0.40–0.80 | 80–96 |

**P7 — Clap**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 22 SnareDrum | 0.55–0.90 | 0.65–0.95 | 0.50–0.90 | 48–62 |
| 17 Noise | 0.05–0.20 | 0.70–1.00 | 0.40–0.80 | 55–70 |

**P8 — Tom**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 21 BassDrum | 0.30–0.65 | 0.10–0.40 | 0.30–0.60 | 48–72 |
| 20 Modal | 0.30–0.70 | 0.20–0.60 | 0.10–0.50 | 48–72 |

**P9 — Perc**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 20 Modal | 0.10–0.30 | 0.20–0.55 | 0.30–0.60 | 60–84 |
| 23 HiHat | 0.10–0.30 | 0.50–0.90 | 0.50–1.00 | 76–96 |
| 22 SnareDrum | 0.05–0.20 | 0.05–0.35 | 0.30–0.60 | 66–80 |

String (19) removed from the Perc pool — Karplus-Strong reads as a loud melodic pluck, not percussion. Modal tail capped (was 0.40–0.90). Snare engine pitched high with body-heavy timbre = rim/wood tick. Particle (18) removed from Snare/Clap/Perc pools.

Default per-slot volumes at randomize time (tuning targets): Kick 0.9, Snare 0.8, CHH 0.55, OHH 0.65, Clap 0.75, Tom 0.7, Perc 0.5 (was 0.6 — perc sat too far forward).

---

## Deliberate differences from the visualizer — settled

Confirmed as intended during the 2026-08-04/05 rounds; don't "fix" without
deciding the UX first.

- Rec drum-slot editing uses flatter "Slot X" labels vs. the web's full
  per-knob nesting (128×32 char budget) — `display/oled_ui.h`.
- The dead-knob message is `no effect`, not `no effect on <model>`.
- ~~No idle/status fallback~~ — resolved by A4 (2026-08-04): both sides now
  show the same per-mode status row after the same 2.2 s timeout.

---

## V. Visualizer round — all six shipped (moved from `ROADMAP.md` 2026-08-07)

The round `ROADMAP.md` filed as *Next up item 2*: a pass over the app in its
own right, after six protocol additions had been tracked field-by-field
without anyone standing back from it. Nothing here touched the protocol or the
firmware. Verified in a headless browser at desktop, phone-portrait and
phone-landscape sizes; the two layout items were checked against measured
element rects, not against how the screenshots looked.

- **V1 — the mini OLED stopped tracking the drawing below a certain size.**
  Cause: `place()` in `oled-mini.ts` clamped the dot-grid cell to
  `MIN_CELL = 3` and then *sized the element from the clamped cell*, so under
  ~384 device px the screen stopped shrinking while the drawing carried on
  down — measured at panel zoom 0.32 the screen stayed 384 px wide against a
  61 px zone, floating clear of the panel entirely. The clamp itself is right
  (a whole-device-pixel grid is what keeps the dots crisp); sizing the element
  from it was the bug. Outside `[MIN_CELL, MAX_CELL]` the letterboxed size now
  wins and the canvas is CSS-scaled — the 1:1 device-pixel mapping gives way,
  not the geometry. A squeezed screen also drops the inter-dot gaps and lets
  the browser resample, because nearest-neighbour downscaling of a dot grid
  eats whole rows of dots and with them the letterforms.
  Worth knowing: at a normal desktop window the screen is *already* in the
  squeezed path (a 217 px zone wants cell 1.7), and that is what the crisp
  screenshots show — the snapped path only engages when the drawing is zoomed
  well in.
- **V2 — the expanded display sat on the user LED.** `oled-wide.ts`'s SCREEN
  covered the whole Daisy silhouette out to x 216; the two LEDs are at
  x 193.5–202.1, and `#led-user` is driven by `bindings.ts` — it carries the
  limit/state blinks and reads nowhere else on the drawing. Right edge now
  stops at 190. What stays covered is board only (the reset/boot buttons at
  x 169–182 have no state to show). Cost: the screen is 132 units wide instead
  of 158, so its text is ~16% smaller at the same label scale — three lines
  fit more comfortably in the same height as a result.
- **V3 — one settings bar.** The buttons lived in two rows that had each grown
  their own idiom: a cluster pinned to the drawing (label/screen text size +
  reset) and the info panel's title bar (overlay mode, info font size, reset).
  New `ui/settings-bar.ts` collects them at the stage's top-left, with ⚙ to
  collapse (persisted; collapsed by default on phone-sized viewports). Two
  decisions taken on the walk:
  - *Fixed to the stage, not to the drawing.* It's chrome, not part of the
    device, and a settings row that slides around while you pan is what made
    the old cluster hard to aim at.
  - *Two A−/A+ pairs, not one.* They resize different text (faceplate labels +
    screens vs. the info panel) and the finer control is worth keeping for
    video framing — but they now step and clamp identically (0.15, 0.6–2.2;
    the info scale used to run to 3×), which is what "resizing should behave
    the same" was asking for. Each pair is captioned, since they are otherwise
    indistinguishable.
  What deliberately stayed put: both ⠿ drag grips. A drag handle has to be on
  the thing it drags. The drawing's grip now steps below the bar when the two
  land on the same corner (`layout.ts` `place()` takes `settings.bounds()`).
- **V4 — the menu read as one flat list.** A transport, a panel toggle and a
  link to another page all looked alike, and the entries that navigate away
  didn't read as links. Three captioned sections now — Connect / Display /
  Elsewhere — with the site links as real `<a>` rows carrying → (same tab) or
  ↗ (new tab). The code map joined the list; it had never been linked from
  the app.
- **V5 — auto-fit on mobile.** Four separate things, and the one that actually
  broke it was `height: 100vh`: on a phone that is the viewport with the URL
  bar *hidden*, so the drawing was sized to a box taller than what you can see
  and its bottom sat under the browser chrome — with `overflow: hidden` and no
  scrolling, pinching was the only way to reach it. Now `100dvh` (with `vh` as
  the fallback). Then: the drawing takes its intrinsic height when the
  viewport is narrower than 232:361, so the width-limited case collects all
  its slack at the bottom instead of splitting it into two useless bands with
  the info panel over the pads; turning the phone re-fits, since a pan saved in
  the other orientation points somewhere that no longer exists (the stored
  transform now carries the orientation it was made in); and a *Fit to screen*
  menu entry for everything else.
  **Found while measuring, not filed:** the info panel is positioned from
  `offsetHeight` before its content lands, which on a phone left its bottom
  third off the screen with no way to scroll to it. A `ResizeObserver` now
  re-clamps the box into the stage whenever it changes size, which covers the
  fill-in, the model section expanding and a font change alike.
- **V6 — keep the phone awake.** `navigator.wakeLock.request('screen')` on a
  menu toggle (the API needs a user gesture, so it can't be applied on load),
  re-taken on `visibilitychange` — the browser drops the lock on every tab
  switch, screen blank and rotation — and, after a reload, on the first tap
  anywhere, which is the earliest gesture we're allowed to use. The wish is
  persisted; the lock is not, because it can't be.
  The roadmap asked what Fullscreen alone buys on iOS: **nothing here.**
  Fullscreen doesn't hold the screen up on any platform, and iPhone Safari has
  no Fullscreen API at all (iPad and video elements only) — which is why the
  existing entry is feature-detected and simply doesn't appear there. So on
  iPhone the wake lock is the only lever, and only from iOS 16.4; below that
  the entry reads *n/a* and says so in its tooltip rather than pretending.
