# TouchPlaited — Roadmap

Working prototype is running. Full implementation history is in `notesarchive/plan_archive.md`.

The description of the prompting approach is in `notesarchive/readme.md`

Open questions and feature details are in `notes.md`. 

---

## Implementation order for the planned revision

These are interdependent — follow this sequence.

### Step 1 — Mode restructure (SW2 layout change)

Merge Soft + Full Random into "Random". Move Seq to SW2 Up. Drum mode becomes Seq-exclusive.

**P0+P2 stages in Random mode (all pitched — no drums):**
- Stage 1 (1s / 250 blocks): `generate_soft_random(engine, h, t, m, d, 0.25f)` — same engine, tight
- Stage 2 (2s / 500 blocks): `generate_soft_random(engine, h, t, m, d, 0.45f)` — same engine, wide
- Stage 3 (3s / 750 blocks): `generate_full_random_models()` — each pad gets random engine + params, NO drums. All pads still use `compute_note()` (scale pitches).

**Engine default for Random mode entry:** use `current_engine`. If entering from Seq (which uses drum engines), reset to engine 0 (VA+VCF).

- [x] Change `sw2_to_mode()`: down=BASIC_PITCH, center=RANDOM, up=SEQ
- [x] On SW2 Up enter: `generate_drum_random()` if not drum mode, start seq immediately
- [x] On SW2 Up exit: SW2 flip stops seq (removed P1 long-hold exit — SW2 is now the sole entry/exit)
- [x] `PlayMode` enum: replace SOFT_RANDOM + FULL_RANDOM with single RANDOM
- [x] Merge `slots_ready_m2` + `slots_ready_m3` → `slots_ready` single bool
- [x] `is_drum_mode` = only set by Seq enter, cleared by Seq exit. Never set from Random.
- [x] Remove P1 seq entry from touch callback. P1 in non-Seq modes: no action (reserved for future).
- [x] P0+P2 hold stages: remove stage 3 drum case; replace with `generate_full_random(2)` (no drums)
- [x] Random mode NoteOn: always `compute_note(pad)` for scale pitches (stages 1–3)
- [x] Update `process_model_select()`: remove `PlayMode::FULL_RANDOM` check; now just "if RANDOM, update slot engine"
- [x] Blink: SW2 up entry → CONFIRM blink (3 rapid); SW2 center = 2 blinks; down = 1 blink

### Step 2 — PadSlot expansion

Add per-slot volume and drive. Update all call sites.

- [x] Add `float volume = 1.0f;` and `float drive = 0.0f;` to `PadSlot` struct
- [x] Update `NoteOnWithParams` signature: add `volume, drive` params (default 1.0 / 0.0 so old call sites stay valid)
- [x] In `VoicePool::NoteOnWithParams`: `voices[idx].SetDrive(drive)` at note-on time
- [x] Apply `volume` scale in `VoicePool::Render()` per-voice via temp buffer + scale loop
- [x] In `generate_drum_random()`: set default `slot.volume` per role (Kick 0.9, Snare 0.8, CHH 0.55, OHH 0.65, Clap 0.75, Tom 0.7, Perc 0.6)
- [x] `generate_soft_random()` and `generate_full_random()` leave volume/drive at struct defaults (1.0 / 0.0)
- [x] `Audition()` and `AuditionWithParams()` set `voice_volume[idx] = 1.0f`

### Step 3 — S30 → Drive; unified decay

- [x] Change `fm` variable → `drive`: `float drive = k.s30().Value()`
- [x] Route: `pool.SetDrive(drive)` in all non-seq modes. Seq mode: no global SetDrive (per-slot drive from NoteOnWithParams)
- [x] `SetFMAmount` removed from both modes (FM dead without CV). Always 0.0f.
- [x] `fill_drum_slot`: `slot.morph = 0.5f` (fixed); `slot.decay = rand_range(o.mlo, o.mhi)` (tail time)
- [x] Seq tick: engine 21–23 → `s.morph = s.decay * (1.0f - tightness * 0.8f)` (routes tail→morph, applies to all drum engines not just hats)
- [x] Dead `else if (is_drum_mode)` branch removed from voice params section

### Step 4 — Recording redesign

New state machine for entry, save, cancel, copy.

- [x] `entry_hold_pad` + `entry_hold_count` added; drum entry via 800ms hold detected in AudioCallback
- [x] Random pitched entry: P0 + pad tap (guarded with `!seq_mode_on` — disabled in Seq)
- [x] Cancel/copy in AudioCallback: `cancel_pad` + `cancel_count`; <12 ignore / 12–200 cancel / ≥200 + src held = copy + confirm blink; resets so more pads can be copied
- [x] `LedEvent::BEAT` suppressed when `rec_mode != RecMode::IDLE`
- [x] Seq tick: `triggers |= (1u << rec_slot)` force-fires rec slot every step
- [x] Audition retrigger suppressed when `seq.IsActive()` (seq drives the trigger)
- [x] `RecMode::COPY` and `rec_copy_dst` removed; copy inline within `RECORDING` state
- [x] Drum engine audition morph fixed: `aud_morph = slot.decay` for engines 21–23 in enter/retrigger

### Step 5 — Recording mode: per-slot S30 (drive) and S36 (volume) in rec mode

- [x] Add `rec_k30` (KnobPickup for drive) and `rec_k36` (KnobPickup for volume) to rec pickup set
- [x] Arm in `enter_rec_mode()`: `rec_k30.arm(s30_value)`, `rec_k36.arm(s36_value)`
- [x] In recording knob tick: if `rec_k30.update(s30)`: `slot.drive = s30`; if `rec_k36.update(s36)`: `slot.volume = s36`
- [x] Global drive (S30) only applies when not in recording mode

### Step 6 — Output soft-clip limiter

Replace fixed `/kVoices` division with per-voice scaling + limiter.

- [x] In `VoicePool::Render()` (or at the output copy): apply `x / (1.0f + fabsf(x))` after summing all voices
- [x] Remove `/kVoices` from the output scale in AudioCallback
- [ ] Tune `out_level` default so that single Basic Pitch voice at center is comfortable (hardware test)

### Step 7 — Verification pass

- [ ] Seq hardware test: tempo, shuffle, genres, density, manual triggers, P0+P2 re-randomize
- [ ] All three SW2 modes: mode switch, blink counts, slot persistence
- [ ] Recording mode: enter (drum hold + pitched P0+pad), edit, save, cancel, copy to 1+ pads
- [ ] Per-slot volume: set different volumes in recording, confirm audible difference in seq
- [ ] Drive: S30 audible in Basic Pitch; per-slot drive difference audible in drum mode
- [ ] Unified decay: S37 audibly controls tail time for kick, snare, hat in seq mode
- [ ] Six-Op models: audition preset fires on model change; check Random mode audibility

### Step 8 — SEQ knob remap — DONE (needs hardware test)

Drive stays on S30 globally (including Seq). Seq knobs shift: Tempo moves to S31, freeing S34 for Kick punch.

- [x] Seq knob reads: S31 → tempo, S32 → shuffle, S33 → density, S34 → kick punch
- [x] S30 in Seq: overall drive, applied per-trigger as `overall × slot.drive` (slot.drive is a ratio, default 1.0 for drum slots)
- [x] Fix S37 Tightness direction: factor changed from `1.0f - tightness * 0.8f` to `0.2f + tightness * 0.8f` — S37 down = shorter tail
- [ ] Hardware test: confirm S37 direction and the new knob layout feel right

### Step 9 — Playmode randomize redesign + mode memory — DONE (needs hardware test)

Three related changes from the design review session. Implemented with **per-mode slot arrays**: `bp_slots[]` (Basic Pitch snapshots), `pad_slots[]` (Random), `drum_slots[]` (Seq kit) — each mode keeps its own state.

**Basic Pitch gets P0+P2 randomize stages:**
- [x] P0+P2 hold in BASIC_PITCH: 1s → soft random ±0.25 into `bp_slots`; 2s → ±0.45. Same engine, no model change.
- [x] While `bp_slots_active`, pads play their snapshots; grabbing any timbral knob (S32/S33/S34/S37 moved >5%) or picking a model via P0/P2+S35 returns to live knob mode
- [x] LED flash at 1s and 2s (hold animation now runs in every playmode)

**Random mode drops to 2 full-random stages:**
- [x] Stage 1 (1s): `generate_full_random(false)` — decay locked to current S37 value
- [x] Stage 2 (2s): `generate_full_random(true)` — decay spread ±0.25 around S37 value
- [x] Old soft same-engine stages removed from Random (moved to Basic Pitch). Engine pool = all engines except Chiptune, incl. drum engines played at scale pitches.

**SEQ P0+P2 staged re-randomize (replaces the instant P0+P2 / P1+P2 combos):**
- [x] Stage 1 (1s): `mutate_drum_soft()` — nudge harmonics/timbre/decay ±0.1 of current kit; engines + notes unchanged
- [x] Stage 2 (2s): full `generate_drum_random()` — new kit, seq restarts from bar 0

**Mode memory:**
- [x] Separate slot arrays per mode; Seq entry no longer clears Random slots (`slots_ready = false` removed); drum kit generated only when `!drum_kit_ready` (first entry) or via P0+P2 stage 2
- [ ] Hardware test: flick between all 3 modes and confirm each returns to last state

### Step 10 — Bug fix: SEQ recording entry triggers sound continuously — DONE (needs hardware test)

- [x] Root cause found (differs from the guess): in the seq tick, `triggers |= (1u << rec_slot)` ran **every audio block** (250×/s), not once per step — `Tick()` only returns a step mask on the firing block, but the force-OR was unconditional
- [x] Fix: added `Sequencer::StepFired()`; rec slot is force-fired only on blocks where a 16th step actually fired
- [x] Also: entry audition voice skipped when the seq is running (the forced step trigger already sounds the slot; audition just doubled it)
- [ ] Hardware test: enter Seq recording, confirm the slot fires on steps only

### Step 11 — Background drum seq + P2+P11 transport — DONE (needs hardware test)

Bug fix + feature from the 2026-07-03 session.

**Bug: Seq-mode pads played Random-mode models.** Regression from the Step 9 array split — pads fell through to the `current_mode` switch reading `pad_slots`.
- [x] Pads 3–9 in Seq mode route to `trigger_drum()` (shared by seq steps and manual hits: punch, tightness routing, drive ratio)

**Feature: drum seq plays under Basic Pitch / Random:**
- [x] Seq runs independently of SW2; flipping away from Up no longer stops it
- [x] First SW2-Up entry (or boot into Seq) auto-starts; later entries keep last play/pause state (`seq_entered_once`)
- [x] P2+P11 combo (P2 first) toggles play/pause in all 3 playmodes; replaces P1 tap; P1 now unused
- [x] While P2 held, P11 octave-up is disabled; normal after both released. Combo blocked during recording (P11 = drum pitch there).
- [x] Seq settings lock at last SW2-Up values when in pitched modes (`seq_punch_lk`/`seq_tight_lk`/`seq_drive_lk` + tempo/shuffle/density/genre not updated)
- [x] VoicePool param lock: seq triggers use `lock_params=true` + slot ids 16+i so pitched-mode global setters and pad NoteOffs can't touch drum voices
- [x] Starting the seq from a pitched mode before ever entering Seq generates the kit on the spot
- [ ] Hardware test: drums + synth together — listen for voice-steal artifacts (4 voices shared; "expand pool to 7" is parked)

### Step 12 — Seq knob pickup, model-select fix, BP clean stage — DONE (needs hardware test)

- [x] **Seq knob pickup:** seq settings (drive/tempo/shuffle/density/punch/tightness) live in stored values; pots only take effect after crossing the stored value. Armed on every Seq entry and re-armed after recording exit. First-ever entry = knobs live immediately.
- [x] **Bug: Basic Pitch model select dead while seq engaged.** Root cause: global `SetEngine` skips param-locked drum voices (correct), but `NoteOn` never re-applied the engine to a recycled voice — pads played stale drum engines. Fix: VoicePool caches all globals; `NoteOn`/`Audition` rehydrate the voice from the cache. (Not a pad-release issue.)
- [x] **Basic Pitch P0+P2 stage 3 (3s):** drops the randomize snapshots and restores the clean live-knob sound, with a confirming audition.
- [x] **CpuLoadMeter:** ISR load printed over USB serial every 2s (`CPU avg X% max Y%`) — measurement groundwork for Step 13.
- [ ] Hardware test: re-enter Seq after twisting knobs in a pitched mode — nothing may jump; model select in Basic Pitch with seq running; 3s hold restores clean sound

### Step 13 — Voice expansion + MIDI groundwork (analysis in notes.md)

Measure first, then bump. Full budget analysis in `notes.md` → "Voice expansion + MIDI".

**Baseline measured 2026-07-03 (4 voices, SDRAM, -O2):** idle avg 44% / max 49%; playing avg 47–79%, max 95–100% with drum seq + busy Random. Verdict: no headroom for a raw voice bump — optimize first.

**Optimizations applied (need re-measurement):**
- [x] Voice memory SDRAM → internal SRAM (scratch + impl, ~100 KB; SRAM now 25% used, SDRAM 0%) — physical-modeling engines walk these buffers every sample; external-RAM latency was the likely dominant cost
- [x] Voice sleep — a voice below −80 dBFS for 32 ms with gate off stops rendering until the next trigger. Idle floor should collapse (all voices slept), and drum-seq voices become nearly free between hits. Gate-held pads never sleep (silent Six-Op regions stay knob-sweepable). Behavior note: Chiptune (7) is silent until its first trigger, then never sleeps.
- [x] `OPT = -O3` for project + Plaits sources (binary 275→301 KB, flash 3.7%)

**Re-measured after optimizations (2026-07-03):** idle 3% (was 44%); heavy use 50–70%; peak-hold max 96–98% (single worst block since boot — no audible artifacts, only the 4-voice steal limit). Sleep + SRAM move confirmed effective.

**Bumped to 6 voices** (`kVoices` + `kMaxVoices`; SRAM 35%). Meter max is now **windowed** (resets after each 2s print) so it reads the recent worst block instead of peak-hold since boot.

**6-voice abuse test (2026-07-03):** idle 3%/4%; abuse peaks hit **111% and 138%** — real overruns (per-voice worst ≈ 23%, so even 5 voices could exceed 100% on all-expensive engines). No obvious audible artifacts thanks to DMA double-buffering absorbing isolated spikes, but not acceptable.

**Mitigation: load-shedding guard** — if the previous block exceeded 90% of the budget, the oldest awake non-held voice is force-slept at the start of the next block; two voices if the block actually overran (>100%). Shed events print in the CPU line (`shed N` per 2s window).

- [x] Hardware test the guard (2026-07-03): **confirmed good middle ground — no audible crackling.** Abuse: `shed 3–9`, avg ≤77%, clean recovery; calm: `shed 0`. Residual first-block spikes (~111–115%) are absorbed by DMA double-buffering, inaudible. 6 voices + sleep + shed guard is the accepted configuration.
- [ ] Optional future: ITCM placement (64 KB, unused) to lower per-voice cost — prerequisite for 7 voices, or if shedding ever becomes audible in normal play
- [ ] Next: MIDI phase 1 (below) — remember the USB port conflict with serial logging
- [ ] MIDI phase 1 (after voices settled): USB MIDI in — ch1 notes = pitched (note number = pitch, bypasses scale), ch10 = GM drum map to slots 0–6; CC20–31 mapped to *functions* (not pots), CC writes re-arm the pot pickup
- [ ] MIDI drum pitch phase 2: notes within ±6 of a slot's GM anchor play that slot transposed
- [ ] Note: USB serial log and USB MIDI share the port (`#ifndef USB_MIDI` guard) — measurement builds and MIDI builds are mutually exclusive unless TRS MIDI is used

### Step 14 — Testing round 2 fixes (2026-07-03) — DONE (needs hardware test)

- [x] **Bug: S30/S34 dead after seq re-entry (+ rec knobs one-directional).** Pickup crossing test could never fire when the stored value sat at a pot extreme (0.0/1.0), and rec pickups armed at the pot's entry position only engaged moving down. Fix: inclusive crossing + 1% near-window; rec pickups arm to the **slot's actual value** (true pickup, both directions).
- [x] **Rec force-fire halved** — every other step (8th notes) instead of every 16th.
- [x] **Unified recording flow** — hold pad **1200ms** to enter in both Seq and Random (P0+pad entry removed); confirm/cancel/copy thresholds unified at 1200ms.
- [x] **Copy audible confirmation** — the copied sound fires on the target slot (trigger_drum in Seq, audition in Random).
- [x] **Seq volume separate** — S36 drives per-group volumes in VoicePool (locked/drum vs pitched); Seq's volume has pickup + survives mode switches; pitched S36 no longer affects drums.
- [x] **Drive loudness makeup** — distort() output scaled by 1/(1+drive) (−6dB at full); tune slope by ear.
- [x] **Six-Op random generation anchored** to `kSixOpAud[]` audible presets (h ±0.08, t/m ±0.15) — was the old P2 "Six-Op audition in random modes" item.
- [ ] Hardware test: seq knobs after mode round-trips (incl. extremes), rec knob pickup from both directions, 1200ms entry feel in Random, copy confirmation, seq-vs-pitched volume balance, drive loudness

### Step 15 — Testing round 3 fixes (2026-07-03 20:52 notes) — DONE (needs hardware test)

- [x] **Bug: rec audition every ~3s until a knob picked up.** The retrigger was keyed on the knob-change flag, but `KnobPickup::update()` returns the caught *state* (level, true every block once caught), not a change *edge* — so the audition ran at 3s before the first pickup and 500ms after (moving S37 to min caught its pickup, which is why it "fixed" the timing). Now a fixed **500ms audition pulse** from rec entry, whenever the seq isn't force-firing the slot.
- [x] **Per-pad volume in Random** — already stored/applied via rec S36 (Step 5/14 work); the gap was that auditions played at full volume so edits were inaudible while recording. `AuditionWithParams` gained a volume arg; rec entry, the 500ms pulse, copy confirmation, and drum pitch nudges now play at the slot's stored volume.
- [ ] Hardware test: enter rec with seq paused → steady 0.5s pulse immediately; S36 changes audible live in rec (Random and Seq-paused); confirm volumes persist on normal play.

---

## Priority 2 — After the numbered steps are stable

- [x] **Six-Op audition in random modes** — done in Step 14: `generate_full_random()` anchors engines 2–4 to `kSixOpAud[]` presets.
- [x] **Electro pattern redesign** — done 2026-07-04: breaks-style bank transcribed from the 6-pattern sketch; layers encoded as weights so density reproduces the build (see notes.md Known Issues). Hardware-listen pending.
- [ ] **Per-track pattern variants — S35 in Seq mode** (feasibility decided 2026-07-03: **variant bank wins over generative**). S35 is free in Seq mode (model select disabled there). Hold a pad + turn S35 (quantized zones, deadzone pickup like the bank select) → pick that track's pattern variant from hand-authored rows: `kSeqWeights` grows a variant dimension, `Sequencer` gets `variant_[7]`. Flash cost trivial (~5 KB for 4 variants × 7 tracks × 3 genres); keeps genre feel; S35 position is repeatable. Phase authoring: 2 variants per track first. A generative variant (Euclidean E(k,16) + rotation) can be the last slot per track later. UI guard required: S35 move past deadzone while a pad is held must reset the rec-entry hold counter or it collides with 1.2 s Recording entry. Pairs naturally with the Electro redesign (author the new rows in the same pass).
- [ ] **Density + chance axis** — S32 split: 0–0.45 = less density, 0.5 = normal, 0.55–1 = chance/mutation. Changes seq tick logic.
- [ ] **LED blink on model load** — brief single blink when engine changes via S35. Helps confirm Six-Op switches.

## Priority 3 — Feature additions

- [ ] **P1 = melodic seq trigger** — in Basic Pitch / Random modes, P1 toggle arms the drum sequencer to fire its patterns against the current scale/pad layout (each drum track → scale degree). P1 is reserved for this and currently unused in those modes.
- [ ] Rhythm mutation gesture (mutate weight tables, not just sounds)
- [ ] 4-bar fill (P1 held = max density for one bar)
- [ ] Live record into pattern

## Parking Lot

OLED screen, persistent state, chord mode, TRS MIDI, Phase 8F retry.  
(USB MIDI and voice pool expansion promoted to Step 13.)  
See `notes.md` Parking Lot section.
