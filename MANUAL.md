# TouchPlaited — Controls Reference

> **Tip:** the [visualizer webapp](https://jonwaterschoot.github.io/TouchPlaited/visualizer/) shows all of this live on a drawing of the panel while you play — see [Visualizer webapp](#visualizer-webapp) below.

## Controls at a glance

### Basic Pitch (SW2 Down)

| Control | Function | MIDI CC |
|---------|----------|---------|
| S30 | Drive | 24 |
| S31 | Decay | 23 |
| S32 | Harmonics | 20 |
| S33 | Timbre | 21 |
| S34 | Morph (no effect on engines 19–23) | 22 |
| S35 | — *(only active with P0/P2 held)* | — |
| S36 | Output level | 26 |
| S37 | Model mix — OUT↔AUX blend | — |
| — | LPG colour (no knob) | 25 |
| SW1 | Scale: minor (left) / chromatic (center) / major (right) | — |
| P3–P9 | Play notes | notes in, ch 1 |
| P10 / P11 | Octave down / up | — |
| **Hold P0 (shift)** | | |
| P0 + S35 | Model select, bank 0 (engines 0–11) | — |
| P0 + S37 | Stereo width | — |
| P0 + P10 / P11 | Root semitone down / up | — |
| P0 + P2 hold 1 s / 2 s / 3 s | Randomize tight / randomize wide / back to clean | — |
| **Hold P2 (shift)** | | |
| P2 + S35 | Model select, bank 1 (engines 12–23) | — |
| P2 (hold) + P10 | Melodic transport — arp + Rec loop run / stop | — |
| P2 (hold) + P11 | Drum seq play / pause | Start/Continue/Stop |
| **Hold P1 (FX shift)** | | |
| P1 + S30 | Reverb (pitched voices) — center = off; left half = room, right half = hall; wet grows outward | — |
| P1 + S35 | Delay (pitched voices) — center = off; left half = slapback, right half = synced dotted 1/8 | — |

### Arp/Mel (SW2 Center)

SW1 picks the sub-state — **Hold** (left) · **Arp** (center) · **Rec** (right) — change-latched like scale/genre: it takes effect only when flicked *while in the mode*. The knobs shape the arp in every sub-state; the Rec loop replays notes as recorded.

| Control | Function | MIDI CC |
|---------|----------|---------|
| S30 | Drive — live, applied to every arp/loop trigger | 24 |
| S31 | Decay — live on the arp; stamped per note into a Rec take (same knob as every mode) | — |
| S32 | Division — 1/4 · 1/8 · 1/8T · 1/16 · 1/16T · 1/32 against the master tempo (center = 1/16) | — |
| S33 | Swing — delays odd arp steps, up to 50% of the division | — |
| S34 | Density — Euclidean fill: upper half steady 0–100%, lower half the same sweep with a 75% chance roll | — |
| S35 | Order — played / up / down / ping-pong / random (default) | — |
| S36 | Output level (pitched group) | 26 |
| S37 | Blend (hold P0: stereo width) | — |
| SW1 | Sub-state: Hold (left) / Arp (center) / Rec (right) | — |
| P3–P9 | Arp: feed the pool while held · Hold: latch, re-touch removes · Rec: play + record | notes in, ch 1 (play the arp sound) |
| P10 / P11 | Base octave − / + (transposes the running arp; Rec: your live notes) | — |
| **Hold P0 (shift)** | | |
| P0 + P10 / P11 | Root semitone − / + · **in Rec: P0+P10 tap = undo layer, hold 1.5 s = clear all** | — |
| P0 + pad 1–4 | **Rec only:** clear that layer (pad 1 = oldest) | — |
| P0 + S35 | Model select on the arp's sound, bank 0 | — |
| P0 + S37 | Stereo width (pitched group) | — |
| P0 + P1 hold 1 s | **Sound edit** toggle — knobs become S30 drive · S31 decay · S32 harmonics · S33 timbre · S34 morph on the arp's own sound; arp functions freeze until toggled back | — |
| P0 + P2 hold 1 s / 2 s | Vary the arp's sound — tight / wide | — |
| **Hold P1 / P2 (shift)** | | |
| P1 + P10 / P11 | Octave range − / + (0–3 extra octaves the arp climbs) | — |
| P1 + S30 / P1 + S35 | Reverb / delay (pitched group) | — |
| P2 + S35 | Model select on the arp's sound, bank 1 | — |
| P2 (hold) + P10 | Melodic transport — arp + Rec loop run / stop (any playmode) | — |
| P2 (hold) + P11 | Drum seq play / pause | Start/Continue/Stop |

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
| P1 + S30 | Reverb (drum group) — center = off; room ◄ · ► hall | — |
| P1 + S35 | Delay (drum group) — center = off; slapback ◄ · ► dotted 1/8 | — |
| P3–P9 | Play drums: kick / snare / cl. hat / op. hat / clap / tom / perc | notes in/out, ch 10 (GM) |
| Hold P3–P9 for 2 s | Enter Recording for that drum | — |
| P0 + P2 hold 1 s / 2 s | Vary current kit / generate new kit | — |
| P2 (hold) + P11 | Play / pause | Start/Continue/Stop |
| SW1 | Genre: IDM (left) / techno (center) / electro (right) | — |

### Recording (hold a musical pad 2 s in Seq)

| Control | Function |
|---------|----------|
| S30 | Per-slot drive |
| S31 | Per-slot decay |
| S32 / S33 / S34 | Per-slot harmonics / timbre / morph |
| S36 | Per-slot volume |
| S37 | Per-slot blend |
| P0 / P2 + S35 | Per-slot model select (bank 0 / bank 1) |
| P0 + S37 | Per-slot stereo width |
| P1 + S30 / P1 + S35 | Per-slot reverb / delay send trim |
| P10 / P11 | Drum pitch −1 / +1 semitone |
| Hold source pad 1.2 s | Confirm — save and exit |
| Tap any other pad | Cancel — restore and exit |
| Source pad + other pad 1.2 s | Copy slot to the other pad |

MIDI CCs keep addressing the *global* functions while recording — they never edit the slot being recorded. Recording is Seq-only: in Arp/Mel holding a pad is the playing gesture, so pitched per-slot editing retired with the old Random mode.

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
5. **Let it play itself.** Flick **SW2 Center** (Arp/Mel) and hold a few pads — the arpeggiator plays them. Turn **S31** for the rate, **S35** for the note order, **S33** to thin the pattern out. Flick **SW1 left** (Hold) and the notes latch — hands free. Flick **SW1 right** (Rec) and the pads play directly while recording into a 2-bar loop.
6. **Fine-tune one drum.** In Seq mode, hold any musical pad for 2 s to enter **Recording** — the LED counts down with an accelerating blink, then the knobs edit just that slot. Release, then hold the same pad 1.2 s to save.
7. **Pause / resume the drums** from any mode: hold **P2**, then tap **P11**. Same for the arp and its loop: **P2**, then **P10**.

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
| Center | **Arp/Mel** — arpeggiator + layered note recorder, playing its own sound |
| Up | **Seq** — 16-step generative drum sequencer |

Switching SW2 always takes effect immediately. Each mode remembers its last state — switching back returns to the same sounds that were last set, not a fresh randomize.

**The drum sequencer is independent of the switch position.** It auto-starts on the first Seq entry (including booting with SW2 Up) and keeps playing when you flick to Basic Pitch or Arp/Mel — drums and synth playing together. **P2 + P11** (P2 first) pauses/resumes it from any mode. While the seq plays behind a pitched mode, all its settings (tempo, shuffle, density, punch, tightness, drive, genre) stay locked at their last Seq-mode values, so every knob is free for the active mode. A fresh drum kit is generated only on first use or via P0+P2 stage 2 in Seq mode.

**The arp and its Rec loop are just as independent.** A latched (Hold) arp and a recorded loop keep playing when you flick SW2 to another mode, with their settings locked at the last Arp/Mel values. **P2 + P10** (P2 first) stops/starts them together from any mode — the melodic mirror of P2+P11. Both follow the master tempo: the seq tempo (S31 in Seq, CC27, or an external MIDI clock); when the drum seq is running the arp aligns to its grid.

---

## SW1 — Scale / Genre / Arp state (left toggle)

In **Basic Pitch**:

| Position | Scale |
|----------|-------|
| Center | Chromatic |
| Right flick | Major |
| Left flick | Minor |

In **Arp/Mel** SW1 selects the sub-state instead (the arp plays the scale last set in Basic Pitch):

| Position | Sub-state |
|----------|-----------|
| Center | **Arp** — the pool is your held pads |
| Left flick | **Hold** — latch: touch adds, re-touch removes |
| Right flick | **Rec** — pads play directly and record into the loop |

In **Seq** mode SW1 selects the drum pattern genre instead:

| Position | Genre |
|----------|-------|
| Center | Techno (four-on-floor, off-beat hat) |
| Right flick | Electro (breaks kick, snare + open hat on 2 & 4) |
| Left flick | IDM / Ambient (irregular, shifting kick) |

Each genre holds its own bank of patterns (the files in `synth/patterns/<genre>/`); turn **S35** in Seq mode to step through the patterns of the selected genre.

**Each role remembers its own setting.** A flick only takes effect in the mode you are in: moving SW1 in Basic Pitch changes the scale but leaves the genre and arp state untouched, and so on — so flicking back and forth between playmodes never changes a setting by itself (the switch equivalent of the knob pickup). The genre starts at Techno and changes only when SW1 is moved while in Seq; the arp sub-state starts at Arp and changes only when SW1 is moved while in Arp/Mel — which is what lets a latched arp keep playing in the background across mode flicks.

---

## Pads — layout

Modifiers / Buttons / Togglers
Notes in Basic Pitch · arp pool / loop notes in Arp/Mel
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

**Unified Decay.** One Decay control, always on **S31** — Basic Pitch, Arp/Mel, Arp/Mel sound edit and per-slot in Recording: for most engines it sets the LPG envelope decay; for the engines whose real decay lives on their MORPH parameter it drives that instead. Those are Six-Op A/B/C (2–4, MORPH is the DX7 envelope time — their LPG is bypassed entirely), String and Modal (19–20, damping) and the drum engines (21–23, tail length). Morph has no effect on those eight engines — the Decay knob owns it. LPG Colour was retired to make room (fixed at its neutral midpoint).

### Basic Pitch (SW2 Down)

All knobs apply globally and in real time to every voice.

After a P0+P2 randomize (see *Re-randomize gestures*), each pad plays its own frozen snapshot instead. To return to live knob control: hold P0+P2 for 3 s (stage 3 — clean), move any timbral knob (S31/S32/S33/S34), or pick a model with P0/P2+S35.

| Knob | Function | MIDI CC |
|------|----------|---------|
| S30 | Drive — soft-clip saturation | 24 |
| S31 | Decay — unified: LPG envelope, or the model's own decay for engines 2–4 and 19–23 | 23 |
| S32 | Harmonics | 20 |
| S33 | Timbre | 21 |
| S34 | Morph (no effect on engines 2–4 and 19–23 — their morph is the decay, owned by S31) | 22 |
| S35 | Model select — hold P0 while turning for bank 0 (engines 0–11); hold P2 for bank 1 (engines 12–23) | — |
| S36 | Output level (pitched voices only - the drum seq keeps its own volume) | 26 |
| S37 | Model mix — OUT↔AUX blend, mono to both outputs. Hold P0: stereo width (0 = mono, 1 = raw OUT/AUX split). No effect on Six-Op (2–4): their AUX output is identical to OUT | — |
| — | LPG colour — has no knob since the unified Decay took S31; neutral 0.5 unless a CC sets it | 25 |
| P1 + S30 / P1 + S35 | Reverb / delay for the pitched voices — see *FX* below | — |

### Arp/Mel (SW2 Center)

An arpeggiator plus a layered note recorder, sharing one sound and one transport. SW1 picks the sub-state (see *SW1* above): **Arp** (center), **Hold** (left), **Rec** (right). The knobs are the same in all three — they shape the arp; the Rec loop replays notes exactly as recorded.

| Knob | Function | MIDI CC |
|------|----------|---------|
| S30 | Drive — live, applied to every arp/loop trigger | 24 |
| S31 | Decay — live on arp notes; stamped per note into a Rec take as you record. The unified Decay knob, same position as Basic Pitch and sound edit | — |
| S32 | Division — the arp's rate against the master tempo: 1/4 · 1/8 · 1/8T · 1/16 · 1/16T · 1/32, center = 1/16 | — |
| S33 | Swing — delays odd arp steps, 0 to ~50% of the division | — |
| S34 | Density — Euclidean fill over a 16-step cycle. Upper half: steady fill from silence (left of full) to all steps (full right). Lower half: the same fill sweep with a 75% chance roll on each sounding step. The note order holds its place on masked steps | — |
| S35 | Order — how the arp walks the pool: played / up / down / ping-pong / random (default). Octave range (P1+P10/P11) expands the walk | — |
| S36 | Output level (pitched group, shared with Basic Pitch) | 26 |
| S37 | Blend — OUT↔AUX, written into every trigger. Hold P0: stereo width | — |
| P1 + S30 / P1 + S35 | Reverb / delay for the pitched group — see *FX* below | — |

All six arp knobs go through pickup on mode entry, so pots that served another mode don't jump the settings. A setting stored at a pot extreme (e.g. the boot defaults for Density and Order, both 100%) also engages on a small deliberate turn (~3%), so the knob never feels dead just because the rail is out of reach.

#### The sub-states

- **Arp** — hold pads and the arp plays them; release everything and it stops. Pads never sound directly (the arp does the sounding), and what you hold is the whole pool — leaving the mode clears it.
- **Hold** — the pool latches: touch a pad to add a note, touch it again to remove it. Flicking Hold away (to Arp or Rec) drops the latch, keeping only pads you're still physically holding. A latched arp keeps playing in the background of every playmode.
- **Rec** — a layered note recorder over a fixed **2-bar loop**. Pads play directly (with the arp's sound) and are stamped into the take; the **first note starts the clock**. Every 2-bar pass commits what you played as one **layer** and opens a fresh one — committed layers replay every pass, the open take is heard live and joins the loop on the wrap. Max **4 layers × 48 notes** (LIMIT blink when full). P10/P11 transpose your live playing; the recording replays as recorded. With the drum seq stopped, the LED pulses quarter notes as a metronome.

**Rec editing gestures** (SW1 in Rec):

| Gesture | Result |
|---------|--------|
| P0 + P10 tap | **Undo** — clears the open take first, then pops committed layers newest-first; when nothing is left, the clock resets and waits for a fresh first note |
| P0 + P10 hold ~1.5 s | **Clear all** — wipes every layer and resets the clock (3 rapid blinks) |
| P0 + pad 1–4 | **Clear one layer** — pad 1 = oldest; the LED blinks the layer number, LIMIT if there's no such layer. Pads neither sound nor record while P0 is held |

**Transport** — the plain arp is gate-driven: it runs whenever the pool has notes. The latched arp and the Rec loop are transport-driven: **P2 + P10** (P2 first) stops/starts them together, from any playmode, mirroring the drum seq's P2+P11. Both stay running in the background across SW2 flicks.

#### The arp's sound

Arp/Mel plays **its own sound model**, independent of Basic Pitch. It is seeded from the live Basic Pitch sound once — on the very first Arp/Mel entry — and never re-latched: tweak Basic Pitch all you want, the arp keeps its identity, and vice versa.

To shape it:

| Gesture | Result |
|---------|--------|
| **P0 + P1 hold ~1 s** | Toggle **sound edit**: the knobs become S30 drive · S31 decay · S32 harmonics · S33 timbre · S34 morph on the arp's sound (the Basic Pitch layout), and the arp functions freeze. Entry = 3 rapid blinks + an audition; with the arp running every trigger is live feedback. Toggle again (2 blinks), flick SW1, or leave and re-enter the mode to get the arp knobs back — every hand-off is pickup-protected |
| P0 / P2 + S35 | Model select on the arp's sound (bank 0 / bank 1) — works without leaving play |
| P0 + P2 hold 1 s / 2 s | Vary the sound around where it is — tight (±0.10) / wide (±0.25); the engine stays |

### Seq (SW2 Up)

Pads P3–P9 play the drum kit directly (also while the seq is paused). Model select is disabled globally in Seq mode; per-slot model, drive and FX send trims can be set during Recording.

These knob assignments apply only while SW2 is Up. If the seq keeps playing in another mode, all of these settings stay locked at their last values.

**Knob pickup:** on re-entering Seq (and after leaving Recording), each knob only takes effect once it crosses its stored setting — so a pot that was used by another mode doesn't jump the tempo (or anything else) the moment you flick back. On the very first Seq entry the knobs are live immediately. **SW1 gets the same protection:** the genre only changes when you flick SW1 *while in Seq* — the position it was left in as the scale selector is ignored, so re-entering Seq never switches the pattern by itself.

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
| P1 + S30 / P1 + S35 | Reverb / delay for the drum group — see *FX* below | — |

---

## FX — reverb & delay (P1 + S30 / P1 + S35)

One reverb and one delay, shared by everything, each on a single **mirror knob**: the center of the knob is **off**, each half is a different character, and the wet level grows as you turn away from the center. Hold **P1** and nudge the knob (~3% of travel, same movement-catch as the width controls) — from then on, while P1 stays held, the knob position is the setting. Release P1 and the knob returns to its normal role (drive / pattern select) behind the usual pickup, so nothing jumps.

| Knob (P1 held) | Full left ◄ | Center | ► Full right |
|----------------|-------------|--------|--------------|
| S30 — Reverb | **Room**, max wet | off | **Hall**, max wet |
| S35 — Delay | **Slapback** (~120 ms), max wet | off | **Synced dotted 1/8**, max wet |

- **Room**: short, damped — thickens drums without washing them out. **Hall**: long, bright tail; the decay opens further as wet rises.
- **Slapback**: fixed short echo, low feedback — rockabilly/dub thickener. **Dotted 1/8** follows the sequencer tempo (three 16th steps); repeats grow with wet, darker dub-style tail. Tempo changes bend the pitch of the tail tape-style. Under an external MIDI clock the synced time still follows the *knob* tempo (the external rate isn't measured).
- **Per-group wet, mode memory**: like volume and width, the drum group (Seq) and the pitched group each remember their own wet level — set a touch of room on the drums in Seq, flick to Basic Pitch and crank the hall, and each mix survives mode switches. The *character* (which side of the knob) is shared — the last edit from either mode sets it for both.
- **Per-slot send trims (Seq recording)**: while recording a drum, the same combo (P1 + S30 / P1 + S35) sets that slot's **send trim** — its share (0–100%) of the drum group's wet. The trim multiplies the group send, so the mirror knob stays the master level and character while the trims fine-balance individual drums under it (a wet clap over a dry kick). Trims default to 100%, reset with a new kit (P0+P2 stage 2), and are best set with the sequencer running — the paused-seq audition pulse doesn't ride the drum-group send. The global mirror knobs themselves are not editable while Recording, and no FX setting is reachable over MIDI.
- An idle FX costs nothing: each sleeps once its input and tail fall silent, like voices do.

---

## Recording mode

Recording lets you edit a single slot's parameters while hearing it in real time. Knobs have **pickup protection**: each pot takes effect only when it reaches the value it is editing — no jumps, from either direction.

### Entering recording

**Seq mode only:** hold a musical pad (P3–P9) for **2 s**. The LED shows where you are in the hold: from ~0.2 s in it blinks with steadily accelerating speed, ending very fast as the 2 s mark lands. Release at any point before that to abort. When the threshold is reached the LED plays a short rapid burst — you're in. (In Arp/Mel holding a pad is the playing gesture, so per-slot recording retired from the pitched modes with the old Random mode — the arp's sound is edited with P0+P1 sound edit instead.)

While in Recording mode the LED shows a **fast double blink on every audible hit** of the slot being edited (each forced seq step while playing, each 0.5 s audition while paused) — locked to the audio and clearly distinct from the sequencer's single beat flash. Keeping the pad held past the 2 s entry does nothing further; confirm requires releasing first.

### Knobs while recording

All six timbral knobs are per-slot and require pickup before changing the slot.

| Knob | Function |
|------|----------|
| S30 | Per-slot drive — a ratio (0–100%) of the overall S30 drive, which stays frozen at its entry value while recording |
| S31 | Per-slot decay — for engines 19–23 this controls the model's own tail length |
| S32 | Per-slot harmonics |
| S33 | Per-slot timbre |
| S34 | Per-slot morph (no effect on engines 19–23) |
| S35 | Per-slot model select — hold P0 (bank 0) or P2 (bank 1), turn S35 |
| S36 | Per-slot volume — this slot's level in the mix; audible live in the audition |
| S37 | Per-slot model mix — OUT↔AUX blend for this slot. Hold P0: per-slot stereo width — fader fully down = mono; a mono'd slot stays dead center regardless of group width. Blend/width reset to defaults when the kit is regenerated (P0+P2 stage 2) |
| P1 + S30 / P1 + S35 | Per-slot reverb / delay send trim: this drum's share (0–100%) of the drum group's wet set on the global mirror knobs (level only; the character stays global). Same pickup hand-off as everywhere: each role catches its own stored value, so flipping between drive and reverb send on S30 never jumps either |

The tempo (S31's idle role) stays frozen at its entry value while recording and is pickup-protected afterwards.

While the sequencer runs it fires the slot being edited every other step (8th notes), so you hear changes in rhythmic context without it dominating the mix. While the sequencer is paused the slot re-auditions at a steady pulse every 0.5 s from the moment you enter, playing at the slot's stored volume. Paused-seq drum auditions go through the drum group's level, width and FX sends and carry the full seq-trigger shaping (punch, tightness, overall drive × slot ratio), so what you hear while tweaking is exactly what the pattern will play.

### Drum pitch

| Gesture | Effect |
|---------|--------|
| P10 alone | Pitch the drum down 1 semitone |
| P11 alone | Pitch the drum up 1 semitone |

### Confirming, cancelling, copying

| Gesture | Result |
|---------|--------|
| Hold the *source pad* alone for **1.2 s** | **Confirm** — saves edits, exits recording (3 rapid blinks). Keeping the pad held after the save is ignored — it can't re-enter recording or copy; release everything and start a fresh 2 s hold to edit again |
| Tap any *other* pad (0.05–1.2 s) then release | **Cancel** — restores original slot, exits recording |
| Hold *source pad* + hold *another pad* for **1.2 s** | **Copy** — the accelerating countdown animation restarts while both pads are down, then the clone lands with an affirmation on both channels: the copied sound plays on the target and the LED gives 3 rapid blinks; repeat to copy to more pads |

---

## Pitch controls (Basic Pitch and Arp/Mel)

| Gesture | Effect |
|---------|--------|
| P10 | Octave down (range −3 to +3; transposes a running arp live) |
| P11 | Octave up (disabled while P2 is held — P2+P11 is the seq play/pause combo) |
| P0 + P10 | Root semitone down (within one octave) — in Arp/Mel's Rec state this is **undo** instead |
| P0 + P11 | Root semitone up |
| P1 + P10 / P11 | *(Arp/Mel only)* octave range down / up — 0–3 extra octaves the arp climbs beyond the base octave |

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

### In Arp/Mel mode — P0 + P2 hold

Hold both pads together. Two stages fire in sequence; releasing before a stage cancels the hold. Both vary the arp's own sound around where it is — the engine stays (change it with P0/P2 + S35, or fine-edit with P0+P1 sound edit).

| Hold time | Stage | Result |
|-----------|-------|--------|
| 1 s | 1 — Tight | Harmonics / timbre / morph nudged ±0.10 around the current sound |
| 2 s | 2 — Wide | Harmonics / timbre / morph thrown ±0.25 |

The LED lights while held and shows a brief flash at each stage, with an audition of the new sound.

### In Seq mode — drum re-randomize

Hold both pads together. Two stages fire in sequence.

| Hold time | Stage | Result |
|-----------|-------|--------|
| 1 s | 1 — Soft variance | Randomizes parameters of current loaded drum models with slight variance; engines stay the same |
| 2 s | 2 — Full new kit | Fully randomizes all drum models and parameters; new engines picked from drum pools. Sequencer restarts from bar 0. |

**Mode memory:** switching between Basic Pitch, Arp/Mel, and Seq always restores the last state for that mode — no re-randomize on mode switch. Only P0+P2 forces a change. On first power-on Seq generates a fresh drum kit automatically.

---

## Transport — P2 + P11 (drums) · P2 + P10 (melody), any mode

| Gesture | Result |
|---------|--------|
| Hold P2, then tap P11 | Toggle drum seq play / pause (2 blinks = paused; 3 blinks = playing) |
| Hold P2, then tap P10 | Toggle the melodic transport — the arp and the Rec loop together (same blink code) |

The order matters: **P2 first**. While P2 is held, P10/P11's octave functions are disabled; after both are released they work normally again. Starting the seq from a pitched mode before ever entering Seq generates a drum kit automatically. Stopping the melodic transport closes all open arp/loop gates; starting it again resumes in phase with the master tempo.

---

## Visualizer webapp

[The visualizer](https://jonwaterschoot.github.io/TouchPlaited/visualizer/) mirrors the panel live in the browser while you learn or play: connect the device over USB MIDI (Chrome/Edge, grant the SysEx permission) and the pads light up as you touch them, the knobs follow the pots, and contextual callouts name whatever you're using — quantized settings show their actual names (Six-Op patch x/32, chord types, arp Order Played/Up/Down/Ping-pong/Random). A **Demo** button runs a scripted loop when no hardware is connected.

![The visualizer with dynamic labels following the panel](img/TouchPlaited_dynamicvisualizer.png)

The draggable info panel shows model / mode / step, a knob map of what every pot does *right now* (engine-aware — it also tells you which knobs have no effect on the current model), the drum kit in Seq, and a log of your last gestures. Hover it for the font-size buttons and the label-overlay toggle, which cycles **dyn** (live callouts only, the default) / **S#** (permanent S30…S37 / P0…P11 designators) / **Aa** (full faceplate-style labels for the current mode and model).

![Hover the info screen for the size buttons and label toggle](img/TouchPlaited_dynamicvisualizer_hovernotconnected.png)

It also sends MIDI *to* the device: click the drum pads on the drawing, or open the MIDI drawer for a fader per supported CC and a piano on ch 1. URL flags for OBS overlays and the full feature list: [visualizer/README.md](visualizer/README.md).

---

## MIDI

TouchPlaited speaks MIDI on two ports at once:

- **USB** — plug into a computer; the device shows up as a USB MIDI device. (In the default build the USB port is MIDI-only; serial logging builds disable it.)
- **TRS** — for hardware-modded boards: USART1 on Daisy pins **D13 (TX)** and **D14 (RX)**, standard 31250 baud. Unmodded boards can ignore this; the port sits idle.

Both ports behave identically, in and out.

### MIDI in — notes

**Channel 1 — pitched.** The note number **is** the pitch (0–127, 60 = C4) — scale, root and octave settings don't apply, so a keyboard is always chromatic. Velocity scales loudness. Notes play the current playmode's sound:

- *Basic Pitch (live):* the global knob sound; knob moves affect notes you're holding.
- *Basic Pitch after a randomize:* each key picks one of the 7 snapshot sounds (key number mod 7), so every key has a stable timbre.
- *Arp/Mel:* the arp's own sound — external notes play alongside the arp (they don't join the pool yet).
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
| 20 | Harmonics | S32 (Basic Pitch) |
| 21 | Timbre | S33 (Basic Pitch) |
| 22 | Morph | S34 (Basic Pitch) |
| 23 | Decay | S31 (Basic Pitch) |
| 24 | Drive — sets pitched drive *and* drum drive together | S30 (all modes) |
| 25 | LPG colour | none — CC only (knob retired) |
| 26 | Output level, pitched voices | S36 (pitched modes) |
| 27 | Seq tempo (ignored while an external MIDI clock is running) | S31 (Seq) |
| 28 | Seq shuffle | S32 (Seq) |
| 29 | Seq density | S33 (Seq) |
| 30 | Seq kick punch | S34 (Seq) |
| 31 | Seq tightness | S37 (Seq) |
| 85 | Reverb — pitched voices | P1+S30 (pitched modes) |
| 86 | Reverb — drums | P1+S30 (Seq) |
| 87 | Delay — pitched voices | P1+S35 (pitched modes) |
| 88 | Delay — drums | P1+S35 (Seq) |
| 120 / 123 | All Sound Off / All Notes Off — releases MIDI-held notes | — |

CCs 85–88 use the same center-off encoding as the FX mirror knobs: value 64 ≈ off, below 64 the wet grows with character A (room / slapback), above 64 with character B (hall / synced dotted-1/8). The panel knob edits whichever group the current playmode plays; over MIDI each group has its own CC, so you can automate the drum reverb while playing a pitched mode. The character (which side, how deep) is shared by both groups — the last edit wins, from knob or CC alike.

CCs 20–23 address the Basic Pitch sound; the arp's own sound (Arp/Mel) is edited only on the device (P0+P1 sound edit, P0/P2+S35, P0+P2).

Not reachable over MIDI: model select (S35), pattern/variant select, the arp controls (division, swing, density, order, octave range) and the arp's sound, seq volume (S36 in Seq), blend (S37), stereo widths, the per-slot FX send trims (P1+S30/S35 in drum recording), and everything else in Recording mode — CCs keep addressing the global functions while you record.

### MIDI out

- Pad presses in Basic Pitch (and Rec live notes in Arp/Mel) send NoteOn/NoteOff on **channel 1** — the actual pitch you hear, with scale/octave/root applied. The NoteOff always matches even if you shift octave or root while holding the pad.
- In Arp/Mel, every arp step and Rec-loop note goes out on **channel 1** as it sounds — record the arp into a DAW, or drive external gear from it.
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
| 1 blink | Mode / scale / arp-state position 1 (SW2 Down or SW1 right flick); also: Rec undo landed |
| 2 blinks | Mode / scale / arp-state position 2 (SW2 Center or SW1 center); also: transport paused (P2+P10/P11), sound edit left |
| 3 blinks | Mode / scale / arp-state position 3 (SW2 Up or SW1 left flick); also: Seq resumed |
| N blinks | Numbered feedback — arp octave range (1–4 = range 0–3), Rec layer cleared (its number) |
| 3 rapid blinks | Confirm — recording saved, copy completed, Seq entered/re-randomized, transport started, sound edit entered, Rec cleared |
| 3 fast triple | At a limit — octave/root range, Rec layers/notes full, nothing to undo/clear |
| Accelerating blink | Hold in progress — recording entry (2 s) or copy (1.2 s); speeds up as the threshold nears |
| Short rapid burst | Recording entered (the 2 s hold landed) |
| Fast double blink | Recording mode active — one double blink per audible hit of the slot being edited, in sync with the audio (unlike the single beat flash) |
| Single flash on beat | Quarter-note pulse — from the sequencer, or from the Rec loop as a metronome while the seq is stopped. Lowest priority: shows only when the LED is otherwise idle, and stays off during Recording, for ~2 s after leaving it, and around any other blink |

---

## Plaits engine banks

The 24 Plaits models are spread over the two shift pads: hold **P0** and turn S35 for **bank 0** (engines 0–11), hold **P2** and turn S35 for **bank 1** (engines 12–23). The knob travel divides evenly across the bank — 11 zones on P0 (Chiptune is skipped), 12 on P2 — and a brief audition fires on each new engine. The engine only changes once the knob moves past a small dead zone, so grabbing a shift pad never jumps the model by itself.

### Bank 0 — P0 + S35 (engines 0–11)

| # | Model | Character |
|---|-------|-----------|
| 0 | Virtual analog VCF | Classic waveshapes through a resonant filter |
| 1 | Phase distortion | CZ-style phase distortion / phase modulation |
| 2 | Six-Op A * | 6-operator FM, patch bank A — harmonics steps through the patches, timbre is the modulator level |
| 3 | Six-Op B * | 6-operator FM, patch bank B |
| 4 | Six-Op C * | 6-operator FM, patch bank C |
| 5 | Wave terrain | Wave terrain synthesis — an orbit scanned over a 2-D surface |
| 6 | String machine | 70s string-ensemble chords |
| 7 | *Chiptune — skipped* | Its built-in arpeggiator free-runs without a gate, so it never lands on the knob |
| 8 | Virtual analog | Pair of detuned classic waveshapes |
| 9 | Waveshaping | Triangle through a wavefolder |
| 10 | FM 2-op | Two-operator phase modulation |
| 11 | Grain | Granular formant oscillator |

### Bank 1 — P2 + S35 (engines 12–23)

| # | Model | Character |
|---|-------|-----------|
| 12 | Additive | Harmonic oscillator — mixture of sine harmonics |
| 13 | Wavetable | Wavetable scanning |
| 14 | Chord | Four-note chord generator |
| 15 | Speech | Vowel and speech synthesis |
| 16 | Swarm | Swarm of enveloped sawtooth grains |
| 17 | Noise | Filtered / clocked noise |
| 18 | Particle | Dust noise through resonators |
| 19 | String * | Inharmonic plucked-string model |
| 20 | Modal * | Modal (struck bar / membrane) resonator |
| 21 | Bass drum * | Analog kick model |
| 22 | Snare drum * | Analog snare model |
| 23 | Hi-hat * | Analog hi-hat model |

\* Starred engines (Six-Op 2–4, and 19–23) are the **morph-decay engines**: their real decay lives on the model's MORPH parameter — the DX7 envelope time for Six-Op, damping/tail for 19–23 — so S31 Decay drives it and S34 Morph has no effect (see *Unified Decay*). In Seq mode, S37 Tightness compresses the tails of 19–23. Six-Op additionally renders identical OUT and AUX signals, so the S37 blend fader and P0+S37 stereo width do nothing on 2–4.

The random drum kits draw from a curated subset of these: kicks from 21 and 10 (an FM kick), snares/claps from 22 and 17, hats from 23 and 17, toms from 21 and 20, perc from 20/22/23. Particle (18) is deliberately excluded — its sporadic crackle reads as a hardware fault in a kit.
