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

---

## Next up — in this order

Agreed 2026-08-05. Nothing here is blocked; the ordering is deliberate,
because each step narrows what the next one has to read. (Step 1 of the
original list — this archive sweep — is done; that's why the file is short.)

- [x] **1 — Read MANUAL and README end-to-end.** *Done 2026-08-06*, checked
      against the source rather than against these notes. Seven doc bugs
      fixed in place; the two findings that are about **behaviour** rather
      than wording are filed below as F1/F2.
      What was wrong, for the record: MIDI out still claimed *every* outgoing
      note leaves at velocity 100, which the ch10 accent work had made false
      in the same document; README called the synth 7-voice (`kVoices` is 6 —
      7 is the parking-lot item, not the shipped build) and said all 24
      engines are selectable without mentioning that Chiptune is skipped; the
      LED table credited a limit blink to the base octave, which clamped
      silently (F1 below has since given it the blink for real), and listed
      only two of the six accelerating build-ups; the
      open-hat pool was described as two engines when it has one; one section
      cross-reference pointed at a heading that does not exist; and value-row
      screen strings were quoted in capitals throughout (only the label and
      note rows are drawn uppercase — the manual now says so once and quotes
      the rest as drawn). README also gained the OLED in its feature list,
      which it had never mentioned outside the source-tree table.
      One stale code comment fixed while here: `arp_oct_range` still cited
      `P1+P10/P11` after the 2026-08-05 swap.
- [x] **2 — A round of visualizer tweaks.** *Done 2026-08-07 — all six items
      (V1–V6) shipped; write-up in `notesarchive/notes_archive_2026-08.md` →
      "V. Visualizer round".* Two of the six were real bugs rather than taste
      (the mini screen stopped tracking the drawing at small sizes; `100vh`
      put the bottom of the drawing under a phone's browser chrome), and
      measuring for V5 turned up a third that wasn't filed: the info panel is
      positioned before its content lands, so on a phone its bottom third fell
      off the screen. The two open questions the filing carried were both
      answered on the way: the A−/A+ pairs stay separate but step alike, and
      Fullscreen buys nothing towards keeping a phone awake — on iPhone Safari
      it doesn't exist at all, so the wake lock is the only lever.
- [ ] **3 — Code map check & update.** Section *Docs & site* below.

## Docs & site

- [ ] **Code map — check & update.** `tools/codemap.html` (served at
      `/codemap/`) is the interactive hardware + memory atlas, and it hasn't
      been walked against the source since a good deal shipped: the OLED and
      its I2C interlock, protocol v11 telemetry, the per-engine held-note
      cap, the output limiter, and the re-measured performance figures. Same
      method as the 2026-08-06 doc read (item 1) — check it against the
      source, not against the notes. Brief and design history:
      `doc/codemap_brief.md` → `notesarchive/codemap-brief-archive.md`.

## F. From the 2026-08-06 doc read

Two places where the docs described something better than the firmware does
it. Both are small, both are taste calls, and the manual now documents the
shipped behaviour either way — so neither is blocking.

- [x] **F1 — the base octave hits its rail silently.** *Fixed 2026-08-06,
      confirmed on hardware the same day.*
      Plain P10/P11 clamped at ±3 with a bare `std::max`/`std::min`
      (`TouchPlaited.cpp`, the pad-down handler) and no `LedEvent::LIMIT`,
      while root, arp octave range, the layer stack and undo all blink at
      their limits. Consistency won: both branches now step only when there
      is room and blink LIMIT when there isn't, so every rail on the panel
      answers the same way. The manual's LED table drops the exception it had
      just gained and lists the base octave with the other limits.
- [0] **F2 — Seq Recording swallows both transport combos.** While editing a
      drum slot, P10/P11 stay on drum pitch regardless of P2, so the P2+P11
      branch (drum play/pause) and the P2+P10 branch (melodic transport) are
      both unreachable — you have to save or cancel out of the slot first.
      This is the same class of fault C12 fixed for Arp/Mel Rec, and it was
      found the same way: writing down what the combo does in every mode and
      noticing one mode has no answer. Whether it matters is a real question
      though — in Rec the missing state was *stopping what you were making*,
      whereas here it is pausing the drums while tweaking one of them, which
      the audition pulse arguably covers. If it should work, the fix is the
      same shape as C12's: test P2 before the drum-pitch branch.

      > Decision: We do not need access to the transport combo in this mode

## E. Arp/Mel screen round (filed 2026-08-05, from the second walk)

Three findings on the same screen; **E1 and E3 shipped** (E1's in the
archive). Both needed new telemetry, which is why they were scoped here
rather than bolted onto the branch that raised them. E2 is what's left.

- [ ] **E2 — `P1+P10 UNDO LAYER` leaves the value row empty, and the layer
      display generally.** Two separable things, deliberately split:
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

      > Decision: 
      >  - add the info for "what the next press will remove"  a repeated or larger icon row is not needed      

- [x] **E3 — Arp/Hold: show the pool with the held notes marked.**
      *Done 2026-08-06, confirmed on hardware the same day* — the 9 px marker
      and the 18 px columns read fine on the real panel, so the geometry
      stands as shipped. `Arp::PoolMask()` (7 bits over `notes_[]`) goes out
      as STATE payload byte 38 (fw v11); `OledScreen::ShowPool` draws the
      label row, then seven 18-px columns carrying each pad's note name with a
      filled (in pool) or hollow (not) marker under it. Both implementation
      notes held up: the markers are drawn like `StatusIcons`, and label +
      notes + markers fit the 32 px with the spare row spent as the gaps that
      keep the two rows apart.
      **One thing the filing didn't see, and it changed the design:** a
      pad-down callout would have been *unreachable* in Hold, because pressing
      a pad there is what takes a note out of the pool — you cannot look
      without changing what you're looking at. So the pool is also the idle
      home screen while it has notes in it (in place of the model name), which
      is what actually answers "fingers off, four notes latched". It yields to
      `Arp stopped`, which is the more urgent thing to say.
      The row names pitch classes, which is what the pool stores — the octave
      is applied at fire time, so P10/P11 move the whole thing and the row
      stays true. The one way to make it lie is to leave Arp/Mel with Hold
      latched, shift the root in Basic Pitch, and come back; root and scale
      are Basic Pitch-only, so nothing reachable from inside the mode can.
      The visualizer decodes the byte, mirrors both screens and logs pool
      moves by name (`Arp pool +D#`) — in Hold that log line is the only
      report of which way a press went.

      > Important note:
      >
      > While holding notes and touching / releasing notes, the display does not immediatly update. In this case we should make each pad touched, pad released trigger the update.

      > In Pitch mode:
      > We'll also show the touched notes, this can also help show which voice is 'lost' when more notes are touched


## OLED — the two strands left from the 2026-08-04/05 round

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
- [0] **A2 leftover — the bar sits full while P0+P2 stays held.** In Basic
      Pitch `p0p2_all_done` pins `progress = 127` while the pads stay held
      (`TouchPlaited.cpp`), so after the 3 s stage the full bar sits there
      until release. A1's hold-end redraw doesn't cover it — `hold_kind` is
      still 1. Decide whether the bar should clear at `all_done` or keep
      showing "nothing more to reach".

      > Decision: No change needed. keep current message until released

## Sound

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
      `patch.decay` (`decay_via_morph()`, `TouchPlaited.cpp:249` and
      `269-270`). On those engines the knob is the *model's own* envelope
      time (the DX7 EG for Six-Op, damping/tail for 19–23), and the gate
      genuinely holds — Six-Op keys its EG for as long as the trigger line
      stays high (`plaits_voice.cpp:133-137`). That is why Six-Op feels like
      it has its own decay mapped onto the same knob: it does. `MANUAL.md` →
      *Unified Decay* already documents the routing; what it doesn't say is
      that the two halves are different *kinds* of envelope, which is the
      part that makes a shared ADSR hard.
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


- [ ] **`kDrumKick` is two engines wide** (21 Bass drum, 10 FM 2-op) — option
      (c) of the old kick-curation item, untouched. Now that the pools are a
      table (`kDrumPools`, `TouchPlaited.cpp`), widening one is a one-line
      edit plus an audition pass; it's a taste call, not a code problem.
      **Open question (2026-08-05): can more kicks come from existing engines,
      or do they need writing?** Two routes, and they are not equally
      expensive. (a) A pool entry is an engine *plus a parameter preset* —
      several non-drum engines sit in kick territory at the right
      harmonics/decay (Waveshaping and the 2-op FM already do; Modal and
      Particle plausibly), so widening may be preset work rather than DSP
      work. (b) A purpose-written engine is a real addition to
      `thirdparty/plaits`, worth it only if (a) is exhausted. Audition (a)
      before considering (b). Analysis: `notes.md` → *Drum engine
      parameters* (what each drum engine's knobs mean, which non-drum engines
      hold up as percussion, and the tuning lessons that keep being
      relearned).

      > Decision: 
      > - of all the drum sounds Kick is to me the most important one, hence i'd like more variations, atm the variation is an either very synth sound or the rather low energy kick model. 
      > - another improvement would be that we could make kick be a priority voice, avoiding that it gets choked out too quickly
      > - suggestion to research make a branch that focuses on a test round, build kick presets from the existing models, number them to the display for easy auditioning and judging
      > - in this audiotion mode we could use some of the buttons to tune parameters per preset, which could then be reported by me as what ranges are efficient

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

## Hardware verification still owed

- [ ] **CV clock in/out trigger thresholds.** The Schmitt-triggered
      pulse-to-MIDI-clock bridge on S43 (in) / S40 (out) is implemented and
      the wording on screen was checked in the 2026-08-04 cross-mode walk,
      but the **thresholds themselves** have never been verified against a
      real CV source. `notes.md` had been claiming this is tracked here; it
      wasn't, so it is now. Behaviour: `MANUAL.md` → "Clock sync — MIDI and
      CV"; the original sketch is in
      `notesarchive/notes_archive_2026-07.md` → "Syncing".

## Parking lot — performance

Re-measured 2026-08-07 on hardware (Six-Op C, one group). Raw captures and
the analysis are in `notes.md` → "Six-Op crackle". The numbers changed what
is worth doing here, so read them before picking anything up.

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM
  (64 KB, 0% used); code currently executes from QSPI. Still the only lever
  that changes the arithmetic rather than working around it, and now the
  clear first choice: Six-Op costs **~17% of a block per voice** (idle 16%,
  then 41 / 58 / 75 / 92% for one to four held), so everything else is
  rationing.
- **Expand voice pool to 7** — *reframed.* The pool size was never the
  binding constraint; concurrent **expensive** voices are. Four held Six-Op
  voices already sit at 92-95% against a 90% shed threshold, and the shed
  guard cannot fire on held voices at all, which is what the per-engine cap
  now handles. A 7th voice buys nothing on these engines until ITCM (or a
  cheaper engine mix) moves the per-voice cost. Still fine for cheap engines,
  where the pool does run out first.
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


--

closing comment of last round 7/8/2026

Deleted both. You're on main at 05669c3, clean tree, in sync with origin.

One thing surfaced by the cleanup that I did not touch: libdaisy-sync-ssd1306-fix still exists locally and on origin. That's the branch carrying your SSD1306 driver fixes — the ones pinned in the submodule at 62ab175 and filed upstream as electro-smith/libDaisy#634, still open since 2025-08-13. It's outside this round and it's tracking an upstream PR rather than your own work, so it's the one branch that legitimately outlives the short-lived convention. Left alone.

That closes the round. Summary of where it ended up versus where it started:

The question was "will a dedicated OLED bus give me a smoother progress bar without costing audio?" The answer turned out to be no on both counts — it couldn't cost audio because the display never ran in the audio path, and it couldn't help smoothness because frame latency is set by main-loop starvation, not bus speed. Building it anyway was what produced the instrumentation, and the instrumentation is what caught the two things that actually mattered: the output stage was saturating every voice from zero, and four held Six-Op notes sat above the shed threshold with the safety net structurally unable to fire.

Both of those were audible problems you'd described as separate mysteries. Neither was on the original plan.