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
| S31 | **Frequency (coarse)** | Main pitch knob |
| S32 | **Harmonics** | Direct Plaits param |
| S33 | **Timbre** | Direct Plaits param |
| S34 | **Morph** | Direct Plaits param |
| S30 | **Model select** (0–23) | Left side knob — scrolls models |
| S35 | **Level / VCA** | Right side knob |
| S36 (fader) | **Decay** | Envelope decay time |
| S37 (fader) | **FM amount** | Or Timbre attenuverter |
| SW1 (3-pos) | **Scale family** | e.g. minor / chromatic / major |
| SW2 (3-pos) | **Playmode selector** | 1 = standard / Z = per-pad presets / 3 = randomizer |
| P3–P9 | **7 note pads / 7 preset slots** | Role changes per playmode |
| P10 | **Octave up** (momentary) | Top pad — all playmodes |
| P11 | **Octave down** (momentary) | Top pad — all playmodes |
| P0 | **Modifier hold** | Enters secondary settings mode / pad-edit in Playmode Z |
| P2 | **Trigger / accent** | Retrigger without changing pitch |
| P0 + P8/P9 | **Increment / decrement** secondary value | While P0 held (Playmode 1) |
| LED | **Blink feedback** | On trigger; pattern on value/mode change |

---

> ### ⬛ VERIFY 2.A — Hardware mapping
>
> Review the table above. Confirm, reject, or revise each row before any code is written.
>
> **Changes:** ___

---

> ### ⬛ VERIFY 2.B — Model selection method
>
> S30 as a continuous knob sweeping 0–23 is the simplest implementation.
>
> **Options:**
> - (a) S30 knob sweeps 0–23 continuously (no mode change needed)
> - (b) P0 + pad combo selects model (frees S30 for another param)
> - (c) SW1/SW2 combo steps through model groups
> - (d) Hybrid: S30 selects model bank (0–7, 8–15, 16–23), SW selects within bank
>
> **Decision:** ___

---

> ### ⬛ VERIFY 2.C — Scale system
>
> **Updated context:** SW2 is now the playmode selector, so scale selection is SW1 only.
> Octave shift is handled by P10/P11 (momentary), so SW2 is freed up.
>
> SW1 (3-pos) selects scale family; P3–P9 play the 7 degrees of that scale.
> Octave is shifted at any time via P10 (up) / P11 (down).
>
> **Proposed scale list (SW1 positions):**
> - Down: Minor (natural) — good default for moody/bass use
> - Center: Chromatic (7 semitones, P3=root ascending)
> - Up: Major
>
> Additional scales accessible via P0 modifier mode (step through extended list).
>
> **Decision — which scales, in which SW1 positions:** ___

---

> ### ⬛ VERIFY 2.D — Multi-voice / playmode architecture
>
> **New finding:** Engine switching is instant (≤0.25 ms), CPU per voice ≈10–15%, safe ceiling ≈4 simultaneous voices.
>
> **Three proposed playmodes (SW2):**
>
> **Playmode 1 (SW2 down)** — Standard, monophonic:
> - 1 active Plaits voice; 7 pads = 7 scale degrees; knobs control the one model globally.
> - Voice stealing per VERIFY 5.A when two pads pressed.
>
> **Playmode Z (SW2 center)** — Per-pad presets, polyphonic:
> - 7 pad slots, each storing its own `Patch` (model + all parameters).
> - Up to 4 simultaneous voices from the voice pool; oldest-note steal when pool full.
> - Hold P0 + press a pad → enter "edit this pad" mode: all knobs address that pad's stored Patch.
> - **Pot pickup / catch mode required**: knob value only takes effect once it crosses the stored value (avoids jumps when switching pad context). Must be implemented before this mode is usable.
> - Each pad can have a completely different model — pressing a pad instantly switches that slot's voice to its stored model (instant because engine switching is ≤0.25 ms).
> - Pitch in Playmode Z: pads still follow the scale (SW1) + octave (P10/P11), just each pad ALSO has its own timbre/model settings.
>
> **Playmode 3 (SW2 up)** — Randomizer:
> - Touch a pad → generate random `Patch` (bounded random — constrained ranges, not pure chaos), immediately play it.
> - Release P0 modifier (or a defined "commit" gesture) → store that random Patch permanently in that pad's slot.
> - Shares the 7-slot storage system with Playmode Z (randomizer fills slots, Playmode Z edits them manually).
> - LED blinks to confirm a slot is stored.
>
> **Voice pool:** Allocate N voice instances at startup (per decision below), each with 16 KB SDRAM scratch.
>
> **Decide:**
> - (a) Allocate 4 voices (safe ceiling, oldest-note steal at capacity) — **recommended**
> - (b) Allocate 7 voices (one per pad, simultaneous; risky on heavy engines, fine on VA/noise)
> - (c) Start with 2 (match mosc), expand after measuring with CpuLoadMeter
>
> **Decision — voice count and playmode layout:** ___

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

- [ ] Read ADC for S31 → `patch.note` (map 0..1 → MIDI note range, e.g. 24..84 for C1–C5)
- [ ] Read ADC for S32 → `patch.harmonics` (0..1 direct)
- [ ] Read ADC for S33 → `patch.timbre` (0..1 direct)
- [ ] Read ADC for S34 → `patch.morph` (0..1 direct)
- [ ] Read ADC for S30 → `patch.engine` via `min(max((int)(v * 23 + 0.5f), 0), 23)` (per VERIFY 2.B)
- [ ] Read ADC for S35 → `patch.lpg_colour` (LPG colour — 0=VCA mode, 1=LPG mode)
- [ ] Read ADC for S36 → `patch.decay` (0..1 direct)
- [ ] Read ADC for S37 → `patch.frequency_modulation_amount` (0..1 direct)
- [ ] Note: `daisy::AnalogControl` has built-in smoothing — use it, do not add redundant filtering
- [ ] Test: twisting each knob produces expected audible change

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

> ### ⬛ VERIFY 5.A — Voice stealing in Playmode 1
>
> In Playmode 1 there is one active Plaits voice. If two pads are pressed simultaneously, which note wins?
> (Playmode Z handles polyphony via a voice pool — this gate applies to Playmode 1 only.)
>
> **Options:**
> - (a) Last-note priority (most recent pad wins, retriggers) — most expressive for melody
> - (b) First-note priority (ignore new presses while one is held)
> - (c) Highest note
> - (d) Lowest note
>
> **Decision:** ___

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

Goal: User can reach all 24 models; LED confirms selection.

- [ ] Implement model selection per VERIFY 2.B decision
- [ ] Clamp/wrap model index at 0 and 23
- [ ] Implement LED blink feedback on model change (per VERIFY 7.A)
- [ ] Test: cycle through all 24 models, each sounds distinct

---

> ### ⬛ VERIFY 7.A — LED feedback scheme
>
> The LED is on/off only. Options for communicating model number or state change:
>
> - (a) N short blinks = model number (slow for model 23)
> - (b) Grouped blinks: tens digit as long blinks + ones as short (e.g. 2 long + 3 short = model 23)
> - (c) Single blink on any change (no numerical info)
> - (d) Blink rate encodes position in range (faster near max)
> - (e) LED stays on in "model select mode," off otherwise
>
> **Decision:** ___

---

## Phase 8: Modifier Mode (P0 combo)

Goal: Hold P0 to enter a secondary settings mode; P8/P9 increment/decrement a value.

- [ ] Implement P0 hold detection: >300 ms hold → modifier mode active
- [ ] While in modifier mode: disable normal note playing
- [ ] P8 (while P0 held) → decrement current secondary value
- [ ] P9 (while P0 held) → increment current secondary value
- [ ] LED blinks once on each increment/decrement
- [ ] LED blinks 3× on reaching minimum; 3× on reaching maximum
- [ ] Release P0 → exit modifier mode, resume note playing
- [ ] Define which secondary values are accessible (per VERIFY 8.A)
- [ ] Test each secondary parameter end-to-end

---

> ### ⬛ VERIFY 8.A — What lives in modifier mode?
>
> Candidates (pick 2–4 for v0.1):
> - Noise mix (Plaits has a noise parameter)
> - Envelope attack time
> - FM ratio / coarse tuning
> - Scale root note (transpose without changing octave)
> - Active secondary value cycles with each P0 press (P0 tap = next parameter, P0 hold = adjust)
>
> **Decision — which parameters, and how to cycle between them:** ___

---

## Phase 8B: Multi-voice Pool

Goal: Allocate N Plaits voices in SDRAM; implement a voice pool with oldest-note stealing. This is the shared foundation for Playmodes Z and 3.

- [ ] Decide final voice count (per VERIFY 2.D) before this phase
- [ ] Allocate N `plaits::Voice` instances, each behind the Pimpl wrapper
- [ ] Allocate N × 16 KB scratch buffers in SDRAM: `uint8_t scratch[N][16384] DSY_SDRAM_BSS`
- [ ] Initialize all N voices at startup
- [ ] Implement `VoicePool`: tracks which voices are active, which pad owns each, and time-of-last-trigger for oldest-note stealing
- [ ] Instrument with `CpuLoadMeter`: log CPU % with 1, 2, 3, 4 voices active to validate estimates
- [ ] Confirm: 4 simultaneous voices stays within acceptable CPU load; adjust N if not
- [ ] Each pad slot stores a `Patch` struct (7 slots × ~48 bytes = negligible RAM)
- [ ] Mix N active voice outputs: sum `Frame.out` values, divide by N for consistent level

---

## Phase 8C: Playmode Switcher (SW2)

Goal: SW2 selects the active playmode; behavior of pads, knobs, and LED changes per mode.

- [ ] Read SW2 on every main loop iteration (3 states: down=1 / center=Z / up=3)
- [ ] Implement a `playmode` enum: `STANDARD`, `PER_PAD`, `RANDOMIZER`
- [ ] On SW2 change: transition cleanly (release any held notes, reset voice pool)
- [ ] Route pad presses, knob reads, and P0 modifier to the correct playmode handler
- [ ] LED blinks N times on mode switch (1, 2, or 3 blinks) to confirm new mode
- [ ] Test: switch modes mid-session, correct behavior in each

---

## Phase 8D: Playmode Z — Per-pad Presets

Goal: Each of the 7 pads has its own stored model + full Patch. Multiple pads can sound simultaneously (up to voice pool limit).

- [ ] Initialize all 7 pad slots with distinct default `Patch` values (spread across different models)
- [ ] On pad press: assign a free voice from the pool to that pad, load pad's stored `Patch`, trigger
- [ ] On pad release: gate off that pad's voice; return voice to pool after decay
- [ ] Simultaneously pressed pads each play their own voice (up to N-voice limit)
- [ ] **Implement pot pickup / catch mode:**
  - Each knob tracks its last-known ADC value
  - On entering a pad-edit context, knob is "uncaught" — it does not affect the parameter until the ADC value sweeps through (crosses) the stored parameter value
  - Once caught, the knob controls the parameter normally
  - LED blinks briefly when a knob is "caught" (optional, if LED is free)
- [ ] Hold P0 + press a pad → enter "edit this pad" context; all 4 main knobs (S31–S34) address that pad's Harmonics/Timbre/Morph/Model
- [ ] S30 in edit context → adjust that pad's model (0–23) with pot pickup
- [ ] Release P0 → exit edit context, pad's updated Patch is persisted in its slot
- [ ] All 7 pads remain playable even while editing one (hold P0 = P3–P9 still trigger notes)
- [ ] Test: assign different models to each pad; press multiple pads simultaneously; confirm each sounds its own model

---

## Phase 8E: Playmode 3 — Randomizer

Goal: Touch a pad to receive a random sound; lock it to that pad slot with a modifier gesture.

- [ ] Implement `randomize_patch(Patch& p)`: fills all Patch fields with constrained random values
  - `engine`: random 0–23 (uniform)
  - `harmonics`, `timbre`, `morph`: random 0..1, biased toward 0.2–0.8 range (avoid extreme silence/clipping)
  - `note`: keep the pad's normal scale degree (do not randomize pitch)
  - `decay`: random 0.1–0.9
  - `lpg_colour`: random 0..1
- [ ] On pad press in Playmode 3: call `randomize_patch()`, load into voice, trigger immediately
- [ ] The randomized Patch is *temporary* — not stored unless confirmed
- [ ] Confirm gesture (options — decide at implementation time): double-tap the pad, or hold P0 briefly while pad is pressed
- [ ] On confirm: copy the temporary Patch into that pad's permanent slot (shared with Playmode Z)
- [ ] LED: blinks 3× fast on each new randomize; long blink on store confirmation
- [ ] Test: tap many pads rapidly — each gives a new random sound; confirm-store a few; switch to Playmode Z and verify the stored sounds match

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
- **OLED screen** — future hardware revision; would replace the LED blink scheme entirely
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
| 2.A | Final hardware mapping table (SW2 now = playmode selector) | | |
| 2.B | Model selection in Playmode 1 (knob S30 / pad combos / hybrid) | | |
| 2.C | Scale system (which scales, which SW1 positions) | | |
| 2.D | Multi-voice architecture (voice count, playmode layout) | | |
| 5.A | Voice stealing in Playmode 1 (last-note / first-note / highest / lowest) | | |
| 7.A | LED blink feedback scheme | | |
| 8.A | Which parameters live in modifier mode (Playmode 1) | | |
| 10.A | Audio input strategy (passthrough / exciter / modulator / ignore) | | |
