# TouchPlaited manual

The [Play page](https://jonwaterschoot.github.io/TouchPlaited/play/) shows these controls on a drawing of the panel, one mode at a time, and plays the firmware in the browser.

## The panel

```
            [ P10 ] [ P11 ]             down / up
        [ P0 ]  [ P1 ]  [ P2 ]          held pads
[ P3 ]  [ P4 ]  [ P5 ]  [ P6 ]  [ P7 ]  notes or drums
            [ P8 ]  [ P9 ]
```

- **SW2** (right switch): playmode. Up = Seq, centre = Arp/Mel, down = Basic Pitch.
- **SW1** (left switch): genre in Seq, arp state in Arp/Mel, scale in Basic Pitch.
- **P0** held: sound and pitch. **P1** held: FX. **P2** held: transport.
- **S30–S35** are the knobs, **S36** the left fader, **S37** the right fader.

Holding P0, P1 or P2 alone lists on the OLED what it unlocks in the current mode.

The drum sequencer and the arp keep playing when you switch modes, with their settings held. Each mode comes back as you left it.

## Seq (SW2 up)

| Control | Function | MIDI |
|---------|----------|------|
| S30 | Drive | CC 24 |
| S31 | Tempo, 60–180 BPM | CC 27 |
| S32 | Shuffle | CC 28 |
| S33 | Pattern density: accents only → every hit | CC 29 |
| S34 | Step chance. Centre = as written, left = fuller, right = sparser | CC 30 |
| S35 | Pattern, within the genre | — |
| S36 | Drum volume | — |
| S37 | Tightness: shortens the drum tails | CC 31 |
| SW1 | Genre: IDM (left), Techno (centre), Electro (right) | — |
| P3–P9 | Kick, snare, closed hat, open hat, clap, tom, perc | notes, ch 10 |
| P0 + P10 / P11 | Previous / next kick | — |
| P0 + S37 | Drum stereo width | — |
| P1 + S30 / S35 | Drum reverb / delay | CC 86 / 88 |
| P2, then P11 | Play / pause | Start, Stop |
| P0 + P2, 2 s / 4 s | Vary the kit / new kit | — |
| Hold P3–P9 for 2 s | Edit that drum | — |

<details>
<summary>Editing one drum</summary>

Hold a drum pad for 2 s. The LED blinks faster until you are in. The drum then repeats so you hear the changes.

| Control | Function |
|---------|----------|
| S30 | Drive, as a share of the overall drive |
| S31 | Decay |
| S32 / S33 / S34 | Harmonics / timbre / morph |
| S36 | Volume |
| S37 | OUT ↔ AUX blend. With P0: stereo width |
| P0 or P2 + S35 | Model, bank 0 or bank 1 |
| P1 + S30 / S35 | This drum's share of the reverb / delay |
| P10 / P11 | Pitch down / up a semitone |
| P0 + P2, 2 s / 4 s | Vary this drum / new sound for this drum |
| Hold the same pad 1.2 s | Save and leave |
| Tap another pad | Cancel and leave |
| Hold the same pad + another pad 1.2 s | Copy the sound to the other pad |

Each knob takes over once it reaches the stored value (pickup). The transport combos (P2 + P10 / P11) don't work here. Save or cancel first. MIDI CCs keep addressing the overall functions.

</details>

<details>
<summary>The kick bank</summary>

The kick pad uses eleven fixed kicks. A new kit picks one of them.

| K | Name | K | Name |
|---|------|---|------|
| 1 | 808 DEEP | 7 | HYBRID |
| 2 | 808 SUB | 8 | 909+808 |
| 3 | 808 DRIVE | 9 | FOLD SUB |
| 4 | 909 PUNCH | 10 | VCF BOOM |
| 5 | 909 SWEEP | 11 | MODAL KNOCK |
| 6 | 909 CLICK | | |

- **P0 + P11 / P10**: next / previous kick, in Seq or while editing the kick. The OLED names it (`K05 909 SWEEP`). The LED blinks the limit code at either end.
- **While editing the kick pad** (hold P3 for 2 s): S32 selects the kick, S33 is tone, S34 punch, S37 body. Each stays inside a range set for that kick. P0 + S35 fully right also selects the bank.
- **Tightness** (S37 in Seq) leaves a kick from the bank alone. Shorten one with S31 while editing it.
- **K8** uses two voices. **K11** is the most CPU-heavy.
- **To leave the bank**, pick another model with P0 or P2 + S35 while editing the kick, or copy another drum onto it.

</details>

## Arp/Mel (SW2 centre)

SW1 picks the state: **Hold** (left), **Arp** (centre), **Rec** (right). The arp (Arp and Hold) and Rec each have their own model, volume, drive, blend, FX and octave.

| Control | Arp / Hold | Rec |
|---------|------------|-----|
| S30 | Drive | Drive |
| S31 | Decay | Decay |
| S32 | Division: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | Speed of the loop, 1× to 8× |
| S33 | Swing | Shift the loop in time |
| S34 | Density. Lower half adds a 75% chance | Chance per note |
| S35 | Order: played, up, down, ping-pong, random | Left: as recorded. Right: pitches shuffled |
| S36 | Volume | Volume |
| S37 | Blend | Blend |
| P3–P9 | Arp: held notes. Hold: tap to add or remove | Play and record |
| P10 / P11 | Octave down / up | Octave down / up |
| P0 + P10 / P11 | Arp octave range, 0–3 extra octaves | same |
| P0 or P2 + S35 | Model, bank 0 or 1 | same |
| P0 + S37 | Stereo width | same |
| P0 + P1, 1 s | Sound edit on / off | same |
| P0 + P2, 2 s / 4 s | Vary the sound a little / a lot | same |
| P1 + S30 / S35 | Reverb / delay | same |
| P2, then P10 | Arp run / stop | Record → loop → stop |

<details>
<summary>Recording a loop (Rec)</summary>

The loop is 2 bars long and holds up to 5 layers of 48 notes.

1. Flick SW1 right. The pads play Rec's own sound. Nothing is recorded yet.
2. Hold P2 and tap P10 to arm. The first note you play starts the loop.
3. Each pass of the loop keeps what you played as a new layer.

**P2, then P10** steps through three states:

| State | OLED | LED |
|-------|------|-----|
| Recording, loop running | `Rec + play` | 3 blinks |
| Loop running, not recording | `Play no rec` | 2 blinks |
| Stopped, layers kept | `Rec stopped` | 1 blink |

Layers, with P3–P7 standing for layers 1–5:

| Gesture | Result |
|---------|--------|
| P1 + P10 | Undo: the open take first, then the newest layer |
| P2 + pad, tap | Mute / unmute that layer |
| P2 + pad, hold 1.2 s | Clear that layer |
| P2 + two pads, hold 1.2 s | Clear all layers |

The top right of the OLED shows a blinking circle while recording and five dots for the layers (filled = recorded, hollow = muted). The loop keeps playing in other modes until you stop it. With the drums stopped, the LED blinks quarter notes.

</details>

<details>
<summary>Sound edit, and what the OLED shows</summary>

- **Sound edit** (P0 + P1 for 1 s): the knobs become drive (S30), decay (S31), harmonics (S32), timbre (S33) and morph (S34) for the arp's sound, or Rec's sound when SW1 is on Rec. The same combo, or a flick of SW1, gives the arp knobs back.
- **The pool**: in Arp and Hold the OLED shows the seven notes with a filled marker under each one the arp is playing.
- **Hold** latches notes. They keep playing in every mode until you stop the arp with P2, then P10.
- The arp follows the drum tempo and lines up with the pattern.

</details>

## Basic Pitch (SW2 down)

| Control | Function | MIDI |
|---------|----------|------|
| S30 | Drive | CC 24 |
| S31 | Decay | CC 23 |
| S32 | Harmonics | CC 20 |
| S33 | Timbre | CC 21 |
| S34 | Morph | CC 22 |
| S35 | Model, with P0 (bank 0) or P2 (bank 1) held | — |
| S36 | Volume | CC 26 |
| S37 | OUT ↔ AUX blend. With P0: stereo width | — |
| SW1 | Scale: minor (left), chromatic (centre), major (right) | — |
| P3–P9 | Notes | notes, ch 1 |
| P10 / P11 | Octave down / up (−3 to +3) | — |
| P0 + P10 / P11 | Root note down / up a semitone | — |
| P1 + S30 / S35 | Reverb / delay | CC 85 / 87 |
| P0 + P2, 2 s / 4 s / 6 s | Randomise a little / a lot / back to the knobs | — |

<details>
<summary>Details</summary>

- **Held notes**: four at once. Three on Six-Op, Speech, Particle, String and Modal. One more releases the oldest.
- **Decay** (S31) sets the envelope. On Six-Op, String, Modal and the three drum models it sets the model's own decay, and S34 does nothing.
- **After a randomise**, each pad keeps its own sound. Turning S31–S34, picking a model, or the 6 s stage gives the knobs back.
- **Root** stops at C and B. The scale moves with it. The OLED names the root and the seven notes.
- **Blend** (S37) does nothing on Six-Op: its two outputs are the same.

</details>

## In every mode

**Reverb and delay.** Hold P1 and turn S30 (reverb) or S35 (delay). The centre is off. Reverb: left = room, right = hall. Delay: left = slapback, right = dotted 1/8 in time with the tempo. Basic Pitch, the arp, Rec and the drums each have their own.

**Transport.** Hold P2 first, then tap P11 for the drums or P10 for the arp and loop. The LED blinks 3 times for running, 2 for paused.

**Pickup.** A knob does different things per mode. After a switch it takes over once it reaches the stored value. The OLED shows the stored value, where the knob is and where it has to go.

**SW1 remembers per mode.** A flick only counts in the mode you are in. When the lever no longer matches what is loaded, the OLED adds `*` (`SEQ TECHNO*`).

**Hold gestures.** Every timed hold shows a progress bar on the OLED and an accelerating LED blink. Let go early to cancel.

**Saved settings.** Sounds, kit, levels, FX and octaves are saved a few seconds after the last change. Not saved: the recorded loop and whether things were playing. The first boot after some updates starts fresh.

## MIDI

USB and TRS (pins D13 / D14, on modified boards) behave the same.

- **In, channel 1**: plays the current pitched sound. The note number is the pitch, without scale or octave.
- **In, channel 10**: plays the drums.
- **Out**: pads, arp and loop notes on channel 1, drum hits on channel 10. Sequencer steps carry their accent as velocity.
- **Clock**: sends MIDI clock and follows one, with start and stop.

<details>
<summary>Drum notes</summary>

| Pad | Drum | Notes in (bold = sent) |
|-----|------|------------------------|
| P3 | Kick | 35, **36** |
| P4 | Snare | **38**, 40 |
| P5 | Closed hat | **42**, 44 |
| P6 | Open hat | **46** |
| P7 | Clap | **39** |
| P8 | Tom | **41**, 45, 47, 48, 50 |
| P9 | Perc | 37, **43**, 54, 56, 75, 76 |

Sequencer steps send velocity 45, 70, 95 or 120 by weight. Pad taps send 100.

</details>

<details>
<summary>CC list</summary>

A CC addresses a function, in any mode and on any channel. After a CC, the knob takes over again once it reaches the CC's value.

| CC | Function |
|----|----------|
| 20 / 21 / 22 | Harmonics / timbre / morph (Basic Pitch) |
| 23 | Decay (Basic Pitch) |
| 24 | Drive, pitched and drums |
| 25 | LPG colour (no knob) |
| 26 | Volume (Basic Pitch) |
| 27 | Tempo. Ignored while an external clock runs |
| 28 / 29 / 30 / 31 | Seq shuffle / density / chance / tightness |
| 85 / 86 | Reverb: Basic Pitch / drums |
| 87 / 88 | Delay: Basic Pitch / drums |
| 120, 123 | All notes off |

CC 85–88: 64 is off, lower values are room or slapback, higher values hall or dotted 1/8. The arp's and Rec's sounds, model select and pattern select have no CC.

</details>

<details>
<summary>Clock in and out</summary>

- **Out, always**: MIDI clock (24 ppqn) and a 16th-note pulse on S40. Starting or pausing the drums sends Start, Continue or Stop.
- **In**: MIDI clock, or a pulse per 16th on S43 (above 1 V). The sequencer follows it and passes it on to both outputs. The tempo knob and CC 27 are off while it runs.
- **Both at once**: MIDI wins. If it stops, the CV clock takes over.
- **MIDI Start** resets to step 1 and plays. **Continue** resumes, **Stop** pauses.
- **Clock lost** (MIDI: 0.5 s, CV: about 2.5 pulses): back to the knob tempo.
- The synced delay always follows the knob tempo.

</details>

## Reference

<details>
<summary>LED codes</summary>

| Pattern | Meaning |
|---------|---------|
| Slow blink | Booting. One quick flash when ready |
| 1 / 2 / 3 blinks | Switch position. 2 = paused, 3 = playing. Rec: 3 recording, 2 looping, 1 stopped |
| N blinks | A number: arp octave range, the layer you cleared |
| 3 rapid blinks | Confirmed: saved, copied, new kit, kick loaded |
| 3 fast triples | At a limit: root, octave, layers full, nothing to undo, end of the kick bank |
| Accelerating blink | A hold in progress |
| Fast double blink | Editing a drum, on each hit |
| Flash on the beat | Quarter notes from the sequencer or the loop |

</details>

<details>
<summary>The 24 models</summary>

Hold P0 and turn S35 for bank 0, P2 for bank 1. Each new model plays a short note.

| Bank 0 (P0) | | Bank 1 (P2) | |
|---|---|---|---|
| 0 | Virtual analog VCF | 12 | Additive |
| 1 | Phase distortion | 13 | Wavetable |
| 2 | Six-Op A * | 14 | Chord |
| 3 | Six-Op B * | 15 | Speech |
| 4 | Six-Op C * | 16 | Swarm |
| 5 | Wave terrain | 17 | Noise |
| 6 | String machine | 18 | Particle |
| 7 | Chiptune (skipped) | 19 | String * |
| 8 | Virtual analog | 20 | Modal * |
| 9 | Waveshaping | 21 | Bass drum * |
| 10 | FM 2-op | 22 | Snare drum * |
| 11 | Grain | 23 | Hi-hat * |

\* On these, S31 Decay sets the model's own decay and S34 Morph does nothing. Chiptune is left out because its built-in arpeggiator runs without a gate. Bass drum has an 808-style kick on OUT and a 909-style one on AUX, so S37 fades between the two.

</details>

<details>
<summary>Hardware mods (optional)</summary>

- **OLED**: an I2C SSD1306, 128 × 32, on D11 / D12 (shared with the touch sensor). It names the last control touched and its value, then goes back to a status row per mode. Without one, the LED codes cover the same events.
- **CV clock**: S43 (in) is on pin A11, S40 (out) on D25.
- **TRS MIDI**: USART1 on D13 (TX) and D14 (RX).
- **P1 reach**: P1 is hard to reach with both switches fitted. Push a short piece of solid wire into the hole in the pad, or stick copper tape on the front.

</details>

The longer v1.9 reference, with the reasoning behind each control, is in [notesarchive/manual_v1.9_full.md](notesarchive/manual_v1.9_full.md).
