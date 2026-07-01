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

**Block size: 96, rendered as 8 × 12-sample chunks**  
`hw.SetAudioBlockSize(96)`. Each audio callback processes 96 samples by calling Plaits' Render() in 8 consecutive 12-sample passes. This eliminates ISR glitches while staying within Plaits' max block size constraint. 2ms per callback at 48kHz.

**P0 and P2 have no standalone sound action**  
Touching P0 or P2 alone must never trigger a model audition or any sound change. Fixed by anchoring `bank_caught = false` and `bank_thresh = current S35 value` at the moment of TOUCH (not release). The catch gate then requires the pot to actually move before model select activates.

**Engine LED blink removed**  
The original plan called for a grouped-blink scheme to show the engine number (VERIFY 7.A option b). Tested — too complex to read in practice. Removed entirely. Model changes are confirmed by the audition tone only. OLED screen is the right answer for showing engine numbers; deferred to a future hardware revision.

**MPR121 thresholds: defaults only**  
Attempted raising touch threshold to 20 and adding hardware debounce (register 0x5B). This broke pad registration entirely (threshold too high, and MPR121 may not accept register writes in run mode). Reverted to library defaults (12/6). Ghost-touch suppression is handled in software via `any_musical_pad_held()` guard.

**SW1/SW2 are fully independent**  
Previous implementation had wrong pin assignments (D10 used, which isn't a switch pin). Fixed by restoring the TouchBass template files. SW1 only affects scale/LED. SW2 only affects playmode.

---

## Current state by feature

| Feature | Status |
|---|---|
| Basic Pitch (SW2 down) | Working — 4-voice pool, oldest-note steal |
| Soft Random (SW2 center) | Working — 7 pad slots, anchored spread ±0.25 normal / ±0.45 extreme |
| Full Random (SW2 up) | Working — 7 slots, per-stage randomization |
| SW2 blink on mode change | Working — 1/2/3 blinks for down/center/up |
| SW1 blink on scale change | Working — confirms scale switch |
| P0+P2 hold animation | Working — charging blink 150ms→40ms; stages at 1000ms/2000ms/3000ms |
| P0+P2 hold audio preview | Working — auditions pad_slots[0] with actual params at each stage |
| Model select (P0+S35, P2+S35) | Working — dead zone (3%) required before catch activates |
| Root note (P0+P10/P11) | Working — C through B, audition tone, LIMIT blink at edges |
| Octave shift (P10/P11) | Working — ±3 octaves |
| FM amount (S30) | Working — skips audition voice (audition always FM=0) |

---

## Still to verify on hardware

[x] - SW1 scale polarity: confirm which physical flick = Major vs Minor
[x] - SW2 position polarity: confirm down = Basic Pitch (1 blink) and up = Full Random (3 blinks)
[x] - P0+P2 hold: confirm 1000ms/2000ms/3000ms timing feels right; currently 500 blocks = 1000ms at 2ms/block

→ result / decision: 
[x]   - SW1 = good
[x]   - SW2 = reverse order, atm down is Full random → should be down = basic pitch mode
[x]   - P0+P2 hold: good
---




## TODO (deferred)

- **P0 as stepped pot selector**: while P0 held, pot movement quantized to fixed steps; each step auditions the result. Useful for precise per-slot parameter control in random modes. Revisit after recording mode is implemented.

- **OLED screen**: would display engine name, params, current mode, root note. Planned for a future hardware revision. Would replace the LED blink scheme for engine number display.
- **Audio input** (VERIFY 10.A): passthrough / exciter / modulator / ignore — decision pending


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
Each slot stores its own `note` value, randomized within a role-appropriate range — so drums can be differently tuned for character. Recording mode will allow per-slot note editing (mechanism TBD, P10/P11 or a knob remap).

**`PadSlot` needs a `note` field** (currently absent — required for drum mode to work correctly).

```
P3    P4    P5    P6    P7    P8    P9
Kick  Snr   CHH   OHH  Clap  Tom   Perc
```

GM reference notes (for future MIDI alignment): 36=Kick · 38=Snare · 42=CHH · 46=OHH · 39=Clap · 41=Low Tom

---

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

---

### Implementation notes

- `generate_full_random(3)` becomes `generate_drum_random()` that iterates all 7 slots and picks engine+params from the role's pool.
- `PadSlot` gains a `float note` field (default 60.0f); `NoteOnWithParams` uses `slot.note` instead of `root_note_f()` in drum mode.
- Stage 3 re-randomize (P0+P2 hold 3s) also re-randomizes each slot's note within role range.
- Recording mode: in drum mode, P10/P11 while in rec_mode shift `pad_slots[rec_slot].note` by ±2 semitones (bypasses global octave shift). Exact control TBD.

## Notes

[ ] the distortion for drums should also be included in the normal play mode of playmode basic pitch 1


[?]done, need to check the 6 op models

**Recording mode**: per-pad parameter recording for Soft/Full random modes (Phase 8 in plan):
Recording presets: 

- in soft and full random allow storing pots S30 and S31 and fader S37 as well
- in full random mode drum mode this might be different when the model doesn't use those pots, chart which they are, so when can apoint other fx or values

- add a recordable volume per pad, we can use fader S36 for this that fader will there for also need a pickup functionality

When changing the model in recording mode, trying to confirm fails due to the need to use the P0 as a confirm key, we need to resolve this: options? 
  - ?

**General model comments**: 
- the contiuous mode 7, it's hard to keep this in the game, maybe, can we manually add a envelope to it, looks like it is only reacting to S34, maybe add functions to the "free" pots, or should we just ditch it. 

- some models are not or barely audible when auditioning and they are hard to set with the normal controls: the Six-Op A, B and C models secifically. 
  - one improvent that would help to make clear a new pmodel is in fact loaded is a LED blink.
  - a more user friendly experience here would be to have our preview to be audible, and to have the controls mapped better as those are also hard to find the right positions where it does start to produce sound
  - e.g. with these model SIX OP there is a sound on every second tap of a key at some settings S32 sometimes does nothing at all? 


  ---

  After latest rother drum update:

- editing sound designing drum sounds is messy and to hard. 
  - move to system where you can edit any pad / make it active by last touched, this could make it handy to also live tweak separate sounds
- we need to review all live controls in drum mode
  - if we want live parameters + simple per sound editing by last touched pad, then we need a new way to enter leave recording

 → streamline controls so they match as much as possible with normal instrument mode (e.g. drive, and FM amount)

in seq mode live parameter:

- Decay seems to be hard maybe because it differs per model, 
  - Maybe we can force decay on each model to be on S37, which models have decay linked to one of the stadard knobs in plaits?
  - in live mode we could apply a percentage of decay globally, making all sounds tighter, but follow their original per instrument decay
  

- would be cooler if the "drum" sequencer just started on any playmode. And that we then make sure it follows the last set scale (that wy it could also be a melodic seq)

- Electro pattern doesn't feel like the type of electro i had in mind, sketch it out manually first then copy to the model here

- more variance in the drum patterns generate other patterns not just new sounds
- the density knob could be used in conjunction with a random/chance parameter
  - on one knob we could devide left O as least density, original pattern, 0.5 center = normal, 1 = as most deviation and less chance

## Live record playing into a pattern
- Two possible routes, actual record audio into a buffer that could be played back so it would be possible to play on top in the other modes, controls for this might be the hardest part.
- Or record live playback, but this will limit the abillity to play on top. (might be easier as it might require less overlapping hardware controls)

