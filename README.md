# TouchPlaited

A Synthux Simple Touch firmware based on Mutable Instruments Plaits (Émilie Gillet, MIT License): a 7-voice touch synth plus a 16-step generative drum sequencer, playable at the same time (though limited by voice stealing).

Full controls reference: [MANUAL.md](MANUAL.md).

**On the web:** [visualizer](https://jonwaterschoot.github.io/TouchPlaited/visualizer/) · [pattern editor](https://jonwaterschoot.github.io/TouchPlaited/editor/) · [manual](https://jonwaterschoot.github.io/TouchPlaited/manual.html) — hosted from this repo via GitHub Pages.

This version is using all 24 models (the 16 original + the 8 new ones). There are three main playmodes. Basic Pitch, Arp/Mel, and Seq.

- **Basic Pitch**: 1 model with the pads or a midi input.
- **Arp/Mel**: an arpeggiator (with hold/latch) plus a layered 2-bar note recorder, playing its own sound
- **Seq**: loads only drum and percussive sounds, can play preloaded patterns

Using a fader to mix between the models AUX or OUT outputs. Adding a way to spread these over stereo.

**MIDI in/out** on USB and on TRS (USART1, D13/D14 — for hardware-modded boards): notes on ch1 (pitched, chromatic) and ch10 (GM drums), CC20–31 for sound and sequencer functions, the pads/sequencer mirrored to MIDI out, and full clock sync — it follows an external MIDI clock (with start/stop) and sends its own when there isn't one. See *MIDI at a glance* below and the full mapping in [MANUAL.md](MANUAL.md#midi).

Still working on this, so things might move around.

## Cheat sheet

**SW2 — playmode:** Down = Basic Pitch · Center = Arp/Mel · Up = Seq
**SW1 — scale** minor / chromatic / major *(Basic Pitch)* · **arp state** hold / arp / rec *(Arp/Mel)* · **genre** IDM / techno / electro *(Seq)*

### Knobs

| Knob | Basic Pitch | Arp/Mel | Seq |
|------|-------------|---------|-----|
| S30 | Drive | Drive | Drive |
| S31 | Decay | Division | Tempo |
| S32 | Harmonics | Swing | Shuffle |
| S33 | Timbre | Density (Euclid) | Density |
| S34 | Morph | Decay | Kick punch |
| S35 | *(model select — needs P0 or P2)* | Order | Pattern |
| S36 | Volume | Volume | Seq volume |
| S37 | Model mix OUT ↔ AUX | Model mix | Tightness |

- Morph does nothing on engines 19–23; Seq *Tightness* is the decay of those same engines.
- The arp plays **its own sound** (seeded from Basic Pitch once, then independent) — edit it by holding **P0 + P1 ~1 s**: the knobs become drive / decay / harmonics / timbre / morph until you toggle back.

### Pads

| Pad | Basic Pitch | Arp/Mel | Seq |
|-----|-------------|---------|-----|
| P3–P9 | Play notes | Feed the arp *(Rec: play + record)* | Kick · snare · cl. hat · op. hat · clap · tom · perc |
| P10 / P11 | Octave − / + | Octave − / + | — |
| Hold P3–P9 2 s | — | — | Recording |

### Shift layers — hold, then turn / tap

| Hold | + | Does |
|------|---|------|
| P0 | S35 | Model select, bank 0 (engines 0–11) |
| P2 | S35 | Model select, bank 1 (engines 12–23) |
| P0 | S37 | Stereo width |
| P0 | P10 / P11 | Root semitone − / + *(Arp/Mel Rec: P0+P10 tap = undo layer, hold 1.5 s = clear all)* |
| P0 | pad 1–4 | *(Arp/Mel Rec)* clear that recorded layer |
| P0 + P1 | hold 1 s | *(Arp/Mel)* toggle sound edit on the arp's sound |
| P1 | S30 | **Reverb** — room ◄ off ► hall |
| P1 | S35 | **Delay** — slapback ◄ off ► dotted 1/8 |
| P1 | P10 / P11 | *(Arp/Mel)* arp octave range 0–3 |
| P2 | P10 | Arp + Rec loop run / stop *(any mode)* |
| P2 | P11 | Drum seq play / pause *(any mode)* |
| P0 + P2 | hold 1 s / 2 s | Randomize tight / wide *(Arp/Mel: vary arp sound · Seq: vary kit / new kit)* · 3 s *(Basic Pitch)*: back to clean |

- FX knobs are mirror knobs: center = off, wet grows outward. They affect the pitched voices in pitched modes, the drum group in Seq.

### Recording (Seq only — hold a drum pad 2 s)

Knobs now edit **that slot only**: drive, decay, harmonics/timbre/morph, volume, blend — plus P0/P2 + S35 model, P0 + S37 width, P1 + S30/S35 FX send trims, P10/P11 drum pitch ∓ 1 semitone.

**Save:** hold the same pad 1.2 s · **Cancel:** tap any other pad · **Copy:** source pad + other pad 1.2 s

### MIDI

- **Notes** — ch 1 in: pitched, chromatic · ch 10 in/out: GM drums · pads out on ch 1
- **CC 20–26** harmonics · timbre · morph · decay · drive · LPG colour · volume
- **CC 27–31** *(Seq)* tempo · shuffle · density · punch · tightness
- **CC 85–88** reverb / delay, pitched / drums (value 64 = off, below/above = character A/B)
- **Clock** — follows external clock + Start/Stop when present; sends its own otherwise

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
| P0 + S35 | Model select, bank 0 (engines 0–11) | — |
| P0 + S37 | Stereo width | — |
| P0 + P10 / P11 | Root semitone down / up | — |
| P0 + P2 hold 1 s / 2 s / 3 s | Randomize tight / randomize wide / back to clean | — |
| P2 + S35 | Model select, bank 1 (engines 12–23) | — |
| P2 (hold) + P10 | Arp + Rec loop run / stop | — |
| P2 (hold) + P11 | Drum seq play / pause | Start/Continue/Stop |

### Arp/Mel (SW2 Center)

SW1 picks the sub-state: **Hold** (left) · **Arp** (center) · **Rec** (right) — change-latched, so it only applies when flicked inside the mode. The arp plays its own sound, independent of Basic Pitch.

| Control | Function | MIDI CC |
|---------|----------|---------|
| S30 | Drive (live per trigger) | 24 |
| S31 | Division — 1/4 … 1/32 against the master tempo (center = 1/16) | — |
| S32 | Swing | — |
| S33 | Density — Euclidean fill; lower half adds a 75% chance roll | — |
| S34 | Decay (stamped per note into a Rec take) | — |
| S35 | Order — played / up / down / ping-pong / random | — |
| S36 | Output level (pitched group) | 26 |
| S37 | Blend (hold P0: stereo width) | — |
| P3–P9 | Arp: feed the pool · Hold: latch, re-touch removes · Rec: play + record into a 2-bar loop (4 layers) | notes in ch 1 play the arp sound |
| P10 / P11 | Base octave − / + (transposes the running arp) | — |
| P1 + P10 / P11 | Octave range 0–3 | — |
| P0 + P10 / P11 | Root semitone − / + · Rec: **P0+P10 tap = undo, hold 1.5 s = clear all** | — |
| P0 + pad 1–4 | Rec: clear that layer (pad 1 = oldest) | — |
| P0 + P1 hold 1 s | Sound edit toggle — knobs become drive / decay / harmonics / timbre / morph on the arp's sound | — |
| P0 / P2 + S35 | Model select on the arp's sound (bank 0 / 1) | — |
| P0 + P2 hold 1 s / 2 s | Vary the arp's sound — tight / wide | — |
| P2 (hold) + P10 | Arp + Rec loop run / stop (any mode) | — |
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
| P3–P9 | Play drums: kick / snare / cl. hat / op. hat / clap / tom / perc | notes in/out, ch 10 (GM) |
| Hold P3–P9 for 2 s | Enter Recording for that drum | — |
| P0 + P2 hold 1 s / 2 s | Vary current kit / generate new kit | — |
| P2 (hold) + P11 | Play / pause | Start/Continue/Stop |
| SW1 | Genre: IDM (left) / techno (center) / electro (right) | — |

### Recording (hold a drum pad 2 s in Seq)

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

(MIDI CCs keep addressing the global functions while recording — they never edit the slot being recorded.)

### MIDI at a glance

Works identically on **USB** and **TRS** (USART1: D13 TX / D14 RX, 31250 baud — hardware mod required; see MANUAL).

| MIDI | Function |
|------|----------|
| Notes in, ch 1 | Pitched, chromatic (note number = pitch; velocity = level) — plays the current mode's sound |
| Notes in, ch 10 | GM drums → the 7 kit slots (36 kick, 38 snare, 42/44 CHH, 46 OHH, 39 clap, 41–50 tom, 37… perc) |
| CC 20–26 | Harmonics, timbre, morph, decay, drive, LPG colour, volume (pot pickup re-armed on every CC write) |
| CC 27–31 | Seq tempo, shuffle, density, punch, tightness |
| CC 120/123 | All sound off / all notes off |
| Clock in (F8) + Start/Continue/Stop | Sequencer hard-syncs to external clock (tempo knob disabled); clock passes through to the output |
| Clock out | Always on: internal 24 ppqn locked to the drums when no external clock; Start/Continue/Stop sent on local transport changes |
| Notes out | Pads → ch 1 (heard pitch); seq steps + drum pads → ch 10 GM, velocity 100 |

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
5. **Let it play itself.** Flick **SW2 Center** (Arp/Mel) and hold a few pads — the arpeggiator plays them. **S31** sets the rate, **S35** the note order, **S33** thins the pattern out. Flick **SW1 left** (Hold) to latch the notes, **SW1 right** (Rec) to record pads into a 2-bar loop.
6. **Fine-tune one drum.** In Seq mode, hold any musical pad for 2 s to enter **Recording** — the knobs now edit just that slot. Hold the same pad 1.2 s again to save.
7. **Pause / resume the drums** from any mode: hold **P2**, then tap **P11**. Same for the arp and its loop: **P2**, then **P10**.

See [MANUAL.md](MANUAL.md) for the full per-mode knob maps, recording mode, gestures, and LED codes.

## Installing

The firmware runs from QSPI flash (`APP_TYPE = BOOT_QSPI` — it's too big for Daisy's internal SRAM), so the **Daisy bootloader must be on the Seed first**. That's a one-time step per device.

### Option A — easy install (release .bin + Web Programmer)

1. Download `TouchPlaited.bin` from the [releases page](../../releases).
2. **Once per device:** install the Daisy bootloader. Connect the Seed over USB, put it in DFU mode (hold **BOOT**, tap **RESET**, release BOOT), open the [Daisy Web Programmer](https://flash.daisy.audio/) and flash the bootloader.
3. **Enter the Daisy bootloader:** tap **RESET** (or power up), then press **BOOT** while the LED is doing its slow "breathing" pulse. The bootloader only waits about 2 seconds before launching the app — pressing BOOT during that window makes it wait indefinitely, so you can take your time.
4. Upload `TouchPlaited.bin` with the Web Programmer.

> **Two different button dances.** Hold-BOOT-tap-RESET enters the chip's built-in *DFU mode* — used only for installing the bootloader itself (step 2). Tap-RESET-then-BOOT enters the *Daisy bootloader* — used every time you flash the app (step 3).

### Option B — build from source with make

Requires the ARM GNU toolchain (`arm-none-eabi-gcc`), `make`, and Python 3 (the build runs `tools/gen_patterns.py` to register the drum patterns).

```bash
git clone https://github.com/jonwaterschoot/TouchPlaited.git
cd TouchPlaited

# Pull in libDaisy + stmlib
# (the Plaits DSP source is already vendored in thirdparty/plaits/ — nothing to copy)
git submodule update --init --recursive

# Build libDaisy once, then the firmware
cd lib/libDaisy && make && cd ../..
make

# Flash: bootloader once per device, then the app
make program-boot
make program-dfu
```

The build output is `build/TouchPlaited.bin`. See [thirdparty/README.md](thirdparty/README.md) for the full setup details.

If you prefer flashing a `.bin` from the command line instead of the Web Programmer (with the Seed in the Daisy bootloader, as in Option A step 3):

```bash
dfu-util -a 0 -s 0x90040000:leave -D TouchPlaited.bin
```

`-D` downloads the file to the device, `-a 0` selects the flash interface, and `-s 0x90040000` is where to write it: the Seed's QSPI flash is memory-mapped at `0x90000000` and the bootloader reserves the first 256 KB (`0x40000`) for itself, so apps live at `0x90000000 + 0x40000`. The `:leave` suffix reboots into the app when the transfer finishes.

## Drum pattern editor

The drum sequencer's genre patterns are plain headers in [synth/patterns/](synth/patterns/) (one folder per genre: `techno/`, `electro/`, `idm/`). [tools/pattern_editor.html](tools/pattern_editor.html) is a browser-based editor for making them — open it directly in a browser, no server needed, to draw steps on the 16-step grid with four per-step chance levels (always / 75% / 50% / 25% — these interact with the Density knob) and audition the pattern with the live preview. Editing itself needs nothing but a browser; it's turning the exported `.h` file into a pattern the hardware can play that needs Python 3 and the rest of the [build-from-source toolchain](#option-b--build-from-source-with-make) — drop the file into the right genre folder and run `make`, which calls `tools/gen_patterns.py` to regenerate the pattern registry. The new pattern then shows up under S35 for its genre in Seq mode.

![TouchPlaited SEQ editor](img/TouchPlaited_SEQeditor.png)

**Connect folder…** (Chrome/Edge, via the File System Access API) links the editor straight to your `synth/patterns/` folder so **Load** and **Save .h** read and write the genre subfolders directly — no manual copy/paste or re-picking a save location each time. The folder reference itself isn't stored in the repo or any config file: it's a handle kept in that browser profile's IndexedDB, so it's local to the machine/browser you connected from. Permission can lapse (e.g. after a browser restart); if it does, one click on **Connect folder…** re-authorizes the same remembered folder instead of making you browse for it again. Browsers without the API fall back to the copy/paste export.

I built this editor because hand-writing 64-byte step/velocity arrays in a header file is exactly the kind of tedium that turns into typos — a visual grid with instant audio feedback is a much shorter loop from "I want this pattern" to hearing it on the sequencer.

## About this project

TouchPlaited was built almost entirely by prompting: I can't write full C++ firmware myself, but years of making Simple Touch firmwares (mostly with Plugdata/HVCC) taught me how I want an instrument to *feel* — so I supplied the UI/UX thinking and the hardware knowledge, and let Claude (in VSCode, via the Claude Code extension) do the engineering.

The approach was deliberate rather than freewheeling:

1. **Start with a written brief.** A short prompt describing the goal, the hardware, and the reference codebases went in first — kept as [notesarchive/Initial-Prompt-plan.md](notesarchive/Initial-Prompt-plan.md).
2. **Demand a plan with decision gates.** Claude turned it into a phased [implementation plan](notesarchive/plan_archive.md) with explicit VERIFY blocks — design choices I had to answer myself before any code got written.
3. **Iterate with a paper trail.** From there it was a loop of implement → test on hardware → feed findings back. Decisions and analyses accumulate in [notes.md](notes.md), future work lives in [ROADMAP.md](ROADMAP.md), and each finished era gets archived instead of deleted — the completed v1 steps are in [notesarchive/roadmap_v1_archive.md](notesarchive/roadmap_v1_archive.md).

The full story — the original prompt, the plan, and a timeline from the first commit to the first stable version — is in [notesarchive/readme.md](notesarchive/readme.md). If you're curious what a mainly prompt-driven firmware project actually looks like end to end, that folder is the honest record.

### Thanks & credits

- **Émilie Gillet / Mutable Instruments** — [Plaits](https://github.com/pichenettes/eurorack/tree/master/plaits) and [stmlib](https://github.com/pichenettes/stmlib), the heart of this firmware (MIT License). Also the excellent [documentation](https://pichenettes.github.io/mutable-instruments-documentation/modules/plaits/).
- **shakfu** — [sk-engines](https://github.com/shakfu/sk-engines) and its [mosc](https://github.com/shakfu/sk-engines/blob/0.5.1/docs/engines/mosc.md) firmware, which put Plaits on Spotkach (also Daisy powered) first and gave me the inspiration to build this.
- **Vlad (Bleeptools) / Synthux Academy** — the [TouchBass](https://github.com/Synthux-Academy/TouchBass) repo was the base template this project grew from, and the [Simple Touch](https://synthux.academy/) itself is the platform that makes tinkering like this so approachable (including their [libDaisy fork](https://github.com/Synthux-Academy/libDaisy) used as the submodule here).
- **Electrosmith** — the [Daisy Seed](https://electro-smith.com/) and [libDaisy](https://github.com/electro-smith/libDaisy), the hardware and library everything runs on.
- **Anthropic's Claude** — wrote the C++ under my direction; see the workflow story above.
