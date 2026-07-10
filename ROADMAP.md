# TouchPlaited — Roadmap

**v1 stable — 2026-07-08.** The numbered implementation steps that built it
(Steps 1–15: SW2 mode restructure → recording redesign → soft-clip output →
mode memory → background drum seq → voice expansion + sleep + shed guard →
MIDI notes/CC/clock) are archived verbatim in
`notesarchive/roadmap_v1_archive.md`. The workflow story since the very first
prompt is in `notesarchive/readme.md`.

Design decisions, budget analyses, and implementation write-ups live in
`notes.md`. **This file is the single owner of future work** — ideas and
priorities live here and nowhere else; a roadmap item links to its analysis in
`notes.md` when one exists.

---

## Open verification (carried over from the v1 steps)

Hardware checks still unticked when the steps were archived. Daily use has
likely covered much of this — tick or delete as confirmed, the detailed
per-step lists are in the archive.

- [ ] MIDI clock (Step 13): DAW → TouchPlaited sync incl. shuffle feel under external clock; tempo knob dead while synced + fallback after clock stops; TouchPlaited → drum machine sync (clock out + Start/Stop); chained pass-through
- [ ] Soft-clip level (Step 6): single Basic Pitch voice at center is comfortable
- [ ] General verification sweep (Step 7 list in the archive): modes, recording, per-slot volume/drive, unified decay, Six-Op audibility

## Priority 1 — Six-Op gate fix (in progress 2026-07-10)

Root cause of "Six-Op nearly silent / every second press dead" found: every
note was a 0.5 ms trigger pulse (`PlaitsVoice::Render` auto-zeroed
`modulations.trigger` after each block), and the six-op engines are the only
ones with real DX7 key-on/key-off gate semantics — worse, their two staggered
FM voices only saw the one-block gate on every other press. The DX7 factory
banks were never the problem (all three are compiled in and load correctly).
Full write-up in notes.md "Six-Op: silent/alternating pad triggers".

- [x] Step A — real gate in `PlaitsVoice`: trigger stays high while the pad is
      held; retriggering a still-high line (voice steal, one-shot repeat)
      inserts one forced-low block so Plaits still sees a rising edge.
- [x] Step B — one-shot gate timer in `VoicePool`: drum-seq triggers and
      auditions never get a NoteOff, so their gate drops after a decay-scaled
      hold (20–400 ms). Inert for non-FM engines (only six-op reads gate
      length).
- [x] Step D — Six-Op level pad (added 2026-07-10 after Step A/B confirmed
      working on hardware): with real gates the engine turned out to be the
      hottest in Plaits — registered at out_gain 1.0 with the LPG bypassed
      (`already_enveloped`), its internal soft-clip pinning dense DX7 patches
      at full scale for the whole gate, vs. 0.6–0.8 through a decaying LPG
      for every other melodic engine. Polyphonic sums saturated the master
      soft-clip even at low volume. Host-side pad on engines 2–4 in
      `PlaitsVoice::Render` (vendored Plaits untouched per thirdparty
      policy). Started at ×0.45, then ×0.35 (still hot — the 2 dB step was
      too timid), now **×0.20** (−14 dB vs. unity; sustained tones read much
      louder than the other engines' decaying LPG plucks, so the comparable
      level is lower than gain math suggests).
      **Value is a by-ear starting point — tune on hardware.**
- [x] Step E — Six-Op note-start anti-click (2026-07-10): stolen/reused
      voices still carry the previous tail when the engine resets operator
      phases / loads a new patch at key-on → discontinuity click. Plaits
      only sees our edge `kTriggerDelay` (5) blocks late, so the tail-only
      window is masked in `PlaitsVoice::Render`: fade-out on the edge block,
      mute the 5 stale blocks, one-block ramp-in exactly where the key-on
      lands (~3 ms total, DX7 attack character untouched). FM engines only —
      drum transients stay raw.
- [x] Step F — Six-Op patch-index LED blink (2026-07-10): S32 preset zone
      changes (32 per bank, mirroring the engine quantizer with a
      15%-into-zone guard) fire the existing short MODEL blink. Basic Pitch
      live path only; armed silently on engine entry.
- [ ] Step C — hardware verify: every press fires (no alternation); Six-Op
      pads/organs/strings sustain while held and release on lift; MORPH
      envelope stretch audible on held notes; seq FM drums decay and voices
      still sleep (CPU meter); rec auditions keep their 500 ms cadence; voice
      stealing retriggers cleanly (fast playing past 6 voices); Six-Op level
      sits comfortably next to other engines, chords don't squash (Step D pad,
      adjust 0.35 by ear); note starts click-free incl. fast re-presses and
      S32 patch changes mid-tail (Step E); S32 blinks once per preset zone
      while browsing, no chatter when the knob rests on a boundary (Step F).
- Follow-ups after verify (not started): **FM velocity** — `level_patched=true`
  + per-note level on engines 2–4 only (the DX7 velocity mappings are dormant;
  accent is currently pinned at 0.8); revisit `kSixOpAud` anchors / random
  ranges now that the full banks actually speak; patch-index LED blink could
  fold into the P2 "LED blink on model load" item.

## Priority 2 — after the open verification

- [ ] **Per-track pattern variants — S35 in Seq mode** (feasibility decided 2026-07-03: **variant bank wins over generative**). S35 is free in Seq mode (model select disabled there). Hold a pad + turn S35 (quantized zones, deadzone pickup like the bank select) → pick that track's pattern variant from hand-authored rows: `kSeqWeights` grows a variant dimension, `Sequencer` gets `variant_[7]`. Flash cost trivial (~5 KB for 4 variants × 7 tracks × 3 genres); keeps genre feel; S35 position is repeatable. Phase authoring: 2 variants per track first. A generative variant (Euclidean E(k,16) + rotation) can be the last slot per track later. UI guard required: S35 move past deadzone while a pad is held must reset the rec-entry hold counter or it collides with 1.2 s Recording entry.
- [ ] **Density + chance axis** — S32 split: 0–0.45 = less density, 0.5 = normal, 0.55–1 = chance/mutation per step. Changes seq tick logic.
- [x] **LED blink on model load** — already implemented (`LedEvent::MODEL` fires in `process_model_select` on every engine change; discovered while adding the Six-Op patch-index blink, Priority 1 Step F, which reuses the same blink).

## Priority 3 — feature additions

- [ ] **Reverb/delay FX send — implemented on the `FX` branch (2026-07-09), hardware verification open.** Design record in notes.md "Reverb / delay FX — implementation". P1+S30 = reverb, P1+S35 = delay, both mirror knobs (center off, left/right = character, wet grows outward): room|hall, slapback|synced dotted 1/8. Per-group sends (drums vs. pitched) with mode memory; shared character from the last edit. Reverb = Rings' Griesinger/Dattorro topology on the vendored Plaits `FxEngine` (64 KB SDRAM); delay = cross-feedback stereo line (512 KB SDRAM). FX sleep keeps idle cost at zero. To verify on hardware: levels/tapers, character params, CPU under dense seq + Random (`shed N` drift), P1 reachability in practice. Remaining ideas from the analysis: per-slot send edits in Recording, send randomization in kit generators, ITCM placement if peaks pinch.
- [ ] **Melodic seq trigger** — in Basic Pitch / Random modes, arm the drum sequencer to fire its patterns against the current scale/pad layout (each drum track → scale degree; drum weight tables apply to scale degrees). P1 was earmarked for this but is now the FX modifier (`FX` branch) — a bare P1 *tap* (no knob move) is still free and could be the toggle. Big change, design first.
- [ ] **Rhythm mutation gesture** — mutate the weight tables (changes rhythm, not just sounds). No current gesture available; needs a button combo or context-dependent trigger.
- [ ] **4-bar fill** — P1 held in seq mode triggers a max-density bar then returns to normal.
- [ ] **Live record into pattern** — two routes: audio buffer loop (complex controls) or live trigger capture (simpler; can't layer on top). Not designed yet.
- [ ] **MIDI drum pitch phase 2** — ch10 notes within ±6 of a slot's GM anchor play that slot transposed.

## Parking Lot

- **P1 + bare S35 spent on the FX layer** (updated 2026-07-09) — the `FX` branch uses hold-P1 + S30/S35 as the FX knob layer, consciously reversing the earlier "P1 never held" note: an FX level is a set-and-forget edit, not a performative gesture, and it left bare S35 untouched. **Ergonomics still to confirm on hardware** — if the P1 hold proves too awkward, the fallback is bare S35 as pitched-mode send. The melodic seq trigger (Priority 3) needs a new gesture now. ⚠ The *4-bar fill* idea above assumes holding P1 in Seq — reconsider that gesture. Shelved ideas for the record: all 24 engines on bare S35 (~4% travel per engine, jitter-prone, destructive in Random where an engine change regenerates all 7 slots), P1 + P10/P11 model ±1 stepping.
- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **OLED screen** — I2C 128×32/64 add-on; shows engine name, params, mode, root note. V2 hardware.
- **Persistent state** — libDaisy `PersistentStorage` for last model + pad slots across power cycles.
- **Chord mode** — single pad triggers multiple Plaits voices at scale intervals.
- **Mod-pad mirror on MIDI ch16** — remaining idea from the MIDI implementation.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
