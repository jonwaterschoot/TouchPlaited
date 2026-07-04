# TouchPlaited — Controls Reference

## Hardware overview

| Component | Count | Identifiers |
|-----------|-------|-------------|
| Pots | 8 | S30 S31 S32 S33 S34 S35 S36 S37 |
| Touch pads | 12 | P0–P11 |
| Toggle switches | 2 | SW1 (left) · SW2 (right) |
| LED | 1 | — |

---

## SW2 — Playmode (right toggle)

| Position | Mode |
|----------|------|
| Down | **Basic Pitch** — single shared engine, all params live on knobs |
| Center | **Random** — 7 independent slots, each with a frozen param snapshot |
| Up | **Seq** — 16-step generative drum sequencer |

Switching SW2 always takes effect immediately. Each mode remembers its last state — switching back returns to the same sounds that were last set, not a fresh randomize.

**The drum sequencer is independent of the switch position.** It auto-starts on the first Seq entry (including booting with SW2 Up) and keeps playing when you flick to Basic Pitch or Random — drums and synth playing together. **P2 + P11** (P2 first) pauses/resumes it from any mode. While the seq plays behind a pitched mode, all its settings (tempo, shuffle, density, punch, tightness, drive, genre) stay locked at their last Seq-mode values, so every knob is free for the active mode. A fresh drum kit is generated only on first use or via P0+P2 stage 2 in Seq mode.

---

## SW1 — Scale / Genre (left toggle)

In **Basic Pitch and Random** modes:

| Position | Scale |
|----------|-------|
| Center | Chromatic |
| Right flick | Major |
| Left flick | Minor |

In **Seq** mode SW1 selects the drum pattern genre instead:

| Position | Genre |
|----------|-------|
| Center | Techno (four-on-floor, off-beat hat) |
| Right flick | Electro (breaks kick, snare + open hat on 2 & 4) |
| Left flick | IDM / Ambient (irregular, shifting kick) |

---

## Pads — layout

Modifiers / Buttons / Togglers
Notes in Playmode Basic Pitch + Random
Drums in Playmode Seq

```
            [ P10 ] [ P11 ]             ← down / up
        [ P0 ]  [ P1 ]  [ P2 ]          ← control pads
[ P3 ]  [ P4 ]  [ P5 ]  [ P6 ]  [ P7 ]  ← musical pads
            [ P8 ]  [ P9 ]              ← musical pads
```

P3–P9 map to slots 0–6. In Seq mode these are fixed drum roles:

| Pad | Slot | Drum role |
|-----|------|-----------|
| P3 | 0 | Kick |
| P4 | 1 | Snare |
| P5 | 2 | Closed hat |
| P6 | 3 | Open hat |
| P7 | 4 | Clap |
| P8 | 5 | Tom |
| P9 | 6 | Perc |

---

## Knob functions by mode

### Basic Pitch (SW2 Down)

All knobs apply globally and in real time to every voice.

After a P0+P2 randomize (see *Re-randomize gestures*), each pad plays its own frozen snapshot instead. To return to live knob control: hold P0+P2 for 3 s (stage 3 — clean), move any timbral knob (S32/S33/S34/S37), or pick a model with P0/P2+S35.

| Knob | Function |
|------|----------|
| S30 | Drive — soft-clip saturation |
| S31 | LPG Colour — filter/envelope character (0 = VCF-like, 1 = LPG-like) |
| S32 | Harmonics |
| S33 | Timbre |
| S34 | Morph |
| S35 | Model select — hold P0 while turning for bank 0 (engines 0–11); hold P2 for bank 1 (engines 12–23) |
| S36 | Output level (pitched voices only - the drum seq keeps its own volume) |
| S37 | Decay |

### Random (SW2 Center)

Each of the 7 pads plays its own stored snapshot (engine, harmonics, timbre, morph, decay). Knobs S32–S34 and S37 do not change currently playing notes.

| Knob | Function |
|------|----------|
| S30 | Drive — soft-clip saturation (global) |
| S31 | LPG Colour (global) |
| S32 | Harmonics center — used when S35 forces a new engine onto all slots |
| S33 | Timbre center — used when S35 forces a new engine onto all slots |
| S34 | Morph center — used when S35 forces a new engine onto all slots |
| S35 | Model select — forces the chosen engine onto all 7 slots with a soft spread around the S32–S34/S37 centers |
| S36 | Output level (pitched voices only - the drum seq keeps its own volume) |
| S37 | Decay anchor — P0+P2 stage 1 locks every slot's decay to this value; stage 2 spreads around it |

### Seq (SW2 Up)

Pads P3–P9 play the drum kit directly (also while the seq is paused). Model select is disabled globally in Seq mode; per-slot model and drive can be set during Recording.

These knob assignments apply only while SW2 is Up. If the seq keeps playing in another mode, all of these settings stay locked at their last values.

**Knob pickup:** on re-entering Seq (and after leaving Recording), each knob only takes effect once it crosses its stored setting — so a pot that was used by another mode doesn't jump the tempo (or anything else) the moment you flick back. On the very first Seq entry the knobs are live immediately.

| Knob | Function |
|------|----------|
| S30 | Drive — overall soft-clip saturation (per-slot drive settable in Recording as a percentage of overall) |
| S31 | Tempo — 60–180 BPM |
| S32 | Shuffle — swing delay on odd 16th steps (0 = straight, max = ~50%) |
| S33 | Density — how many pattern steps fire (1 = strong hits only … 4 = everything including ghosts) |
| S34 | Kick punch — boosts kick timbre on each trigger |
| S35 | *(not mapped)* |
| S36 | Seq volume - drum group level, independent of the pitched modes; picked up on re-entry |
| S37 | Tightness — compresses the tail of all drum engine (Plaits engines 21–23) voices; lower = shorter decay |

---

## Recording mode

Recording lets you edit a single slot's parameters while hearing it in real time. Knobs have **pickup protection**: each pot takes effect only when it reaches the value it is editing — no jumps, from either direction.

### Entering recording

One gesture everywhere: hold a musical pad (P3–P9) for **1.2 s** — in Seq mode *and* in Random mode. The threshold is deliberately long so holding a sustained note in Random doesn't enter recording by accident.

The LED blinks steadily while in Recording mode.

### Knobs while recording

All six timbral knobs are per-slot and require pickup before changing the slot.

| Knob | Function |
|------|----------|
| S30 | Per-slot drive — in Seq this is a ratio (0–100%) of the overall S30 drive, which stays frozen at its entry value while recording |
| S32 | Per-slot harmonics |
| S33 | Per-slot timbre |
| S34 | Per-slot morph |
| S35 | Per-slot model select — hold P0 (bank 0) or P2 (bank 1), turn S35 |
| S36 | Per-slot volume — this slot's level in the mix (Seq drums and Random pads alike); audible live in the audition |
| S37 | Per-slot decay — for drum engines this controls tail length |

S31 (LPG Colour) continues to apply globally.

In **Seq** mode the sequencer keeps running and fires the slot being edited every other step (8th notes), so you hear changes in rhythmic context without it dominating the mix. In **Random** mode — and in Seq while the sequencer is paused — the slot re-auditions at a steady pulse every 0.5 s from the moment you enter, playing at the slot's stored volume.

### Drum pitch (Seq recording only)

| Gesture | Effect |
|---------|--------|
| P10 alone | Pitch the drum down 1 semitone |
| P11 alone | Pitch the drum up 1 semitone |

### Confirming, cancelling, copying

| Gesture | Result |
|---------|--------|
| Hold the *source pad* alone for **1.2 s** | **Confirm** — saves edits, exits recording (3 rapid blinks) |
| Tap any *other* pad (0.05–1.2 s) then release | **Cancel** — restores original slot, exits recording |
| Hold *source pad* + hold *another pad* for **1.2 s** | **Copy** — clones the edited slot to the other pad; the copied sound plays on the target as audible confirmation (plus 3 blinks); repeat to copy to more pads |

---

## Pitch controls (Basic Pitch and Random)

| Gesture | Effect |
|---------|--------|
| P10 | Octave down (range −3 to +3) |
| P11 | Octave up (disabled while P2 is held — P2+P11 is the seq play/pause combo) |
| P0 + P10 | Root semitone down (within one octave) |
| P0 + P11 | Root semitone up |

---

## Re-randomize gestures

### In Basic Pitch mode — P0 + P2 hold

Hold both pads together. Two stages fire in sequence; releasing before a stage cancels the hold.

| Hold time | Stage | Result |
|-----------|-------|--------|
| 1 s | 1 — Soft tight | All pads get new random params — same engine, tight spread (±0.25) around current knob positions. Scale pitches preserved. |
| 2 s | 2 — Soft wide | All pads get new random params — same engine, wider spread (±0.45). Scale pitches preserved. |
| 3 s | 3 — Clean | Back to the original sound: snapshots are dropped and all pads follow the live knobs again. |

The LED lights while held and shows a brief flash at each stage.

After stage 1 or 2 the pads play frozen snapshots. To return to live knob control: hold on to stage 3, move any timbral knob (S32/S33/S34/S37), or pick a model with P0/P2+S35.

### In Random mode — P0 + P2 hold

Hold both pads together. Two stages fire in sequence; releasing before a stage cancels the hold.

| Hold time | Stage | Result |
|-----------|-------|--------|
| 1 s | 1 — Full random | Each slot gets a random engine + params from the full pool (all engines except Chiptune; drum engines play at scale pitches). Decay locked to current S37 value. |
| 2 s | 2 — Full random spread | Each slot gets a random engine + params. Decay spread around current S37 value. |

The LED lights while held and shows a brief flash at each stage.

### In Seq mode — drum re-randomize

Hold both pads together. Two stages fire in sequence.

| Hold time | Stage | Result |
|-----------|-------|--------|
| 1 s | 1 — Soft variance | Randomizes parameters of current loaded drum models with slight variance; engines stay the same |
| 2 s | 2 — Full new kit | Fully randomizes all drum models and parameters; new engines picked from drum pools. Sequencer restarts from bar 0. |

**Mode memory:** switching between Basic Pitch, Random, and Seq always restores the last state for that mode — no re-randomize on mode switch. Only P0+P2 forces a re-randomize. On first power-on Seq generates a fresh drum kit automatically.

---

## Sequencer play/pause — P2 + P11 (any mode)

| Gesture | Result |
|---------|--------|
| Hold P2, then tap P11 | Toggle drum seq play / pause (2 blinks = paused; 3 blinks = playing) |

The order matters: **P2 first, then P11**. While P2 is held, P11's octave-up function is disabled; after both are released, P11 works normally again. Starting the seq from a pitched mode before ever entering Seq generates a drum kit automatically.

P1 is currently unused (reserved for a future melodic-seq trigger).

---

## LED blink codes

| Pattern | Meaning |
|---------|---------|
| 1 blink | Mode / scale position 1 (SW2 Down or SW1 right flick) |
| 2 blinks | Mode / scale position 2 (SW2 Center or SW1 center) |
| 3 blinks | Mode / scale position 3 (SW2 Up or SW1 left flick); also: Seq resumed |
| 3 rapid blinks | Confirm — recording saved, copy completed, or Seq entered/re-randomized |
| 3 fast triple | At octave/root limit |
| Steady slow blink | Recording mode active |
| Single flash on beat | Quarter-note pulse from sequencer (visible between other events) |

---

## Plaits engine banks

Hold **P0** and turn S35 to scan bank 0. Hold **P2** and turn S35 to scan bank 1. A brief audition fires on each new engine.

**Bank 0 — P0 held (engines 0–11, Chiptune excluded):**
0 VA+VCF · 1 VA+VCA · 2 Six-Op A · 3 Six-Op B · 4 Six-Op C · 5 Waveshaping · 6 FM 2-op · 8 Grain · 9 Additive · 10 Wavetable · 11 Chord

**Bank 1 — P2 held (engines 12–23):**
12 Speech · 13 Swarm · 14 Noise · 15 Particle · 16 String · 17 Modal · 18 Bass · 19 Kick sim · 20 Snare sim · 21 Analog kick · 22 Analog snare · 23 Analog hat
