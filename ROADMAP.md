# TouchPlaited — Roadmap

**This file is the single owner of future work** — ideas and priorities live
here and nowhere else; a roadmap item links to its analysis when one exists.
Once an item is done it moves to `notesarchive/`, so anything still written
below is genuinely open.

Design decisions, budget analyses, and implementation write-ups live in
`notes.md`, with resolved material in `notesarchive/`:

- **v1 stable — 2026-07-08.** The numbered implementation steps that built it
  (Steps 1–15: SW2 mode restructure → recording redesign → soft-clip output →
  mode memory → background drum seq → voice expansion + sleep + shed guard →
  MIDI notes/CC/clock) are in `notesarchive/roadmap_v1_archive.md`. The
  workflow story since the very first prompt is in `notesarchive/readme.md`.
- **Six-Op gate fix — hardware-verified 2026-07-24.**
  `notesarchive/notes_archive_2026-07.md` → "Priority 1 — Six-Op gate fix".
- **SSD1306 128×32 OLED bring-up** — driver bugs found, fixed, and merged
  into the Synthux libDaisy fork; also filed upstream against
  `electro-smith/libDaisy` (#634). `notesarchive/notes_archive_2026-07.md` →
  "SSD1306 128×32 OLED", closing note in `notes_archive_2026-08.md`.
- **The 2026-08-04/05 rounds — closed and walked twice.** Kick curation, MIDI
  drum velocity, Chiptune (won't ship), root-note readouts, and the whole
  OLED round (A1–A6, B1–B4, C1–C12, section D's hardware checklists, E1) are
  in `notesarchive/notes_archive_2026-08.md`. Item letters/numbers there are
  still the reference for cross-links elsewhere in the repo.
- **The visualizer round (V1–V6) — shipped 2026-08-07.** The app's own pass:
  the mini screen's size floor, the expanded display uncovering the user LED,
  one settings bar, a grouped menu, mobile auto-fit and the screen wake lock.
  `notesarchive/notes_archive_2026-08.md` → "V. Visualizer round".
- **The kick lab — shipped 2026-08-08, branch `kick-lab`.** Eleven curated
  kicks as the kick pad's pool and as a loadable model, Tone/Punch/Body knob
  windows, kick voice priority, and the two voice-pool faults chasing it
  turned up. `notesarchive/notes_archive_2026-08.md` → "The kick-lab round";
  the analysis anyone editing the bank needs stays live in `notes.md` →
  "Kick lab".
- **Roadmap sweep 2026-08-08.** F1, F2, the A2 leftover, E3 and the two
  completed "next up" steps moved out.
  `notesarchive/notes_archive_2026-08.md` → "Roadmap sweep 2026-08-08".
- **The stale `libdaisy.a` evening — 2026-08-10, resolved.** Both units back
  to normal. An out-of-date prebuilt library linked against newer submodule
  headers, presenting as pads firing by themselves. Read
  `notes.md` → *The stale `libdaisy.a`* before picking up anything below; it
  voids one previous conclusion and adds a build rule
  (**move the submodule pin → `make -C lib/libDaisy`**).
- **Plaits' tables in DTCM — measured 2026-08-10, ~1%, not merged.** A
  deliberate negative result: it rules data fetch out and leaves ITCM as the
  only remaining lever. It also re-baselined Six-Op's cost, which had drifted
  14 points since 08-07 without anyone noticing. `notes.md` → "Plaits' tables
  in DTCM". Any CPU claim below that predates this wants re-checking.

---

## Next up — in this order

The ordering is deliberate: each step narrows what the next one has to read.

- [ ] **1 — Listen to the kick bank's knob windows.** Section *Sound* below.
      The one thing the kick lab left unfinished, and it needs ears, not code.
- [ ] **2 — Code map check & update.** Section *Docs & site* below.

## Docs & site

- [ ] **Code map — check & update.** `tools/codemap.html` (served at
      `/codemap/`) is the interactive hardware + memory atlas, and it hasn't
      been walked against the source since a good deal shipped: the OLED and
      its I2C interlock, protocol **v12** telemetry, the per-engine held-note
      cap, the output limiter, the re-measured performance figures, and now
      the kick bank (`synth/kick_presets.h`, the voice-pool steal order and
      the drum sleep threshold). Same method as the 2026-08-06 doc read —
      check it against the source, not against the notes. Brief and design
      history: `doc/codemap_brief.md` → `notesarchive/codemap-brief-archive.md`.

## Sound

- [ ] **Kick bank — are the Tone/Punch/Body windows drawn in the right
      places?** The last open piece of the kick lab. Each of the eleven
      presets carries a per-knob window (`KickAxes`, `synth/kick_presets.h`)
      that S33/S34/S37 sweep inside, so one gesture means the same thing
      across the bank and no knob can be turned out of kick territory. Those
      windows were derived from **reading** the engines, not from playing
      them — so the failure modes to listen for are a knob that runs out of
      usefulness before its end, and one that does nothing over half its
      travel. Report per preset and per knob; the numbers are one edit each.
      Two smaller questions riding along: whether **FOLD SUB** is still long
      enough after its decay came down 0.60 → 0.34 (the LPG trap fix, which
      shortened an already-approved preset), and whether the randomizer
      landing on **909+808** — the one two-voice entry — is noticeable against
      a dense pattern. Analysis: `notes.md` → *Kick lab*.

- [ ] **A full ADSR — today there is only a decay** (filed 2026-08-07 from
      the sketch). The wish is A/D/S/R where S31 is now the single unified
      Decay.
      **First, the question the sketch asks — what is controlling "ADS" in
      the current Decay, and why does it differ per model type?** It was
      worth chasing, because the answer decides how expensive the feature
      is. There is no envelope of ours anywhere: `PlaitsVoice::Init` patches
      the trigger and pins `level` at 1.0 while leaving `level_patched`
      **false** (`synth/plaits_voice.cpp:107-110`), which puts Plaits' own
      LPG into *ping* mode — `lpg_envelope_.ProcessPing(attack, ...)`,
      `thirdparty/plaits/dsp/voice.cc:242-243`. In that mode the attack is
      derived from the note frequency (high notes attack faster, and you
      cannot set it), the fall comes from `patch.decay` and `lpg_colour`
      (`voice.cc:236-237`), and there is **no sustain segment at all**: the
      note decays whether or not the pad is still down. So "A" is fixed and
      pitch-dependent, "S" doesn't exist, and "R" is just "D" — which is
      exactly the shape the sketch drew, with the arrow marked *no longer
      held* landing on a curve that was already falling.
      **The per-model difference is real and has a single cause**: engines
      flagged `already_enveloped` bypass the LPG entirely
      (`voice.cc:230-231`), and those are the morph-decay family — Six-Op
      2–4 and 19–23 — where S31 is routed to `patch.morph` instead of
      `patch.decay` (`decay_via_morph()`, `TouchPlaited.cpp`). On those
      engines the knob is the *model's own* envelope time (the DX7 EG for
      Six-Op, damping/tail for 19–23), and the gate genuinely holds — Six-Op
      keys its EG for as long as the trigger line stays high
      (`plaits_voice.cpp:133-137`). That is why Six-Op feels like it has its
      own decay mapped onto the same knob: it does. `MANUAL.md` →
      *Unified Decay* already documents the routing; what it doesn't say is
      that the two halves are different *kinds* of envelope, which is the
      part that makes a shared ADSR hard.
      **The kick lab measured how badly the LPG half behaves at long
      settings** and it belongs in this design: ping mode ignores the gate
      entirely and its fall is violently non-linear — decay 0.60 takes 4.5 s
      to reach silence, 0.72 takes 7.4 s (`notes.md` → *The LPG tail trap*).
      Any A/D/S/R that keeps using `patch.decay` inherits that curve; one
      that patches `level` instead escapes it, which is a point in favour of
      the first option below.
      **What building it would take**, in rising order of cost:
      - *The DSP is nearly free for the LPG engines.* Set
        `level_patched = true` and write our own AD/ADSR into
        `modulations.level` each block; Plaits then runs
        `lpg_envelope_.ProcessLP(compressed_level, ...)`
        (`voice.cc:239-240`), which is precisely the "envelope patched in"
        path the hardware module uses. One catch that is not cosmetic:
        patching level also makes `p.accent` follow it
        (`voice.cc:162`, `0.8f` fixed today), so **timbre** moves with the
        envelope, not just level. That is Plaits' intent, but it changes the
        shipped sound of every LPG engine, so it wants an audition pass.
      - *The morph-decay engines need a different answer*, since their LPG
        is bypassed — our envelope would have to be applied as a plain VCA
        after `Render`, and S31's current meaning on them (the model's own
        time) has to be reconciled with a D knob that means something else.
        Possibly they simply keep the unified Decay and opt out of A/S/R.
      - *The control surface is the real cost.* Four knobs where there is
        one, and S31 is currently the same Decay in Basic Pitch, sound edit,
        Arp/Hold and Rec — the one knob genuinely shared between modes. The
        sketch puts A D S R in a row with S31 under them, which is a knob
        layer, not four free pots. Decide that before any of the above.
      **Design this together with *Weight → audio level* below** — both want
      a per-note level into `modulations.level`, and doing them separately
      means building that path twice.

      > Decision:
      > On Hold: most likely interaction would be the FX modifier and then swap S31 - S34 with A D S R (moves decay temporary from current implementation)
      > Another option would be to just make S31 a knob that makes a curve from 1 - 0; to: 0 - 1 - 0; to 0 - 1 
      > - However in this case: the decay setting won't be like our current implementation being a short burst, up to long decay.
      > - E.g. with samples with a set length you could use that as the length info Spotykach has a envelope from off to fade-out to fade-in/out to fade-in.
      > Thin we'd need at least an AD Attack and a Decay (Decay being like a release) But then we'd still need at least two knobs, making it again "might as well use 4 in the FX mode

- [ ] **Weight → audio level.** MIDI out now carries the pattern's accents as
      velocity, but the *audio* still fires every step at full level — weight
      is only a density gate there. Making ghosts quieter internally is
      arguably the point of ghost notes, but it changes the shipped feel of
      every pattern, so it's filed rather than done. Analysis: `notes.md` →
      *Drum pattern system* (the `0xCW` step encoding and the
      `weight + density >= 5` rule this would reinterpret).
      Shares its mechanism with the ADSR item above: both need a per-note
      value on `modulations.level`, and patching that also switches Plaits'
      accent from a fixed 0.8 to follow it — so build the path once.

## Screens

- [ ] **E2 — `P1+P10 UNDO LAYER` leaves the value row empty.** Name **what
      the next press will remove** — the open take, or `Layer 3` — rather
      than leaving the row blank. `rec_layers` and `rec_mute` are already
      published and `icons_for()` already derives the open take from them.
      *Scoped down 2026-08-08 by the decision below:* the other half of the
      original filing (bigger layer squares in the value row) is dropped.
      C10 already draws the five-dot stack on the **label** row precisely so
      it survives every callout, and drawing it large in the value row either
      duplicates it at two sizes or gives up that persistence.

      > Decision:
      >  - add the info for "what the next press will remove"  a repeated or larger icon row is not needed      

- [ ] **The pool row doesn't redraw on pad touch/release** *(raised on
      hardware with E3, 2026-08-06)*. In Arp/Hold, touching and releasing
      notes changes the pool but the screen waits for the next redraw
      trigger, so the row lags exactly when it is being used. Each pad-down
      and pad-up should force the update.

      > Important note:
      >
      > While holding notes and touching / releasing notes, the display does not immediatly update. In this case we should make each pad touched, pad released trigger the update.

- [ ] **Show the touched notes in Basic Pitch too** *(same walk)*. The pool
      row's counterpart for the pitched mode: marking which pads are sounding
      also shows **which voice is lost** when more notes are held than the
      per-engine cap allows (`kBPMaxHeld` / `kBPMaxHeldHeavy`), which is
      currently silent.

      > In Pitch mode:
      > We'll also show the touched notes, this can also help show which voice is 'lost' when more notes are touched

- [~] **A3 — stop the value row changing size, scroll instead.** *Deferred
      2026-08-05, deliberately: the shrink-to-fit rule stays for now.* Filed
      as a **possible change**, not a pending one — nothing else is waiting
      on it, and the two items that used to cite it (C1/C2's wording) were
      solved by choosing strings that don't straddle a font boundary, which
      is the cheaper answer. The one place a fixed font is now in use is
      `ShowPickup` (B1), where the value doesn't move while you hunt for it,
      so there is nothing for a font change to signal. Revisit only if a
      real string on hardware reads badly while resizing.
      The original analysis, still accurate: decide the rule — one fixed font
      per context (probably `Font_11x18` for values, `Font_6x8` for the label
      row as now), and when the string overflows, hold it still for a beat,
      then marquee horizontally rather than shrinking. Two things this runs
      into: (i) a marquee needs *continuous* redraws, so give it a stop
      condition (scroll once or twice, then park at the end, or park
      truncated); (ii) every redraw is an I2C transfer on the bus shared with
      the MPR121 touch controller (`i2c1_lock.h`), which is what the throttle
      bounds — **40 ms since 2026-08-07**, and only safe at that because the
      interlock is now held per *page* rather than per frame. Don't let a
      marquee run when nothing is being scrolled. Emulator parity:
      `pickValueFont()` in `oled-mini.ts` implements the same shrinking rule
      and has to change with it.
      One measured caveat for whoever builds this: redraw rate is bounded by
      **audio load, not the bus**. A frame is wall clock, and the audio ISR
      preempts it — at 79% CPU a frame took ~52 ms against ~13 ms of actual
      I2C traffic, and observed rate topped out near 19 fps where the 40 ms
      interval allows 25. Moving the display to its own faster bus was built,
      rewired and measured, and changed nothing observable (`notes.md`).

## Hardware verification still owed

- [ ] **CV clock in/out trigger thresholds.** The Schmitt-triggered
      pulse-to-MIDI-clock bridge on S43 (in) / S40 (out) is implemented and
      the wording on screen was checked in the 2026-08-04 cross-mode walk,
      but the **thresholds themselves** have never been verified against a
      real CV source. `notes.md` had been claiming this is tracked here; it
      wasn't, so it is now. Behaviour: `MANUAL.md` → "Clock sync — MIDI and
      CV"; the original sketch is in
      `notesarchive/notes_archive_2026-07.md` → "Syncing".

## Upstream — libDaisy PRs

Two PRs open against `electro-smith/libDaisy`, both from bugs this firmware
hit. Reference material — the column-offset rule, why the OLED bug existed,
the probe, and the fork/pin topology — is in `notes.md` → *Upstream libDaisy*.
Maintainer (`stephenhensley`) has explicitly welcomed further PRs.

- [ ] **#707 — SSD1306 128×32 column start.** Reworked per review and pushed
      2026-08-08 (`3d4803e6`): default is now geometry-derived
      (`(width == 64 && height == 32) ? 0x12 : 0x10`) and exposed as a
      `Config` member, which answers the maintainer's concern that a flat
      `0x10` would break the kxmx_bluemchen's 64×32 panel. Hardware-verified
      on the 128×32 I2C build via `oled_probe/`. **Waiting on:** the comment
      being posted with the photo collage, then review. Two questions raised
      for the maintainer to rule on — the fourth copy of the same bug in
      `oled_sh1106.h:24` (latent; both its aliases are 128×64), and whether
      the 64×48 aliases should get `0x12` by the offset rule (unreported,
      probably unused — deliberately not changed on a guess).

- [ ] **#708 — `HAL_UART_ErrorCallback` null deref.** Needs the **TRS MIDI
      unit**, which is why it's still open. The maintainer gave a merge path
      — *"if that works for you, please update... then I can give this a
      quick regression test and we can merge it"* — and two non-equivalent
      variants. Push **v1** (null guard, `DmaTransferFinished` always runs);
      it keeps existing behaviour and only adds the guard. If the freeze
      survives it, **v2** (skip `DmaTransferFinished` while in listener mode)
      is the likely answer — and that difference lands exactly on the TRS
      MIDI listening path this firmware uses, so whichever holds is real
      information for him. Note his v2 snippet dereferences `handle`
      unguarded, i.e. the very bug being fixed; he flags that himself.

- [ ] **Fork sync — after #707 merges, not before.** `origin/master`
      (Synthux-Academy) currently carries the superseded flat `0x10`, and
      `origin/touchplaited-pin` — what the submodule actually pins — carries
      it too plus the re-applied UART fix that fork master reverted. Doing
      this before the PRs settle just creates a third state. Then:
      `git checkout master && git merge upstream/master && git push`, rebase
      `touchplaited-pin` onto it, re-pin the submodule here and commit that.
      Worth doing in the same sitting as #708.

## Parking lot — performance

Re-measured 2026-08-10 on hardware (Six-Op C, one group, no FX): **idle 15%,
then 36 / 50 / 64 / 78% for one to four held notes** — ~14% per added voice.
These supersede the 08-07 figures (16 / 41 / 58 / 75 / 92, ~17% per voice)
that the rest of this file was written against; fourteen points of headroom
at four voices appeared between the two dates and the cause is not known. See
`notes.md` → "Plaits' tables in DTCM". Raw captures and the per-voice analysis
are in `notes.md` → "Six-Op crackle". **Always measure a same-day control
before crediting a change with a CPU win.**

- **480MHz boost (`hw.Init(true)`) — verdict VOID, needs a clean retest.**
  Worth ~20% of the audio budget for one character, and libDaisy's default is
  400MHz. It was tried on 2026-08-10 and blamed for the evening's chaos, but
  that test ran against the stale `libdaisy.a`, so **the result means
  nothing** and the write-up on branch `cpu-boost` states its conclusion far
  too confidently. What was actually established: boost changes the core clock
  and D1HCLK only — SAI (audio rate), ADC and I2C4 run from PLL3, FMC/SDRAM
  and SDMMC from PLL2, all from constants independent of `cpu_freq`, and both
  the old and new libDaisy carry the 120MHz PCLK1 I2C timing branch. The one
  unproven mechanism left is QSPI XIP: the flash clock goes 100 → 120MHz while
  the memory-mapped read keeps 6 dummy cycles, past the IS25LP064A's rating
  for that dummy count. Retest on a clean build, and **build it with
  `-DNO_PERSIST`** so a bad run cannot poison the settings journal again.

- **Plaits lookup tables in DTCM — MEASURED, ~1% flat. Do not merge.**
  Branch `cpu-boost`, commit `c81a510`. A same-day A/B on one device gave
  15 / 35 / 49 / 63 / 77% against the control's 15 / 36 / 50 / 64 / 78 —
  a one-point offset with the **per-voice slope completely unchanged** at 14%.
  The tables were never the bottleneck; the D-cache was already absorbing
  them. Not worth a 324-line linker script and 75KB of DTCM that cuts the
  stack guard from 128KB to 56KB. Full write-up, including why the negative
  result is useful, in `notes.md` → "Plaits' tables in DTCM".

  **Keep the infrastructure, drop the placement.** `LDSCRIPT` set before
  libDaisy's core Makefile include (it declares `LDSCRIPT` with `?=`, so
  setting it first wins), the `EXCLUDE_FILE` mechanics, the priority-101
  constructor that copies ahead of every static ctor, and the link-time
  `ASSERT` — ITCM needs all of it.

- **ITCM placement — now the only lever left, and the one the DTCM result
  points at.** Move the hottest Plaits render paths into ITCMRAM (64 KB, 0%
  used); code currently executes from QSPI. Since data fetch has been ruled
  out by measurement, instruction fetch is what remains, and Six-Op costs
  **~14% of a block per voice** (idle 15%, then 36 / 50 / 64 / 78% for one to
  four held) — so everything else on this list is rationing. Budget: the
  Six-Op path is ~22.7 KB of `.text` before `--gc-sections`
  (`six_op_engine.o` 14064 B, `voice.o` 4200, `algorithms.o` 2808,
  `plaits_voice.o` 1672; `dx_units.o` and `units.o` are 0 — fully inlined
  into their callers), comfortably inside 64 KB. Note that ITCM sits at
  `0x00000000` and QSPI at `0x90040000`, far outside `bl` range, so the link
  depends on long-branch veneers.
- **Expand voice pool to 7** — *reframed.* The pool size was never the
  binding constraint; concurrent **expensive** voices are. This was written
  when four held Six-Op voices sat at 92-95% against a 90% shed threshold; on
  2026-08-10 the same test measures **78%**, so the premise is weaker than it
  was and the per-engine cap of 3 may now be leaving headroom on the table.
  Re-derive the cap from current numbers before extending the pool. The shed
  guard still cannot fire on held voices at all, which is what the cap
  handles. Still fine for cheap engines,
  where the pool does run out first — and the drum side got most of that
  benefit for free in the kick lab, by learning to reuse *sleeping* voices
  before stealing sounding ones (`notes.md` → "Kick priority").
- **Weighted polyphony — partly done.** The simple form shipped
  2026-08-07: `VoicePool::engine_is_heavy()` caps held Basic Pitch notes at 3
  on Six-Op A/B/C, Speech, Particle, String and Modal, 4 elsewhere. Six-Op is
  measured; the other four are inferred from the 2026-07-03 budget analysis
  and want confirming per engine. A full per-engine *cost table* (rather than
  a two-bucket cap) remains the richer version if it is ever needed.
- **FX consolidation — deprioritised, previously over-valued.** Sharing the
  four `FxSection` instances was briefly filed as a ~25% saving; re-measured
  it is **~5% per active group** (1 voice 41→45%, 2 voices 58→63%, 3 voices
  75→80% with hall + dotted delay). The earlier figure came from a session
  with drums *and* synth active and was misattributed. Worth 5-10% total, so
  the large version — one reverb + one delay with per-group dry/wet and an FX
  knob layer under P1 — is not worth its control-surface cost yet. The small
  version (share instances, keep per-group sends, mirror knob becomes global)
  stays available if the CPU is ever needed; it costs only the ability to run
  two reverb *characters* at once.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` /
  `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns
  at kBlockSize=192.
