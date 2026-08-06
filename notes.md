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

## OLED redraw cost — the I2C4 experiment and what it actually found (2026-08-06)

Question was whether the display deserves its own I2C bus, at the cost of TRS
MIDI (USART1 wants D13/D14, so the two cannot coexist). Built it, rewired it,
measured it: **no observable difference, reverted.** The interesting part is
why, because it moves where the next effort should go.

### What was measured

Branch `oled-i2c4-no-trs` (kept for the trail; the bus move is not merged).
OLED on I2C4 / D13-D14 at 1MHz, interlock compiled out, TRS MIDI dropped.
Boot animation, progress bars and general use looked **identical** — the one
thing the change was meant to improve, progress-bar smoothness, did not move.

Serial capture, idle → simple synth → 6-op FM poly → + seq + FX → idle:

| CPU avg | `oled max` | main loop's share | implied bus time |
|---|---|---|---|
| 15% | 6268 µs | ~85% | ~5.3 ms |
| 88% | 43920 µs | ~12% | ~5.3 ms |

Peak observed: `max 239570us` — a 239 ms frame.

### The finding

The bus change worked exactly as designed and was irrelevant. A four-page
frame is ~5.3ms of bus time at ~886kHz (vs ~11.5ms at 400kHz), and that
prediction matches the idle measurement to within a few percent, so I2C4 at
1MHz was genuinely running. But **frame latency is dominated by audio-ISR
preemption, not by the bus**: the transfer runs in the main loop, which at 88%
audio load gets an eighth of the wall clock. Doubling bus speed sped up 5ms of
a 125ms round trip. Invisible, exactly as it felt.

Frame rate under load was ~16 frames per 2s ≈ 125 ms/frame ≈ 80 ms throttle +
~45 ms stretched transfer. Neither term is bus speed.

### What was done instead

1. **`display/oled_dirty_driver.h`** — `SSD1306DirtyDriver` transmits only the
   pages whose pixels changed, against a shadow of the last frame sent. A
   progress bar animates two pages of four, the capture blink one, an
   unchanged status row none. This cuts the *stretched* time by the same
   factor as the bus time, which is why it beats the faster bus.
2. **`kMinRedrawIntervalMs` 80 → 40ms.** The real constraint is not the bus
   but the fraction of time the pads are blind behind `i2c1_bus_busy`. At 80ms
   with full frames that was ~14%; halving the bytes and halving the interval
   holds it at ~14% while doubling how often the screen may move.

`oled N fr M pg max Nus` on the CPU print now carries pages as well as frames.
Pages/frames near 2 means dirty-paging is working; 4.0 means it is not, and
the 40ms interval is then buying blindness rather than smoothness.

### Second capture (129 windows, dirty pages + 40ms) — two corrections

**Dirty-paging fires less than the layout suggests: 3.08 pages/frame, not 2.**
61 of 129 windows sat at 3.5–4.0. Cause is `ShowLine`'s value row: Font_11x18
at y14..31 spans pages 1–3, so any callout changing both label and value
dirties all four. The byte saving is real but ~23%, not 50%. Only the
blink-only and no-change redraws reach 0–1 pages.

That invalidated the basis for 40ms, which had been sized assuming the byte
count halved. The fix was not the interval but **where the interlock is
held**: `i2c1_bus_busy` across a whole frame blinds the pads for the frame's
wall-clock duration (~52ms at 79% CPU, 244ms worst observed). It is now held
per page inside `SSD1306DirtyDriver::Update()` and released between them, so
worst consecutive blindness is one page and the poll gets a window every
page. That decoupling is what 40ms rests on now.

**Retracted: the settings-journal stall.** The first capture showed all three
`sv` increments landing on the worst rows, and `tp_qspi_ram_op` does mask all
interrupts for a write. But across 129 windows only **1 of 8** `sv` increments
coincides with an outlier. The extreme frames track CPU instead: at `avg 95%`
the main loop gets ~5% of wall clock, so ~12.7ms of bus becomes ~254ms —
against 239937us measured. Preemption explains them without the QSPI story.
Small-sample coincidence; not worth a branch.

**Still open: the CPU baseline in the archive is stale.** Peaks of 137–150%
with `shed` up to 38 sustained, against the 111–115% worst case recorded
2026-07-03, which predates the FX work's fixed +8–12% per block. Re-baseline
before spending anything on ITCM or voice count.

**Also open: redraw rate is capped by audio load, not the display.** Observed
rate topped out near 38 frames per 2s window at a 40ms interval, where 50 are
possible. Lowering the interval further only queues more redraws behind the
same starved main loop.

---

## "Distorts at polyphony 4" was gain staging, not CPU (2026-08-06)

Diagnosed off one observation: **reverb hall distorts more than room, dotted
delay in between.** `FxSection::SetReverbCharacter` (`synth/fx.cpp`) shows
room and hall are the *same code path* — same buffers, same per-sample work —
differing only in two scalars (decay 0.35-0.60 vs 0.75-0.95, damping 0.45 vs
0.80). Identical CPU. So a character that distorts more at equal cost is
distorting on **level**, not load. The ranking hall > dotted delay > room is
exactly the accumulated-tail-energy ordering.

Mechanism: `VoicePool::Render` sums voices unscaled (`out_left[s] += l * vol`,
default vol 1.0), FX returns land on that sum *before* the output stage, and
the output stage was `x/(1+|x|)` — a curve with **no linear region**, bending
from zero. Four voices ≈ 4.0 plus a hall tail ≈ 5-6 came out at 0.83, ~15dB
of saturation applied to the whole waveform. Happens identically at 0% CPU.

Replaced with `soft_limit()` (TouchPlaited.cpp): linear below `kLimitKnee`,
the same rational curve above it, rescaled so value and slope match at the
crossing. Verified continuous (slope 1.0000/0.9999 either side) and
asymptotic to exactly 1.0.

**Known trade, left for ears to settle:** at knee 0.8 the instrument is ~5dB
louder at one voice, and one-to-four voices spans +0.8dB where the old curve
gave +4.1dB — cleaner chords that add less weight. A bounded output plus an
unscaled voice sum must compress somewhere; lowering the knee trades back
toward polyphonic range (0.5 → +1.9dB, 0.3 → +2.7dB) at the cost of shaping
more of the signal. Having both means trimming voices so four land near 1.0
rather than 4.0 — a loudness decision, not made here.

### Separately: FX cost ~25%, not the budgeted 8-12%

Measured 60% → 79-85% when FX come in. Cause is `synth/fx.cpp`'s four
independent `FxSection`s — each group owns its own reverb *and* delay ("own
buffers, own character, own sleep state"), so two active groups run two
reverbs and two delays. The 2026-07-08 analysis recorded "one shared reverb
with per-group send levels (cheap — chosen)"; the implementation went the
other way because a shared instance cannot be room and hall at once
(`synth/fx.h`).

Deferred to its own round, deliberately. Two shapes discussed:
- **Small:** share the instances, keep the per-group sends (sends are just
  gain). Loses only *simultaneous different characters*; no UI change at all,
  the mirror knob becomes global. Most of the CPU for little work.
- **Large:** one reverb + one delay with per-group dry/wet, and an FX
  parameter layer taking over the knobs while P1 is held. Coherent, and P1 is
  already the sound-edit modifier — but it is a control-surface redesign
  touching telemetry, OLED and settings persistence.

Worth re-measuring after the limiter change before choosing: if four voices
under hall no longer distort, the remaining complaint is crackle above 100%,
which is a different problem with different levers.

### Note on reading the CPU log

Above ~100% the audio callback overruns its 4ms block and the main loop gets
essentially nothing — serial prints, OLED and MIDI service all stall together.
So **missing log lines during crackle are themselves data**, and the worst
episodes are systematically underreported. `CPU avg` hides them (one window
read `avg 78%` while containing a 507777us OLED frame, ~10x its neighbours at
the same average); `max` and `shed` are the columns that carry signal.

### Dead end worth recording

DMA is not the follow-up. libDaisy returns `ERR` unconditionally from
`TransmitDma` on I2C4 (needs BDMA with buffers in SRAM4; `src/per/i2c.cpp` has
the TODO), and `SSD130xI2CTransport` has no DMA path on any peripheral — only
the SPI transport does, and only `SSD1307Driver` wires it up. Real zero-CPU
frames would mean converting the panel to 4-wire SPI.

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
