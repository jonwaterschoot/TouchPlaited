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

---

## Next up — in this order

Agreed 2026-08-05. Nothing here is blocked; the ordering is deliberate,
because each step narrows what the next one has to read. (Step 1 of the
original list — this archive sweep — is done; that's why the file is short.)

- [ ] **1 — Read MANUAL and README end-to-end.** Both have been updated
      per-change and neither has been read whole since the OLED work started.
      The screen has since grown a status row, a pickup display, combo lists,
      capture indicators, the `*` marker and the octave/root readouts, and
      README still describes the project from before most of that. Check
      against **shipped behaviour**, not against these notes — the notes are
      what would repeat an error rather than catch it. Expect findings to
      split into doc bugs and real bugs; file the second kind here.
- [ ] **2 — A round of visualizer tweaks.** The app has been kept in
      lockstep field-by-field through six protocol additions without anyone
      standing back from it. Worth a pass in its own right now that the
      firmware side has settled.

## E. Arp/Mel screen round (filed 2026-08-05, from the second walk)

Three findings on the same screen; **E1 shipped** (see the archive). The two
left need new telemetry, which is why they were scoped here rather than
bolted onto the branch that raised them.

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
      the MPR121 touch controller (`i2c1_lock.h`), which is why the throttle
      is 80 ms — a marquee stepping at that rate is ~12 fps, fine, but don't
      let it run when nothing is being scrolled. Emulator parity: `fitFont()`
      in `oled-mini.ts` implements the same shrinking rule and has to change
      with it.
- [ ] **A2 leftover — the bar sits full while P0+P2 stays held.** In Basic
      Pitch `p0p2_all_done` pins `progress = 127` while the pads stay held
      (`TouchPlaited.cpp`), so after the 3 s stage the full bar sits there
      until release. A1's hold-end redraw doesn't cover it — `hold_kind` is
      still 1. Decide whether the bar should clear at `all_done` or keep
      showing "nothing more to reach".

## Sound

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
      before considering (b).
- [ ] **Weight → audio level.** MIDI out now carries the pattern's accents as
      velocity, but the *audio* still fires every step at full level — weight
      is only a density gate there. Making ghosts quieter internally is
      arguably the point of ghost notes, but it changes the shipped feel of
      every pattern, so it's filed rather than done.

## Hardware verification still owed

- [ ] **CV clock in/out trigger thresholds.** The Schmitt-triggered
      pulse-to-MIDI-clock bridge on S43 (in) / S40 (out) is implemented and
      the wording on screen was checked in the 2026-08-04 cross-mode walk,
      but the **thresholds themselves** have never been verified against a
      real CV source. `notes.md` → "Syncing → CV clock in/out" has been
      claiming this is tracked here; it wasn't, so it is now. Behaviour:
      `MANUAL.md` → "Clock sync — MIDI and CV".

## Parking lot — performance

- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM
  (64 KB, 0% used); code currently executes from QSPI. Enabler for both the
  FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on
  in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` /
  `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns
  at kBlockSize=192.
