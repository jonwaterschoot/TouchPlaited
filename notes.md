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
| **S31** | LPG colour (pitched modes) / fixed 0.5 (drum) | Tempo (60–180 BPM) |
| **S32** | Harmonics — live in Basic Pitch, per-slot in Random | Shuffle (0–50% swing) |
| **S33** | Timbre — live in Basic Pitch, per-slot in Random | Density (0–4 weight threshold) |
| **S34** | Morph — live in Basic Pitch, per-slot in Random | Kick punch (timbre boost) |
| **S35** | Model select (P0=bank 0–11 / P2=bank 12–23, pickup) | — disabled |
| **S36** | Pitched volume (live). Per-slot volume in recording mode | Seq/drum volume (pickup; independent of pitched volume) |
| **S37** | Decay / tail (→ patch.morph for drum engines 21–23) | Tightness (drum morph scale) |
| **SW1** | Scale: Center=Chromatic / Left=Major / Right=Minor | Genre: Center=Techno / Left=Electro / Right=Ambient |
| **SW2** | Playmode: Down=Basic Pitch / Center=Random / Up=Seq | — |
| **P0** | Modifier A — hold + S35 → model 0–11; hold + P10/P11 → root note | — |
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
- Flipping SW2 away from Up does **not** stop the seq — drums keep playing behind Basic Pitch / Random with all seq settings (tempo, shuffle, density, punch, tightness, drive, genre) locked at their last SW2-Up values.
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
| **S35** | — (disabled in seq mode) |
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
| 19 | String           | Brightness    | Damping    | Position  | Env decay |
| 20 | Modal            | Brightness    | N modes    | Position  | Env decay |
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

`slot.decay` = tail time. For engines 21–23 this routes to `patch.morph` at playback.

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
| 23 HiHat | 0.00–0.15 | 0.30–0.80 | 0.30–0.80 | 60–84 |
| 17 Noise | 0.00–0.12 | 0.65–0.95 | 0.25–0.65 | 60–84 |

**P6 — Open Hi-Hat**

| Engine | Morph→decay (slot.decay) | Timbre | Harmonics | Note |
|--------|--------------------------|--------|-----------|------|
| 23 HiHat | 0.40–0.85 | 0.30–0.70 | 0.30–0.80 | 60–84 |

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
| 19 String | 0.05–0.25 | 0.30–0.70 | 0.20–0.60 | 55–79 |
| 20 Modal | 0.40–0.90 | 0.30–0.70 | 0.20–0.60 | 60–84 |
| 23 HiHat | 0.15–0.40 | 0.50–0.90 | 0.50–1.00 | 72–96 |
| 18 Particle | 0.20–0.50 | 0.30–0.70 | 0.30–0.70 | 60–80 |

Default per-slot volumes at randomize time (tuning targets): Kick 0.9, Snare 0.8, CHH 0.55, OHH 0.65, Clap 0.75, Tom 0.7, Perc 0.6.

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

## Feature Ideas / Parking Lot

- **Sequencer on any playmode (melodic seq)** — seq fires against active Random/Basic Pitch mode, following scale. Makes drum weight tables apply to scale degrees. Big change, design first.
- **Density + chance axis** — S32 split: 0–0.45 = less density, 0.5 = normal, 0.55–1 = more mutation/chance per step.
- **Rhythm mutation** — P0+P2 second gesture mutates weight tables (changes rhythm, not just sounds). No current gesture available; needs hardware button combo or context-dependent.
- **Live record into pattern** — two routes: audio buffer loop (complex controls) or live trigger capture (simpler; can't layer on top). Not designed yet.
- **4-bar fill** — P1 held in seq mode triggers max-density bar then returns to normal.
- **OLED screen** — I2C 128×32/64 add-on; shows engine name, params, mode, root note. V2 hardware.
- **Persistent state** — libDaisy `PersistentStorage` for last model + pad slots across power cycles.
- **USB MIDI** — NoteOn/NoteOff from DAW via `MidiUsbHandler`.
- **Chord mode** — single pad triggers multiple Plaits voices at scale intervals.
- **Expand voice pool to 7** — after CPU headroom confirmed on in-use engines.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
- **TRS MIDI** — for hardware-modded Simple Touch boards.
- **Per-track pattern variants — S35 in Seq mode** (feasibility decided 2026-07-03, see ROADMAP Priority 2): S35 is free in Seq mode (model select is disabled there). Hold a pad + turn S35 → pick that track's pattern **variant** from a small hand-authored bank. Variant bank beats a generative method on feasibility: the sequencer is already a `[genre][track][64]` weight-table lookup, so variants are just extra rows (4 variants × 7 tracks × 3 genres × 64 bytes ≈ 5 KB flash — nothing); hand-authored rows keep the genre feel that made the templates work; and an S35 position is repeatable — you can get back to a pattern you liked, which a continuous generative morph can't do without seed quantization anyway. A generative option (e.g. Euclidean E(k,16) with rotation) can slot in later as the *last* variant per track, giving both worlds. Main work is authoring (63 new rows for a full bank — phase it: 2 variants per track first) plus one UI guard: an S35 move past its deadzone while a pad is held must reset the rec-entry hold counter, or picking patterns turns into accidental Recording at 1.2 s.



---

## Drum pattern system (2026-07-04)

Patterns live in **`synth/patterns/NN_name.h`** — one file per pattern, defining
`static constexpr uint8_t kPat_NN_name[7][64]`. `tools/gen_patterns.py` (run by
`make` on every build, rewrites only on change) registers them sorted by the
numeric prefix into the generated `synth/patterns_gen.h` (gitignored):
`kNumPatterns`, `kSeqPatterns[]`, `kSeqPatternNames[]`. Drop a new file in the
folder → next `make` embeds it. `Sequencer::kGenres` now comes from
`kNumPatterns`; SW1 (3 positions) reaches only patterns 0–2 — more patterns
need the planned S35 picker.

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
