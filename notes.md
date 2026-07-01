# TouchPlaited — Working Notes

## Control reference

### Normal modes (SW2 selects: down=Basic Pitch / center=Soft Random / up=Full Random)

| Control | Function | Notes |
|---------|----------|-------|
| **S30** | FM amount | `frequency_modulation_amount` 0..1 |
| **S31** | LPG colour / saturator | Non-drum: 0=VCA, 1=LPG. Drum mode: drives soft-clip drive |
| **S32** | Harmonics | Direct Plaits `harmonics` in Basic Pitch; ignored in Soft/Full Random (per-slot values used) |
| **S33** | Timbre | Direct Plaits `timbre` in Basic Pitch |
| **S34** | Morph | Direct Plaits `morph` in Basic Pitch |
| **S35** | Model select | Needs P0 or P2 held + pickup (see below) |
| **S36** | Output volume | Linear 0..1 output scale |
| **S37** | Decay | Plaits `decay` 0..1 |
| **SW1** | Scale | Center=Chromatic / Left=Major / Right=Minor (blinks 2/3/1) |
| **SW2** | Playmode | Down=Basic Pitch (1 blink) / Center=Soft Random (2) / Up=Full Random (3) |
| **P0** | Modifier A | No standalone sound. Hold + S35 → model 0–11; hold + P10/P11 → root note; hold + pad → recording mode |
| **P2** | Modifier B | No standalone sound. Hold + S35 → model 12–23 |
| **P0 + P2** | Re-randomize (modes 2 & 3) | Hold 1s = spread ±0.25 / 2s = spread ±0.45 (Soft); 1s/2s/3s stages (Full Random) |
| **P3–P9** | Note pads | Scale degree 0–6 in Basic/Soft; root pitch in Full Random; drum roles in drum mode |
| **P10** | Octave down | Cycles −1 oct (range −3 to +3). Hold P0 → root note −1 semitone |
| **P11** | Octave up | Cycles +1 oct. Hold P0 → root note +1 semitone |
| **P0 + P10** | Root note down | −1 semitone (C→B range); audition tone; LIMIT blink at C |
| **P0 + P11** | Root note up | +1 semitone; audition tone; LIMIT blink at B |
| **P0 + S35** | Model select bank 0 | Maps S35 0..1 → engines 0–11 (pickup/catch required) |
| **P2 + S35** | Model select bank 1 | Maps S35 0..1 → engines 12–23 (pickup/catch required) |
| **LED** | Blink feedback | SW1/SW2 change: N blinks. Root-note limit: 3 rapid blinks. Rec confirm: 3 rapid blinks |

### Recording mode (modes 2 & 3 only — P0 + pad)

| Action | Effect |
|--------|--------|
| Tap P0 + pad (P3–P9) | Enter recording mode for that slot; LED blinks 150ms; other pads silent |
| S32–S34, S37 in rec mode | Edit harmonics / timbre / morph / decay with pickup; audition updates live |
| P0/P2 + S35 in rec mode | Change that slot's engine only (separate pickup) |
| P0 + same pad held ≥500ms | Confirm / store; 3 rapid blinks; exit recording mode |
| P0 + different pad (in rec) | Enter copy mode; double-blink; hold P0 + original pad ≥500ms to store copy |
| SW2 position change | Cancel recording mode; reload previous slot state |

### Full Random drum mode (SW2 up + P0+P2 hold 3s)

Pad roles: P3=Kick P4=Snare P5=CHH P6=OHH P7=Clap P8=Tom P9=Perc

In drum mode: scale / root note / octave ignored. Each slot stores its own `note` (pitch of that drum sound).
P10/P11 while in recording mode → shift that slot's note ±1 semitone.

---

### Sequencer mode (P1 toggle)

P1 is the tucked pad (between the top and middle rows). All other controls stay active.

| Control | Sequencer function |
|---------|-------------------|
| **P1** | Toggle sequencer on / off |
| **SW1** | Genre: Center=Techno / Left=Electro / Right=Ambient |
| **S30** | Tempo (60–180 BPM) ← replaces FM amount |
| **S31** | Shuffle / swing (0–50% delay on off-beats) ← replaces LPG / drive |
| **S32** | Density (0–4; controls how many weight-steps fire) ← replaces harmonics |
| **S33** | (not repurposed — passes through to voices) |
| **S34** | (not repurposed — passes through to voices) |
| **S35** | (not repurposed — model select disabled in seq mode) |
| **S36** | Volume (unchanged) |
| **S37** | Decay (unchanged — controls drum tail length) |
| **P3–P9** | Manual trigger on top of running sequencer |
| **P0 + P2** | Re-randomize drum sounds while sequencer plays |
| **LED** | Short 20ms pulse on step 0 (downbeat marker) |
| **SW2** | Ignored while seq active (mode locked to Full Random / drum) |

**On enter:** `generate_drum_random()` loads fresh random drum sounds; sequencer starts immediately on step 0.  
**On exit:** sequencer stops; `AllNotesOff()`; mode restored from SW2 position.

Sequencer weight logic: each step fires when `weight + density ≥ 5`.
Density 1 = only weight-4 (strong) hits fire. Density 4 = weight-1 ghost notes also fire.

---

## Hardware reference

**SW1 (left switch) — Scale**
- PCB labels: S10 / S9
- Daisy pins: D9 (pos 1 = left flick) / D8 (pos 2 = right flick)
- Code accessor: `touch.switches().B()` → `_switch_9_10`
- Positions: center = Chromatic, left flick = Major, right flick = Minor  
  (verify polarity on hardware; swap entries in `kScales[]` if inverted)

**SW2 (right switch) — Playmode**
- PCB labels: S7 / S8
- Daisy pins: D7 / D6 — polarity inverted vs. label; code reads sw=2=Down, sw=1=Up
- Code accessor: `touch.switches().A()` → `_switch_7_8`
- Positions: center = Soft Random (2 blinks), down = Basic Pitch (1 blink), up = Full Random (3 blinks)

**Note:** Switch files (`touch/switches.h`, `touch/switches.cpp`) are kept exactly as the TouchBass template — no modifications, no added comments.

---

## Deliberate decisions

**Block size: 192, rendered as 8 × 24-sample chunks**  
`hw.SetAudioBlockSize(192)`. Each audio callback processes 192 samples by calling Plaits' Render() in 8 consecutive 24-sample passes. Eliminates ISR glitches on heavy engines (drum, modal, granular) while staying within Plaits' max block size constraint. ~4ms per callback at 48kHz.

**P0 and P2 have no standalone sound action**  
Touching P0 or P2 alone must never trigger a model audition or any sound change. Fixed by anchoring `bank_caught = false` and `bank_thresh = current S35 value` at the moment of TOUCH (not release). The catch gate then requires the pot to actually move before model select activates.

**Engine LED blink removed**  
Tested — counting blinks to identify an engine number is too slow and confusing in practice. Model changes are confirmed by the audition tone only. OLED screen is the right answer for showing engine numbers; deferred to a future hardware revision.

**MPR121 thresholds: defaults only**  
Attempted raising touch threshold to 20 and adding hardware debounce (register 0x5B). This broke pad registration entirely. Reverted to library defaults (12/6). Ghost-touch suppression handled in software via `any_musical_pad_held()` guard.

**SW1/SW2 are fully independent**  
Previous implementation had wrong pin assignments (D10 used, which isn't a switch pin). Fixed by restoring the TouchBass template files.

**Phase 8F (audio callback refactor) — attempted, reverted**  
Moved control processing to `main()` loop to free ISR budget. Caused random device crashes: `generate_*()` runs in main loop while `SetOnTouch` callback fires in ISR and reads `pad_slots[]` mid-write → data race. Reverted to controls-in-callback. Fix requires wrapping every `generate_*()` call in `__disable_irq()` / `__enable_irq()`. Defer unless crackle returns on heavy engines.

---

## Current state by feature

| Feature | Status |
|---|---|
| Basic Pitch (SW2 down) | Working — 4-voice pool, oldest-note steal |
| Soft Random (SW2 center) | Working — 7 pad slots, spread ±0.25 / ±0.45 |
| Full Random (SW2 up) | Working — 7 slots, per-stage randomization |
| Drum mode (Stage 3 of Full Random) | Working — 7 roles, engine pool per pad |
| Sequencer (P1) | Implemented — needs full hardware test |
| Recording mode (modes 2 & 3) | Implemented — confirm conflict known (see Open Decisions) |
| Voice pool (4 voices, SDRAM) | Working |
| SW1/SW2 blinks on change | Working |
| P0+P2 hold animation + audio preview | Working |
| Model select (P0+S35, P2+S35) | Working — 3% dead zone before catch activates |
| Root note (P0+P10/P11) | Working — C through B, audition tone, LIMIT blink |
| Octave shift (P10/P11) | Working — ±3 octaves |
| FM amount (S30) | Working |

---

## Models list + division

Registration order from `voice.cc` (engine2 first, then originals). This is the **software index** — differs from the original Plaits hardware panel order.

FR = Full Random (Mode 3). S1 = stage 1 hold / same group, S2 = stage 2 hold / all, S3 = stage 3 hold / drums.  
Chiptune (7) is excluded from all FR stages — autonomous arpeggiator plays without gate.  
LPG/decay knobs have **no effect** on engines 21–23 — they use their own internal envelope.

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

## Drum engine parameters

Engines 21–23 ignore `decay` and `lpg_colour`. Only `morph` controls the decay time.

**21 — Bass Drum**
- Morph: decay time (short = punchy kick, long = floor tom)
- Timbre: FM attack punch (low = clean, high = FM crunch)
- Harmonics: pitch drop speed / drum body sweep
- Note: base pitch of the kick

**22 — Snare Drum**
- Morph: character from analog to digital (also sets decay)
- Timbre: noise vs tone body balance
- Harmonics: snare noise density / frequency
- Note: pitch of the drum body

**23 — Hi-Hat**
- Morph: decay time (very short = closed hat, long = open/crash)
- Timbre: metallic vs noise balance
- Harmonics: spread of metallic frequency cluster
- Note: pitch center of the 6-oscillator cluster

Non-drum engines that can be tuned percussively:
- **17 Noise** — hi-hat / snare: short morph, high timbre
- **18 Particle** — snare / rim: sparse clicks, low harmonics
- **19 String** — rimshot / clave: very short decay, high pitch
- **20 Modal** — cowbell / conga / metallic perc: note sets pitch, morph shapes ring time

---

## Drum pad layout — DECIDED

Stage 3 Full Random fills each pad with a role-appropriate sound, like a DAW drum map.  
Scale, root_semitone, and octave_offset are **all ignored** in drum mode.  
Each slot stores its own `note` value, randomized within a role-appropriate range.

```
P3    P4    P5    P6    P7    P8    P9
Kick  Snr   CHH   OHH  Clap  Tom   Perc
```

GM reference notes: 36=Kick · 38=Snare · 42=CHH · 46=OHH · 39=Clap · 41=Low Tom

### Per-role engine pool + param ranges

All param ranges are for the randomizer. `note` replaces `root_note_f()` — scale/octave ignored.

**P3 — Kick**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 21 BassDrum (primary) | 0.05–0.30 | 0.20–0.65 | 0.20–0.55 | 36–48 |
| 10 Two-Op FM (alt) | 0.10–0.30 | 0.00–0.30 | 0.10–0.40 | 36–48 |

**P4 — Snare**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 22 SnareDrum (primary) | 0.10–0.60 | 0.30–0.80 | 0.30–0.70 | 48–60 |
| 17 Noise (alt) | 0.05–0.20 | 0.55–0.90 | 0.30–0.70 | 48–60 |
| 18 Particle (alt) | 0.05–0.20 | 0.40–0.80 | 0.10–0.50 | 48–60 |

**P5 — Closed Hi-Hat**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 23 HiHat (primary) | 0.00–0.15 | 0.30–0.80 | 0.30–0.80 | 60–84 |
| 17 Noise (alt) | 0.00–0.12 | 0.65–0.95 | 0.25–0.65 | 60–84 |

**P6 — Open Hi-Hat**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 23 HiHat (primary) | 0.40–0.85 | 0.30–0.70 | 0.30–0.80 | 60–84 |

CHH and OHH both use engine 23 — distinguished purely by morph range.

**P7 — Clap**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 22 SnareDrum (as clap) | 0.55–0.90 | 0.65–0.95 | 0.50–0.90 | 48–62 |
| 17 Noise | 0.05–0.20 | 0.70–1.00 | 0.40–0.80 | 55–70 |
| 18 Particle | 0.05–0.20 | 0.50–0.90 | 0.10–0.50 | 55–70 |

**P8 — Tom**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 21 BassDrum (high) | 0.30–0.65 | 0.10–0.40 | 0.30–0.60 | 48–72 |
| 20 Modal | 0.30–0.70 | 0.20–0.60 | 0.10–0.50 | 48–72 |

**P9 — Perc (misc)**

| Engine | Morph | Timbre | Harmonics | Note |
|--------|-------|--------|-----------|------|
| 19 String (rimshot/clave) | 0.05–0.25 | 0.30–0.70 | 0.20–0.60 | 55–79 |
| 20 Modal (cowbell/conga) | 0.40–0.90 | 0.30–0.70 | 0.20–0.60 | 60–84 |
| 23 HiHat (cymbal perc) | 0.15–0.40 | 0.50–0.90 | 0.50–1.00 | 72–96 |
| 18 Particle (shaker) | 0.20–0.50 | 0.30–0.70 | 0.30–0.70 | 60–80 |

### Implementation notes

- `generate_full_random(3)` becomes `generate_drum_random()` that iterates all 7 slots and picks engine+params from the role's pool.
- `PadSlot` gains a `float note` field (default 60.0f); `NoteOnWithParams` uses `slot.note` instead of `root_note_f()` in drum mode.
- Stage 3 re-randomize (P0+P2 hold 3s) also re-randomizes each slot's note within role range.
- Recording mode: in drum mode, P10/P11 while in rec_mode shift `pad_slots[rec_slot].note` by ±2 semitones (bypasses global octave shift).

---

## Open Decisions

These need a design choice before implementation.

### 1. Recording mode: confirm gesture conflicts with model-change gesture

**Problem:** in recording mode, P0+S35 changes the slot's engine (P0 held + scroll S35). The confirm gesture is P0+same pad ≥500ms. If you've just changed the engine and then try to confirm, the P0 hold state overlaps and the confirm doesn't register (or triggers unexpectedly).

**Options:**
- (a) In recording mode, holding the rec pad alone for ≥500ms = confirm (no P0 required). Simpler, removes the conflict entirely. Risk: accidental confirm from a long pad hold.
- (b) Require P0 to be released, then re-held + pad for confirm. Forces a deliberate two-step. More explicit but adds a step.
- (c) Disable model select inside recording mode — you can't change engines while editing other params. Simplest state machine fix; costs a feature.
- (d) Double-tap the rec pad (with P0 released) = confirm. Different gesture class entirely.

Leaning toward **(a)** — pad-alone confirm is intuitive and matches how you'd expect "hold to confirm" to work.

### 2. Per-pad stored parameters — scope

**Current:** recording mode stores per slot: engine, harmonics, timbre, morph, decay, note (drum).  
**Proposed addition:** also store FM amount (S30) and LPG colour/drive (S31) per slot.

**Considerations:**
- S31 already does two things: LPG in pitched modes, soft-clip drive in drum mode. Per-slot S31 in drum mode means per-slot drive level, which is actually useful for balancing kick vs hat volume.
- Engines 21–23 don't respond to S30 or S37 (they use internal envelopes only). Storing S30 per-slot for drums is pointless unless we remap S30 to something else for drum slots.
- Decision: store S30 and S31 per-slot for pitched modes (Soft Random, Full Random non-drum). For drum mode, handle S31 as per-slot drive but skip S30 since drum engines ignore FM amount.

### 3. Per-pad volume

**Proposal:** add a `volume` float to `PadSlot`; S36 fader controls per-pad volume when in recording mode (with pickup), global output volume otherwise.

This requires S36 to switch meaning on recording mode entry/exit. S36 needs pickup when entering recording mode so the global volume doesn't snap to the slot's stored volume.

**Decision needed:** yes/no, and whether S36 pickup resets to the slot's stored volume or to the global volume on exit.

### 4. Drum mode editing: last-touched pad vs explicit recording mode

**Current:** must do P0+pad tap to enter recording mode, then P0+same pad ≥500ms to confirm. Messy in practice when designing multiple drum sounds back to back.

**Proposed:** any pad press in drum mode makes it the "active" editing slot — knobs (S32–S34, S37) immediately affect that pad's sound. No recording mode entry needed. Confirm/store would need a new gesture (SW2 center tap? P0 double-tap?).

**Impact:** large UX change. The current recording mode concept (other pads silenced, audition retriggers) is useful for focused editing. The last-touched approach is better for live tweaking. These could coexist: last-touched for quick knob tweaks, recording mode for full editing + confirm.

### 5. Chiptune engine (7): keep, ditch, or fix

Chiptune is a self-running arpeggiator — it ignores gate and only responds to S34 (morph). Hard to use in a live pad context.

**Options:**
- Remove from the model selection list entirely (it's already excluded from all FR stages).
- Keep as selectable in Basic Pitch mode only; document it as an "arpeggiator mode."
- Map S30/S32/S33 to its internal parameters (rate, octave range, waveform) to make it more playable.

---

## Known Issues

### Distortion (S31 drive) not active in Basic Pitch mode

S31 drives soft-clip distortion in drum mode but passes through as LPG colour in Basic Pitch. The distortion processing should also be available in Basic Pitch mode. Low-effort fix — the processing path already exists, just needs to be routed.

### Six-Op A/B/C models (2, 3, 4) barely audible on audition

These models produce sound only in specific parameter regions. At init/random values they're often silent. Known behavior: sound appears only on every second gate trigger at some settings; S32 (harmonics) sometimes does nothing depending on the patch state.

**What helps:**
- A brief LED blink on model load would at least confirm the engine switched.
- The audition tone on model select should use known-good parameter values for Six-Op specifically (mid harmonics 0.5, timbre 0.3–0.6, morph 0.4–0.7) rather than current knob positions.
- Worth creating a `kModelAuditionParams[]` table with sane defaults per engine for the audition tone.

### Decay in sequencer mode — drum engines use morph not decay

In sequencer mode S37 drives `patch.decay`, but engines 21–23 use `morph` for their decay time (S37 does nothing for them). The drum tail length control is effectively broken for the three primary drum engines.

**Fix:** in seq mode when the slot uses engine 21–23, map S37 to `morph` instead of `decay`. This can be a special case in the seq tick where it sets `patch.morph = decay_knob_value` for those engine indices.

Alternatively: a global "tightness" multiplier that scales down the morph value of all active drum slots proportionally (preserving relative differences between sounds while making everything shorter/longer together).

### Six-Op model parameter check needed

After the drum mode implementation, Six-Op A/B/C behavior in Soft Random and Full Random modes needs a hardware verification pass. The concern is that random parameter values frequently land in silent regions.

---

## Feature Ideas

These are not planned yet — sketch and decide before implementing.

### Distortion in Basic Pitch mode
Add S31 soft-clip drive to Basic Pitch output path. Already exists in drum mode; route it for non-drum use too. Quick win.

### Sequencer on any playmode (melodic sequencer)
Currently P1 forces FULL_RANDOM/drum mode. Alternative: sequencer fires whatever playmode is active, following the current scale. In Basic Pitch mode this becomes a 7-track melodic pattern sequencer (each track = one scale degree). Would need the weight tables to map to scale degrees instead of drum roles.

### Density knob + chance parameter on one axis
Current: S32 = pure density (how many steps fire). Proposed split:
- 0.0–0.45: density reduction (below normal — fewer steps)
- 0.5: normal pattern as programmed
- 0.55–1.0: chance/mutation (steps start deviating from template — ghost notes, random skips)

This gives the "organic looseness" dial that density alone doesn't provide.

### More pattern variation — rhythm mutation
P0+P2 currently re-randomizes drum sounds but keeps the same rhythm. A second gesture (e.g. longer hold, or double P0+P2) could mutate pattern weights slightly — adding ghost notes or swapping accents — without replacing all the sounds.

### Electro pattern redesign
The current Electro template doesn't feel right. Sketch the intended pattern manually first:
- Syncopated kick (not just a straight offset — 16th-note pickup before beat 2, double hit on beat 4)
- Hard snare on 2+4, ghost notes on the 16ths around it
- Straight 16th-note hats with accent every 4th
- Tom fill on bar 4

### Live record into a pattern
Two routes under consideration — not ready to pick yet:
- **Audio buffer:** record synth output into a loop buffer, play back as a layer. Powerful but requires a separate audio buffer in SDRAM and complex transport controls.
- **Note/trigger record:** capture pad triggers + timing into a step pattern, quantized. Simpler, but then the recorded pattern replaces live playing rather than layering on top.

The second route is more feasible given the hardware constraints. Main problem: which controls handle record arm, start, stop, and loop length without clashing with everything else.

---

## Parking Lot (deferred, not forgotten)

- **OLED screen** — 128×32 or 128×64 I2C add-on would show engine name, params, mode, root note. The chosen path for engine number display once hardware is revised.
- **P0 as stepped pot selector** — while P0 held, pot movement quantized to fixed steps + audition. Revisit after recording mode redesign settles.
- **Audio input** — passthrough / exciter / modulator / ignore. Decision still open (VERIFY 10.A in archived plan).
- **Persistent state** — save last model + pad slots across power cycles (libDaisy `PersistentStorage`).
- **USB MIDI** — NoteOn/NoteOff from DAW or keyboard. `MidiUsbHandler` in libDaisy.
- **Chord mode** — single pad triggers a chord (multiple sequential Plaits triggers at scale intervals).
- **Additional scales** — P0+pad combos beyond the 3 SW1 positions.
- **Expand voice pool to 7** — once CPU headroom is confirmed on the engines actually in use.
- **TRS MIDI** — for users who hardware-mod their Simple Touch board.
- **Phase 8F retry** — move control processing out of ISR into main loop for CPU budget headroom. Requires `__disable_irq()` / `__enable_irq()` wrapping every `generate_*()` call. Only needed if crackle returns on heavy engines at kBlockSize=192.
