# TouchPlaited — Controls Reference

## Controls at a glance

### Basic Pitch (SW2 Down) · Random (SW2 Center)

The two pitched modes share the same layout — one table, one column per mode. *same* = identical to Basic Pitch; — = no function.

| Control | Basic Pitch | Random | MIDI CC |
|---------|-------------|--------|---------|
| S30 | Drive | Drive (global) | 24 |
| S31 | Decay | Decay anchor for randomize | 23 |
| S32 | Harmonics | Harmonics center (for P0/P2 + S35 engine force) | 20 |
| S33 | Timbre | Timbre center | 21 |
| S34 | Morph (no effect on engines 19–23) | Morph center | 22 |
| S35 | — *(only active with P0/P2 held)* | *same* | — |
| S36 | Output level | *same* | 26 |
| S37 | Model mix — OUT↔AUX blend | Blend center | — |
| — | LPG colour (no knob) | *same* | 25 |
| SW1 | Scale: minor (left) / chromatic (center) / major (right) | *same* | — |
| P3–P9 | Play notes | Play the 7 slot sounds | notes in, ch 1 |
| Hold P3–P9 1.2 s | — | Enter Recording for that slot | — |
| P10 / P11 | Octave down / up | *same* | — |
| **Hold P0 (shift)** | | | |
| P0 + S35 | Model select, bank 0 (engines 0–11) | Force bank-0 engine onto all 7 slots (spread around the S31–S34 centers) | — |
| P0 + S37 | Stereo width | Stereo width (all pitched voices) | — |
| P0 + P10 / P11 | Root semitone down / up | *same* | — |
| P0 + P2 hold 1 s / 2 s / 3 s | Randomize tight / randomize wide / back to clean | Full random / full random with decay spread / — | — |
| **Hold P2 (shift)** | | | |
| P2 + S35 | Model select, bank 1 (engines 12–23) | Force bank-1 engine onto all 7 slots | — |
| P2 (hold) + P11 | Drum seq play / pause | *same* | Start/Continue/Stop |

### Seq (SW2 Up)

| Control | Function | MIDI CC |
|---------|----------|---------|
| S30 | Drive | 24 |
| S31 | Tempo (60–180 BPM) | 27 (muted by ext. clock) |
| S32 | Shuffle | 28 |
| S33 | Density | 29 |
| S34 | Kick punch | 30 |
| S35 | Pattern select (within SW1 genre) | — |
| S36 | Seq volume | — |
| S37 | Tightness (decay of engines 19–23) | 31 |
| P0 + S37 | Drum-group stereo width | — |
| P3–P9 | Play drums: kick / snare / cl. hat / op. hat / clap / tom / perc | notes in/out, ch 10 (GM) |
| Hold P3–P9 for 1.2 s | Enter Recording for that drum | — |
| P0 + P2 hold 1 s / 2 s | Vary current kit / generate new kit | — |
| P2 (hold) + P11 | Play / pause | Start/Continue/Stop |
| SW1 | Genre: IDM (left) / techno (center) / electro (right) | — |

### Recording (hold a musical pad 1.2 s in Random or Seq)

| Control | Function |
|---------|----------|
| S30 | Per-slot drive |
| S31 | Per-slot decay |
| S32 / S33 / S34 | Per-slot harmonics / timbre / morph |
| S36 | Per-slot volume |
| S37 | Per-slot blend |
| P0 / P2 + S35 | Per-slot model select (bank 0 / bank 1) |
| P0 + S37 | Per-slot stereo width |
| P10 / P11 | Drum pitch −1 / +1 semitone (Seq only) |
| Hold source pad 1.2 s | Confirm — save and exit |
| Tap any other pad | Cancel — restore and exit |
| Source pad + other pad 1.2 s | Copy slot to the other pad |

MIDI CCs keep addressing the *global* functions while recording — they never edit the slot being recorded.

---

## Quick tutorial — your first five minutes

Two toggles, eight knobs, twelve touch pads. **SW2** (right toggle) picks the playmode; **SW1** (left toggle) picks the scale — or the drum genre when sequencing.

```
            [ P10 ] [ P11 ]             ← down / up
        [ P0 ]  [ P1 ]  [ P2 ]          ← control pads
[ P3 ]  [ P4 ]  [ P5 ]  [ P6 ]  [ P7 ]  ← musical pads
            [ P8 ]  [ P9 ]              ← musical pads
```

1. **Start the drums.** Flick **SW2 Up** (Seq mode) — a fresh drum kit is generated and the 16-step sequencer starts playing. Turn **S31** for tempo, **S32** for shuffle, **S33** for density. Flick **SW1** left or right to switch genre (Techno / Electro / IDM); turn **S35** to step through that genre's patterns.
2. **Play drums live.** Tap the musical pads **P3–P9** — kick, snare, closed hat, open hat, clap, tom, perc.
3. **Add a synth on top.** Flick **SW2 Down** (Basic Pitch) — the drums keep playing. P3–P9 now play notes; **P10 / P11** shift the octave down / up, and SW1 picks the scale (minor / chromatic / major).
4. **Shape the sound.** **S32** harmonics, **S33** timbre, **S34** morph, **S31** decay, **S30** drive, **S37** OUT↔AUX blend. Choose an engine by holding **P0** (bank 0) or **P2** (bank 1) while turning **S35**.
5. **Get surprised.** Flick **SW2 Center** (Random), then hold **P0 + P2**: after ~1 s every pad gets its own random sound; keep holding to ~2 s for a wilder spread. The same gesture in Seq mode re-randomizes the drum kit.
6. **Fine-tune one sound.** Hold any musical pad for 1.2 s to enter **Recording** — the knobs now edit just that slot. Hold the same pad 1.2 s again to save.
7. **Pause / resume the drums** from any mode: hold **P2**, then tap **P11**.

Everything below is the full reference.

---

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

Each genre holds its own bank of patterns (the files in `synth/patterns/<genre>/`); turn **S35** in Seq mode to step through the patterns of the selected genre.

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

**Model mix & stereo width — S37 / P0 + S37.** Every Plaits engine renders two *different* signals: OUT (the canonical sound) and AUX (a variant — e.g. a lo-fi rendition, an alternate noise source, the raw exciter). **S37 is the Blend fader**: 0 = OUT only, 0.5 = 50/50, 1 = AUX only — always summed to mono on both outputs. Holding **P0** turns S37 into the **stereo width** control instead: 0 = mono blend (the default), 1 = the raw OUT-left/AUX-right split (at full width the blend has no effect). Global width and per-slot width (set in Recording) multiply, so a slot set to mono stays dead center no matter what the global width does. The width control engages on *movement*: hold P0 and nudge the fader (~3% of travel) — from then on, while P0 stays held, the fader position is the width. Blend/tightness go through normal knob pickup on P0 release, so flipping P0 never jumps them.

**Unified Decay — S31.** One Decay control, always: for most engines it sets the LPG envelope decay; for engines 19–23 (String, Modal, Bass/Snare drum, Hi-hat) — whose real decay lives on their MORPH parameter — it drives that too. S34 Morph has no effect on those five engines. LPG Colour was retired to make room (fixed at its neutral midpoint).

### Basic Pitch (SW2 Down)

All knobs apply globally and in real time to every voice.

After a P0+P2 randomize (see *Re-randomize gestures*), each pad plays its own frozen snapshot instead. To return to live knob control: hold P0+P2 for 3 s (stage 3 — clean), move any timbral knob (S31/S32/S33/S34), or pick a model with P0/P2+S35.

| Knob | Function | MIDI CC |
|------|----------|---------|
| S30 | Drive — soft-clip saturation | 24 |
| S31 | Decay — unified: LPG envelope, plus the model's own decay for engines 19–23 | 23 |
| S32 | Harmonics | 20 |
| S33 | Timbre | 21 |
| S34 | Morph (no effect on engines 19–23 — their morph is the decay, owned by S31) | 22 |
| S35 | Model select — hold P0 while turning for bank 0 (engines 0–11); hold P2 for bank 1 (engines 12–23) | — |
| S36 | Output level (pitched voices only - the drum seq keeps its own volume) | 26 |
| S37 | Model mix — OUT↔AUX blend, mono to both outputs. Hold P0: stereo width (0 = mono, 1 = raw OUT/AUX split) | — |
| — | LPG colour — has no knob since the unified Decay took S31; neutral 0.5 unless a CC sets it | 25 |

### Random (SW2 Center)

Each of the 7 pads plays its own stored snapshot (engine, harmonics, timbre, morph, decay, blend, width). Knobs S31–S34 do not change currently playing notes.

| Knob | Function | MIDI CC |
|------|----------|---------|
| S30 | Drive — soft-clip saturation (global) | 24 |
| S31 | Decay anchor — P0+P2 stage 1 locks every slot's decay to this value; stage 2 spreads around it | 23 |
| S32 | Harmonics center — used when P0/P2 + S35 forces a new engine onto all slots | 20 |
| S33 | Timbre center — used when P0/P2 + S35 forces a new engine onto all slots | 21 |
| S34 | Morph center — used when P0/P2 + S35 forces a new engine onto all slots | 22 |
| S35 | Model select — hold P0 (bank 0, engines 0–11) or P2 (bank 1, engines 12–23) while turning; forces the chosen engine onto all 7 slots with a soft spread around the S31–S34 centers; blend taken from S37. Bare S35 does nothing | — |
| S36 | Output level (pitched voices only - the drum seq keeps its own volume) | 26 |
| S37 | Blend center — stamped onto all slots when P0/P2 + S35 forces a new engine; refine per slot in Recording. Hold P0: stereo width for all pitched voices (0 = mono, 1 = raw OUT/AUX split) | — |

### Seq (SW2 Up)

Pads P3–P9 play the drum kit directly (also while the seq is paused). Model select is disabled globally in Seq mode; per-slot model and drive can be set during Recording.

These knob assignments apply only while SW2 is Up. If the seq keeps playing in another mode, all of these settings stay locked at their last values.

**Knob pickup:** on re-entering Seq (and after leaving Recording), each knob only takes effect once it crosses its stored setting — so a pot that was used by another mode doesn't jump the tempo (or anything else) the moment you flick back. On the very first Seq entry the knobs are live immediately.

| Knob | Function | MIDI CC |
|------|----------|---------|
| S30 | Drive — overall soft-clip saturation (per-slot drive settable in Recording as a percentage of overall) | 24 |
| S31 | Tempo — 60–180 BPM | 27 (muted while an external MIDI clock is present) |
| S32 | Shuffle — swing delay on odd 16th steps (0 = straight, max = ~50%) | 28 |
| S33 | Density — how many pattern steps fire (1 = strong hits only … 4 = everything including ghosts) | 29 |
| S34 | Kick punch — boosts kick timbre on each trigger | 30 |
| S35 | Pattern select — steps through the patterns of the current SW1 genre (knob range splits evenly across that genre's pattern count; custom patterns can be drawn with `tools/pattern_editor.html` and added via a firmware rebuild — see the README) | — |
| S36 | Seq volume - drum group level, independent of the pitched modes; picked up on re-entry | — |
| S37 | Tightness — compresses the tail of all morph-decay engines (19–23); lower = shorter decay. Hold P0: drum-group stereo width (0 = mono) | 31 |

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
| S31 | Per-slot decay — for engines 19–23 this controls the model's own tail length |
| S32 | Per-slot harmonics |
| S33 | Per-slot timbre |
| S34 | Per-slot morph (no effect on engines 19–23) |
| S35 | Per-slot model select — hold P0 (bank 0) or P2 (bank 1), turn S35 |
| S36 | Per-slot volume — this slot's level in the mix (Seq drums and Random pads alike); audible live in the audition |
| S37 | Per-slot model mix — OUT↔AUX blend for this slot. Hold P0: per-slot stereo width — fader fully down = mono; a mono'd slot stays dead center regardless of group width. Blend/width reset to defaults when the kit is regenerated (P0+P2 stage 2) |

In Seq mode the tempo (S31's idle role) stays frozen at its entry value while recording and is pickup-protected afterwards.

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

After stage 1 or 2 the pads play frozen snapshots. To return to live knob control: hold on to stage 3, move any timbral knob (S31/S32/S33/S34), or pick a model with P0/P2+S35.

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

## MIDI

TouchPlaited speaks MIDI on two ports at once:

- **USB** — plug into a computer; the device shows up as a USB MIDI device. (In the default build the USB port is MIDI-only; serial logging builds disable it.)
- **TRS** — for hardware-modded boards: USART1 on Daisy pins **D13 (TX)** and **D14 (RX)**, standard 31250 baud. Unmodded boards can ignore this; the port sits idle.

Both ports behave identically, in and out.

### MIDI in — notes

**Channel 1 — pitched.** The note number **is** the pitch (0–127, 60 = C4) — scale, root and octave settings don't apply, so a keyboard is always chromatic. Velocity scales loudness. Notes play the current playmode's sound:

- *Basic Pitch (live):* the global knob sound; knob moves affect notes you're holding.
- *Random / Basic Pitch after a randomize:* each key picks one of the 7 slot sounds (key number mod 7), so every key has a stable timbre.
- *While in Seq mode:* the last pitched mode's sound — play synth lines over the drum machine.

Held notes are released by NoteOff, by CC 120/123 (All Sound Off / All Notes Off — what a DAW sends when you press stop), and MIDI-held voices participate in normal voice stealing (6 voices).

**Channel 10 — drums**, General MIDI mapping to the 7 kit slots (velocity scales the hit):

| Slot | Drum | GM notes accepted |
|------|------|-------------------|
| P3 | Kick | 35, **36** |
| P4 | Snare | **38**, 40 |
| P5 | Closed hat | **42**, 44 (pedal hat) |
| P6 | Open hat | **46** |
| P7 | Clap | **39** |
| P8 | Tom | 41, 43, **45**, 47, 48, 50 |
| P9 | Perc | **37** (rimshot), 54, 56, 75, 76 |

Bold = the note TouchPlaited itself sends for that slot on MIDI out. Notes outside the table are ignored. Each hit plays the slot at its stored pitch, exactly like tapping the pad.

### MIDI in — CC map (received on any channel)

CCs control *functions*, not knobs — so a CC always does the same thing no matter which playmode the panel is in. After a CC write, the physical pot is muted for that function until it crosses the CC's value — then the pot takes over again (same pickup rule as everywhere else).

| CC | Function | Panel knob it shadows |
|----|----------|-----------------------|
| 20 | Harmonics | S32 (pitched modes) |
| 21 | Timbre | S33 (pitched modes) |
| 22 | Morph | S34 (pitched modes) |
| 23 | Decay | S31 (pitched modes) |
| 24 | Drive — sets pitched drive *and* drum drive together | S30 (all modes) |
| 25 | LPG colour | none — CC only (knob retired) |
| 26 | Output level, pitched voices | S36 (pitched modes) |
| 27 | Seq tempo (ignored while an external MIDI clock is running) | S31 (Seq) |
| 28 | Seq shuffle | S32 (Seq) |
| 29 | Seq density | S33 (Seq) |
| 30 | Seq kick punch | S34 (Seq) |
| 31 | Seq tightness | S37 (Seq) |
| 120 / 123 | All Sound Off / All Notes Off — releases MIDI-held notes | — |

Not reachable over MIDI: model select (S35), pattern/variant select, seq volume (S36 in Seq), blend (S37), stereo widths, and everything in Recording mode — CCs keep addressing the global functions while you record.

### MIDI out

- Pad presses in pitched modes send NoteOn/NoteOff on **channel 1** — the actual pitch you hear, with scale/octave/root applied. The NoteOff always matches even if you shift octave or root while holding the pad.
- Drum hits — sequencer steps and Seq-mode pad taps — go out on **channel 10** as NoteOn+NoteOff pairs using the bold GM notes in the table above.
- The pads aren't pressure-sensitive, so all outgoing notes use velocity 100.
- Knob moves are not sent, and incoming notes/CCs are never echoed back out (clock and start/stop are — see below).

### MIDI clock and start/stop

TouchPlaited always puts a clock on its MIDI outputs, and follows one when you give it one:

- **No clock coming in (master):** a steady 24 ppqn clock at the sequencer's tempo goes out from power-on, locked to the drum steps so synced gear can't drift. Starting/pausing the sequencer (SW2 Up first entry, P2+P11) also sends Start/Continue/Stop, so external devices follow your transport.
- **Clock coming in on either port (follower):** the sequencer hard-syncs to the external clock — 16th steps every 6 ticks, shuffle included. The **tempo knob and CC27 are disabled** while the external clock is present (they still set the fallback tempo). The incoming clock and transport messages pass through to the MIDI output, so you can chain more gear behind TouchPlaited.
- **MIDI Start** resets the pattern to step 0 and starts it (generating a drum kit if you never entered Seq); **Continue** resumes from the current step; **Stop** pauses. This works from any playmode, like P2+P11.
- If the external clock disappears for half a second, TouchPlaited switches back to its internal clock at the knob tempo.

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
