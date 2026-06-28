# TouchPlaited — Implementation Plan

**Project:** libDaisy/C++ port of Mutable Instruments Plaits for the Synthux Simple Touch  
**Target device:** Daisy Seed on Simple Touch PCB  
**Base template:** [TouchBass](https://github.com/Synthux-Academy/TouchBass)  

---

## How to use this plan

- Check off `[x]` when a task is done
- **VERIFY blocks** are explicit decision gates — do not proceed past one without confirming the answer
- Phases are ordered; tasks within a phase are roughly independent and can be done in any order
- Parking lot at the bottom = deferred scope, not forgotten

---

## Phase 0: Research & Codebase Audit

Goal: Understand the three codebases before writing a line of project code.

### 0.1 — Plaits DSP surface

- [x] Read `plaits/dsp/voice.h` — understand `Voice`, `Patch`, `Modulations` structs and their fields
- [x] Enumerate all 24 engine classes and their enum values
- [x] Note assumed sample rate and block size in Plaits render loop
- [x] List external dependencies pulled in by Plaits (stmlib, etc.) and their paths
- [x] Identify any STM32/hardware-specific code that must be stubbed or removed for Daisy

**Research notes:**
```
Voice::Render signature:
  void Voice::Render(const Patch& patch, const Modulations& modulations,
                     Frame* frames, size_t size);
  Frame = { int16_t out; int16_t aux; }   ← OUTPUT IS int16, not float

Patch fields (all float unless noted):
  float note;               // MIDI note number (e.g. 60.0 = middle C)
  float harmonics;          // 0..1
  float timbre;             // 0..1
  float morph;              // 0..1
  float frequency_modulation_amount;  // 0..1
  float timbre_modulation_amount;     // 0..1
  float morph_modulation_amount;      // 0..1
  int   engine;             // 0..23  ← set as: min(max((int)(v*23+0.5f),0),23)
  float decay;              // 0..1
  float lpg_colour;         // 0..1  ← LPG colour (not in original Plaits panel)

Modulations fields:
  float engine, note, frequency, harmonics, timbre, morph, trigger, level;
  bool  frequency_patched, timbre_patched, morph_patched, trigger_patched, level_patched;
  (trigger = 1.0f when gate active, else 0.0f)

Sample rate: 48 kHz (designed for this; no hardcoded rate in Voice itself)

Block size: MAXIMUM 12 samples (internal downsampler requirement — not flexible)
  ⚠ TouchBass uses 4 samples. Mismatch — see VERIFY 0.B.

External deps: stmlib (BufferAllocator, etc.)
  Needs: ~16 KB scratch buffer passed to Voice::Init(&allocator)

STM32-specific: Only in plaits/plaits.cc, ui.cc, settings.cc, drivers/ — all excluded.
  plaits/dsp/ is fully portable. Include only dsp/ + resources.h/cc.

Engines:
  plaits/dsp/engine/  (16 original): VirtualAnalog, Waveshaping, FM, Grain, Additive,
    Wavetable, Chord, Speech, Swarm, Noise, Particle, String, Modal,
    BassDrum, SnareDrum, HiHat
  plaits/dsp/engine2/ (8 newer):   VirtualAnalogVCF, PhaseDistortion, SixOp,
    WaveTerrain, StringMachine, Chiptune, + 2 more
```

### 0.2 — TouchBass template audit

- [x] Clone TouchBass, confirm it compiles and flashes
- [x] Note libDaisy version pinned (commit hash or tag)
- [x] Map ADC channel assignments for all 8 pots (which ADC pin → which pot)
- [x] Understand how MPR121 is initialized and read (polling interval, I2C address, pad bitmask)
- [x] Note audio callback signature and default block size
- [x] Note build structure (how sources are added, how libDaisy is linked)

**Research notes:**
```
Build system: Makefile + ARM GCC; libDaisy + DaisySP as git submodules under lib/

Audio callback signature:
  void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
  hw.SetAudioBlockSize(4);         ← 4 samples per block
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

  ⚠ Block size 4 conflicts with Plaits max 12 — see VERIFY 0.B

MPR121:
  Class: daisy::Mpr121I2C
  I2C address: 0x5A (default)
  Read method: _mpr.Touched() → 16-bit bitmask (1 bit per pad, 12 pads used)
  Pattern: compare current vs. previous bitmask each loop → fire on_touch/on_release callbacks
  Event-driven, not value-polling

Pots:
  8× daisy::AnalogControl on pins S30–S37
  AnalogControl has built-in smoothing/hysteresis — use it, don't re-implement

Project structure:
  TouchBass.cpp     ← main entry, audio callback
  config.h          ← synthesis constants, scale tables
  touch/            ← touch.h, pads.h/cpp, knobs.h
  ui/               ← ui.h
  bass/             ← bass synth class
  lib/              ← libDaisy + DaisySP submodules
```

### 0.3 — mosc engine audit

- [x] Read mosc source — how does it include Plaits source (submodule? copy? symlink?)
- [x] Note any compile-time patches or `#ifdef` workarounds for libDaisy
- [x] Note which `Patch` and `Modulations` fields mosc drives, and with what values
- [x] Note anything mosc intentionally omits (attenuverters? specific models?)

**Research notes:**
```
Plaits include strategy:
  Full plaits/dsp/ subtree copied into thirdparty/plaits/ (with resources.h/cc).
  Hardware files excluded (plaits.cc, ui.cc, settings.cc, drivers/).

Critical workaround — Pimpl idiom:
  stmlib declares a global `namespace impl` that collides with local variables named
  `impl` if Plaits headers are exposed in the main header. Solution: hide the
  plaits::Voice instance behind a private Impl struct using the Pimpl pattern.
  MUST do the same in TouchPlaited.

Memory workaround:
  Binary exceeds the 186 KB Daisy SRAM execution limit at ~292 KB.
  mosc executes the engine from QSPI flash; DSP data lives in SDRAM.
  ⚠ We will hit the same limit — must plan QSPI/SDRAM layout from Phase 1.

Voice init:
  uint8_t* scratch = ar.alloc<uint8_t>(kScratchBytes, 16);  // ~16 KB
  stmlib::BufferAllocator alloc(scratch, kScratchBytes);
  voice.Init(&alloc);

Driven fields (per block):
  patch.note                    = base_note + cv_pitch
  patch.harmonics/timbre/morph/decay/lpg_colour = knob [0..1]
  patch.engine                  = min(max((int)(v*23+0.5f), 0), 23)
  modulations.trigger           = 1.0f (gate on) / 0.0f (gate off)
  modulations.level             = 1.0f (fixed)
  modulations.timbre_patched    = true when CV present

Intentional omissions (no hardware for these on Spotykach):
  modulations.engine, modulations.note, modulations.frequency (no CV)
  modulations.morph_patched, morph (no morph CV)
  These are exactly the ones WE will want to drive from touch pads and knobs.

Output:
  Frame.out → left channel, Frame.aux → right channel (stereo main+aux)
  Must convert int16_t → float for Daisy output buffers
```

### 0.4 — Multi-voice feasibility on Daisy Seed

- [x] Estimate CPU cost per Plaits Voice at 48 kHz on Daisy H750
- [x] Determine safe upper limit for simultaneous voices
- [x] Confirm engine-switching latency
- [x] Review mosc dual-voice mixing strategy

**Research notes:**
```
CPU per voice estimate:
  Original Plaits runs STM32F373 at 72 MHz using 80-95% CPU.
  Daisy H750: 480 MHz + dual-issue M7 pipeline = ~6-7x raw clock advantage.
  Conservative estimate: 10-15% CPU per voice at 48 kHz on Daisy.
  Heavy engines (speech, granular, modal resonator) cost more; VA and noise cost less.

Safe voice count:
  mosc confirmed: 2 voices works cleanly.
  Estimate: 4 simultaneous voices safe ceiling (40-60% CPU with margin).
  6-7 voices risky on heavy engines; feasible only for cheap engines.
  ⚠ Plan for 4, measure with CpuLoadMeter before increasing.

Engine switching latency:
  INSTANT — single render block boundary (~0.25 ms at 48kHz/12-sample blocks).
  New engine gets Reset() called, renders immediately. No crossfade, no overlap.
  This makes per-pad model assignment practical (switch on press, no audible artifact).

mosc mixing strategy (2 voices → stereo):
  Stereo:          A.out → L, B.out → R
  DoubleMono:      (A.out + B.out) * 0.5 → both channels
  GenerativeStereo: L = A.main + B.aux, R = B.main + A.aux

Memory per voice: 16 KB scratch buffer in SDRAM (64 MB available — not a concern).
  7 voices × 16 KB = 112 KB SDRAM for scratch alone.

Reference: hemmer/PlaitsPatchInit — single Plaits on Daisy Patch.init().
  CpuLoadMeter API: hw.OnBlockStart() / hw.OnBlockEnd() — use from Phase 3 onward.
```

---

> ### ✅ VERIFY 0.A — DSP integration strategy
>
> **Decision: (a) Copy `plaits/dsp/` subtree + stmlib directly into the project repo.**
>
> mosc's DSP layer wraps the same `plaits/dsp/` files we would copy anyway. Its value-add — Pimpl pattern, QSPI/SDRAM layout, int16→float mix — are now fully understood from research and can be replicated in targeted code patterns. mosc's "two-deck" architecture does not map to our 7-slot voice pool; adopting it would create more to strip out than to gain. Use mosc as a reference only for specific implementation patterns (Pimpl struct, SDRAM init, mixing loop). TouchBass provides the hardware layer template (MPR121, AnalogControl, audio callback structure).

---

> ### ✅ VERIFY 0.B — Block size
>
> **Decision: (b) Set block size to 12.**
>
> TouchBass used 4 for its own latency reasons — we have no such requirement. Block size 12 is Plaits' sweet spot (aligned with its internal 4× downsampler: 12 × 4 = 48 internal samples). With 4 simultaneous voices, the difference in callback rate is significant:
> - Block 4: 12,000 callbacks/sec — fixed Render() overhead paid 3× more often
> - Block 12: 4,000 callbacks/sec — confirmed working in mosc
>
> Latency: 0.25 ms at block 12 vs 0.08 ms at block 4 — both imperceptible for a musical instrument. Block 12 wins on efficiency for the voice pool with no perceptible downside. 

---

## Phase 1: Project Scaffold

Goal: A buildable, flashable project that blinks the LED — no audio yet.

- [ ] Copy/fork TouchBass directory structure into TouchPlaited root
- [ ] Add Plaits `dsp/` subtree + `resources.h/cc` + stmlib (per VERIFY 0.A)
- [ ] Update `CMakeLists.txt` / Makefile: project name, target name, Plaits source files
- [ ] Set linker flags for QSPI flash execution + SDRAM data (binary will exceed 186 KB SRAM)
  - Reference: mosc uses `__attribute__((section(".qspiflash")))` on the engine class
  - Daisy linker scripts: `STM32H750IB_qspi.lds` for QSPI-booted firmwares
- [ ] Allocate SDRAM block for the 16 KB Plaits scratch buffer
- [ ] Wrap `plaits::Voice` in a Pimpl struct to avoid `stmlib::impl` namespace collision
- [ ] Confirm project compiles with zero errors
- [ ] Flash to device — LED blinks on startup

---

## Phase 2: Hardware Mapping

Goal: Lock in the control layout before any audio code is written. Changing this later is expensive.

### 2.1 — Hardware inventory

**Simple Touch controls:**

| ID | Type | Location |
|----|------|----------|
| S31, S32, S33, S34 | Knobs | Top row |
| S30, S35 | Knobs | Left and right sides |
| S36, S37 | Faders | Left and right |
| SW1, SW2 | Toggle (on/off/on = 3 positions) | Left and right |
| P10, P11 | Touch pads | Top row |
| P0, P2 | Touch pads | Middle row (P1 is tucked, harder to reach) |
| P3–P9 | Touch pads | Bottom row — 7 pads |
| LED | Single LED, on/off only | User LED |
| Audio in | Mono audio input | — |

**Plaits parameters that need a home:**

| Parameter | Description |
|-----------|-------------|
| Note / Frequency | Pitch of the voice |
| Harmonics | Texture parameter |
| Timbre | Brightness/character |
| Morph | Waveshape/crossfade |
| Model | 0–23 synthesis model |
| Trigger | Voice retrigger |
| Gate | Voice on/off |
| Level | VCA / output volume |
| Decay | Envelope decay time |
| LPG Colour | 0 = VCA mode, 1 = low-pass gate mode |
| (Optional) FM amount | FM attenuverter analog |
| (Optional) Timbre atten | Timbre attenuverter analog |
| (Optional) Morph atten | Morph attenuverter analog |

### 2.2 — Proposed mapping

| Control | Proposed Function | Notes |
|---------|-------------------|-------|
| S31 | **Frequency (coarse pitch)** | Global pitch offset in all modes |
| S32 | **Harmonics** | Direct Plaits param |
| S33 | **Timbre** | Direct Plaits param |
| S34 | **Morph** | Direct Plaits param |
| S30 | **FM amount** | `frequency_modulation_amount` |
| S35 | **Model select** (0–23) | P0 held = models 0–11; P2 held = models 12–23; pickup mode |
| S36 (fader) | **Volume / Level** | Output level |
| S37 (fader) | **Decay** | Envelope decay time |
| SW1 (3-pos) | **Scale family** | Down=Minor / Center=Chromatic / Up=Major |
| SW2 (3-pos) | **Playmode** | Down=1 Basic Pitch / Center=2 Soft Random / Up=3 Full Random |
| P3–P9 | **7 note pads / 7 sound slots** | Scale degrees in mode 1; per-pad presets in modes 2–3 |
| P10 | **Octave up** (cycles +1) | Applies to pad input only, not MIDI |
| P11 | **Octave down** (cycles −1) | Applies to pad input only, not MIDI |
| P0 | **Modifier A** — no standalone action | Hold + S35 → model 0–11; hold + P10/P11 → root note; hold + pad → record mode |
| P2 | **Modifier B** — no standalone action | Hold + S35 → model 12–23 |
| P0 + P2 | **Re-randomize all pads** | Modes 2 and 3 only |
| P0 + P10 | **Root note up** (+1 semitone) | C default; C–B range; LED blink at limits; plays tone to audition |
| P0 + P11 | **Root note down** (−1 semitone) | Same as above |
| P0 + pad (tap) | **Enter recording mode** for that pad | LED blinks; other pads silent |
| P0 + pad (≥500ms) | **Confirm / store** pad slot | Fast gradual blink; SW2 flip = cancel |
| LED | **Blink feedback** | Trigger, value change, mode entry, root note limits |

**Notes:**
- `lpg_colour`: not a panel parameter on original Plaits; hardcoded `0.5f` in v0.1 — parking lot item
- Octave range: proposed ±3 from C4 (C1–C7, MIDI 24–96); tighten after testing
- S31 sets the base register; root note and scale offsets are applied on top of it

---

> ### ✅ VERIFY 2.A — Hardware mapping
>
> **Decision: confirmed in table above (2026-06-28).** Key changes from original proposal:
> S30→FM amount, S35→model select (P0/P2 modifier halves), S36→volume, S37→decay.
> P0 and P2 are modifier-only in all modes (no standalone action).
> lpg_colour hardcoded 0.5f — not a panel param on original Plaits.

---

> ### ✅ VERIFY 2.B — Model selection method
>
> **Decision: S35 + modifier (option e — new).**
> S35 = model select knob with pickup/catch mode.
> P0 held + S35 → models 0–11 (maps 0..1 knob range to 0–11).
> P2 held + S35 → models 12–23 (maps 0..1 knob range to 12–23).
> Neither P0 nor P2 alone triggers a model change — pickup mode required to prevent jumps.
> Switching modifier mid-selection: model stays at current value until S35 crosses the stored value.

---

> ### ✅ VERIFY 2.C — Scale & root note system
>
> **Decision: SW1 Down=Minor / Center=Chromatic / Up=Major**
>
> Octave: P10 cycles up (+1 oct), P11 cycles down (−1 oct) — click-through, not momentary.
> Applies to pad input only; MIDI input is unaffected.
> Proposed range: ±3 oct from C4 (C1–C7 = MIDI 24–96).
>
> Root note: P0 + P10 → +1 semitone, P0 + P11 → −1 semitone.
> Default: C. Range: C through B (12 semitones). LED blink sequence at both limits.
> Switching root note: Plaits plays the root tone immediately to audition.
> Note: S31 (frequency knob) sets the base pitch register — if S31 is not near center,
> the auditioned root tone will sound offset from the expected pitch. User must be aware.

---

> ### ✅ VERIFY 2.D — Multi-voice / playmode architecture
>
> **Voice pool: 4 voices in SDRAM; oldest-note steal when full.**
>
> ---
>
> **Playmode 1 — Basic Pitch (SW2 down)**
> - 1 Plaits voice; knobs directly set the live Patch (no pickup needed — what you see is what you get).
> - 7 pads = 7 scale degrees (SW1) + current octave (P10/P11).
> - Monophonic: voice stealing per VERIFY 5.A.
> - Model: S35 + P0 (0–11) or S35 + P2 (12–23) with pickup mode.
>
> **Playmode 2 — Soft Random (SW2 center)**
> - Same engine as the current Basic Pitch model; each of 7 pad slots has its own random variant of harmonics/timbre/morph/decay.
> - Pitch still follows scale (SW1) + octave (P10/P11) — pads play different notes with different timbres.
> - Polyphonic: up to 4 simultaneous pads via voice pool.
> - Changing model (S35 + P0/P2) in this mode: all 7 pad slots re-randomize their params for the new model immediately.
> - P0 + P2 → re-randomize all 7 pads while keeping current model.
> - First load from another mode: generates 7 random variants of the current Basic Pitch model.
> - Subsequent returns to mode 2: loads last stored random variants (does not re-randomize).
>
> **Playmode 3 — Full Random (SW2 up)**
> - Each of 7 pad slots gets its own random model + random params.
> - All pads trigger at the same base root note — scale selection (SW1) has no pitch effect here.
>   (Point is 7 different timbres/textures at one pitch, not a melodic scale.)
> - Polyphonic: up to 4 simultaneous pads via voice pool.
> - P0 + P2 → re-randomize all 7 pad slots (new random models + params).
> - First load from another mode: full randomize all 7 slots.
> - Subsequent returns to mode 3: loads last stored random slots.
>
> **Slot storage shared across modes 2 and 3.** Mode 1 is independent (live knob values).
>
> **Recording mode (modes 2 and 3):**
> - Tap P0 + pad → enter recording mode for that pad; LED blinks; all other pads silent.
> - Hold P0 + same pad ≥500ms → confirm/store; LED fast gradual blink animation confirms.
> - While in recording mode: can copy current pad to another slot (different blink pattern); user chooses to store to that slot or cancel.
> - Cancel: flip SW2 → cancels, reloads previous setting for the pad.



---

## Phase 3: Audio Engine — Bare Voice

Goal: Plaits makes a sound when a pad is pressed, hardcoded model and pitch.

- [ ] Allocate 16 KB scratch buffer in SDRAM: `uint8_t scratch[16384] DSY_SDRAM_BSS`
- [ ] Init voice: `stmlib::BufferAllocator alloc(scratch, 16384); voice.Init(&alloc);`
- [ ] Set `hw.SetAudioBlockSize(12)` — Plaits sweet spot, confirmed decision
- [ ] Allocate render buffer: `plaits::Voice::Frame frames[12]` per voice
- [ ] Initialize `Patch` with safe defaults: `note=60.0f, harmonics=0.5f, timbre=0.5f, morph=0.5f, engine=0, decay=0.5f, lpg_colour=0.5f`
- [ ] Initialize `Modulations`: all zero, `trigger_patched=true`, `level=1.0f`
- [ ] Wire into Daisy audio callback: `voice.Render(patch, modulations, frames, size)`
- [ ] Convert output: `out[0][i] = frames[i].out / 32768.0f` (left); same for right (can use `frames[i].aux` for stereo)
- [ ] Wire `CpuLoadMeter`: call `cpu_meter.OnBlockStart()` / `OnBlockEnd()` in the audio callback; log the % somewhere accessible (serial debug or LED pattern)
- [ ] Test: press any pad → tone sounds at middle C
- [ ] Record baseline CPU % for 1 voice on each of the 24 engines — this data drives VERIFY 2.D voice count decision

---

## Phase 4: Knob Control

Goal: All knobs drive Plaits parameters in real time, smoothly.

- [ ] Read S31 → `patch.note` (0..1 → MIDI range 24..96, C1–C7; log curve applied in Phase 11)
- [ ] Read S32 → `patch.harmonics` (0..1 direct)
- [ ] Read S33 → `patch.timbre` (0..1 direct)
- [ ] Read S34 → `patch.morph` (0..1 direct)
- [ ] Read S30 → `patch.frequency_modulation_amount` (0..1 direct)
- [ ] Read S35 → model select with pickup/catch mode:
  - Read P0 and P2 pad state each loop
  - If P0 held: map S35 0..1 → engine 0..11 (`(int)(v * 11.5f)`)
  - If P2 held: map S35 0..1 → engine 12..23 (`12 + (int)(v * 11.5f)`)
  - Pickup: only apply if S35 ADC value has crossed the last stored value for that bank
  - Neither P0 nor P2 alone triggers a model change
- [ ] Read S36 → output level/VCA gain (0..1 → applied as output scale in audio callback)
- [ ] Read S37 → `patch.decay` (0..1 direct)
- [ ] `patch.lpg_colour` hardcoded to `0.5f` — not user-accessible in v0.1
- [ ] Note: `daisy::AnalogControl` has built-in smoothing — do not add redundant filtering
- [ ] Test: all 8 pots produce audible changes in their assigned parameters

---

## Phase 5: Touch → Note Input

Goal: P3–P9 play notes; P10/P11 shift octaves.

- [ ] Initialize MPR121, confirm all 12 pads register touches correctly
- [ ] Map P3–P9 indices to scale-degree slots 0–6
- [ ] Build scale lookup: `scale_degree + root_note + octave_offset * 12 → MIDI note number`
- [ ] Set `patch.note` directly to the MIDI note number as a float (e.g. 60.0f = C4) — no V/Oct conversion needed; Plaits `patch.note` IS the MIDI note number
- [ ] On pad press: set `modulations.trigger = 1.0f`, update `patch.note`
- [ ] On pad release: set gate off
- [ ] Wire P10 → `octave_offset++`, P11 → `octave_offset--`, clamped to a sensible range
- [ ] Test: pressing different pads plays different pitches; octave shift works

---

> ### ✅ VERIFY 5.A — Voice stealing in Playmode 1
>
> **Decision: (a) Last-note priority — most recent pad wins and retriggers the voice.**

---

## Phase 6: Scale System

Goal: SW1 selects scale family; pads play the correct intervals for that scale.

- [ ] Read SW1 toggle position (3 states: down / center / up)
- [ ] Define scale interval arrays for each SW1 position (per VERIFY 2.C)
- [ ] Map SW1 state → active scale interval array
- [ ] Combine with Phase 5 octave offset: `note = root_midi + scale_intervals[pad_index] + octave_offset * 12`
- [ ] Switching SW1 mid-play takes effect immediately on next pad press
- [ ] Test: switching scale mid-play, pads sound the correct intervals in each scale
- [ ] Note: SW2 reads happen in Phase 8B (playmode switcher), not here

---

## Phase 7: Model Selection UI

Goal: S35 + P0/P2 selects any of 24 models; LED confirms selection.

- [ ] Implement pickup/catch logic for S35 model select (see Phase 4)
- [ ] Clamp model index: 0–11 for P0 bank, 12–23 for P2 bank; no wrap-around
- [ ] LED blinks on model change to confirm new selection (per VERIFY 7.A)
- [ ] Test: cycle through all 24 models audibly; confirm each bank (P0 and P2) covers its 12

---

> ### ✅ VERIFY 7.A — LED feedback scheme
>
> **Decision: (b) Grouped blinks — tens as long blinks, ones as short blinks.**
> Fallback to (e) "LED stays on in model-select mode" if (b) proves too clunky in practice.
>
> **Encoding:**
> - Long blink = ~400 ms on; short blink = ~100 ms on; gap between blinks = ~150 ms; pause between groups = ~500 ms
> - Model 1–9: 0 long + N short (e.g. model 7 = 7 short blinks)
> - Model 10–19: 1 long + N short (e.g. model 15 = 1 long + 5 short)
> - Model 20–23: 2 long + N short (e.g. model 23 = 2 long + 3 short)
> - Model 0: special case — 1 very short pulse (50 ms) to avoid a silent result
> - Models with 0 ones digit (10, 20): long blinks only, no short group
>
> **Fallback rule:** If user testing finds counting ≥7 blinks too slow/confusing, switch to (e): LED simply stays on while P0 or P2 is held (model-select active), off when released. No numerical info, but instant feedback.

---

## Phase 8: Modifier + Recording Mode (P0/P2 combos)

Goal: Implement all P0 and P2 modifier combinations as defined in the hardware mapping.

**P0 and P2 have no standalone action in any mode.**

- [ ] Implement `ModifierState`: tracks P0 held, P2 held, and elapsed hold time
- [ ] P0 + S35 → model select 0–11 (with pickup; see Phase 4)
- [ ] P2 + S35 → model select 12–23 (with pickup; see Phase 4)
- [ ] P0 + P10 → root note +1 semitone; LED blink; play audition tone; blink sequence at limit (B)
- [ ] P0 + P11 → root note −1 semitone; same feedback; blink sequence at limit (C)
- [ ] P0 + P2 → re-randomize all 7 pad slots (modes 2 and 3 only; no-op in mode 1)

**Recording mode (applies to modes 2 and 3 only):**
- [ ] P0 + pad (short tap < 500ms): enter recording mode for that pad
  - LED blinks steadily; all other 6 pads go silent (no note trigger)
  - Voice pool still renders the selected pad's current sound while in recording mode
- [ ] While in recording mode, S31–S35 + P0/P2 can adjust the selected pad's Patch directly (with pickup on each knob)
- [ ] P0 + same pad held ≥500ms: confirm/store → LED fast gradual blink animation confirms; exit recording mode; all pads resume
- [ ] While in recording mode: P0 + any OTHER pad = "copy to this slot"
  - LED uses a distinct blink pattern (e.g. double blink) to show copy mode
  - P0 + original pad (≥500ms) = store copy there; or SW2 flip = cancel copy
- [ ] SW2 position change at any time → cancel recording mode, reload previous Patch for the pad
- [ ] Test: enter, edit, confirm, copy, cancel — all transitions work correctly

---

## Phase 8B: Multi-voice Pool

Goal: Allocate 4 Plaits voices in SDRAM; voice pool with oldest-note stealing.

- [ ] Allocate 4 `plaits::Voice` instances, each behind the Pimpl wrapper
- [ ] Allocate 4 × 16 KB scratch buffers in SDRAM: `uint8_t scratch[4][16384] DSY_SDRAM_BSS`
- [ ] Initialize all 4 voices at startup
- [ ] Implement `VoicePool`: tracks active voices, which pad slot owns each, time-of-last-trigger
- [ ] On voice pool full: steal oldest active voice (gate off immediately, reassign)
- [ ] Each pad slot stores a `Patch` struct (7 slots × ~48 bytes = negligible RAM)
- [ ] Mix active voice outputs: accumulate `Frame.out` samples, divide by number of active voices
- [ ] Instrument: log CPU % with 1, 2, 3, 4 active voices via `CpuLoadMeter`
- [ ] Confirm ≤70% CPU with all 4 active on heaviest engines

---

## Phase 8C: Playmode Switcher (SW2)

Goal: SW2 position routes all pad/knob/LED logic to the correct playmode handler.

- [ ] Read SW2 each main loop (3 states: down=1 / center=2 / up=3)
- [ ] Implement `Playmode` enum: `BASIC_PITCH`, `SOFT_RANDOM`, `FULL_RANDOM`
- [ ] On SW2 change: gate off all held voices; cancel any active recording mode; LED blinks 1/2/3 times to confirm mode
- [ ] On entering mode 2 for first time: generate 7 soft-random variants from current Basic Pitch model
- [ ] On entering mode 3 for first time: fully randomize all 7 pad slots
- [ ] On subsequent entries to modes 2 or 3: load last stored slots (no re-randomize)
- [ ] Route pad presses, knob reads, P0/P2 combos to mode-specific handlers
- [ ] Test: switch modes mid-session; correct sounds in each; stored slots persist

---

## Phase 8D: Playmode 2 — Soft Random

Goal: 7 pad slots each with a random variant of the current model's params; still pitched/scaled.

- [ ] `generate_soft_random_slots(model)`: for each of 7 slots, copy Basic Pitch model, randomize harmonics/timbre/morph/decay within ±0.3 of pad index spread (constrained, not full random)
- [ ] On pad press: load that slot's Patch into a pool voice, use scale pitch for that pad, trigger
- [ ] S35 (+ P0/P2) model knob change while in mode 2: call `generate_soft_random_slots(new_model)` immediately, updating all 7 slots
- [ ] P0 + P2: re-call `generate_soft_random_slots(current_model)` to generate new random variants
- [ ] Recording mode (Phase 8 modifier): fully editable per-pad — all knobs adjustable with pickup
- [ ] Test: 7 pads sound different timbres of same engine; model knob change reshuffles all

---

## Phase 8E: Playmode 3 — Full Random

Goal: 7 pad slots each with a fully random model + params; all at root pitch.

- [ ] `generate_full_random_slots()`: for each of 7 slots, randomize engine (0–23), harmonics/timbre/morph (0.2–0.8 biased), decay (0.1–0.9); `lpg_colour` = 0.5f
- [ ] On pad press: load that slot's Patch, trigger at root note pitch (ignore scale degrees — pitch is fixed)
  - Root note = current root + current octave offset, same for all 7 pads
- [ ] P0 + P2: re-call `generate_full_random_slots()` to randomize all 7 slots fresh
- [ ] Recording mode (Phase 8 modifier): can store/confirm any slot; SW2 flip cancels
- [ ] Test: all 7 pads play same pitch, different random timbres; P0+P2 reshuffles

---

## Phase 9: USB MIDI Input

Goal: MIDI NoteOn/NoteOff drives the voice alongside pad presses.

- [ ] Enable Daisy USB MIDI (check libDaisy `MidiUsbHandler` or equivalent)
- [ ] Parse `NoteOn` → set pitch, trigger voice
- [ ] Parse `NoteOff` → gate off (if no pad also held)
- [ ] Map MIDI velocity → `patch.level_patched` or accent parameter
- [ ] MIDI and pad input share the same voice stealing logic (VERIFY 5.A)
- [ ] Test: play from DAW or MIDI keyboard, notes sound

---

## Phase 10: Audio Input

Goal: Do something useful with the audio input jack.

---

> ### ⬛ VERIFY 10.A — Audio input strategy
>
> **Options:**
> - (a) Passthrough mix: audio in added to synth output (Plaits + external source)
> - (b) External oscillator / exciter: feed audio in as the Plaits "ext" input for models that support it
> - (c) Modulator: use audio in amplitude to modulate Timbre or Morph in real time (audio-rate or envelope-follower)
> - (d) Ignore / leave unconnected for v0.1, revisit later
>
> **Decision:** ___

---

- [ ] Implement per VERIFY 10.A decision

---

## Phase 11: Polish & Release

Goal: Stable, well-tuned, flashable v0.1 firmware.

- [ ] Audit all knobs — apply log curve for frequency (linear feels wrong), linear for others
- [ ] Confirm parameter smoothing everywhere (no zipper noise on any knob)
- [ ] Full playthrough test: all 24 models, all scales, all octaves
- [ ] Stress test: all 7 pads held simultaneously + all knobs at extremes
- [ ] Profile DSP load: confirm < 70% CPU on Daisy Seed
- [ ] Write `FLASH.md` — step-by-step instructions for DFU flash
- [ ] Tag `v0.1.0` in git

---

## Parking Lot (deferred, not forgotten)

- **Persistent state** — save last model + settings across power cycles (requires flash write via libDaisy `PersistentStorage`)
- **TRS MIDI** — for users who have hardware-modded their Simple Touch
- **Additional scales** — accessible via modifier mode (P0 + pad combos beyond the 3 SW1 positions)
- **128px OLED screen (optional add-on)** — small I2C 128×32 or 128×64 OLED; would show model name, param values, current mode, root note. Would replace or supplement the LED blink scheme. User is considering this for a v2 hardware build.
- **Clock sync** — connect a mono jack to the clock input, sync envelope or arpeggio
- **Chord mode** — single pad triggers a chord (maps to multiple sequential Plaits triggers)
- **Persistent pad slots** — save all 7 per-pad Patches across power cycles (libDaisy `PersistentStorage`)
- **MIDI per-pad** — in Playmode Z, route incoming MIDI to specific pad slots
- **Expand voice pool to 7** — once CpuLoadMeter confirms headroom on the engines the user actually plays

---

## Decision Log

Fill this in as VERIFY blocks are resolved:

| # | Decision | Chosen | Date |
|---|----------|--------|------|
| 0.A | DSP integration strategy | Copy `plaits/dsp/` + stmlib directly; mosc = reference only | 2026-06-27 |
| 0.B | Block size | 12 samples — Plaits sweet spot, 3× more efficient for voice pool | 2026-06-27 |
| 2.A | Final hardware mapping table | Confirmed — see table in Phase 2 | 2026-06-28 |
| 2.B | Model selection method | S35 + P0 (0–11) / P2 (12–23), pickup mode | 2026-06-28 |
| 2.C | Scale system | SW1: Down=Minor / Center=Chromatic / Up=Major; root via P0+P10/P11 | 2026-06-28 |
| 2.D | Multi-voice / playmode architecture | 4 voices; modes: Basic Pitch / Soft Random / Full Random | 2026-06-28 |
| 5.A | Voice stealing in Playmode 1 | Last-note priority (most recent pad wins, retriggers) | 2026-06-28 |
| 7.A | LED blink feedback scheme | (b) grouped blinks; fallback to (e) if clunky | 2026-06-28 |
| 8.A | ~~Modifier mode parameters~~ | Superseded by recording mode (Phase 8) | 2026-06-28 |
| 10.A | Audio input strategy (passthrough / exciter / modulator / ignore) | | |
