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
- [~] **A3 — stop the value row changing size, scroll instead.** *Deferred
      2026-08-05, deliberately: the shrink-to-fit rule stays for now.* Filed
      as a **possible change**, not a pending one — nothing else is waiting
      on it, and the two items that used to cite it (C1/C2's wording) were
      solved by choosing strings that don't straddle a font boundary, which
      is the cheaper answer. The one place a fixed font is now in use is
      `ShowPickup` (B1), where the value doesn't move while you hunt for it,
      so there is nothing for a font change to signal. Revisit only if a
      real string on hardware reads badly while resizing. The original
      analysis, still accurate: decide the rule: one fixed font per context (probably `Font_11x18` for values,
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
      `visualizer/PLAN.md` §2. **Needs hardware verification** (section D).
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
- [X] **C6 — broader per-mode audit.** Standing item. The list above came
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
- [x] Arp/Mel: knob turn (incl. dead-knob "no effect"), pad = pitched note,
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
- [x] **The 2026-08-05 pass, all code-verified only:**
  - [x] **Knob pickup (B1)** — in every mode, grab a pot that's armed: does the
    value row read the *stored* value in its right units, and does the track
    make the direction obvious without thinking? Check the moment of catch —
    the track should vanish, not linger. Worth hitting the rails
    specifically (a target at 0 or 127 catches on movement, not crossing).
  - [x] **Rec P0+P2 per-pad randomize** — pacing in the hand, and whether stage 2
    actually lands on role-appropriate sounds often enough. Also that no pot
    jumps afterwards (both stages re-arm the layer).
  - [x] **Randomize no longer starts the seq** — stage 2 with the transport
    stopped should stay stopped; with it running should restart at bar 0.
  - [x] **Rec status cycle (C5)** + the new blinking circle in Seq slot editing.
  - [x] **Root readouts** — `P0+P10 ROOT - D#` with the note row, `SW1 MINOR - D#`,
    `PITCH MINOR D#`; and confirm by ear that the pads really play D♯ minor.
  - [x] **ch10 velocity out** — ghosts vs. downbeats should be visibly different
    velocities on whatever's listening; and a 4×4 pad controller's bottom two
    rows should drive the kit unmapped.

### Deliberate differences from the visualizer — don't "fix" without deciding

- [x] Rec drum-slot editing uses flatter "Slot X" labels vs. the web's full
  per-knob nesting (128×32 char budget) — `display/oled_ui.h`.
- [x] The dead-knob message is `no effect`, not `no effect on <model>`.
- [x] ~~No idle/status fallback~~ — resolved by A4 (2026-08-04): both sides now
  show the same per-mode status row after the same 2.2 s timeout.

## Parking Lot - performance 

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.


--

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

**Still needs hardware — only the swap.** Everything else from this pass is
walked and ticked, including section D's whole 2026-08-05 block (pickup, Rec
per-pad randomize, no-autostart, Rec cycle, root readouts, ch10 velocity) and
A5/A6's pacing, which had been carried over unwalked since 08-04. The swap is
a muscle-memory change more than a code one: does `P0+P10/P11` feel right for
range, and is undo still findable on `P1+P10`?

## Open items summary

The 2026-08-05 pass closed the previous list — Kick curation (all three
parts), MIDI drum "phase 2" (velocity instead of transposition), Chiptune
(closed as won't-ship), Root note (clamp kept, root/scale now named on
screen), **B1** and **C5**. What's left:

### Next up, once this branch is on `main` — in this order

Agreed 2026-08-05. Nothing here is blocked; the ordering is deliberate,
because each step narrows what the next one has to read.

- [ ] **1 — Reorganize this file into `notesarchive/`.** Everything ticked
      through the 2026-08-04/05 rounds is history now, and it is crowding the
      items that are actually open: sections A–D are almost entirely `[x]`,
      and the hardware-walk checklists have served their purpose. Move the
      closed material out the way the rest of the project already does
      (`notesarchive/notes_archive_2026-08.md`), keeping this file to what is
      genuinely future work. Do this **first** — the doc sweep below is much
      cheaper against a roadmap that only lists open items.
- [ ] **2 — Read MANUAL and README end-to-end.** Both have been updated
      per-change and neither has been read whole since the OLED work started.
      The screen has since grown a status row, a pickup display, combo lists,
      capture indicators, the `*` marker and the octave/root readouts, and
      README still describes the project from before most of that. Check
      against **shipped behaviour**, not against these notes — the notes are
      what would repeat an error rather than catch it. Expect findings to
      split into doc bugs and real bugs; file the second kind here.
- [ ] **3 — A round of visualizer tweaks.** The app has been kept in
      lockstep field-by-field through six protocol additions without anyone
      standing back from it. Worth a pass in its own right now that the
      firmware side has settled.

### E. Arp/Mel screen round (filed 2026-08-05, from the second walk)

Three findings on the same screen, in rising order of cost. The last one is
the one worth doing first. Scoped here rather than bolted onto the branch that
raised them, since two of the three need new telemetry.

- [x] **E1 — `P0+P10/P11 ARP OCTAVES -` leaves the value row empty.** *Done
      2026-08-05, as scoped: the value row now names the **span**, not the
      range — `+1..+3`, or `+1 only` at range 0. Rec reports `+2 extra`
      instead, because `t.octave` there is Rec's own octave while the range
      governs the arp's climb from `arp_octave`; pairing them would be a
      confident lie. `arp_flags` bits4-5 carry the range, so no new field and
      no length guard. Every variant is <= 11 chars, so the value row holds
      one font across the sweep. Needs hardware.*
      *Original:* This is
      unfinished business from the same round that gave plain P10/P11 a value
      row — the combo version got the label and nothing under it. What belongs
      there isn't the range on its own: **base octave and range compose**, so
      the useful readout is the span the arp will actually cover — base `+1`
      with range 2 climbs to `+3`. Something like `+1..+3` (or with note
      names). **Cheap:** `arp_oct_range` isn't published today, but `arp_flags`
      uses only bits 0-3 and the value is 0..3, so bits 4-5 take it with no
      new field and no length-guard change.
- [ ] **E2 — `P1+P10 UNDO LAYER` likewise, and the layer display generally.**
      Two separable things, deliberately split:
      - *The undo readout* is easy and worth doing: name **what the next press
        will remove** — the open take, or `Layer 3` — rather than leaving the
        row blank. `rec_layers` and `rec_mute` are already published and
        `icons_for()` already derives the open take from them.
      - *Bigger layer squares in the value row* is *not* just a rendering
        change, and shouldn't be taken on its own. C10 already draws the
        five-dot stack on the **label** row precisely so it survives every
        callout; drawing it large in the value row either duplicates it at two
        sizes or gives up that persistence. Agreed on the walk that **Rec's
        layer UX needs its own pass** — this belongs in that, not ahead of it.
- [ ] **E3 — Arp/Hold: show the pool with the held notes marked.** The best of
      the three: a note row in the style of the root-note readout, with a
      marker row aligning a filled/hollow shape under each pad.
      **The catch that decides the design: in Hold, the pool is not the pads.**
      `Arp::notes_[]` latches — a note stays in the pool after you lift, and a
      re-touch toggles it off — so building this from `t.pads` (which *is*
      published) would look correct in Arp and be silently useless in Hold,
      which is the exact state where the display earns its keep: fingers off,
      four notes latched, nothing on the panel telling you which four. Needs
      `Arp::PoolMask()` (7 bits, one accessor over `notes_[]`) and one
      published byte.
      Two implementation notes so they aren't rediscovered: the markers must
      be **drawn, not written** — Font_6x8 accepts ASCII 32-126 only and
      `WriteString` aborts the whole string on the first rejected char, so
      filled shapes can't be glyphs (draw them like `StatusIcons` does). And
      the layout fits: 32 px is four 8-px rows, this needs label + notes +
      markers, leaving one spare.

**Still genuinely open:**
- **A3** — value-row font stepping. Deferred by decision, not blocked;
  listed as a possible change. See the item for why nothing depends on it.
- **`kDrumKick` is two engines wide** (21 Bass drum, 10 FM 2-op) — option (c)
  of the old kick-curation item, untouched. Now that the pools are a table,
  widening one is a one-line edit plus an audition pass; it's a taste call,
  not a code problem. **Open question (2026-08-05): can more kicks come from
  existing engines, or do they need writing?** Two routes, and they are not
  equally expensive. (a) A pool entry is an engine *plus a parameter
  preset* — several non-drum engines sit in kick territory at the right
  harmonics/decay (Waveshaping and the 2-op FM already do; Modal and Particle
  plausibly), so widening may be preset work rather than DSP work. (b) A
  purpose-written engine is a real addition to `thirdparty/plaits`, worth it
  only if (a) is exhausted. Audition (a) before considering (b).

- **Weight → audio level.** MIDI out now carries the pattern's accents as
  velocity, but the *audio* still fires every step at full level — weight is
  only a density gate there. Making ghosts quieter internally is arguably the
  point of ghost notes, but it changes the shipped feel of every pattern, so
  it's filed rather than done.

