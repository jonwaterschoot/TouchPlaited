# TouchPlaited — Working Notes

This is the freeform running log: design reasoning, debugging write-ups,
budget analyses, dated entries — the "why" and "how it went" behind the code.
It is **not** the TODO list — `ROADMAP.md` is the single owner of future work,
so check there for what's actually open. Resolved/historical material gets
moved out to `notesarchive/` once it's superseded rather than deleted (see
`notesarchive/readme.md` for the full story); the most recent sweep is
`notesarchive/notes_archive_2026-07.md`.

> **Status:** working prototype. Control reference and mode layout below
> reflect current code — these sections stay live reference, not history.  
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
- Positions: center=Chromatic/Techno, left=Minor/IDM-Ambient, right=Major/Electro

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

Non-drum engines usable as percussion: 17 Noise (hat/snare), 19 String (rimshot/clave), 20 Modal (cowbell/conga). Particle (18) is banned from all random drum pools — its intentionally sporadic crackle reads as a hardware fault in a generated kit (still manually selectable in rec mode).

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

String (19) removed from the Perc pool — Karplus-Strong reads as a loud melodic pluck, not percussion. Modal tail capped (was 0.40–0.90). Snare engine pitched high with body-heavy timbre = rim/wood tick. Particle (18) removed from Snare/Clap/Perc pools — see ban note above.

Default per-slot volumes at randomize time (tuning targets): Kick 0.9, Snare 0.8, CHH 0.55, OHH 0.65, Clap 0.75, Tom 0.7, Perc 0.5 (was 0.6 — perc sat too far forward).

---

## Open Decisions

All three resolved — detail in `notesarchive/notes_archive_2026-07.md` →
"Open Decisions — resolved" (items 1–2) and
`notesarchive/notes_archive_2026-08.md` → "Priority 3 — feature additions",
"Chiptune engine (7)" (item 3 — the 2026-07-24 decision to bring it into
manual selection was reversed on 2026-08-05; it is not shipping, and the
reason is recorded there).

---

## Known Issues

All fixed — the debugging write-ups (Six-Op audibility + gate/click fixes,
seq pickup bugs, tightness direction, model-select-while-seq, Electro
pattern redesign, the SSD1306 driver bugs found during OLED bring-up, etc.)
moved to `notesarchive/notes_archive_2026-07.md` → "Known Issues — all
fixed". Its one follow-up — filing the SSD1306 fix upstream — is closed too:
merged into the Synthux libDaisy fork and filed against
`electro-smith/libDaisy` (#634). See
`notesarchive/notes_archive_2026-08.md` → "Housekeeping — SSD1306 driver fix,
filed upstream".

---

## Voice expansion + MIDI — budget analysis, MIDI implementation, FX send

Three dated implementation logs moved to
`notesarchive/notes_archive_2026-07.md`, all fully shipped and in daily use:

- **"Voice expansion + MIDI — budget analysis (2026-07-03)"** — the CPU/SRAM
  measurements behind the 4→6 voice bump, voice sleep, the load-shed guard,
  and the SDRAM→SRAM move. Remaining levers (ITCM placement, 7th voice) are
  carried in `ROADMAP.md` Parking Lot.
- **"MIDI implementation (2026-07-07) — phase 1 + CC map, both transports"**
  — TRS + USB transport architecture, the threading model, note/CC/clock
  mapping, the `eff_*` CC-vs-pot arbitration layer. MIDI clock DAW-sync
  hardware-verified 2026-07-24.
- **"Reverb / delay FX send"** — the resource analysis (2026-07-08) and the
  `FX` branch implementation (2026-07-09): P1+S30/P1+S35 mirror knobs,
  Rings-derived reverb + hand-rolled delay, per-group sends. Hardware-verified
  2026-07-24 (levels/character/CPU headroom) — full write-up in
  `notesarchive/notes_archive_2026-07.md`.

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


---

## Playmode overhaul → Arp/Mel

The original design sketch that became the Arp/Mel playmode (SW2 center,
replacing Random) — arpeggiator, Hold, layered Rec note recorder, then a
follow-up round for per-mode independence (sound/volume/FX/octave no longer
shared across Basic Pitch/Arp/Rec). Fully implemented across 16 phases and
two hardware rounds; conflict analysis + phase-by-phase log in
`notesarchive/arp-mel-plan-archive.md` §6–§9, current user-facing behavior
in `MANUAL.md`. Original sketch text preserved in
`notesarchive/notes_archive_2026-07.md` → "Playmode overhaul".

## Syncing → CV clock in/out

The original sketch for listening to a clock signal on S43 (in) and driving
one out on S40 — implemented (Schmitt-triggered pulse-to-MIDI-clock bridge,
MIDI-outranks-CV priority with timeout fallback). See `MANUAL.md` "Clock
sync — MIDI and CV" for current behavior; hardware verification of the
trigger thresholds is still open, tracked in `ROADMAP.md`. Original sketch
text preserved in `notesarchive/notes_archive_2026-07.md` → "Syncing".

**23/07/2026 visualizer mobile-UX musing** (drag handle, label-size icons,
overlapping dynamic labels, small-OLED idea) — resolved the same day by the
on-faceplate OLED screen + device handle cluster and the physical OLED
build. Original text in `notesarchive/notes_archive_2026-07.md` →
"Visualizer mobile UX musing".

---
