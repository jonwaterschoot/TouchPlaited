# TouchPlaited — Working Notes

The freeform design record: *why* things are the way they are, and the
reference material that isn't obvious from the code. It is **not** the TODO
list — `ROADMAP.md` is the single owner of future work — and it is **not** the
control reference: `MANUAL.md` is, and has been since long before this file
stopped tracking the panel. Resolved/historical material moves to
`notesarchive/` rather than being deleted (`notesarchive/readme.md` tells the
whole story).

**Swept 2026-08-06.** The v1-era control reference this file used to open with
described a device that no longer exists (SW2 center as *Random mode*, S34 as
Kick punch, Decay on S37) — that, the build-out status table and the per-role
drum-pool tables are now in `notesarchive/notes_archive_2026-08.md` →
"notes.md sweep (2026-08-06)". What's left below is what's still true and
still useful.

---

## Hardware reference

**SW1 (left switch) — Scale / Genre / Arp sub-state**
- PCB labels: S10 / S9; Daisy pins: D9 / D8
- Code: `touch.switches().B()` → `_switch_9_10`
- Positions: center = Chromatic / Techno / Arp · left flick = Minor / IDM ·
  right flick = Major / Electro
- Change-latched **per role**: a flick only takes effect in the mode you're
  in, which is why `t.sw1_latch` (state byte 37) exists and why the screen
  marks a diverged lever with `*`

**SW2 (right switch) — Playmode**
- PCB labels: S7 / S8; Daisy pins: D7 / D6 — polarity inverted vs. label
- Code: `touch.switches().A()` → `_switch_7_8`; sw=2=Down, sw=0=Center, sw=1=Up
- Positions: down = Basic Pitch (1 blink), center = Arp/Mel (2 blinks),
  up = Seq (3 blinks)

**Switch files** kept as TouchBass template — no modifications. (SW1/SW2 were
briefly non-independent; the fix was restoring those templates, D10 was the
wrong pin.)

**Optional mod pins:** OLED (SSD1306, I2C) on D11/D12, sharing the MPR121
touch bus — see `i2c1_lock.h` for the arbitration. CV clock in on A11 (S43),
out on D25 (S40). Both documented for users in `MANUAL.md` → *Hardware mods*.

---

## Deliberate decisions

**Block size: 192, rendered as 8 × 24-sample chunks**
~4ms per callback at 48kHz. Larger block chosen to give ISR headroom for drum
engines. It is also the unit every hold gesture counts in — `kStageBlocks`
and friends are block counts, not milliseconds.

**P0 and P2 have no standalone sound action**
`bank_caught = false` anchored at TOUCH moment (not release). Pot must move
past dead zone (3%) before model select activates.

**Engine LED blink removed**
Counted blinks too hard to read in practice. Model changes confirmed by
audition tone only. (The OLED later took over this job properly.)

**MPR121 thresholds: library defaults (12/6)**
Raising threshold to 20 broke pad registration. Ghost-touch suppression via
`any_musical_pad_held()` guard in software.

**Phase 8F (controls out of ISR) — reverted**
Data race crash: `generate_*()` writes the slot arrays in the main loop while
the ISR reads them in the `SetOnTouch` touch callback. Fix requires
`__disable_irq()` / `__enable_irq()` wrapping around all `generate_*()` calls.
Deferred unless crackle returns — carried in `ROADMAP.md` Parking Lot.

**S30 repurposed from FM amount to Drive**
`patch.frequency_modulation_amount` has no effect without a CV input
(`modulations.frequency` is always 0 on this hardware). S30 was a dead knob in
its original role.

**Unified decay → `patch.morph` on the morph-decay engines**
Six-Op A/B/C (2–4), String and Modal (19–20) and the drum engines (21–23)
ignore `patch.decay` and use `patch.morph` as their envelope/tail. `slot.decay`
always means "tail time" from the user's perspective; the routing to the
correct patch field happens at playback. The knob is **S31** everywhere (it
was S37 in the v1 layout, which is what the archived tables say).

**Per-mode slot arrays (mode memory)**
`bp_slots[]` holds the Basic Pitch randomize snapshots, `drum_slots[]` the Seq
kit; the arp and Rec each carry their own sound separately. Flicking SW2
restores each mode's last state; nothing regenerates on mode switch. Only
P0+P2 re-randomizes, and the drum kit is generated once on first Seq entry
(`drum_kit_ready`). (`pad_slots[]`, the old Random-mode array, went with the
mode.)

**Seq per-slot drive is a ratio, not an absolute**
`drum_slots[i].drive` defaults to 1.0 and multiplies the overall S30 drive at
trigger time (`overall × ratio`). In recording mode S30 edits the slot's ratio
with pickup while the overall drive is frozen at its rec-entry value, so slot
edits don't move the whole mix. Pitched slots keep `drive = 0.0` (absolute),
since global `SetDrive` overrides per-voice drive outside Seq anyway.

**Basic Pitch snapshot escape via knob grab**
After P0+P2 randomize, Basic Pitch plays frozen `bp_slots` — but the mode's
identity is "all params live on knobs", so any timbral knob moved >5% (or a
model picked via P0/P2+S35) snaps back to live mode. No extra gesture to learn.

**Background seq: param-locked voices + offset slot ids**
Seq/drum triggers call `NoteOnWithParams(..., lock_params=true)` with slot ids
`16+i`. The lock makes all VoicePool global setters (engine,
harmonics/timbre/morph/decay, drive, LPG, FM) skip those voices, so Basic
Pitch live knobs and pitched-mode drive can't reshape a drum mid-decay; the id
offset keeps pad NoteOffs (slots 0–6) from cutting drum tails, and drums ring
out as one-shots. Locked LPG is pinned to 0.5 at trigger. Seq trigger settings
update only while SW2 is Up.

---

## Per-model control reference

What each knob does per engine — the reasoning behind `kEngineKnobs`
(`display/oled_ui.cpp`, mirrored in the visualizer's `controls-meta.ts`),
which is what actually drives the on-screen labels.

Current knob roles: **S31 Decay** (unified — LPG envelope, or the model's own
morph on the starred engines), **S32 Harmonics**, **S33 Timbre**,
**S34 Morph**. LPG colour is retired (fixed 0.5, CC 25 only).

| #  | Engine           | S32 Harmonics | S33 Timbre | S34 Morph | S31 Decay/Tail |
|----|------------------|---------------|------------|-----------|----------------|
| 0  | VA + VCF         | Filter cutoff | Resonance  | Osc mix   | Env decay |
| 1  | Phase Distortion | PD waveform   | Saturation | Symmetry  | Env decay |
| 2  | Six-Op A *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 3  | Six-Op B *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 4  | Six-Op C *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 5  | Wave Terrain     | X position    | Y position | Terrain   | Env decay |
| 6  | String Machine   | Filter cutoff | Chorus     | Voices    | Env decay |
| 7  | Chiptune ⚠       | Arp range     | Waveform   | Speed     | *(inert — see below)* |
| 8  | Virtual Analog   | Detuning      | Crossfade  | Wavefold  | Env decay |
| 9  | Waveshaping      | Level         | Fold       | Asymmetry | Env decay |
| 10 | Two-Op FM        | Ratio         | Mod level  | Feedback  | Env decay |
| 11 | Grain            | Position      | Density    | Size      | Env decay |
| 12 | Additive         | N harmonics   | Inharmonic | Shape     | Env decay |
| 13 | Wavetable        | X position    | Y position | Warp      | Env decay |
| 14 | Chord            | Chord type    | Inversion  | Shape     | Env decay |
| 15 | Speech           | Formant rate  | Vowel      | Accent    | Env decay |
| 16 | Swarm            | Spread        | N voices   | Density   | Env decay |
| 17 | Noise            | Filter color  | Bandwidth  | Saturate  | Env decay |
| 18 | Particle         | Density       | Size       | Backgnd   | Env decay |
| 19 | String *         | Inharmonicity | Brightness | **→ S31** | = morph: damping |
| 20 | Modal *          | Material      | Brightness | **→ S31** | = morph: damping |
| 21 | Bass Drum *      | Pitch drop    | FM punch   | **→ S31** | = morph: tail |
| 22 | Snare Drum *     | Noise freq    | Noise/body | **→ S31** | = morph: char+tail |
| 23 | Hi-Hat *         | Freq spread   | Metallic   | **→ S31** | = morph: tail |

\* morph-decay engines. Six-Op additionally renders identical OUT and AUX, so
blend and stereo width do nothing on 2–4.

**Six-Op A/B/C note:** these engines produce sound only in specific parameter
regions. At random/default values they are often silent, and some combinations
only sound on every second gate trigger. `kSixOpAud[]` (in `TouchPlaited.cpp`)
is the table of audible landing spots used by randomize and audition.

**Chiptune (7):** self-running arpeggiator, no gate response, and it never
calls `set_envelope_shape` — so S31 Decay is inert on it. Excluded from the
random pools *and*, since 2026-08-05, from the selectable banks: an engine
where one standard knob does nothing and whose arpeggiator free-runs against
the device's own doesn't earn a slot. Reasoning archived under Priority 3 in
`notesarchive/notes_archive_2026-08.md`.

---

## Drum engine parameters

The three purpose-built drum engines, and what their knobs mean when a slot
lands on one:

- **21 — Bass Drum:** Harmonics = pitch-drop speed, Timbre = FM punch,
  morph = decay time, Note = base pitch
- **22 — Snare Drum:** Harmonics = noise freq, Timbre = noise/body mix,
  morph = character + decay, Note = drum body pitch
- **23 — Hi-Hat:** Harmonics = freq spread, Timbre = metallic ratio,
  morph = decay time, Note = pitch center

**Non-drum engines that work as percussion:** 17 Noise (hat/snare), 19 String
(rimshot/clave — but too melodic for the Perc pool, see below), 20 Modal
(cowbell/conga), 10 Two-Op FM (already the second kick engine). **Particle
(18) is banned from every random drum pool** — its intentionally sporadic
crackle reads as a hardware fault in a generated kit. It stays manually
selectable in Recording.

Tuning lessons that outlived the tables: hats need to sit **above** the
melodic register (60–84 read as tonal noise rather than metal — they were
moved to 76–100); Modal's tail has to be capped well below 0.9 for percussion;
a Snare engine pitched high with a body-heavy timbre gives the rim/wood tick
that String was wrongly used for.

> **Feeds ROADMAP:** the open `kDrumKick` question — whether more kicks can
> come from existing engines with the right preset, or need a purpose-written
> one — is exactly a question about this list. The pool entries are
> engine + parameter ranges (`kDrumPools`, `TouchPlaited.cpp`), so a
> candidate engine only has to reach kick territory in harmonics/decay at
> some settings. Waveshaping (9) and the 2-op FM (10) are the obvious first
> auditions; Modal (20) and Particle (18, if the crackle can be dialled out)
> are the outside bets.

---

## Drum pattern system (2026-07-04)

Patterns live in **`synth/patterns/<genre>/NN_name.h`** — one file per pattern
in a genre subfolder (`techno/`, `electro/`, `idm/`), defining
`static constexpr uint8_t kPat_<genre>_NN_name[7][64]`. `tools/gen_patterns.py`
(run by `make` on every build, rewrites only on change; genre order hardcoded
to match SW1) registers them sorted by filename within each genre into the
generated `synth/patterns_gen.h` (gitignored): `kNumGenres`, `kNumPatterns`,
`kSeqPatterns[]`, `kGenrePatternCount[]`, `kGenrePatternIdx[][]`. Drop a new
file in a genre folder → next `make` embeds it. SW1 picks the genre
(`SetGenre`), S35 picks the pattern within it (`SetVariant`, pickup-protected,
knob range split evenly across the genre's pattern count).

**Step byte encoding 0xCW:** low nibble W = weight 0–4 (unchanged semantics);
high nibble C = chance rolled after the density threshold passes — 0 = always,
1 = 75%, 2 = 50%, 3 = 25%. Plain decimal 0–4 still means deterministic, so old
tables read unchanged; `0x23` = weight 3 at 50%. Implemented with xorshift32 in
`Sequencer::eval_step()`. First use: Electro clap echoes on steps 5+13 at 50%.

A step is audible when `weight + density >= 5`; S34 then scales the authored
chance nibble (`SetChance`). The full quantization contract — and the display
strings that have to stay in lockstep with it — is documented at the top of
`synth/sequencer.h`.

> **Feeds ROADMAP:** "weight → audio level" is a change to what the weight
> nibble *means* downstream. Today it is purely a density gate for audio and
> an accent for MIDI out (`drum_velocity()`); making ghosts quieter internally
> would change the shipped feel of every pattern in these files, which is why
> it's filed rather than done.

**Pattern editor webapp:** `tools/pattern_editor.html` (open locally in a
browser; no server needed). 7×4×16 grid, click = set/clear, drag ↑↓ = weight,
drag ←→ = chance (color lime→amber→orange→red), density-preview slider dims
sub-threshold steps, bar copy, localStorage library seeded with the three
built-ins, C-table import, and one-click export of a ready `synth/patterns/`
file. UI brainstorming lives in `tools/editornotes.md`.

---

## Where the history lives

Everything below was written up here and has since moved out. Listed so a
pointer from the code still lands somewhere.

**In `notesarchive/notes_archive_2026-07.md`:**
- *Open Decisions — resolved* (items 1–2) and *Known Issues — all fixed* —
  Six-Op audibility + gate/click fixes, seq pickup bugs, tightness direction,
  model-select-while-seq, Electro pattern redesign, the SSD1306 driver bugs
- *Voice expansion + MIDI — budget analysis (2026-07-03)* — the CPU/SRAM
  measurements behind the 4→6 voice bump, voice sleep, the load-shed guard,
  and the SDRAM→SRAM move. The remaining levers (ITCM placement, 7th voice)
  are `ROADMAP.md` Parking Lot items
- *MIDI implementation (2026-07-07) — phase 1 + CC map, both transports*,
  including the **MIDI mapping sketch** the channel-split comment in
  `TouchPlaited.cpp` refers to
- *Reverb / delay FX send* — the 2026-07-08 resource analysis and the
  2026-07-09 implementation (mirror knobs, Rings-derived reverb, per-group
  sends). Hardware-verified 2026-07-24
- *Playmode overhaul* — the original sketch that became Arp/Mel, referenced
  from `synth/arp.h`; the phase-by-phase log is in
  `notesarchive/arp-mel-plan-archive.md` §6–§9
- *Syncing* — the original CV clock in/out sketch. Implemented
  (Schmitt-triggered pulse-to-MIDI-clock bridge, MIDI-outranks-CV with timeout
  fallback); behaviour in `MANUAL.md` → *Clock sync*. **Hardware verification
  of the trigger thresholds is still open** — filed in `ROADMAP.md`
- *Visualizer mobile UX musing* (2026-07-23) — resolved the same day by the
  on-faceplate OLED and the device handle cluster

**In `notesarchive/notes_archive_2026-08.md`:**
- The OLED parity + held-combo progress bar write-up
- The whole 2026-08-04/05 round (kick curation, MIDI drum velocity, Chiptune
  closed as won't-ship, root readouts, OLED items A–E1, the hardware walks)
- *notes.md sweep (2026-08-06)* — the v1-era control reference, the build-out
  status table, and the per-role drum-pool tables this file used to carry

**Feature ideas** all moved to `ROADMAP.md` on 2026-07-08 and stay there: it
is the single owner of future work, and a roadmap item links back here when an
analysis exists.
