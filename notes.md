# TouchPlaited — Working Notes

> **Status:** working prototype. Control reference and mode layout reflect current code.  
> Steps 2–7 (volume, drive, recording redesign, soft-clip limiter) are still planned — see `ROADMAP.md`.  
> Archived original plan is in `notesarchive/plan_archive.md`.

---

## Control reference

### Normal modes (SW2: down=Basic Pitch / center=Random / up=Seq)

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

### Basic Pitch mode — P0+P2 hold stages

SW2 down = Basic Pitch. P0+P2 hold stages apply here (moved from old Random mode stages 1 & 2).

| Hold duration | Stage | What happens |
|--------------|-------|-------------|
| 1s | 1 — Soft tight | Same engine as current selection. Each pad gets random params within ±0.25 spread. All pads play scale pitches. |
| 2s | 2 — Soft wide | Same engine, larger variance ±0.45. Still scale pitches. |
| 3s | 3 — Clean | Drops the snapshots and restores the clean live-knob sound (audition confirms). |

Stages are cumulative: 3s passes through 1 and 2 first.

After stage 1 or 2 fires, pads play the randomized snapshots (`bp_slots`) instead of the live knobs. **Escape back to live mode:** hold to stage 3, move any timbral knob (S32/S33/S34/S37) more than 5%, or pick a model with P0/P2+S35.

### Random mode — P0+P2 hold stages

SW2 center = Random mode. Random is now the true full-random selector — no soft/same-engine stages here.  
Drum mode is **not accessible from Random** — it lives in Seq only (SW2 up).

| Hold duration | Stage | What happens |
|--------------|-------|-------------|
| 1s | 1 — Full random | Each slot gets a random engine + params from the full pool (all engines except Chiptune; drum engines 21–23 included, played at scale pitches). Decay locked to current S37 value. |
| 2s | 2 — Full random spread | Each slot gets a random engine + params. Decay spread ±0.25 around current S37 value. |

Stages are cumulative: 2s passes through 1s first.  
P0+P2 in Seq mode: staged drum randomize — see Seq mode section below.

### Recording mode

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

### Seq mode (SW2 up)

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

**P0+P2 staged re-randomize in Seq:**

| Hold duration | Stage | What happens |
|--------------|-------|-------------|
| 1s | 1 — Soft variance | Randomize parameters of current loaded models with slight variance; keep engines |
| 2s | 2 — Full new kit | Fully randomize all models and parameters; new engines picked from drum pools |

Stages are cumulative: 2s passes through 1s first.

**Mode memory:** switching between playmodes restores the last state for that mode — no re-randomize on mode switch. Only P0+P2 forces a re-randomize. First entry into Seq (or after a full restart) always generates a fresh drum kit.

---

## Hardware reference

**SW1 (left switch) — Scale / Genre**
- PCB labels: S10 / S9; Daisy pins: D9 / D8
- Code: `touch.switches().B()` → `_switch_9_10`
- Positions: center=Chromatic/Techno, left=Major/Electro, right=Minor/Ambient

**SW2 (right switch) — Playmode**
- PCB labels: S7 / S8; Daisy pins: D7 / D6 — polarity inverted vs. label
- Code: `touch.switches().A()` → `_switch_7_8`; sw=2=Down, sw=0=Center, sw=1=Up
- Positions: down=Basic Pitch (1 blink), center=Random (2 blinks), up=Seq (3 blinks)

**Note:** Switch files kept as TouchBass template — no modifications.

---

## Deliberate decisions

**Block size: 192, rendered as 8 × 24-sample chunks**  
~4ms per callback at 48kHz. Larger block chosen to give ISR headroom for drum engines.

**P0 and P2 have no standalone sound action**  
`bank_caught = false` anchored at TOUCH moment (not release). Pot must move past dead zone (3%) before model select activates.

**Engine LED blink removed**  
Counted blinks too hard to read in practice. Model changes confirmed by audition tone only. OLED = future hardware path.

**MPR121 thresholds: library defaults (12/6)**  
Raising threshold to 20 broke pad registration. Ghost-touch suppression via `any_musical_pad_held()` guard in software.

**SW1/SW2 fully independent** — fixed by restoring TouchBass template switch files (D10 was wrong pin).

**Phase 8F (controls out of ISR) — reverted**  
Data race crash: `generate_*()` writes `pad_slots[]` in main loop while ISR reads them in `SetOnTouch` touch callback. Fix requires `__disable_irq()` / `__enable_irq()` wrapping around all `generate_*()` calls. Defer unless crackle returns.

**S30 repurposed from FM amount to Drive**  
`patch.frequency_modulation_amount` has no effect without a CV input (`modulations.frequency` is always 0 on this hardware). S30 is a dead knob in its original role. Repurposed as soft-clip drive.

**Unified decay: S37 → patch.morph for drum engines 21–23**  
BassDrum, SnareDrum, and HiHat ignore `patch.decay` and use `patch.morph` as their tail length. `slot.decay` always means "tail time" from the user's perspective; the routing to the correct patch field happens at playback.

**Per-mode slot arrays (mode memory)**  
Three separate arrays: `bp_slots[]` (Basic Pitch randomize snapshots), `pad_slots[]` (Random), `drum_slots[]` (Seq kit). Flicking SW2 restores each mode's last state; nothing regenerates on mode switch. Only P0+P2 re-randomizes; the drum kit is generated once on first Seq entry (`drum_kit_ready`).

**Seq per-slot drive is a ratio, not an absolute**  
`drum_slots[i].drive` defaults to 1.0 and multiplies the overall S30 drive at trigger time (`overall × ratio`). In recording mode S30 edits the slot's ratio with pickup while the overall drive is frozen at its rec-entry value, so slot edits don't move the whole mix. Pitched slots keep `drive = 0.0` (absolute), since global `SetDrive` overrides per-voice drive outside Seq anyway.

**Basic Pitch snapshot escape via knob grab**  
After P0+P2 randomize, Basic Pitch plays frozen `bp_slots` — but the mode's identity is "all params live on knobs", so any timbral knob moved >5% (or a model picked via P0/P2+S35) snaps back to live mode. No extra gesture to learn.

**Background seq: param-locked voices + offset slot ids**  
Seq/drum triggers call `NoteOnWithParams(..., lock_params=true)` with slot ids `16+i`. The lock makes all VoicePool global setters (engine, harmonics/timbre/morph/decay, drive, LPG, FM) skip those voices, so Basic Pitch live knobs and pitched-mode drive can't reshape a drum mid-decay; the id offset keeps pad NoteOffs (slots 0–6) from cutting drum tails, and drums ring out as one-shots. Locked LPG is pinned to 0.5 at trigger. Seq trigger settings (`seq_punch_lk`/`seq_tight_lk`/`seq_drive_lk`) update only while SW2 is Up.

---

## Current state by feature

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

---

## Models list + division

Registration order from `voice.cc`. Software index differs from original Plaits panel order.

FR = Full Random / Drum mode stages. Chiptune (7) excluded from all FR stages.  
Engines 21–23 ignore `patch.decay` and `patch.lpg_colour` — use `patch.morph` for tail time.

| #  | Engine           | Family  | Bank | Character            | FR S1 | FR S2 | FR S3 |
|----|------------------|---------|------|----------------------|-------|-------|-------|
| 0  | VA + VCF         | engine2 | P0   | Lush VA w/ filter    | grp0  | ✓     | —     |
| 1  | Phase Distortion | engine2 | P0   | Casio-style PD       | grp0  | ✓     | —     |
| 2  | Six-Op A         | engine2 | P0   | DX7 FM patch A       | grp0  | ✓     | —     |
| 3  | Six-Op B         | engine2 | P0   | DX7 FM patch B       | grp0  | ✓     | —     |
| 4  | Six-Op C         | engine2 | P0   | DX7 FM patch C       | grp0  | ✓     | —     |
| 5  | Wave Terrain     | engine2 | P0   | 2D waveshaping       | grp0  | ✓     | —     |
| 6  | String Machine   | engine2 | P0   | String ensemble      | grp0  | ✓     | —     |
| 7  | Chiptune ⚠       | engine2 | P0   | Auto-arpeggiator     | —     | —     | —     |
| 8  | Virtual Analog   | engine  | P0   | Detuned saws         | grp0  | ✓     | —     |
| 9  | Waveshaping      | engine  | P0   | Wavefolding          | grp0  | ✓     | —     |
| 10 | Two-Op FM        | engine  | P0   | 2-op FM              | grp0  | ✓     | —     |
| 11 | Grain            | engine  | P0   | Formant/wavetable    | grp0  | ✓     | —     |
| 12 | Additive         | engine  | P2   | Harmonic partials    | grp1  | ✓     | —     |
| 13 | Wavetable        | engine  | P2   | Wavetable scan       | grp1  | ✓     | —     |
| 14 | Chord            | engine  | P2   | Chord synthesizer    | grp1  | ✓     | —     |
| 15 | Speech           | engine  | P2   | Voice formants       | grp1  | ✓     | —     |
| 16 | Swarm            | engine  | P2   | Detuned oscillators  | grp1  | ✓     | —     |
| 17 | Noise            | engine  | P2   | Filtered noise       | grp1  | ✓     | —     |
| 18 | Particle         | engine  | P2   | Granular noise       | grp1  | ✓     | —     |
| 19 | String           | engine  | P2   | Karplus-Strong       | grp1  | ✓     | —     |
| 20 | Modal            | engine  | P2   | Modal resonator      | grp1  | ✓     | —     |
| 21 | Bass Drum        | engine  | P2   | Analog kick          | grp1  | ✓     | ✓     |
| 22 | Snare Drum       | engine  | P2   | Analog snare         | grp1  | ✓     | ✓     |
| 23 | Hi-Hat           | engine  | P2   | Metallic noise       | grp1  | ✓     | ✓     |

---

## Per-model control reference

What each knob does per engine. FM amount (S30) was dead on this hardware (no CV input); it is now Drive.  
LPG colour (S31) applies to all engines 0–20; ignored by 21–23 (internal envelope).  
Decay (S37) routes to `patch.decay` for 0–20, `patch.morph` for 21–23.

| #  | Engine           | S32 Harmonics | S33 Timbre | S34 Morph | S37 Decay/Tail |
|----|------------------|---------------|------------|-----------|----------------|
| 0  | VA + VCF         | Filter cutoff | Resonance  | Osc mix   | Env decay |
| 1  | Phase Distortion | PD waveform   | Saturation | Symmetry  | Env decay |
| 2  | Six-Op A         | FM algorithm  | Op levels  | Ratio/FB  | Env decay |
| 3  | Six-Op B         | FM algorithm  | Op levels  | Ratio/FB  | Env decay |
| 4  | Six-Op C         | FM algorithm  | Op levels  | Ratio/FB  | Env decay |
| 5  | Wave Terrain     | X position    | Y position | Terrain   | Env decay |
| 6  | String Machine   | Filter cutoff | Chorus     | Voices    | Env decay |
| 7  | Chiptune ⚠       | Arp range     | Waveform   | Speed     | Env decay |
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
| 19 | String           | Inharmonicity | Brightness | **→ S31 (= morph: damping/decay)** | Env decay |
| 20 | Modal            | Material      | Brightness | **→ S31 (= morph: damping/decay)** | Env decay |
| 21 | Bass Drum        | Pitch drop    | FM punch   | **→ S37** | **= morph: tail** |
| 22 | Snare Drum       | Noise freq    | Noise/body | **→ S37** | **= morph: char+tail** |
| 23 | Hi-Hat           | Freq spread   | Metallic   | **→ S37** | **= morph: tail** |

**Six-Op A/B/C note:** these engines produce sound only in specific parameter regions. At random/default values they are often silent. Known behavior: some combinations only produce sound on every second gate trigger. Use the audition preset table (in code: `kSixOpAud[]`) to land at audible FM positions.

**Chiptune (7):** self-running arpeggiator — no gate response. Only S34 (speed) has obvious effect in live use. Already excluded from all random pools. Status: keep as a manually selectable model only; do not add to random pools.

---

## Drum engine parameters

**21 — Bass Drum:** Harmonics=pitch drop speed, Timbre=FM punch, S37→morph=decay time, Note=base pitch  
**22 — Snare Drum:** Harmonics=noise freq, Timbre=noise/body mix, S37→morph=character+decay, Note=drum body pitch  
**23 — Hi-Hat:** Harmonics=freq spread, Timbre=metallic ratio, S37→morph=decay time, Note=pitch center

Non-drum engines usable as percussion: 17 Noise (hat/snare), 18 Particle (rim/snare), 19 String (rimshot/clave), 20 Modal (cowbell/conga).

---

## Drum pad layout

```
P3    P4    P5    P6    P7    P8    P9
Kick  Snr   CHH   OHH  Clap  Tom   Perc
```

GM ref: 36=Kick · 38=Snare · 42=CHH · 46=OHH · 39=Clap · 41=Low Tom

### Per-role engine pool + param ranges

`slot.decay` = tail time. For engines 19–23 this routes to `patch.morph` at playback (their morph is the model's damping/decay — verified in string_voice.cc/modal_voice.cc); S34 morph has no effect on them.

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
| 18 Particle | 0.05–0.20 | 0.40–0.80 | 0.10–0.50 | 48–60 |

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
| 18 Particle | 0.05–0.20 | 0.50–0.90 | 0.10–0.50 | 55–70 |

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
| 18 Particle | 0.10–0.30 | 0.30–0.70 | 0.30–0.70 | 60–80 |
| 22 SnareDrum | 0.05–0.20 | 0.05–0.35 | 0.30–0.60 | 66–80 |

String (19) removed from the Perc pool — Karplus-Strong reads as a loud melodic pluck, not percussion. Modal tail capped (was 0.40–0.90). Snare engine pitched high with body-heavy timbre = rim/wood tick.

Default per-slot volumes at randomize time (tuning targets): Kick 0.9, Snare 0.8, CHH 0.55, OHH 0.65, Clap 0.75, Tom 0.7, Perc 0.5 (was 0.6 — perc sat too far forward).

---

## Open Decisions

### 1. ✅ Recording mode confirm gesture — RESOLVED

**Decision:** hold rec pad alone ≥800ms = confirm.  
**Root cause of original bug:** code was checking P2 (not P0) as modifier. No actual conflict with model-change — feature was just broken.  
**Guard:** `rec_entry_released` flag: pad must be released at least once after entry before confirm hold timer can fire.

### 2. ✅ Mode structure — RESOLVED

**Decision:**
- SW2 Down = Basic Pitch (unchanged)
- SW2 Center = Random (merged Soft + Full; P0+P2 stages 1/2/3 = soft tight / soft wide / full random models — all pitched, no drums)
- SW2 Up = Seq (drum mode exclusively; starts immediately on enter; P1 = play/pause)

**Drum mode is Seq-exclusive.** Not reachable from Random via P0+P2 anymore.

**P1** is unused in Basic Pitch and Random modes for now. Reserved for future: trigger seq patterns against current pitched mode (melodic seq — parking lot).

**P0+P2 stages in Random** are all pitched: stage 3 gives each pad its own random engine + params, but pads still follow scale pitches. Chaos is musical chaos, not drum chaos.

### 3. Chiptune engine (7) — keep, remove, or improve

Already excluded from all random pools. Produces no sound on gate (self-running arpeggiator). Options: keep as manually selectable only (current), remove from S35 model list entirely, or remap S30/S32/S33 to its internal rate/range/waveform params. Recommend keeping as manual-only for now.

---

## Known Issues

### ✅ Six-Op A/B/C models (2, 3, 4) barely audible — FIXED for random generation

The controls *are* wired correctly — these engines are just unusual: **S32 harmonics is a quantized DX7 patch selector** (small moves do nothing, then jump to another patch — this is why knob sweeps feel unrepeatable/random) and **S33 timbre is the FM modulator level** (near zero ≈ silent). Random values therefore usually landed in dead zones. Fix: `generate_full_random()` now anchors engines 2–4 to the `kSixOpAud[]` audible presets with a small variance (h ±0.08, t/m ±0.15) instead of the open 0.2–0.8 range. Manual knob sweeps in Basic Pitch remain fully open — expect the quantized-selector feel on S32.

### Six-Op: first pad touch quiet, second touch sounds (open)

Some Six-Op patches only ring on every second gate — DX7 patches have LFO/envelope state that initializes on the first trigger after an engine switch or voice steal. Not a value-application bug on our side. Possible mitigation if it grates: fire a silent "priming" trigger when a voice switches onto engines 2–4, or pin Six-Op voices so they aren't re-initialized by stealing. Parked until it matters.

### ✅ Seq S30/S34 pots dead after seq re-entry (also rec knobs one-directional) — FIXED

Two pickup flaws: (1) a strict crossing test can never fire when the stored value sits at a pot extreme — drive/punch stored at 0.0 made those pots permanently dead after re-entry; (2) rec pickups armed at the pot's own entry position only engaged when moving *down* through it. Fix: inclusive crossing + 1% near-window, and rec pickups now arm to the **slot's actual value** (true pickup).

### ✅ Decay in seq mode — FIXED

S37 tightness now applies to all engine 21–23 slots (not just hats). `slot.decay` routes to `patch.morph` at trigger time for drum engines, scaled by tightness.

### ✅ S37 Tightness direction inverted in Seq mode — FIXED

Factor changed from `1.0 - tightness * 0.8` to `0.2 + tightness * 0.8`: S37 down now shortens the tail. Needs hardware confirmation.

### ✅ Basic Pitch model select broken while seq engaged — FIXED

Symptom: P0/P2+S35 auditioned the new model but pads kept playing the first pitch model or even drum sounds. Not a pad-release problem — a voice-reuse problem: `SetEngine` (correctly) skips param-locked drum-seq voices, but plain `NoteOn` never re-applied the engine, so a recycled drum voice kept its drum engine. Fix: VoicePool caches all global values (engine, h/t/m/decay, LPG, drive) and `NoteOn`/`Audition` rehydrate the voice from the cache before triggering.

### ✅ Seq-mode pads played Random-mode models instead of drums — FIXED

Regression from the per-mode slot array split: pads 3–9 in Seq mode fell through to the `current_mode` switch and read `pad_slots` (Random) instead of `drum_slots`. Now `seq_mode_on` routes pad touches to `trigger_drum()` (same punch/tightness/drive path as seq steps).

### ✅ SEQ recording mode — sound triggers continuously on entry — FIXED

Root cause: `triggers |= (1u << rec_slot)` in the seq tick ran every audio block (250×/s) instead of once per step. Now gated on `Sequencer::StepFired()` so the rec slot fires on 16th-step boundaries only. The entry audition voice is also skipped while the seq runs (it doubled the forced triggers).

### ✅ Rec audition fires only every ~3s on entry, until a knob picks up — FIXED

Reported 2026-07-03 20:52 (Seq + Random rec, seq paused): auditions came every ~8 LED blinks; moving S37 to min "fixed" the rate. Root cause: the retrigger was keyed on the knob-change flag, but `KnobPickup::update()` returns the caught **state** (level), not a change **edge** — before any knob picked up only the slow 750-block periodic path (3 s) could fire; after the first catch the flag was true every block and the fast path ran permanently. That's why touching S37 flipped the timing: min value crossed the stored decay and caught the pickup. Now a **fixed 500 ms audition pulse** from rec entry onward, whenever the seq isn't already force-firing the slot.

### ✅ Per-pad volume in Random mode — already existed, now audible — FIXED

S36 in recording already stored per-slot volume for Random pads (same as drums) and playback applied it — but auditions always played at full volume, so edits were inaudible until after confirm, which made the feature look absent. `AuditionWithParams` now takes the slot's stored volume: rec entry, the 500 ms pulse, copy confirmation, and drum pitch nudges all play at the slot's level.

### ✅ Electro pattern redesign — FIXED (2026-07-04)

Replaced the Anthony Rother table with a breaks-style bank transcribed from the 6-pattern web-sequencer sketch (chain 1,2,1,2,3,4,3,4,5,6,…). The 6 patterns = 2 kick variations (A: 0+6 / B: 0+6+10+13, bars alternate A B A B) × 3 intensity layers, encoded as weights so **density reproduces the original build**: d1 = kick+snare+OH, d2 adds broken 16th hats, d3 adds rimshot-style Perc 16ths, d4 adds ghost snares (GH row → weight-1 snare ghosts). Tom only in the bar-4 fill (added, not in source). Clap (added 2026-07-04): steps 5+13, **probabilistic** — first chance mechanism in the seq: xorshift32 in `Sequencer`, gate in `eval_step()` for genre 1 track 4 only, odds = margin of (weight+density) over threshold → 25/50/75/100%. Same gate can generalize later for the S32 density+chance axis. Old table in git history.

---

## Voice expansion + MIDI — budget analysis (2026-07-03)

### CPU budget

- 48kHz / 192-sample block = **4ms per callback**. The `CpuLoadMeter` prints `CPU avg X% max Y%` over serial every 2s.
- **Measured baseline (2026-07-03, 4 voices in SDRAM, -O2):** idle avg 44% / max 49% (all 4 voices render even when silent); playing avg 47–79%, **max 95–100%** with drum seq + busy Random. Conclusion: raw `kVoices` bump impossible without optimization.
- Judge by **max**, not avg. Expensive engines: Six-Op FM (2–4), Modal (20), String (19), Speech (15), Particle (18).
- Target: max ≤ ~85% with the worst engine mix.

### Re-measured after optimizations (2026-07-03)

Idle **3%** (was 44). Heavy use 50–70% avg. Peak-hold max 96–98% at 4 voices with no audible artifacts. → **Bumped to 6 voices** (SRAM 35%). Meter max is windowed (resets each 2s print) and the line includes `shed N`.

**At 6 voices:** abuse peaks measured **111–138%** = real overruns (per-voice worst ≈ 23%; even 5 voices can exceed budget on all-expensive engines). DMA double-buffering masked isolated spikes audibly, but it's over the line — hence the **load-shedding guard**: previous block > 90% budget → force-sleep the oldest awake non-held voice next block (two voices if the block actually exceeded 100% — a seq step can wake several drums at once). Trade: an early tail fade (usually a drum tail or released note) instead of a glitch. `shed N` in the serial line counts sheds per 2s window.

**Guard verified on hardware (2026-07-03):** abuse windows show `shed 3–9` with avg controlled ≤77% and clean recovery; calm windows show `shed 0`; no audible crackling. Residual single-block spikes to ~111–115% remain (reactive shedding can't prevent the *first* hot block) — absorbed by DMA double-buffering, inaudible. Fully preventing them would need predictive per-engine cost accounting at trigger time; not worth it unless audible.

### Optimizations applied (re-measure to quantify)

1. **Voice memory SDRAM → internal SRAM** — scratch (16KB/voice) + impl (~9KB/voice) were in external SDRAM; physical-modeling engines walk those buffers every sample, so SDRAM latency was the likely dominant cost. Now in D1 SRAM (25% of 512KB used; 7 voices would be ~40%).
2. **Voice sleep** (`VoicePool::Render`) — a voice whose output stays below −80dBFS for 32ms with its gate off stops rendering until the next trigger. Idle cost collapses; drum-seq voices are free between hits. Gate-held voices never sleep, so holding a pad on a silent Six-Op region and sweeping knobs still brings the sound in. Quirk: Chiptune (7), the gateless arpeggiator, is silent until first pad trigger and never sleeps afterwards.
3. **-O3** for project + Plaits sources (`OPT = -O3` in Makefile; libdaisy.a unaffected). Binary 275→301KB.

### Remaining levers if still short

1. **ITCM placement** — code executes from QSPI flash (slow on I-cache miss); ITCMRAM (64KB) is 0% used. Move the hottest Plaits render paths there.
2. **Weighted polyphony** — per-engine cost table caps concurrent expensive engines.
3. Sample-rate drop to 32kHz — last resort, audible.

### Bumping kVoices

Two constants must move together: `VoicePool::kVoices` (voice_pool.h) and `kMaxVoices` (plaits_voice.cpp). Steal logic and SRAM budget scale fine to 7.

### MIDI cost estimate

MIDI itself is **CPU-negligible** (<1%): even a dense DAW stream is a few hundred 3-byte events/sec against a 480MHz core; parsing happens in the main loop, not the ISR. The *real* cost of MIDI is polyphony demand — chords from a DAW want 6+ voices, which is why voice expansion comes first.

**One hardware constraint:** USB serial logging and USB MIDI share the USB port (that's what the existing `#ifndef USB_MIDI` guard is for). Measure CPU first, then flip to MIDI — or use TRS MIDI (UART) on modded boards to keep both.

### MIDI mapping sketch

- **CCs map to functions, not pots** (pots are reused across modes): e.g. CC20 harmonics, 21 timbre, 22 morph, 23 decay, 24 drive, 25 LPG, 26 volume, 27 tempo, 28 shuffle, 29 density, 30 punch, 31 tightness. A CC write updates the stored value and re-arms that pot's pickup — the pot must cross the value to take over. The pickup infrastructure from the seq knobs generalizes to this.
- **Notes, channel split:** ch1 = pitched (note number = pitch directly, bypassing pad scale logic, into the current mode's sound); ch10 = drums, GM mapping 36→Kick, 38→Snare, 42→CHH, 46→OHH, 39→Clap, 41/45→Tom, 37/56→Perc.
- **Drum pitch over MIDI:** phase 1 — GM note selects the slot, the slot's stored pitch plays (matches pad behavior). Phase 2 — notes within ±6 semitones of the slot's GM anchor play the slot transposed by the offset, so kits become playable chromatically without giving up the one-pad-one-sound model.
- **Mod pads as MIDI:** P0–P11 states could mirror to notes on ch16 (or CC64+) for remote triggering of combos; low priority.

---

## MIDI implementation (2026-07-07) — phase 1 + CC map, both transports

`midi/midi_io.{h,cpp}` owns the transports; TouchPlaited.cpp owns the handlers. Needs hardware test.

### Transports
- **TRS/DIN (always built):** `MidiUartHandler` defaults = exactly the mod's wiring: USART1, **D13 TX / D14 RX**, 31250 baud. Initialized unconditionally — an idle UART is free, so unmodded boards lose nothing.
- **USB device (default build):** `-DUSB_MIDI` in the Makefile. That same define suppresses `startLog` and the CPU print (the pre-existing guards) — one USB port, one owner. Comment the define out for a measurement build; TRS MIDI keeps working there.

### Threading model (the part to remember)
- **In:** transports parse in their own IRQs into per-handler FIFOs. `MidiIO::Service()` (main loop) pops events and runs the handlers inside a short `__disable_irq()` section — handlers write pool/pickup state the audio ISR owns. Never pop in the audio ISR: FIFO push (UART IRQ) vs pop (audio ISR) can tear.
- **Out:** UART TX is blocking `PollTx` — ~1 ms per 3-byte message, a quarter of the block budget — so the ISR never sends. Pad/seq events push into a 64-deep single-producer ring; `Service()` drains ≤4 messages per call to both transports.
- **Main-loop latency:** the LED helpers block for up to seconds (`blink_numbered`), which would starve MIDI both ways — all LED delays now go through `delay_serviced()`, which services MIDI every 1 ms. MIDI jitter is therefore ~1 ms plus at most one in-flight TX.

### In mapping (as sketched, phase 1)
- **ch1 notes:** note number = pitch (scale/octave/root bypassed). Voice slot id = 32+note (pads 0–6, drums 16–22 — no collisions), so NoteOff matches exactly and chords steal like pads. Velocity → `p.volume`, linear. Sound = current mode: BP live = eff params (globals keep refreshing held notes, knobs stay live); Random / BP-snapshots = `slots[note % 7]` — stable multi-timbral cycling per key. In Seq mode ch1 plays the last pitched mode's sound (synth over the drum machine).
- **ch10 drums:** GM → slot with aliases (35/36 kick, 38/40 snare, 42/44 CHH, 46 OHH, 39 clap, 41/43/45/47/48/50 tom, 37/54/56/75/76 perc); unknown notes ignored. Phase 1: slot's stored pitch. Kit auto-generates if not ready. Velocity scales the slot volume via a new `trigger_drum(i, vel)` arg.
- **CC20–31 (any channel):** 20 harmonics, 21 timbre, 22 morph, 23 decay, 24 drive, 25 LPG colour, 26 pitched volume, 27 tempo, 28 shuffle, 29 density, 30 punch, 31 tightness. Every CC write re-arms the corresponding pot pickup. CC24 drive sets **both** pitched drive and `seq_drive_lk` (one function, two stored values). CC25 is the only LPG writer (pot retired; was hardcoded 0.5). CC27–29 also push into the Sequencer directly so tempo/shuffle/density respond while it plays in the background of a pitched mode.

### The eff_* layer (CC ↔ pot arbitration)
The pitched timbral knobs were raw per-block reads, so a CC write would have been stomped one block later. New layer: `eff_h/t/m/d/drive` = what actually sounds. Pot feeds eff through `cc_pu_*` pickups (force-caught at boot = pots live); a CC write sets eff and re-arms, pot must cross to take over — same rule as every mode hand-off. `last_*` stay raw pot reads because the BP snapshot escape watches pot *movement* (a CC write must not fake a grab). All sound-generating sites (BP live setters, soft-random anchors, stage-3 restore, model-select regen) now read eff_*.

### Out
- Pads in pitched modes → ch1 NoteOn/Off vel 100; the sent note is remembered per pad (`midi_pad_note_out`) so the Off matches even if octave/root moved mid-hold, and it fires even if rec mode swallowed the release.
- Seq steps + Seq-mode pad hits → ch10 GM one-shots (`kDrumSlotGm` = 36/38/42/46/39/45/37), NoteOn+NoteOff queued together.
- No CC out; auditions not sent. Incoming notes/CCs are never echoed — only clock and transport pass through (below).

### Clock + transport (2026-07-08) — hardware test pending
- **External clock in (F8 on either port) = hard tick sync**: `Sequencer::SetExternalClock` switches `Tick()` to consuming received ticks (6 per 16th step, `OnMidiClock` from the handler with IRQs off, `ext_ticks_pending_` volatile). No BPM estimation, no drift; shuffle quantizes to whole ticks (0–3 at max swing). The tempo knob and CC27 go inert automatically — the block counter isn't consulted — and still hold the fallback tempo. Ticks only count while playing, so they can't pile up during Stop and burst on Resume.
- **Clock detection**: first F8 flips `midi_ext_clock`; 500 ms without one (checked per block in the ISR) reverts to internal clock at the knob tempo.
- **Transport in**: FA = `seq.Start()` (step 0 fires on the next tick, per spec; kit auto-generates), FB = `seq.Resume()`, FC = `seq.Stop()`. Works with both DAW styles: clock-always (FA arrives inside a running tick stream) and clock-while-playing (FA may precede the first F8 — seq starts internal, first tick re-phases).
- **Clock out**: when external clock is present, incoming F8/FA/FB/FC pass through to the output. Otherwise TouchPlaited is master: `Sequencer::MidiClockTick()` emits 24 ppqn on the step clock's own timebase (`step_blocks_/6`, fractional accumulator — average rate exactly matches the drums, so synced gear can't drift), continuously from boot, phase-reset on Start/Resume so tick 1 lands with step 0. Local start/pause sends FA (SW2 first entry, kit regen) / FB (P2+P11 resume) / FC (P2+P11 pause) — suppressed while following an external clock.
- Realtime bytes are 1-byte queue entries now (`OutMsg{len, b[3]}`); ~48 msgs/s at 120 BPM ≈ 1.5% UART duty, nothing.
- libDaisy parser caveat: a realtime byte interleaved *mid-message* aborts that message (parser limitation). USB MIDI can't interleave (4-byte packets); TRS from typical interfaces inserts between messages — watch for dropped notes under heavy TRS clock+notes if it ever comes up.

---

## Reverb / delay FX send — resource analysis (2026-07-08)

Question: is there room for a reverb and/or delay, ideally as a per-slot FX
send (drums dry-ish while the seq runs, Basic Pitch with a long tail)?
Answer: **memory is a non-issue, CPU fits on average but eats into peak
headroom** — pair it with the ITCM move. Implement on a branch when picked up.

### Memory budget (measured from build/TouchPlaited.map, current build)

| Resource | Total | Used | Free |
|---|---|---|---|
| QSPI flash (code, BOOT_QSPI) | 7.75 MB | ~318 KB | ~7.4 MB |
| AXI SRAM D1 (voices live here) | 512 KB | ~260 KB | ~250 KB |
| SDRAM (external) | 64 MB | **0 bytes** | 64 MB |
| ITCM RAM | 64 KB | 0 | 64 KB |

SDRAM has been completely empty since voice memory moved to internal SRAM.
Reverb/delay buffers are the textbook use for it: unlike the physical-model
engines that thrashed SDRAM per-sample from multiple voices, a delay line is
one sequential read + write per sample — cache-friendly. DaisySP `ReverbSc`
(~390 KB buffer) is normally placed in SDRAM on Daisy anyway; seconds of
stereo delay are a rounding error there. A hand-rolled Dattorro / small FDN
would even fit in the free internal SRAM if SDRAM latency ever shows up.

### CPU budget

Baseline at 6 voices (2026-07-03 measurements): idle ~3%, heavy 50–70% avg,
windowed max 96–98%, shed guard at 90%, rare 111–115% single-block spikes
absorbed by DMA double-buffering.

- Stereo delay: ~1–2% per block.
- ReverbSc-class reverb: ~6–10% per block (SDRAM-resident).

FX render **every block** regardless of voice activity — a fixed +8–12% on
top of every block, including the worst-case ones already touching 100%. So:
works today, but expect `shed N` to climb during dense seq + busy Random
moments (earlier tail fades — the guard already keeps that inaudible).
Levers to buy the headroom back:

1. **ITCM placement** (already on the optimization list, 64 KB at 0% used) —
   moving the hottest Plaits render paths out of QSPI likely recovers more
   than the reverb costs. Do this alongside or before the FX.
2. **FX sleep**, same trick as voice sleep: skip the reverb render when the
   send bus has been silent and the tail has decayed below threshold — keeps
   idle at ~3% instead of ~13%.

### Per-slot send — design sketch (fits the existing architecture)

`VoicePool::Render` already applies per-voice `volume`/`blend`/`width` with
per-group (seq vs. pitched) multipliers — an FX send is the same pattern:

- Add `send` to `PadSlot` / `VoiceParams` (+ `voice_send[i]` in the pool);
  accumulate `l * send` into a second stereo bus inside the same render loop.
- Multiply by per-group send levels (`send_seq`, `send_pitched`) mirroring
  `vol_seq`/`vol_pitched`. That gives the target behavior directly: drums at
  zero-to-slight send in Seq, pitched group cranked with a long tail in Basic
  Pitch — independent state that survives mode flicks, like the volume/width
  pairs.
- In `AudioCallback` after `pool.Render`: run the reverb on the send bus, sum
  into `left`/`right` **before** the soft-clip.
- Per-slot send edits in recording mode (like drive/volume now); randomize it
  in the kit/slot generators.

Open design decision: one shared reverb with per-group send levels (cheap —
**recommended**, the usual hardware-box answer) vs. different reverb
*character* per mode (short room for drums, long hall for pitched). The
latter means either morphing time/damping on mode switch (retimes the old
mode's tails) or two instances (~double CPU). Delay can be a second, much
cheaper send alongside.

---

## Reverb / delay FX — implementation (2026-07-09, `FX` branch)

Follows the analysis above; deltas and decisions:

### Controls — mirror knobs on the P1 layer

P1+S30 = reverb, P1+S35 = delay. Both are **mirror knobs**: center = off
(±0.06 dead zone), each half is a character, wet grows outward. Chosen over
zone/preset splits for consistency and generous wet travel per character:

- Reverb: left = **room** (krt 0.35–0.6, lp 0.45), right = **hall**
  (krt 0.75–0.95, lp 0.80). Tail opens slightly as wet rises.
- Delay: left = **slapback** (fixed 120 ms, fb 0.05–0.30, bright), right =
  **synced dotted 1/8** (= 3 seq steps, fb 0.2–0.7, dark). Time changes slew
  per-sample (~70 ms τ, tape-style bend); snap while the delay is asleep.

Wet levels are **per-group** (`fx_rev_seq_lk`/`fx_rev_pitched_lk`, same for
delay) with mode memory, mirroring the volume/width pairs; sends use a
squared taper. The FX **character is shared** — last edit from either group
wins (one instance can't be room and hall at once; accepted, documented).
P1 edits use MoveCatch (crossing pickup against a mirror encoding felt dead);
on release, S30/S35's bare roles re-arm their pickups (drive: `seq_pu30` /
`cc_pu_drive`, pattern: `seq_pu35`) so nothing jumps. FX edits disabled while
recording (rec owns S30) and under P0/P2 (model select owns S35). Not
reachable over MIDI (candidate CCs later).

**Conscious reversal** of the parking-lot "P1 never held" ergonomics note:
an FX level is set-and-forget, not performative. Verify reachability on
hardware; fallback is bare S35 as pitched send.

### DSP — no new dependencies

- **Reverb**: Rings' `dsp/fx/reverb.h` (Griesinger/Dattorro: 4 input APs +
  2×(2AP+delay) loop, MIT) adapted in `synth/fx.cpp` onto the **already
  vendored** `plaits/dsp/fx/fx_engine.h` — identical base class, Rings also
  runs 48 kHz, so delay-line sizes carry over unchanged. 32768×uint16 buffer.
  Much cheaper than the ReverbSc estimate (~2–4% vs 6–10%).
- **Delay**: hand-rolled cross-feedback (ping-pong flavored) stereo line,
  one-pole damping in the loop, linear-interp fractional read. 2×64k floats.
- Both TUs keep Plaits/stmlib headers confined to `fx.cpp` behind `fx.h`
  (same leak rule as plaits_voice).

### Plumbing

`VoicePool::Render` grew two send buses (reverb/delay stereo pairs) filled
per-voice post-volume/width by group send levels (`rev_send_seq` etc.) —
voice-sleep peak still measured pre-volume, unchanged. `AudioCallback`
renders FX returns into the mix **before** the soft-clip. `Sequencer` exposes
`StepBlocks()` for the synced time (internal/knob tempo only — external MIDI
clock rate is not measured; synced delay follows the knob fallback tempo).

### Memory / CPU (measured at build)

SDRAM 0 → **576 KB** of 64 MB (reverb 64 KB + delay 512 KB); SRAM unchanged
(~50%); QSPI +8 KB. **FX sleep** implemented as planned: each FX skips its
render once input *and* tail are silent — reverb after >1 loop period
(~380 ms), delay only after a full delay-time of silence so a stale tail can
never replay on wake. Idle cost stays ~3%.

### Open on hardware

Levels/tapers (send taper, return gains, feedback ranges), character params
by ear, `shed N` behavior under dense seq + Random with FX cranked, P1
reachability. Future: per-slot send in Recording, send randomization in kit
generators, shimmer as an alternate right-side reverb character (needs
Clouds' pitch shifter port), ITCM placement if peaks pinch.

---

## Feature Ideas / Parking Lot

**Moved to `ROADMAP.md` (2026-07-08).** The roadmap is the single owner of
future work — every idea that used to live in this list (melodic seq, density
+ chance axis, rhythm mutation, live record, 4-bar fill, OLED, persistent
state, chord mode, voice pool 7, ITCM placement, Phase 8F retry, MIDI phase-2
ideas, per-track pattern variants, FX send) was carried over there with its
details. This file keeps the design record and analyses; a roadmap item links
back here when an analysis exists.



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
knob range split evenly across the genre's pattern count) — added 2026-07-06.

**Step byte encoding 0xCW:** low nibble W = weight 0–4 (unchanged semantics);
high nibble C = chance rolled after the density threshold passes — 0 = always,
1 = 75%, 2 = 50%, 3 = 25%. Plain decimal 0–4 still means deterministic, so old
tables read unchanged; `0x23` = weight 3 at 50%. Implemented with xorshift32 in
`Sequencer::eval_step()`. First use: Electro clap echoes on steps 5+13 at 50%.

**Pattern editor webapp:** `tools/pattern_editor.html` (open locally in a
browser; no server needed). 7×4×16 grid, click = set/clear, drag ↑↓ = weight,
drag ←→ = chance (color lime→amber→orange→red), density-preview slider dims
sub-threshold steps, bar copy, localStorage library seeded with the three
built-ins, C-table import, and one-click export of a ready `synth/patterns/`
file.
