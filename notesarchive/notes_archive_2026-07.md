# Notes archive — resolved issues, budget analyses & design sketches (2026-07)

Second cleanup pass, same idea as `roadmap_v1_archive.md`: `notes.md` had
grown to 900+ lines mixing live reference material with fully-resolved
debugging history, superseded design sketches, and implementation logs whose
findings are already baked into the code and the current `ROADMAP.md`. This
file holds the resolved/historical half, moved here (mostly verbatim) rather
than deleted. It covers roughly 2026-07-03 through 2026-07-23 — the
voice-expansion/MIDI/FX work that predates or overlaps `roadmap_v1_archive.md`,
the bugs found and fixed during that stretch, and the two Arp/Mel and CV-sync
design sketches whose resolutions live in `arp-mel-plan-archive.md` and
`MANUAL.md`.

Where a section led to an open follow-up (the SSD1306 upstream PR, the
Chiptune keep/remove decision), that item was moved to `ROADMAP.md` — this
file has the technical detail, `ROADMAP.md` has the action item.

---

## Open Decisions — resolved

### 1. Recording mode confirm gesture — RESOLVED

**Decision:** hold rec pad alone ≥800ms = confirm.
**Root cause of original bug:** code was checking P2 (not P0) as modifier. No actual conflict with model-change — feature was just broken.
**Guard:** `rec_entry_released` flag: pad must be released at least once after entry before confirm hold timer can fire.

### 2. Mode structure — RESOLVED

**Decision:**
- SW2 Down = Basic Pitch (unchanged)
- SW2 Center = Random (merged Soft + Full; P0+P2 stages 1/2/3 = soft tight / soft wide / full random models — all pitched, no drums)
- SW2 Up = Seq (drum mode exclusively; starts immediately on enter; P1 = play/pause)

**Drum mode is Seq-exclusive.** Not reachable from Random via P0+P2 anymore.

**P1** was unused in Basic Pitch and Random at the time this was written (reserved for "melodic seq trigger" — parking lot). Superseded: P1 later became the FX modifier layer (`FX` branch, 2026-07-09), and SW2 center itself became Arp/Mel (see the "Playmode overhaul" sketch below).

**P0+P2 stages in Random** are all pitched: stage 3 gives each pad its own random engine + params, but pads still follow scale pitches. Chaos is musical chaos, not drum chaos.

---

## Known Issues — all fixed

### Six-Op A/B/C models (2, 3, 4) barely audible — FIXED for random generation

The controls *are* wired correctly — these engines are just unusual: **S32 harmonics is a quantized DX7 patch selector** (small moves do nothing, then jump to another patch — this is why knob sweeps feel unrepeatable/random) and **S33 timbre is the FM modulator level** (near zero ≈ silent). Random values therefore usually landed in dead zones. Fix: `generate_full_random()` now anchors engines 2–4 to the `kSixOpAud[]` audible presets with a small variance (h ±0.08, t/m ±0.15) instead of the open 0.2–0.8 range. Manual knob sweeps in Basic Pitch remain fully open — expect the quantized-selector feel on S32. (2026-07-10: the deeper cause — a 0.5 ms gate pulse, see next entry — is now fixed; the anchors stay useful for the S32/S33 dead zones, but the ranges are worth revisiting once hardware confirms the full banks speak.)

### Six-Op: silent/alternating pad triggers — FIXED (2026-07-10, hardware verify open)

The old hypothesis here ("DX7 LFO/envelope state initializes on the first trigger — not a value-application bug on our side") was wrong: it was ours. `PlaitsVoice::Render` zeroed `modulations.trigger` after every 24-sample chunk, so every note was a 0.5 ms gate pulse. Only the six-op engines consume gate **length** (real DX7 key-on/key-off: `TRIGGER_HIGH` → `gate` in `six_op_engine.cc`); every other engine fires on the rising edge only, which is why only Six-Op suffered. Two symptoms, one cause:

- **Every second press silent**: the engine's two paraphonic FM voices alternate per trigger *and* are render-staggered — only one of the two renders per block. When the rising edge landed on the wrong parity, the triggered voice's `gate=true` parameter was overwritten to false before that voice's next render — its envelope never keyed on.
- **Most patches near-silent**: a 0.5 ms key-on jumps straight to release; anything without an instant attack barely spoke. This is the deeper cause behind "barely audible" above — the `kSixOpAud` anchors compensated by picking patches that survive a click-length gate.

Fix: real gate semantics in `PlaitsVoice` (trigger held while the pad is held; retriggering a still-high line inserts one forced-low block so stolen voices still produce a rising edge) + a decay-scaled one-shot gate timer in `VoicePool` (20–400 ms, `one_shot_gate_chunks`) for voices that never get a NoteOff: drum-seq triggers and auditions. Note the DX7 presets were never missing — all three factory banks are compiled in (`syx_bank_0/1/2` in `resources.cc`, loaded via the `fm_patches_table` fallback in `voice.cc` since the Daisy `UserData` stub returns null).

Follow-up (same day, after hardware confirmed the gate fix): with real gates Six-Op revealed itself as the hottest engine in Plaits — registered at out_gain 1.0 with the LPG bypassed (`already_enveloped=true`), and its internal `SoftClip(x*0.25)` pins dense patches at full scale for the whole gate, where every other melodic engine runs 0.6–0.8 through a decaying LPG. Polyphonic sums saturated the master `x/(1+|x|)` stage even at low volume. Fixed with a host-side pad on engines 2–4 in `PlaitsVoice::Render` (vendored Plaits untouched): ×0.45 → ×0.35 → **×0.20** across three hardware rounds — sustained tones read much louder than the decaying LPG plucks of the other engines, so the comparable level is well below what the gain registrations suggest. Tune by ear.

Second follow-up (2026-07-10, hardware round 2): (1) note-start **clicks** — the FM voice resets operator phases (patch-dependent `reset_phase`) and reloads the patch at key-on, a hard discontinuity when the reused voice still carries the previous tail. Since Plaits sees our edge `kTriggerDelay` = 5 blocks (~2.5 ms) late, the tail-only window gets masked host-side in `PlaitsVoice::Render`: fade-out on the edge block, 5 muted blocks, one-block ramp-in landing exactly on the key-on. FM engines only — drum transients stay raw, and the ramp ends as the DX7 envelope starts so attack character is untouched. (2) **S32 patch-index blink** — Basic Pitch live path mirrors the engine's 32-zone quantizer (`harmonics*1.02*32`, 15%-into-zone guard against boundary chatter) and fires the existing `LedEvent::MODEL` single blink per zone change; the P2 "LED blink on model load" roadmap item turned out to be already implemented via the same event.

### Seq S30/S34 pots dead after seq re-entry (also rec knobs one-directional) — FIXED

Two pickup flaws: (1) a strict crossing test can never fire when the stored value sits at a pot extreme — drive/punch stored at 0.0 made those pots permanently dead after re-entry; (2) rec pickups armed at the pot's own entry position only engaged when moving *down* through it. Fix: inclusive crossing + 1% near-window, and rec pickups now arm to the **slot's actual value** (true pickup).

### Decay in seq mode — FIXED

S37 tightness now applies to all engine 21–23 slots (not just hats). `slot.decay` routes to `patch.morph` at trigger time for drum engines, scaled by tightness.

### S37 Tightness direction inverted in Seq mode — FIXED

Factor changed from `1.0 - tightness * 0.8` to `0.2 + tightness * 0.8`: S37 down now shortens the tail. Needs hardware confirmation.

### Basic Pitch model select broken while seq engaged — FIXED

Symptom: P0/P2+S35 auditioned the new model but pads kept playing the first pitch model or even drum sounds. Not a pad-release problem — a voice-reuse problem: `SetEngine` (correctly) skips param-locked drum-seq voices, but plain `NoteOn` never re-applied the engine, so a recycled drum voice kept its drum engine. Fix: VoicePool caches all global values (engine, h/t/m/decay, LPG, drive) and `NoteOn`/`Audition` rehydrate the voice from the cache before triggering.

### Seq-mode pads played Random-mode models instead of drums — FIXED

Regression from the per-mode slot array split: pads 3–9 in Seq mode fell through to the `current_mode` switch and read `pad_slots` (Random) instead of `drum_slots`. Now `seq_mode_on` routes pad touches to `trigger_drum()` (same punch/tightness/drive path as seq steps).

### SEQ recording mode — sound triggers continuously on entry — FIXED

Root cause: `triggers |= (1u << rec_slot)` in the seq tick ran every audio block (250×/s) instead of once per step. Now gated on `Sequencer::StepFired()` so the rec slot fires on 16th-step boundaries only. The entry audition voice is also skipped while the seq runs (it doubled the forced triggers).

### Rec audition fires only every ~3s on entry, until a knob picks up — FIXED

Reported 2026-07-03 20:52 (Seq + Random rec, seq paused): auditions came every ~8 LED blinks; moving S37 to min "fixed" the rate. Root cause: the retrigger was keyed on the knob-change flag, but `KnobPickup::update()` returns the caught **state** (level), not a change **edge** — before any knob picked up only the slow 750-block periodic path (3 s) could fire; after the first catch the flag was true every block and the fast path ran permanently. That's why touching S37 flipped the timing: min value crossed the stored decay and caught the pickup. Now a **fixed 500 ms audition pulse** from rec entry onward, whenever the seq isn't already force-firing the slot.

### Per-pad volume in Random mode — already existed, now audible — FIXED

S36 in recording already stored per-slot volume for Random pads (same as drums) and playback applied it — but auditions always played at full volume, so edits were inaudible until after confirm, which made the feature look absent. `AuditionWithParams` now takes the slot's stored volume: rec entry, the 500 ms pulse, copy confirmation, and drum pitch nudges all play at the slot's level.

### Electro pattern redesign — FIXED (2026-07-04)

Replaced the Anthony Rother table with a breaks-style bank transcribed from the 6-pattern web-sequencer sketch (chain 1,2,1,2,3,4,3,4,5,6,…). The 6 patterns = 2 kick variations (A: 0+6 / B: 0+6+10+13, bars alternate A B A B) × 3 intensity layers, encoded as weights so **density reproduces the original build**: d1 = kick+snare+OH, d2 adds broken 16th hats, d3 adds rimshot-style Perc 16ths, d4 adds ghost snares (GH row → weight-1 snare ghosts). Tom only in the bar-4 fill (added, not in source). Clap (added 2026-07-04): steps 5+13, **probabilistic** — first chance mechanism in the seq: xorshift32 in `Sequencer`, gate in `eval_step()` for genre 1 track 4 only, odds = margin of (weight+density) over threshold → 25/50/75/100%. Same gate can generalize later for the S32 density+chance axis. Old table in git history.

### SSD1306 128×32 OLED: display shifted/garbled + slow refresh — FIXED locally, upstream PR still open

Bring-up of the physical OLED (D11/D12 I2C, shared with the MPR121 touch bus — see `i2c1_lock.h`) surfaced two bugs in the vendored `lib/libDaisy/src/dev/oled_ssd130x.h`, not in our code:

1. **Wrong column-start address.** `SSD130xDriver::Update()` sent the page column-start command as `0x12` for `height==32` panels instead of the standard `0x10` (`0x10` = column 0; `0x12` = column 32 — the upper 4 bits of the "Set Higher Column Start Address" opcode). Every redraw wrote its 128-byte page starting 32 columns in, so the write wrapped: the first 96 intended columns landed 32 columns right of where they belonged, and the last 32 columns wrapped back around to the physical left edge. Visually: the screen's content shifted right, with garbled text at the left edge made of whatever the last ~32 columns of that redraw were (reported as e.g. a label reading "CF ARVIRTUAL ANALOG V" — "CF AR" was the wrapped tail, sitting in front of "VIRTUAL ANALOG V"). Fixed by using `0x10` for both cases (matches the `default:` branch already in the same switch, and every reference SSD1306 driver, e.g. Adafruit_SSD1306's page-mode `display()`).
2. **One I2C transaction per byte.** `SSD130xI2CTransport::SendData()` issued a full `TransmitBlocking` (START+address+ACK+STOP) for every single data byte — 512 transactions for one 128×32 frame (4 pages × 128 bytes), plus ~12 more for the per-page setup commands. The SSD1306 auto-increments its column pointer for every data byte that follows one `0x40` prefix within a transaction, so there's no need to restart the bus per byte. Batched into one transaction per page (128 bytes + the `0x40` prefix); cut a full-screen redraw from ~524 I2C transactions to ~16, and the estimated transfer time from ~30–40 ms to well under 15 ms.

Both fixes are committed *inside* the `lib/libDaisy` submodule checkout (commit `62ab175`, "Fix SSD1306 128x32: wrong column-start address + per-byte I2C sends") so this repo's submodule pointer pins to them, but **not pushed** — `lib/libDaisy` tracks `Synthux-Academy/libDaisy` upstream, which we don't have write access to. Worth a PR there since this affects any 128×32 SSD1306 use with their fork, not just this project's screen. `git log -p` inside the submodule at that commit has the full diff. **Filing the PR is the open item — tracked in `ROADMAP.md`.**

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

### Optimizations applied

1. **Voice memory SDRAM → internal SRAM** — scratch (16KB/voice) + impl (~9KB/voice) were in external SDRAM; physical-modeling engines walk those buffers every sample, so SDRAM latency was the likely dominant cost. Now in D1 SRAM (25% of 512KB used; 7 voices would be ~40%).
2. **Voice sleep** (`VoicePool::Render`) — a voice whose output stays below −80dBFS for 32ms with its gate off stops rendering until the next trigger. Idle cost collapses; drum-seq voices are free between hits. Gate-held voices never sleep, so holding a pad on a silent Six-Op region and sweeping knobs still brings the sound in. Quirk: Chiptune (7), the gateless arpeggiator, is silent until first pad trigger and never sleeps afterwards.
3. **-O3** for project + Plaits sources (`OPT = -O3` in Makefile; libdaisy.a unaffected). Binary 275→301KB.

### Remaining levers if still short

1. **ITCM placement** — code executes from QSPI flash (slow on I-cache miss); ITCMRAM (64KB) is 0% used. Move the hottest Plaits render paths there. *(carried forward — see ROADMAP.md Parking Lot)*
2. **Weighted polyphony** — per-engine cost table caps concurrent expensive engines.
3. Sample-rate drop to 32kHz — last resort, audible.

### Bumping kVoices

Two constants must move together: `VoicePool::kVoices` (voice_pool.h) and `kMaxVoices` (plaits_voice.cpp). Steal logic and SRAM budget scale fine to 7. *(carried forward — see ROADMAP.md Parking Lot, "Expand voice pool to 7")*

### MIDI cost estimate

MIDI itself is **CPU-negligible** (<1%): even a dense DAW stream is a few hundred 3-byte events/sec against a 480MHz core; parsing happens in the main loop, not the ISR. The *real* cost of MIDI is polyphony demand — chords from a DAW want 6+ voices, which is why voice expansion comes first.

**One hardware constraint:** USB serial logging and USB MIDI share the USB port (that's what the existing `#ifndef USB_MIDI` guard is for). Measure CPU first, then flip to MIDI — or use TRS MIDI (UART) on modded boards to keep both.

### MIDI mapping sketch

Superseded by the actual implementation below — kept for the reasoning trail.

- **CCs map to functions, not pots** (pots are reused across modes): e.g. CC20 harmonics, 21 timbre, 22 morph, 23 decay, 24 drive, 25 LPG, 26 volume, 27 tempo, 28 shuffle, 29 density, 30 punch, 31 tightness. A CC write updates the stored value and re-arms that pot's pickup — the pot must cross the value to take over. The pickup infrastructure from the seq knobs generalizes to this.
- **Notes, channel split:** ch1 = pitched (note number = pitch directly, bypassing pad scale logic, into the current mode's sound); ch10 = drums, GM mapping 36→Kick, 38→Snare, 42→CHH, 46→OHH, 39→Clap, 41/45→Tom, 37/56→Perc.
- **Drum pitch over MIDI:** phase 1 — GM note selects the slot, the slot's stored pitch plays (matches pad behavior). Phase 2 — notes within ±6 semitones of the slot's GM anchor play the slot transposed by the offset, so kits become playable chromatically without giving up the one-pad-one-sound model.
- **Mod pads as MIDI:** P0–P11 states could mirror to notes on ch16 (or CC64+) for remote triggering of combos; low priority.

---

## MIDI implementation (2026-07-07) — phase 1 + CC map, both transports

`midi/midi_io.{h,cpp}` owns the transports; TouchPlaited.cpp owns the handlers. Shipped and in daily use; MIDI clock DAW-sync is the one piece still on `ROADMAP.md`'s open-verification list.

### Transports
- **TRS/DIN (always built):** `MidiUartHandler` defaults = exactly the mod's wiring: USART1, **D13 TX / D14 RX**, 31250 baud. Initialized unconditionally — an idle UART is free, so unmodded boards lose nothing.
- **USB device (default build):** `-DUSB_MIDI` in the Makefile. That same define suppresses `startLog` and the CPU print (the pre-existing guards) — one USB port, one owner. Comment the define out for a measurement build; TRS MIDI keeps working there.

### Threading model (the part to remember)
- **In:** transports parse in their own IRQs into per-handler FIFOs. `MidiIO::Service()` (main loop) pops events and runs the handlers inside a short `__disable_irq()` section — handlers write pool/pickup state the audio ISR owns. Never pop in the audio ISR: FIFO push (UART IRQ) vs pop (audio ISR) can tear.
- **Out:** UART TX is blocking `PollTx` — ~1 ms per 3-byte message, a quarter of the block budget — so the ISR never sends. Pad/seq events push into a 64-deep single-producer ring; `Service()` drains ≤4 messages per call to both transports.
- **Main-loop latency:** the LED helpers block for up to seconds (`blink_numbered`), which would starve MIDI both ways — all LED delays now go through `delay_serviced()`, which services MIDI every 1 ms. MIDI jitter is therefore ~1 ms plus at most one in-flight TX.

### In mapping (phase 1, as shipped)
- **ch1 notes:** note number = pitch (scale/octave/root bypassed). Voice slot id = 32+note (pads 0–6, drums 16–22 — no collisions), so NoteOff matches exactly and chords steal like pads. Velocity → `p.volume`, linear. Sound = current mode: BP live = eff params (globals keep refreshing held notes, knobs stay live); Random / BP-snapshots = `slots[note % 7]` — stable multi-timbral cycling per key. In Seq mode ch1 plays the last pitched mode's sound (synth over the drum machine).
- **ch10 drums:** GM → slot with aliases (35/36 kick, 38/40 snare, 42/44 CHH, 46 OHH, 39 clap, 41/43/45/47/48/50 tom, 37/54/56/75/76 perc); unknown notes ignored. Phase 1: slot's stored pitch. Kit auto-generates if not ready. Velocity scales the slot volume via a new `trigger_drum(i, vel)` arg.
- **CC20–31 (any channel):** 20 harmonics, 21 timbre, 22 morph, 23 decay, 24 drive, 25 LPG colour, 26 pitched volume, 27 tempo, 28 shuffle, 29 density, 30 punch, 31 tightness. Every CC write re-arms the corresponding pot pickup. CC24 drive sets **both** pitched drive and `seq_drive_lk` (one function, two stored values). CC25 is the only LPG writer (pot retired; was hardcoded 0.5). CC27–29 also push into the Sequencer directly so tempo/shuffle/density respond while it plays in the background of a pitched mode.
- **CC85–88 — FX mirrors (2026-07-09):** 85 reverb pitched, 86 reverb drums, 87 delay pitched, 88 delay drums. Same center-off mirror encoding as the P1 knob layer (`fx_decode`); each write also sets the shared `*_char_lk` (last edit wins, like the knob). The pot layer is mode-dependent (writes the active group) but per the functions-not-pots rule each group gets its own CC — automate drum sends from a pitched mode. A CC write re-arms `fx_mc_rev`/`fx_mc_dly` at the current pot value, so a caught pot must be nudged ~3% again to take back over mid-P1-hold. 85–88 chosen because 32–63 are the 14-bit LSB pairs for 0–31 (a DAW sending 14-bit CC20 would hit CC52) and 64–84 have defined meanings; 85–90 are undefined. Per-slot rec trims stay pot-only — no sane way to address 7 slots × 2 effects in one CC byte.

### The eff_* layer (CC ↔ pot arbitration)
The pitched timbral knobs were raw per-block reads, so a CC write would have been stomped one block later. New layer: `eff_h/t/m/d/drive` = what actually sounds. Pot feeds eff through `cc_pu_*` pickups (force-caught at boot = pots live); a CC write sets eff and re-arms, pot must cross to take over — same rule as every mode hand-off. `last_*` stay raw pot reads because the BP snapshot escape watches pot *movement* (a CC write must not fake a grab). All sound-generating sites (BP live setters, soft-random anchors, stage-3 restore, model-select regen) now read eff_*.

### Out
- Pads in pitched modes → ch1 NoteOn/Off vel 100; the sent note is remembered per pad (`midi_pad_note_out`) so the Off matches even if octave/root moved mid-hold, and it fires even if rec mode swallowed the release.
- Seq steps + Seq-mode pad hits → ch10 GM one-shots (`kDrumSlotGm` = 36/38/42/46/39/45/37), NoteOn+NoteOff queued together.
- No CC out; auditions not sent. Incoming notes/CCs are never echoed — only clock and transport pass through (below).

### Clock + transport (2026-07-08) — hardware test pending, see ROADMAP.md
- **External clock in (F8 on either port) = hard tick sync**: `Sequencer::SetExternalClock` switches `Tick()` to consuming received ticks (6 per 16th step, `OnMidiClock` from the handler with IRQs off, `ext_ticks_pending_` volatile). No BPM estimation, no drift; shuffle quantizes to whole ticks (0–3 at max swing). The tempo knob and CC27 go inert automatically — the block counter isn't consulted — and still hold the fallback tempo. Ticks only count while playing, so they can't pile up during Stop and burst on Resume.
- **Clock detection**: first F8 flips `midi_ext_clock`; 500 ms without one (checked per block in the ISR) reverts to internal clock at the knob tempo.
- **Transport in**: FA = `seq.Start()` (step 0 fires on the next tick, per spec; kit auto-generates), FB = `seq.Resume()`, FC = `seq.Stop()`. Works with both DAW styles: clock-always (FA arrives inside a running tick stream) and clock-while-playing (FA may precede the first F8 — seq starts internal, first tick re-phases).
- **Clock out**: when external clock is present, incoming F8/FA/FB/FC pass through to the output. Otherwise TouchPlaited is master: `Sequencer::MidiClockTick()` emits 24 ppqn on the step clock's own timebase (`step_blocks_/6`, fractional accumulator — average rate exactly matches the drums, so synced gear can't drift), continuously from boot, phase-reset on Start/Resume so tick 1 lands with step 0. Local start/pause sends FA (SW2 first entry, kit regen) / FB (P2+P11 resume) / FC (P2+P11 pause) — suppressed while following an external clock.
- Realtime bytes are 1-byte queue entries now (`OutMsg{len, b[3]}`); ~48 msgs/s at 120 BPM ≈ 1.5% UART duty, nothing.
- libDaisy parser caveat: a realtime byte interleaved *mid-message* aborts that message (parser limitation). USB MIDI can't interleave (4-byte packets); TRS from typical interfaces inserts between messages — watch for dropped notes under heavy TRS clock+notes if it ever comes up.

---

## Reverb / delay FX send — resource analysis (2026-07-08)

Question: is there room for a reverb and/or delay, ideally as a per-slot FX
send (drums dry-ish while the seq runs, Basic Pitch with a long tail)?
Answer: **memory is a non-issue, CPU fits on average but eats into peak
headroom** — pair it with the ITCM move. This analysis is what got built on
the `FX` branch (2026-07-09), see the implementation section below.

### Memory budget (measured from build/TouchPlaited.map, current build)

| Resource | Total | Used | Free |
|---|---|---|---|
| QSPI flash (code, BOOT_QSPI) | 7.75 MB | ~318 KB | ~7.4 MB |
| AXI SRAM D1 (voices live here) | 512 KB | ~260 KB | ~250 KB |
| SDRAM (external) | 64 MB | **0 bytes** | 64 MB |
| ITCM RAM | 64 KB | 0 | 64 KB |

SDRAM has been completely empty since voice memory moved to internal SRAM.
Reverb/delay buffers are the textbook use for it: unlike the physical-model
engines that thrashed SDRAM per-sample from multiple voices, a delay line is
one sequential read + write per sample — cache-friendly. DaisySP `ReverbSc`
(~390 KB buffer) is normally placed in SDRAM on Daisy anyway; seconds of
stereo delay are a rounding error there. A hand-rolled Dattorro / small FDN
would even fit in the free internal SRAM if SDRAM latency ever shows up.

### CPU budget

Baseline at 6 voices (2026-07-03 measurements): idle ~3%, heavy 50–70% avg,
windowed max 96–98%, shed guard at 90%, rare 111–115% single-block spikes
absorbed by DMA double-buffering.

- Stereo delay: ~1–2% per block.
- ReverbSc-class reverb: ~6–10% per block (SDRAM-resident).

FX render **every block** regardless of voice activity — a fixed +8–12% on
top of every block, including the worst-case ones already touching 100%. So:
works today, but expect `shed N` to climb during dense seq + busy Random
moments (earlier tail fades — the guard already keeps that inaudible).
Levers to buy the headroom back:

1. **ITCM placement** (already on the optimization list, 64 KB at 0% used) —
   moving the hottest Plaits render paths out of QSPI likely recovers more
   than the reverb costs. Do this alongside or before the FX.
2. **FX sleep**, same trick as voice sleep: skip the reverb render when the
   send bus has been silent and the tail has decayed below threshold — keeps
   idle at ~3% instead of ~13%.

### Per-slot send — design sketch (fits the existing architecture)

`VoicePool::Render` already applies per-voice `volume`/`blend`/`width` with
per-group (seq vs. pitched) multipliers — an FX send is the same pattern:

- Add `send` to `PadSlot` / `VoiceParams` (+ `voice_send[i]` in the pool);
  accumulate `l * send` into a second stereo bus inside the same render loop.
- Multiply by per-group send levels (`send_seq`, `send_pitched`) mirroring
  `vol_seq`/`vol_pitched`. That gives the target behavior directly: drums at
  zero-to-slight send in Seq, pitched group cranked with a long tail in Basic
  Pitch — independent state that survives mode flicks, like the volume/width
  pairs.
- In `AudioCallback` after `pool.Render`: run the reverb on the send bus, sum
  into `left`/`right` **before** the soft-clip.
- Per-slot send edits in recording mode (like drive/volume now); randomize it
  in the kit/slot generators.

Open design decision (resolved on the FX branch below): one shared reverb
with per-group send levels (cheap — chosen) vs. different reverb *character*
per mode (short room for drums, long hall for pitched, which means either
morphing on mode switch or doubling CPU).

---

## Reverb / delay FX — implementation (2026-07-09, `FX` branch)

Follows the analysis above; deltas and decisions. Shipped; hardware
verification of levels/tapers/character and `shed N` behavior under load is
tracked in `ROADMAP.md` Priority 3.

### Controls — mirror knobs on the P1 layer

P1+S30 = reverb, P1+S35 = delay. Both are **mirror knobs**: center = off
(±0.06 dead zone), each half is a character, wet grows outward. Chosen over
zone/preset splits for consistency and generous wet travel per character:

- Reverb: left = **room** (krt 0.35–0.6, lp 0.45), right = **hall**
  (krt 0.75–0.95, lp 0.80). Tail opens slightly as wet rises.
- Delay: left = **slapback** (fixed 120 ms, fb 0.05–0.30, bright), right =
  **synced dotted 1/8** (= 3 seq steps, fb 0.2–0.7, dark). Time changes slew
  per-sample (~70 ms τ, tape-style bend); snap while the delay is asleep.

Wet levels are **per-group** (`fx_rev_seq_lk`/`fx_rev_pitched_lk`, same for
delay) with mode memory, mirroring the volume/width pairs; sends use a
squared taper. The FX **character is shared** — last edit from either group
wins (one instance can't be room and hall at once; accepted, documented).
P1 edits use MoveCatch (crossing pickup against a mirror encoding felt dead);
on release, S30/S35's bare roles re-arm their pickups (drive: `seq_pu30` /
`cc_pu_drive`, pattern: `seq_pu35`) so nothing jumps. FX edits disabled while
recording (rec owns S30) and under P0/P2 (model select owns S35). Not
reachable over MIDI at ship time (candidate CCs added later — see the
CC85–88 entry above).

**Conscious reversal** of the parking-lot "P1 never held" ergonomics note:
an FX level is set-and-forget, not performative. Fallback if P1-hold proved
awkward on hardware was bare S35 as pitched send (not needed — P1 held up).

### DSP — no new dependencies

- **Reverb**: Rings' `dsp/fx/reverb.h` (Griesinger/Dattorro: 4 input APs +
  2×(2AP+delay) loop, MIT) adapted in `synth/fx.cpp` onto the **already
  vendored** `plaits/dsp/fx/fx_engine.h` — identical base class, Rings also
  runs 48 kHz, so delay-line sizes carry over unchanged. 32768×uint16 buffer.
  Much cheaper than the ReverbSc estimate (~2–4% vs 6–10%).
- **Delay**: hand-rolled cross-feedback (ping-pong flavored) stereo line,
  one-pole damping in the loop, linear-interp fractional read. 2×64k floats.
- Both TUs keep Plaits/stmlib headers confined to `fx.cpp` behind `fx.h`
  (same leak rule as plaits_voice).

### Plumbing

`VoicePool::Render` grew two send buses (reverb/delay stereo pairs) filled
per-voice post-volume/width by group send levels (`rev_send_seq` etc.) —
voice-sleep peak still measured pre-volume, unchanged. `AudioCallback`
renders FX returns into the mix **before** the soft-clip. `Sequencer` exposes
`StepBlocks()` for the synced time (internal/knob tempo only — external MIDI
clock rate is not measured; synced delay follows the knob fallback tempo).

### Memory / CPU (measured at build)

SDRAM 0 → **576 KB** of 64 MB (reverb 64 KB + delay 512 KB); SRAM unchanged
(~50%); QSPI +8 KB. **FX sleep** implemented as planned: each FX skips its
render once input *and* tail are silent — reverb after >1 loop period
(~380 ms), delay only after a full delay-time of silence so a stale tail can
never replay on wake. Idle cost stays ~3%.

### Open on hardware

Levels/tapers (send taper, return gains, feedback ranges), character params
by ear, `shed N` behavior under dense seq + Random with FX cranked, P1
reachability. Future: per-slot send in Recording, send randomization in kit
generators, shimmer as an alternate right-side reverb character (needs
Clouds' pitch shifter port), ITCM placement if peaks pinch.

---

## Design sketch: "Playmode overhaul" → Arp/Mel (original, 2026-07-16 & 2026-07-20)

The original design musing that became the Arp/Mel playmode. Both rounds
below are fully implemented — the resolved design record, conflict
analysis, and phase-by-phase implementation log are in
`notesarchive/arp-mel-plan-archive.md` §6–§9; the current user-facing
behavior is in `MANUAL.md`. Kept here verbatim as the original sketch.

### Round 1 (before hardware, entry point for the branch)

instead of Random we'll introduce ARP / MEL

So we'll have
1. Basic Pitch
2. Arp / melody
3. Sequencer

2. is new, we'll reuse as much logic as possible, keeping controls tied to the other playmodes, using the catch up logic for pots and switches

There's an Arp mode and a rec mode, SW1 handles three states: Arp, Arp hold, Rec mode

Controls will be:

S30 - Drive (applied only to arp)
S31 - Division (for arp mode, can also change speed of recorded mode)
S32 - Swing
S33 - Density - first half = 0-100% Euclidean fill + chance, second half is Euclidean fill (meaning second half will generate a steady pattern)
S34 - Decay
S35 - Arp order (does not apply to Rec)
      - played order, ascend, descend, pingpong, random (random is default)
S36 - Normal Mix like in basic pitch
P0 / P2 + S35 = change model

Arp modes from sw1
- Arp: dynamically adds notes to the arp as long as they're held
- Arp: hold notes, toggle to remove from arp pool

  - Octaves: P10 / P11 change base octave for the playing notes
    - Use P0 + P10 / P11 to add / subtract a range of octaves

Problems to decide:
  - clear an arp by leaving arp mode or by holding P0 + P2
  - Otherwise arp keeps playing when sequencer is running
  - if you want to play only the arp without seq, what should we do?
  - tied to play / stop? always running? what's the general approach here
  - sequencer running in all modes P2 + P11 is start sequencer, we could also do it per mode, or use P2 + P10 for arp , P2 + P11 for sequencer

Recording:
- As soon as SW1 is in REC it starts listening to the pads
- we record in layers
- undo allows going back layers
- max layers?
- Octave mode apply to live notes or to recorded seq?
- what's the influence of the controls over the recording?
  - S30 drive = ok
  - S31 devision > not applied from the start, after knob is touched can spread recording, center is normal speed, left slower, right faster
  - S32 Swing = yes?
  - S33 density = > not applied from the start, after knob is touched can add chance left 0 % chance, right 100 %
  - S34 Decay = ok, move while recording to change the decay per note
  - S35 Order = use this to shift timing; center original timing, left move back, right move forward

### Round 2 (20/07/2026, follow-up before Phases 6–16)

- Each playmode has its own volume
- Each playmode has its own send to FX

  - REC is considered seperate VOL and FX

- fully Detach sound model from playmodes Pitch, arp and REC mel
  - each starts with a random model at boot, they are no longer linked

- Arp and Mel Rec should both follow the setting of Basic Pitch, meaning that when scales are set they adhere to that setting and map to nearest.
  - Setting scales and setting the root note can only be done in Basic Pitch
  - octaves are independently adjustable per ARP, REC and Basic Pitch

Rec melody:

- method to start stop the rec melody:

If P2 + P11 is start/stop for the SEQ, and P2 + P10 is to start stop the arp mel rec mode, this should work per arp and melrec
The layer on / of switches should be P2 + P3-P7 > one more layer than currently built in
  - clearing layers = hold P2 + P3-P7
  - clearing all layers = hold P2 + P3/P7 (any combo of P3 - P7, at least two + the mod)

In Rec mode the adjustable settings are:
- s30 drive
- S31 decay
- S32 speed playback of the recorded bars 0% is 1X, 100% is 8X
- S33 shift all recorded bars (in full) in time, allow shifting smaller than a step (e.g. 1,2,3 - 16 could become 14,15,16,1,2-13)
- S34 Chance (global for all rec notes)
- S35 Order = 2 options: left of 50% for original, right of 50% for randomized order

---

## Design sketch: "Syncing" → CV clock in/out (original, 2026-07-21)

Implemented: S43 rides the knob ADC scan (9th channel, raw/unsmoothed) with
a software Schmitt trigger polled once per block; 1 pulse = one 16th step,
multiplied to 6 synthetic 24 ppqn ticks down the existing MIDI-clock path
(period-measured, phase snapped to every edge). Hierarchy: MIDI F8 outranks
CV (higher resolution + transport); CV stays measured in the background and
takes over within one pulse if MIDI goes silent 500ms; CV itself times out
after ~2.5 missed pulses → knob tempo. Forwarding both ways: CV in → F8s on
MIDI out, MIDI in (and the internal clock) → ~12ms pulses on S40 (GPIO D25),
phase-anchored to Start. CV clock carries no transport — P2+P11 stays local.
See `MANUAL.md` "Clock sync — MIDI and CV". **Hardware-verified 2026-07-24**:
thresholds work correctly against the jack conditioning, and MIDI-vs-CV
priority handoff/timeout behave as designed. Pin choice (A11 in / D25 out)
wasn't deliberate — just the first free pins taken in order — so `MANUAL.md`
now flags this for anyone wiring their own jacks (see its *Hardware mods*
section).

Original sketch:

Let's also listen for a clock signal on analog inputs for when a jack is connected to: S43 and to S40 for also sending out a clock signal.

S43  clock in: Daisy pins D28	A11
S40 clock out: Daisy pins D25	A10

---

## Visualizer mobile UX musing (23/07/2026) — resolved same day

Written mid-day 2026-07-23, resolved by two commits later that same day:
`53ad1de` ("Visualizer OLED screen: on-faceplate display replaces the
overlapping callouts") and `866a056` ("Physical OLED: 128x32 hardware UI
mirroring the visualizer"). All four points below were addressed: the device
drawing got a drag-grip handle (top-left, clamped on-stage) with A−/A+ label
and screen text sizing shared with the info panel; the double-click-to-reset
gesture (which collided with pad double-taps) was replaced with an explicit
reset button; the overlapping dynamic labels were replaced by an OLED-style
on-faceplate screen carrying status + the last three actions; and the "small
128px OLED" idea was built as actual hardware the same day (see the SSD1306
entry above for the driver bugs found along the way).

Original text:

The visualizer
- flow (on mobile especially) is not optimal. The drawing of Simple touch: a double click resets its position, but when using the pads and "double clicking" that to play that also resets the postion.
- fitting everything in one view is proving to be hard on mobile, readability of the labels becomes edgy when you fit both the device drawing and the info screen.
- not that much can be done to optimize giving the limited screen space, though i'd suggest these features to start:
1. give the device a draghandle (top left corner)
2. give it similar `a A` icons like the info panel to allow setting the label size, as well as the dynamic labels

The dynamic labels that follow the actual knob positions, though informative, when using multiple they do tend to overlap.
  - possible solutions: move them up / down when overlapping
   - make one "screen" in the middle of the faceplate, and use an oled type of info screen that displays the extended info of the last used knobs, e.g. have two or three lines to allow multiple lines

I've been contemplating using one of these small oled screens (128px) but our screen should use the optimal available space for now.

---

## Priority 1 — Six-Op gate fix, full write-up (moved from `ROADMAP.md` 2026-07-24, all hardware-verified)

Root cause of "Six-Op nearly silent / every second press dead" found: every
note was a 0.5 ms trigger pulse (`PlaitsVoice::Render` auto-zeroed
`modulations.trigger` after each block), and the six-op engines are the only
ones with real DX7 key-on/key-off gate semantics — worse, their two staggered
FM voices only saw the one-block gate on every other press. The DX7 factory
banks were never the problem (all three are compiled in and load correctly).
See "Six-Op: silent/alternating pad triggers" above for the narrative
version; this is the full step-by-step record.

- Step A — real gate in `PlaitsVoice`: trigger stays high while the pad is
  held; retriggering a still-high line (voice steal, one-shot repeat)
  inserts one forced-low block so Plaits still sees a rising edge.
- Step B — one-shot gate timer in `VoicePool`: drum-seq triggers and
  auditions never get a NoteOff, so their gate drops after a decay-scaled
  hold (20–400 ms). Inert for non-FM engines (only six-op reads gate
  length).
- Step D — Six-Op level pad (added 2026-07-10 after Step A/B confirmed
  working on hardware): with real gates the engine turned out to be the
  hottest in Plaits — registered at out_gain 1.0 with the LPG bypassed
  (`already_enveloped`), its internal soft-clip pinning dense DX7 patches
  at full scale for the whole gate, vs. 0.6–0.8 through a decaying LPG
  for every other melodic engine. Polyphonic sums saturated the master
  soft-clip even at low volume. Host-side pad on engines 2–4 in
  `PlaitsVoice::Render` (vendored Plaits untouched per thirdparty
  policy). Started at ×0.45, then ×0.35 (still hot — the 2 dB step was
  too timid), settled on **×0.20** (−14 dB vs. unity; sustained tones read
  much louder than the other engines' decaying LPG plucks, so the
  comparable level is lower than gain math suggests). By-ear starting
  point, tuned on hardware.
- Step E — Six-Op note-start anti-click (2026-07-10): stolen/reused
  voices still carry the previous tail when the engine resets operator
  phases / loads a new patch at key-on → discontinuity click. Plaits
  only sees our edge `kTriggerDelay` (5) blocks late, so the tail-only
  window is masked in `PlaitsVoice::Render`: fade-out on the edge block,
  mute the 5 stale blocks, one-block ramp-in exactly where the key-on
  lands (~3 ms total, DX7 attack character untouched). FM engines only —
  drum transients stay raw.
- Step F — Six-Op patch-index LED blink (2026-07-10): S32 preset zone
  changes (32 per bank, mirroring the engine quantizer with a
  15%-into-zone guard) fire the existing short MODEL blink. Basic Pitch
  live path only; armed silently on engine entry.
- Step C — hardware verify (confirmed 2026-07-24): every press fires (no
  alternation); Six-Op pads/organs/strings sustain while held and release
  on lift; MORPH envelope stretch audible on held notes; seq FM drums
  decay and voices still sleep (CPU meter); rec auditions keep their
  500 ms cadence; voice stealing retriggers cleanly (fast playing past 6
  voices); Six-Op level sits comfortably next to other engines, chords
  don't squash; note starts click-free incl. fast re-presses and S32
  patch changes mid-tail; S32 blinks once per preset zone while browsing,
  no chatter when the knob rests on a boundary.
- Step G — Six-Op joins the unified Decay (2026-07-18): `decay_via_morph`
  (ex `morph_is_decay`) now includes 2–4, so S31 Decay drives the DX7
  envelope time via MORPH (the LPG is bypassed there and the real decay
  param was dead) and S34 goes inert — same story as 19–23. The
  visualizer gained per-engine knob labels (`ENGINE_KNOBS` in
  controls-meta.ts, mirrored predicate) and reports unassigned knobs as
  "no effect on <model>". Hardware-confirmed 2026-07-24: S31 sweep on a
  Six-Op patch audibly shortens/stretches the envelope; S34 confirmed
  silent.
- Step H — app info-screen round (2026-07-18): S37 blend/width labeled
  dead on Six-Op (their `aux[i] = out[i]` — AUX identical to OUT);
  telemetry adds `rec slot` to STATE and a KIT frame (7 × engine/h/t/m/d/
  note, ≤10 Hz + 2 s heartbeat) so the app labels rec-mode knobs with the
  edited slot's engine and shows a collapsible model section in the info
  panel (per-engine functions + live values, Six-Op patch x/32, chord
  names; Seq view lists the whole kit). Reflashed and webapp-checked,
  confirmed working 2026-07-24.
- Step I — hardware feedback round (2026-07-18): STATE mode-flags bit 3
  exposes the Arp/Mel sound-edit layer, so the app swaps to engine labels
  there like it does in Seq rec; info panel grip now resizes the box
  (wrap + scroll) with separate A−/A+ font buttons, dblclick resets all;
  kit shown as an aligned grid with h/t/m/d/note columns. Fix: arp
  Density (S33) felt dead — its pickup was armed to the 1.0 boot default,
  practically unreachable at the pot rail; KnobPickup now also catches on
  ~3% movement when armed to a rail target (same idea as the width
  MoveCatch). Applies to Density/Order and any future rail-stored value.
  Also: the Arp/Mel base layer moved Decay to S31 (the unified Decay knob,
  matching every other mode) — Division/Swing/Density shifted one knob
  right to S32/S33/S34, Order stays on S35. Firmware, app labels and docs
  updated together.
- Step J — app UI round 2 (2026-07-18, webapp only, no reflash): model
  section collapse fixed (the panel-drag pointer capture swallowed the
  header click); the section is now a full knob map — all S30–S37 with
  their current function and engine-aware values in every mode (Seq adds
  BPM on S31 and the kit grid below); hovering a row highlights the
  control on the drawing; new label-overlay cycle button (dyn / S# / Aa):
  static designators or full faceplate-style labels in Sofia Sans Extra
  Condensed (Google font, loaded in index.html), pads showing roles (Seq)
  or live note names (pitched). Callouts stay on top in all modes.
  Feedback round (2026-07-19): static labels re-render when the drawing
  is dragged/zoomed (layout.ts fires `tp-panel-layout`, rAF-throttled);
  knob designators moved inside the caps; label font bumped (0.72 ×
  knob-radius px, clamp 8.5–22); arp Order callout names its setting
  (Played/Up/Down/Ping-pong/Random, mirroring arp.h SetOrder).

Follow-ups (FM velocity via `level_patched=true` + per-note level on
engines 2–4; revisiting the `kSixOpAud` anchors/random ranges now the full
banks speak) were scoped but dropped from active `ROADMAP.md` tracking on
2026-07-24 as low-priority — worth revisiting if the Six-Op banks get
another pass, but not tracked as a standing item for now.

---

## Reverb/delay FX send — implementation + hardware verification (moved from `ROADMAP.md` 2026-07-24)

Implemented on the `FX` branch (2026-07-09), hardware-verified 2026-07-24.
Design record above → "Reverb / delay FX — implementation". P1+S30 =
reverb, P1+S35 = delay, both mirror knobs (center off, left/right =
character, wet grows outward): room|hall, slapback|synced dotted 1/8.
Per-group sends (drums vs. pitched) with mode memory; shared character from
the last edit. Reverb = Rings' Griesinger/Dattorro topology on the vendored
Plaits `FxEngine` (64 KB SDRAM); delay = cross-feedback stereo line (512 KB
SDRAM). FX sleep keeps idle cost at zero. Confirmed on hardware: levels/
tapers, character params, CPU under dense seq + Random (`shed N` drift), P1
reachability in practice. Remaining ideas (per-slot send edits in
Recording, send randomization in kit generators) were dropped from active
`ROADMAP.md` tracking on 2026-07-24 as low-priority; ITCM placement itself
stays tracked in `ROADMAP.md` Parking Lot (enabler for the FX send and a
7th voice generally, not FX-send-specific).

## P1 hold ergonomics — resolved 2026-07-24

The `FX` branch (2026-07-09) spent hold-P1 + S30/S35 as the FX knob layer,
consciously reversing an earlier "P1 never held" note — an FX level is a
set-and-forget edit, not a performative gesture. Ergonomics were flagged as
needing hardware confirmation, with a bare-S35-as-pitched-send fallback held
in reserve. Resolved 2026-07-24: keeping P1 as-is — reach isn't a problem
for most users, and for anyone it is, there's a hardware fix (a short
solid-core wire through the P1 pad's hole, optionally with foil/copper tape
on the touchplate for a bigger target — see `MANUAL.md` *Hardware mods*, "P1
pad reach") rather than a firmware fallback. The bare-S35 fallback and the
other shelved ideas (all 24 engines on bare S35 — jitter-prone, destructive
in Random; P1 + P10/P11 model ±1 stepping) are dropped for good.

## Pattern editor UI — brainstorm resolved (moved from `ROADMAP.md` 2026-07-24)

`tools/pattern_editor.html` UI/UX brainstorm carried over from
`tools/editornotes.md`, mostly resolved by the e2703c4 "DAW-style grid"
rework (2026-07-16) and later passes. All items closed as of 2026-07-24:

- Track order — Kick now sits at the bottom row, matching DAW piano-roll
  convention.
- Block-split layout — solved by switching to a scroll layout with a
  better-condensed look, rather than re-deriving pinned label positions on
  wrap/zoom.
- Editor layout direction — settled on the 4-block-split-plus-parking-pool
  approach.
- Cell-size/overlap bug (columns 5, 9, 13) — fixed.
- Row-edit tools scope (density/chance sliders touching every cell instead
  of just filled ones) — fixed.
- Per-cell/per-block lock icon (free / position-locked / density-
  chance-locked) — implemented.
- Selection & copy/paste scheme (right-click select, left-click drag move,
  arrow-key nudge, copy) — implemented.

No open follow-ups from this brainstorm. New ideas may surface once the
manual pattern-authoring pass (`ROADMAP.md` Priority 2) is underway with
real use of the editor — those will be logged fresh when they come up.
