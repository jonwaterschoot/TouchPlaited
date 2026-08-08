# TouchPlaited — Working Notes

The freeform design record: *why* things are the way they are, and the
reference material that isn't obvious from the code. It is **not** the TODO
list — `ROADMAP.md` is the single owner of future work — and it is **not** the
control reference: `MANUAL.md` is, and has been since long before this file
stopped tracking the panel. Resolved/historical material moves to
`notesarchive/` rather than being deleted (`notesarchive/readme.md` tells the
whole story).

**Swept 2026-08-06.** The v1-era control reference this file used to open with
described a device that no longer exists (SW2 center as *Random mode*, S34 as
Kick punch, Decay on S37) — that, the build-out status table and the per-role
drum-pool tables are now in `notesarchive/notes_archive_2026-08.md` →
"notes.md sweep (2026-08-06)". What's left below is what's still true and
still useful.

---

## Hardware reference

**SW1 (left switch) — Scale / Genre / Arp sub-state**
- PCB labels: S10 / S9; Daisy pins: D9 / D8
- Code: `touch.switches().B()` → `_switch_9_10`
- Positions: center = Chromatic / Techno / Arp · left flick = Minor / IDM ·
  right flick = Major / Electro
- Change-latched **per role**: a flick only takes effect in the mode you're
  in, which is why `t.sw1_latch` (state byte 37) exists and why the screen
  marks a diverged lever with `*`

**SW2 (right switch) — Playmode**
- PCB labels: S7 / S8; Daisy pins: D7 / D6 — polarity inverted vs. label
- Code: `touch.switches().A()` → `_switch_7_8`; sw=2=Down, sw=0=Center, sw=1=Up
- Positions: down = Basic Pitch (1 blink), center = Arp/Mel (2 blinks),
  up = Seq (3 blinks)

**Switch files** kept as TouchBass template — no modifications. (SW1/SW2 were
briefly non-independent; the fix was restoring those templates, D10 was the
wrong pin.)

**Optional mod pins:** OLED (SSD1306, I2C) on D11/D12, sharing the MPR121
touch bus — see `i2c1_lock.h` for the arbitration. CV clock in on A11 (S43),
out on D25 (S40). Both documented for users in `MANUAL.md` → *Hardware mods*.

---

## OLED redraw cost — the I2C4 experiment and what it actually found (2026-08-06)

Question was whether the display deserves its own I2C bus, at the cost of TRS
MIDI (USART1 wants D13/D14, so the two cannot coexist). Built it, rewired it,
measured it: **no observable difference, reverted.** The interesting part is
why, because it moves where the next effort should go.

### What was measured

Branch `oled-i2c4-no-trs` (kept for the trail; the bus move is not merged).
OLED on I2C4 / D13-D14 at 1MHz, interlock compiled out, TRS MIDI dropped.
Boot animation, progress bars and general use looked **identical** — the one
thing the change was meant to improve, progress-bar smoothness, did not move.

Serial capture, idle → simple synth → 6-op FM poly → + seq + FX → idle:

| CPU avg | `oled max` | main loop's share | implied bus time |
|---|---|---|---|
| 15% | 6268 µs | ~85% | ~5.3 ms |
| 88% | 43920 µs | ~12% | ~5.3 ms |

Peak observed: `max 239570us` — a 239 ms frame.

### The finding

The bus change worked exactly as designed and was irrelevant. A four-page
frame is ~5.3ms of bus time at ~886kHz (vs ~11.5ms at 400kHz), and that
prediction matches the idle measurement to within a few percent, so I2C4 at
1MHz was genuinely running. But **frame latency is dominated by audio-ISR
preemption, not by the bus**: the transfer runs in the main loop, which at 88%
audio load gets an eighth of the wall clock. Doubling bus speed sped up 5ms of
a 125ms round trip. Invisible, exactly as it felt.

Frame rate under load was ~16 frames per 2s ≈ 125 ms/frame ≈ 80 ms throttle +
~45 ms stretched transfer. Neither term is bus speed.

### What was done instead

1. **`display/oled_dirty_driver.h`** — `SSD1306DirtyDriver` transmits only the
   pages whose pixels changed, against a shadow of the last frame sent. A
   progress bar animates two pages of four, the capture blink one, an
   unchanged status row none. This cuts the *stretched* time by the same
   factor as the bus time, which is why it beats the faster bus.
2. **`kMinRedrawIntervalMs` 80 → 40ms.** The real constraint is not the bus
   but the fraction of time the pads are blind behind `i2c1_bus_busy`. At 80ms
   with full frames that was ~14%; halving the bytes and halving the interval
   holds it at ~14% while doubling how often the screen may move.

`oled N fr M pg max Nus` on the CPU print now carries pages as well as frames.
Pages/frames near 2 means dirty-paging is working; 4.0 means it is not, and
the 40ms interval is then buying blindness rather than smoothness.

### Second capture (129 windows, dirty pages + 40ms) — two corrections

**Dirty-paging fires less than the layout suggests: 3.08 pages/frame, not 2.**
61 of 129 windows sat at 3.5–4.0. Cause is `ShowLine`'s value row: Font_11x18
at y14..31 spans pages 1–3, so any callout changing both label and value
dirties all four. The byte saving is real but ~23%, not 50%. Only the
blink-only and no-change redraws reach 0–1 pages.

That invalidated the basis for 40ms, which had been sized assuming the byte
count halved. The fix was not the interval but **where the interlock is
held**: `i2c1_bus_busy` across a whole frame blinds the pads for the frame's
wall-clock duration (~52ms at 79% CPU, 244ms worst observed). It is now held
per page inside `SSD1306DirtyDriver::Update()` and released between them, so
worst consecutive blindness is one page and the poll gets a window every
page. That decoupling is what 40ms rests on now.

**Retracted: the settings-journal stall.** The first capture showed all three
`sv` increments landing on the worst rows, and `tp_qspi_ram_op` does mask all
interrupts for a write. But across 129 windows only **1 of 8** `sv` increments
coincides with an outlier. The extreme frames track CPU instead: at `avg 95%`
the main loop gets ~5% of wall clock, so ~12.7ms of bus becomes ~254ms —
against 239937us measured. Preemption explains them without the QSPI story.
Small-sample coincidence; not worth a branch.

**Still open: the CPU baseline in the archive is stale.** Peaks of 137–150%
with `shed` up to 38 sustained, against the 111–115% worst case recorded
2026-07-03, which predates the FX work's fixed +8–12% per block. Re-baseline
before spending anything on ITCM or voice count.

**Also open: redraw rate is capped by audio load, not the display.** Observed
rate topped out near 38 frames per 2s window at a 40ms interval, where 50 are
possible. Lowering the interval further only queues more redraws behind the
same starved main loop.

---

## "Distorts at polyphony 4" was gain staging, not CPU (2026-08-06)

Diagnosed off one observation: **reverb hall distorts more than room, dotted
delay in between.** `FxSection::SetReverbCharacter` (`synth/fx.cpp`) shows
room and hall are the *same code path* — same buffers, same per-sample work —
differing only in two scalars (decay 0.35-0.60 vs 0.75-0.95, damping 0.45 vs
0.80). Identical CPU. So a character that distorts more at equal cost is
distorting on **level**, not load. The ranking hall > dotted delay > room is
exactly the accumulated-tail-energy ordering.

Mechanism: `VoicePool::Render` sums voices unscaled (`out_left[s] += l * vol`,
default vol 1.0), FX returns land on that sum *before* the output stage, and
the output stage was `x/(1+|x|)` — a curve with **no linear region**, bending
from zero. Four voices ≈ 4.0 plus a hall tail ≈ 5-6 came out at 0.83, ~15dB
of saturation applied to the whole waveform. Happens identically at 0% CPU.

Replaced with `soft_limit()` (TouchPlaited.cpp): linear below `kLimitKnee`,
the same rational curve above it, rescaled so value and slope match at the
crossing. Verified continuous (slope 1.0000/0.9999 either side) and
asymptotic to exactly 1.0.

**Known trade, left for ears to settle:** at knee 0.8 the instrument is ~5dB
louder at one voice, and one-to-four voices spans +0.8dB where the old curve
gave +4.1dB — cleaner chords that add less weight. A bounded output plus an
unscaled voice sum must compress somewhere; lowering the knee trades back
toward polyphonic range (0.5 → +1.9dB, 0.3 → +2.7dB) at the cost of shaping
more of the signal. Having both means trimming voices so four land near 1.0
rather than 4.0 — a loudness decision, not made here.

### Separately: FX cost ~25%, not the budgeted 8-12%

Measured 60% → 79-85% when FX come in. Cause is `synth/fx.cpp`'s four
independent `FxSection`s — each group owns its own reverb *and* delay ("own
buffers, own character, own sleep state"), so two active groups run two
reverbs and two delays. The 2026-07-08 analysis recorded "one shared reverb
with per-group send levels (cheap — chosen)"; the implementation went the
other way because a shared instance cannot be room and hall at once
(`synth/fx.h`).

Deferred to its own round, deliberately. Two shapes discussed:
- **Small:** share the instances, keep the per-group sends (sends are just
  gain). Loses only *simultaneous different characters*; no UI change at all,
  the mirror knob becomes global. Most of the CPU for little work.
- **Large:** one reverb + one delay with per-group dry/wet, and an FX
  parameter layer taking over the knobs while P1 is held. Coherent, and P1 is
  already the sound-edit modifier — but it is a control-surface redesign
  touching telemetry, OLED and settings persistence.

Worth re-measuring after the limiter change before choosing: if four voices
under hall no longer distort, the remaining complaint is crackle above 100%,
which is a different problem with different levers.

---

## Six-Op crackle — held voices defeat the shed guard (2026-08-07)

The crackle that survived the limiter fix. **Not drive, not the FX** — four
held Six-Op voices sit above the shed threshold in a state where the guard
cannot fire, and any fifth note briefly makes it five.

### Cost per voice (Six-Op C, engine 4, one group, no FX)

| voices | CPU avg | delta |
|---|---|---|
| idle | 16% | |
| 1 | 41% | +25 |
| 2 | 58% | +17 |
| 3 | 75% | +17 |
| 4 | **92-95%** | +17 |

~17% per voice. Four held = 92-95% against `kShedThreshold` 0.90 — and
`shed 0` throughout, because `VoicePool::ShedVoice()` only considers voices
that are `awake && !gate_held`. Four held pads means four gate-held voices,
so **no victim exists and the guard is disabled by construction.**

### The transient

Noticed by ear first ("a consecutive hit playing a new note on top causes a
short burst at the start of the note"), then explained: `cap_bp_voices()`
releases the oldest held voice, but that voice stays *awake* rendering its
release tail. So momentarily 4 held + 1 releasing = five Six-Op voices,
92% + 17% ≈ 109% — against measured peaks of 101 / 115 / 116 / 121%. The
released voice is only then shed-eligible, so `shed` fires reactively, one
block late; the first hot block always crackles.

This is why the fault looks intermittent — "sometimes at 3 voices with drive,
sometimes not even at 4" depends entirely on whether a release tail is still
sounding when the next note lands.

### Two corrections the same capture forced

**Drive costs no CPU.** 1 voice 41% clean vs 40-41% at 100% drive; 3 voices
75-78% vs 77-78%. Drive changes level into the output limiter, not load.

**FX cost ~5% per active group, not the 25% claimed on 2026-08-06.** Measured
here: 1 voice 41→45%, 2 voices 58→63%, 3 voices 75→80%, with hall + dotted
delay on one group. The earlier 25% came from a session with drums *and*
synth active — two groups plus more voices — and was misattributed. **This
substantially weakens the case for the FX consolidation round**: sharing
instances is worth perhaps 5-10%, not 25%.

### Fix applied

Per-engine held-voice ceiling in `VoicePool` — `kBPMaxHeldHeavy = 3` for the
engines `engine_is_heavy()` lists, `kBPMaxHeld = 4` otherwise. Three held
Six-Op voices is 75-78%, leaving ~15% of headroom, about what one overlapping
release tail costs. Six-Op C is the measured case; Speech/Particle/String/
Modal are inferred from the 2026-07-03 budget analysis and are cheap to
correct if one of them turns out not to need it.

`voice_engine[]` now tracks the engine per voice so a mixed-engine chord
(possible with `bp_slots_active`, where each pad carries its own snapshot)
caps on the heaviest engine present rather than only the incoming one.

### Raw captures — 2026-08-07, verbatim

Kept exactly as they came off COM11 so a future round has something to
reference against rather than a paraphrase. Build: `-DUSB_MIDI` commented
out, dirty-page OLED driver, per-page interlock, `soft_limit()` output stage,
**before** the per-engine cap. Six-Op C patch, low master volume (S36).

Note the `max -21445678us` readings: those are the `System::GetUs()` wrap
bug (~21.5s rollover, fixed after this capture — see the commit "time frames
in ticks"). Any negative `max` in these logs is an artefact, not a stall.
Positive values are sound.

Clean, no FX — 1 to 4 voices:

```
CPU avg 16% max 17% shed 0 sv 15 | oled 0 fr 0 pg max 0us (= idle)
1 voice:
CPU avg 41% max 42% shed 0 sv 9 | oled 0 fr 0 pg max 0us
CPU avg 41% max 43% shed 0 sv 9 | oled 1 fr 4 pg max 19827us
CPU avg 41% max 43% shed 0 sv 9 | oled 1 fr 4 pg max 19933us
CPU avg 41% max 43% shed 0 sv 9 | oled 0 fr 0 pg max 0us

2 voices: 
CPU avg 58% max 60% shed 0 sv 13 | oled 1 fr 4 pg max 27799us
CPU avg 58% max 60% shed 0 sv 13 | oled 0 fr 0 pg max 0us
CPU avg 58% max 61% shed 0 sv 13 | oled 0 fr 0 pg max 0us
CPU avg 58% max 60% shed 0 sv 14 | oled 0 fr 0 pg max 0us

3 voices:
CPU avg 75% max 78% shed 0 sv 18 | oled 3 fr 12 pg max 47275us
CPU avg 75% max 78% shed 0 sv 18 | oled 1 fr 4 pg max 47244us
CPU avg 75% max 79% shed 0 sv 18 | oled 0 fr 0 pg max 0us

4 voices:
CPU avg 16% max 17% shed 0 sv 18 | oled 0 fr 0 pg max 0us
CPU avg 92% max 95% shed 0 sv 18 | oled 4 fr 16 pg max 127667us
CPU avg 92% max 95% shed 0 sv 18 | oled 1 fr 4 pg max 123691us
CPU avg 92% max 95% shed 0 sv 18 | oled 0 fr 0 pg max 0us
```

100% drive (S30) — same voice counts, showing drive is CPU-neutral:

```
1 voice with 100% drive S30:
CPU avg 16% max 17% shed 0 sv 23 | oled 0 fr 0 pg max 0us
CPU avg 40% max 43% shed 0 sv 23 | oled 1 fr 4 pg max 19962us
CPU avg 41% max 43% shed 0 sv 23 | oled 1 fr 4 pg max 19907us
CPU avg 41% max 43% shed 0 sv 23 | oled 0 fr 0 pg max 0us

2 voices 100% drive S30:
CPU avg 16% max 17% shed 0 sv 24 | oled 0 fr 0 pg max 0us
CPU avg 39% max 44% shed 0 sv 24 | oled 12 fr 36 pg max 19992us
CPU avg 59% max 61% shed 0 sv 24 | oled 1 fr 4 pg max 28014us
CPU avg 59% max 61% shed 0 sv 25 | oled 1 fr 4 pg max 28034us
CPU avg 59% max 62% shed 0 sv 25 | oled 0 fr 0 pg max 0us
CPU avg 60% max 62% shed 0 sv 25 | oled 0 fr 0 pg max 0us

3 voices 100% drive S30:
CPU avg 77% max 81% shed 0 sv 25 | oled 2 fr 8 pg max 48228us
CPU avg 77% max 80% shed 0 sv 25 | oled 1 fr 4 pg max 48196us
CPU avg 77% max 81% shed 0 sv 25 | oled 2 fr 8 pg max 51222us
CPU avg 78% max 80% shed 0 sv 25 | oled 1 fr 4 pg max 48301us

4 voices 100% drive S30: (this is where reports start hanging, other sessions had revealed above 100%, sometimes it crackled even at drive 2% sometimes it didn't at 100%)
CPU avg 95% max 99% shed 0 sv 28 | oled 1 fr 4 pg max 187917us
CPU avg 79% max 98% shed 2 sv 28 | oled 4 fr 16 pg max 179787us
CPU avg 95% max 97% shed 1 sv 28 | oled 5 fr 20 pg max 179919us
```

Lifting and landing notes, hall reverb + drive, max 4 at a time:

```
CPU avg 79% max 101% shed 1 sv 30 | oled 3 fr 12 pg max 127848us
CPU avg 77% max 96% shed 1 sv 30 | oled 6 fr 21 pg max 123852us
CPU avg 94% max 97% shed 2 sv 30 | oled 6 fr 24 pg max 183854us
CPU avg 76% max 115% shed 9 sv 31 | oled 10 fr 36 pg max 195935us
CPU avg 78% max 115% shed 3 sv 31 | oled 3 fr 12 pg max 191756us
CPU avg 95% max 98% shed 3 sv 31 | oled 4 fr 16 pg max 183862us
CPU avg 95% max 115% shed 1 sv 31 | oled 1 fr 4 pg max 191863us
CPU avg 77% max 115% shed 12 sv 31 | oled 8 fr 32 pg max 207754us
CPU avg 78% max 116% shed 4 sv 31 | oled 4 fr 16 pg max 203796us
```

Dotted delay added on top of hall + 100% drive — this is the +5% FX figure:

```
adding dotted delay on top of 1 voice 100% drive with hall reverb:
CPU avg 45% max 47% shed 0 sv 32 | oled 1 fr 4 pg max 20818us
CPU avg 45% max 47% shed 0 sv 32 | oled 0 fr 0 pg max 0us
CPU avg 45% max 47% shed 0 sv 32 | oled 17 fr 52 pg max 22469us
CPU avg 45% max 49% shed 0 sv 33 | oled 1 fr 0 pg max 32us

2 voices:, playing chords and playing fast and long all stay in this range:
CPU avg 63% max 66% shed 0 sv 34 | oled 9 fr 20 pg max 31802us
CPU avg 63% max 66% shed 0 sv 35 | oled 1 fr 4 pg max 31799us
CPU avg 63% max 66% shed 0 sv 35 | oled 7 fr 28 pg max -21445678us
CPU avg 63% max 66% shed 0 sv 35 | oled 16 fr 64 pg max 31928us
CPU avg 63% max 66% shed 0 sv 35 | oled 15 fr 40 pg max 31851us

3 voices, same playing as before, togheter fast , on top always max 3 at a time, does begin the crackle
CPU avg 80% max 101% shed 1 sv 35 | oled 12 fr 48 pg max 63306us
CPU avg 82% max 85% shed 0 sv 35 | oled 8 fr 24 pg max 59842us
CPU avg 80% max 86% shed 0 sv 35 | oled 26 fr 104 pg max 59972us
CPU avg 79% max 102% shed 2 sv 35 | oled 13 fr 48 pg max 59984us
CPU avg 86% max 99% shed 1 sv 35 | oled 28 fr 112 pg max 63262us

with 4 voices same style of playing: immediatly in distorted crackled territory: messages not coming through only between releasing pads:
CPU avg 16% max 17% shed 0 sv 35 | oled 0 fr 0 pg max 0us
CPU avg 86% max 101% shed 7 sv 35 | oled 10 fr 31 pg max -21415301us
CPU avg 83% max 121% shed 26 sv 35 | oled 22 fr 88 pg max 63228us
CPU avg 83% max 121% shed 38 sv 36 | oled 24 fr 96 pg max 267865us
```

One line worth keeping in view: `oled 1 fr 0 pg max 32us` — a redraw that
transmitted **zero** pages in 32µs. That is the dirty-page shadow doing
exactly its job, and the cheapest possible confirmation that it works.

### Note on reading the CPU log

Above ~100% the audio callback overruns its 4ms block and the main loop gets
essentially nothing — serial prints, OLED and MIDI service all stall together.
So **missing log lines during crackle are themselves data**, and the worst
episodes are systematically underreported. `CPU avg` hides them (one window
read `avg 78%` while containing a 507777us OLED frame, ~10x its neighbours at
the same average); `max` and `shed` are the columns that carry signal.

### Dead end worth recording

DMA is not the follow-up. libDaisy returns `ERR` unconditionally from
`TransmitDma` on I2C4 (needs BDMA with buffers in SRAM4; `src/per/i2c.cpp` has
the TODO), and `SSD130xI2CTransport` has no DMA path on any peripheral — only
the SPI transport does, and only `SSD1307Driver` wires it up. Real zero-CPU
frames would mean converting the panel to 4-wire SPI.

---

## Deliberate decisions

**Block size: 192, rendered as 8 × 24-sample chunks**
~4ms per callback at 48kHz. Larger block chosen to give ISR headroom for drum
engines. It is also the unit every hold gesture counts in — `kStageBlocks`
and friends are block counts, not milliseconds.

**P0 and P2 have no standalone sound action**
`bank_caught = false` anchored at TOUCH moment (not release). Pot must move
past dead zone (3%) before model select activates.

**Engine LED blink removed**
Counted blinks too hard to read in practice. Model changes confirmed by
audition tone only. (The OLED later took over this job properly.)

**MPR121 thresholds: library defaults (12/6)**
Raising threshold to 20 broke pad registration. Ghost-touch suppression via
`any_musical_pad_held()` guard in software.

**Phase 8F (controls out of ISR) — reverted**
Data race crash: `generate_*()` writes the slot arrays in the main loop while
the ISR reads them in the `SetOnTouch` touch callback. Fix requires
`__disable_irq()` / `__enable_irq()` wrapping around all `generate_*()` calls.
Deferred unless crackle returns — carried in `ROADMAP.md` Parking Lot.

**S30 repurposed from FM amount to Drive**
`patch.frequency_modulation_amount` has no effect without a CV input
(`modulations.frequency` is always 0 on this hardware). S30 was a dead knob in
its original role.

**Unified decay → `patch.morph` on the morph-decay engines**
Six-Op A/B/C (2–4), String and Modal (19–20) and the drum engines (21–23)
ignore `patch.decay` and use `patch.morph` as their envelope/tail. `slot.decay`
always means "tail time" from the user's perspective; the routing to the
correct patch field happens at playback. The knob is **S31** everywhere (it
was S37 in the v1 layout, which is what the archived tables say).

**Per-mode slot arrays (mode memory)**
`bp_slots[]` holds the Basic Pitch randomize snapshots, `drum_slots[]` the Seq
kit; the arp and Rec each carry their own sound separately. Flicking SW2
restores each mode's last state; nothing regenerates on mode switch. Only
P0+P2 re-randomizes, and the drum kit is generated once on first Seq entry
(`drum_kit_ready`). (`pad_slots[]`, the old Random-mode array, went with the
mode.)

**Seq per-slot drive is a ratio, not an absolute**
`drum_slots[i].drive` defaults to 1.0 and multiplies the overall S30 drive at
trigger time (`overall × ratio`). In recording mode S30 edits the slot's ratio
with pickup while the overall drive is frozen at its rec-entry value, so slot
edits don't move the whole mix. Pitched slots keep `drive = 0.0` (absolute),
since global `SetDrive` overrides per-voice drive outside Seq anyway.

**Basic Pitch snapshot escape via knob grab**
After P0+P2 randomize, Basic Pitch plays frozen `bp_slots` — but the mode's
identity is "all params live on knobs", so any timbral knob moved >5% (or a
model picked via P0/P2+S35) snaps back to live mode. No extra gesture to learn.

**Background seq: param-locked voices + offset slot ids**
Seq/drum triggers call `NoteOnWithParams(..., lock_params=true)` with slot ids
`16+i`. The lock makes all VoicePool global setters (engine,
harmonics/timbre/morph/decay, drive, LPG, FM) skip those voices, so Basic
Pitch live knobs and pitched-mode drive can't reshape a drum mid-decay; the id
offset keeps pad NoteOffs (slots 0–6) from cutting drum tails, and drums ring
out as one-shots. Locked LPG is pinned to 0.5 at trigger. Seq trigger settings
update only while SW2 is Up.

---

## Per-model control reference

What each knob does per engine — the reasoning behind `kEngineKnobs`
(`display/oled_ui.cpp`, mirrored in the visualizer's `controls-meta.ts`),
which is what actually drives the on-screen labels.

Current knob roles: **S31 Decay** (unified — LPG envelope, or the model's own
morph on the starred engines), **S32 Harmonics**, **S33 Timbre**,
**S34 Morph**. LPG colour is retired (fixed 0.5, CC 25 only).

| #  | Engine           | S32 Harmonics | S33 Timbre | S34 Morph | S31 Decay/Tail |
|----|------------------|---------------|------------|-----------|----------------|
| 0  | VA + VCF         | Resonance     | Filter cutoff | Osc mix/sub | Env decay |
| 1  | Phase Distortion | PD waveform   | Saturation | Symmetry  | Env decay |
| 2  | Six-Op A *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 3  | Six-Op B *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 4  | Six-Op C *       | FM algorithm  | Op levels  | **→ S31** | = morph: DX7 env time |
| 5  | Wave Terrain     | X position    | Y position | Terrain   | Env decay |
| 6  | String Machine   | Filter cutoff | Chorus     | Voices    | Env decay |
| 7  | Chiptune ⚠       | Arp range     | Waveform   | Speed     | *(inert — see below)* |
| 8  | Virtual Analog   | Detuning      | Crossfade  | Wavefold  | Env decay |
| 9  | Waveshaping      | Level         | Fold       | Asymmetry | Env decay |
| 10 | Two-Op FM        | Ratio         | Mod level  | Feedback  | Env decay |
| 11 | Grain            | Position      | Density    | Size      | Env decay |
| 12 | Additive         | N harmonics   | Inharmonic | Shape     | Env decay |
| 13 | Wavetable        | X position    | Y position | Warp      | Env decay |
| 14 | Chord            | Chord type    | Inversion  | Shape     | Env decay |
| 15 | Speech           | Formant rate  | Vowel      | Accent    | Env decay |
| 16 | Swarm            | Spread        | N voices   | Density   | Env decay |
| 17 | Noise            | Filter color  | Bandwidth  | Saturate  | Env decay |
| 18 | Particle         | Density       | Size       | Backgnd   | Env decay |
| 19 | String *         | Inharmonicity | Brightness | **→ S31** | = morph: damping |
| 20 | Modal *          | Material      | Brightness | **→ S31** | = morph: damping |
| 21 | Bass Drum *      | Pitch drop    | FM punch   | **→ S31** | = morph: tail |
| 22 | Snare Drum *     | Noise freq    | Noise/body | **→ S31** | = morph: char+tail |
| 23 | Hi-Hat *         | Freq spread   | Metallic   | **→ S31** | = morph: tail |

\* morph-decay engines. Six-Op additionally renders identical OUT and AUX, so
blend and stereo width do nothing on 2–4.

**Engine 0's harmonics/timbre were swapped in this table until 2026-08-08**
(found while building the kick lab, `virtual_analog_vcf_engine.cc`: cutoff is
`f0 * 2^((timbre - 0.2) * 10)`, resonance is `|harmonics - 0.5|`). The
visualizer's `ENGINE_KNOBS` always had it right, so this row was the only place
the error lived.

**Six-Op A/B/C note:** these engines produce sound only in specific parameter
regions. At random/default values they are often silent, and some combinations
only sound on every second gate trigger. `kSixOpAud[]` (in `TouchPlaited.cpp`)
is the table of audible landing spots used by randomize and audition.

**Chiptune (7):** self-running arpeggiator, no gate response, and it never
calls `set_envelope_shape` — so S31 Decay is inert on it. Excluded from the
random pools *and*, since 2026-08-05, from the selectable banks: an engine
where one standard knob does nothing and whose arpeggiator free-runs against
the device's own doesn't earn a slot. Reasoning archived under Priority 3 in
`notesarchive/notes_archive_2026-08.md`.

---

## Drum engine parameters

The three purpose-built drum engines, and what their knobs mean when a slot
lands on one:

- **21 — Bass Drum:** Harmonics = pitch-drop speed, Timbre = FM punch,
  morph = decay time, Note = base pitch
- **22 — Snare Drum:** Harmonics = noise freq, Timbre = noise/body mix,
  morph = character + decay, Note = drum body pitch
- **23 — Hi-Hat:** Harmonics = freq spread, Timbre = metallic ratio,
  morph = decay time, Note = pitch center

**Non-drum engines that work as percussion:** 17 Noise (hat/snare), 19 String
(rimshot/clave — but too melodic for the Perc pool, see below), 20 Modal
(cowbell/conga), 10 Two-Op FM (already the second kick engine). **Particle
(18) is banned from every random drum pool** — its intentionally sporadic
crackle reads as a hardware fault in a generated kit. It stays manually
selectable in Recording.

Tuning lessons that outlived the tables: hats need to sit **above** the
melodic register (60–84 read as tonal noise rather than metal — they were
moved to 76–100); Modal's tail has to be capped well below 0.9 for percussion;
a Snare engine pitched high with a body-heavy timbre gives the rim/wood tick
that String was wrongly used for.

> **Answered by the kick lab below** (2026-08-08): yes, existing engines
> reach it — nothing had to be written into `thirdparty/plaits`. What was
> missing was not DSP but *reachable parameter space*, in three specific
> places the pool ranges never went.

---

## Kick lab (2026-08-08)

The branch that turned the open `kDrumKick` question into a bank you can step
through. Twenty candidates were built and auditioned on hardware the same day;
**twelve survived** (`synth/kick_presets.h`), and the bank went from a probe to
the kick pad's actual pool.

### The audition round, and what it cut

Verdict from the hardware pass, verbatim in effect: **the entire 2-op FM family
(the old K8–K13) reads as "too much regular synth"** — not as a drum. Six
presets, one engine, one verdict, which makes it a finding about engine 10 as a
kick rather than about six sets of numbers. Worth holding onto: the FM engine's
own envelope is the LPG in ping mode, so an FM kick has no pitch envelope of
its own, and what makes an FM drum read as a drum is precisely the pitch drop.
It can be tuned into kick *territory* — the analysis below was right about the
16–33 Hz fundamental — but it lands there as a bass note with a fast decay,
which is the description of "regular synth".

**Two of the three layered presets also failed**, and in different ways: the
noise transient (old K18, engine 17 over a deep 808) "sounds bad crackled", and
the FM snap (old K19, engine 10 at note 84) sat "too high pitch on top". The
one that survived is the one that layers engine 21 with *itself* — 909 attack
over an 808 sub. So the lesson is not "layering doesn't work" but that a layer
has to be the same instrument twice: a foreign transient stapled to a kick
reads as a fault or as a second sound, not as one drum. That it is also the
cheapest of the three (one engine, no second render path) is a coincidence
worth taking.

Everything else held: the seven engine-21 presets and all four outside bets
(Additive, Waveshaping, VA+VCF, Modal). Notably the outside bets all survived
while the family purpose-built for FM did not, which was not the expected
result.

### What the bank became

| | |
|---|---|
| **Twelve presets** | `synth/kick_presets.h`, grouped: 8 on engine 21, then the four outside bets |
| **The kick's random pool** | `fill_drum_slot_from_pool(0)` draws a preset instead of rolling inside a DrumOpt range — a randomized kit now gets a kick that was *chosen*, and one that can be named |
| **A loadable model** | the 12th position of P0+S35 on the kick pad, past the eleven engines: `KICK BANK` |
| **S32 = kick select** | a 12-way quantizer once the bank is loaded |
| **S33/S34/S37 = Tone / Punch / Body** | bounded windows, per preset |
| **P0+P10/P11** | still the fast stepper, from Seq idle or from Rec |

`kDrumKick` is gone. Its two entries were exactly the two sounds the audition
rejected, and for reasons a parameter range cannot fix — so `kDrumPools[0]`
is now a null entry and slot 0 is special-cased in the two places that read the
table (`fill_drum_slot_from_pool`, `slot_engine_in_pool`). Curated points beat
rolled ones for one instrument; the other six keep their ranges, where the
variety is the point.

**The three live knobs are semantic, not parametric.** Tone is always "dark to
bright", Punch always "soft to hard", Body always "the weight of the drum" —
but they land on the same three PadSlot fields (`timbre`, `harmonics`, `blend`)
whatever engine is underneath, and those fields mean six different things
across the six engines the bank uses. Each preset therefore carries a `KickAxes`
window per knob, and the knob sweeps *inside* it. Two things fall out of that,
both of which were the ask:

- **Consistency.** One gesture means one thing across all twelve presets.
- **You cannot turn a kick out of the instrument.** Full-travel harmonics on
  engine 12 is a chord; full-travel resonance on engine 0 is a whistle. The
  window is the part of each range that still answers to the word "kick".

The authored value always lies inside its own window, so loading a preset and
then catching a knob never moves the sound before you turn it. `mutate_drum_slot_soft()`
re-clamps into the same windows, so "vary kit" — the path most likely to be
used repeatedly — cannot walk a preset out of them either.

One ordering bug this forced, worth recording because it was invisible until
the kick had a blend worth keeping: `generate_drum_random()` reset
blend/width/drive **after** filling the slots. That was harmless while every
slot came from a DrumOpt table (those set none of those fields), but a preset
carries blend, and blend is half of what a preset *means* on engine 21 — so the
reset would have flattened every randomizer kick back to the same 0.5 the bank
exists to escape. The reset now runs first.

### Why the shipped kick sounded like two mediocre choices

*The analysis that built the twenty candidates, kept as written — it is what
decided where in each engine to aim, and the audition above is what decided
which of those aims landed.*

The complaint was "either a very synth sound or a rather low-energy kick
model", i.e. the two entries of `kDrumKick`. Reading the engines rather than
the knob names found that neither entry was reaching the good part of its own
engine, and each for a different, findable reason.

**1 — Engine 21 is two drums, and the kit pinned it halfway between them.**
`bass_drum_engine.cc` renders the 808-style analog bass drum (resonator +
overdrive) to OUT and the "inadvertently 909-ish" synthetic one to AUX. The
Blend fader crossfades them, so 0.0 and 1.0 are two different instruments —
but `generate_drum_random()` resets every slot's blend to 0.5, so every
randomized kick has always been the same half-and-half compromise. That is the
"very synth sound": the 909's click and pitch sweep smeared over the 808's
body. Nothing was broken; the one control that separates them was never
allowed to move.

**2 — Engine 10 plays two octaves below its note.** `fm_engine.cc` opens with
`note = parameters.note - 24`, and its AUX is a further octave down. The
pool's 36–48 note range therefore put the FM kick's fundamental at **16–33 Hz**
— below what most speakers reproduce — with `timbre` capped at 0.30, i.e. a
modulation index of `2 * 0.30² = 0.18`, so barely any harmonics either. An
inaudible fundamental and almost no sidebands is exactly "low energy". The
presets put its notes at 56–72.

**3 — Tails were being cut twice.** On engine 21 the tail is `decay` routed to
morph, and S37 Tightness then multiplies it by `0.2 + tight*0.8`. At the
centre detent that is ×0.6, so the pool's already-short 0.05–0.30 range
sounded as an effective 0.03–0.18. The analog resonator's Q is
`1500 * 2^(morph*80/12)`: 0.18 is a click, ~0.78 is a ~0.4 s ring at 39 Hz,
0.90 is over a second. The long kicks were not missing — they were unreachable.

**4 — `harmonics` on engine 21 is three controls in a row**, and the pool's
0.20–0.55 range stopped just before the interesting half. Analog side:
0.00–0.25 attack-FM depth, 0.25–0.50 self-FM, **0.50–1.00 overdrive**.
Synthetic side: 0.00–0.50 pitch-sweep depth, **0.50–1.00 sweep length**. Both
the grit and the long pitch drops live above 0.5.

Two smaller ones, both fixed with the bank:

- **Kick punch fought every deep preset.** `drum_params()` pushed slot 0's
  timbre toward 1.0 in proportion to S30 Drive. On engine 21 timbre is the
  tone lowpass *and* the synthetic click level, so turning drive up made deep
  kicks bright and clicky — the opposite of what "more kick" should do. Punch
  is now a per-preset share of Drive (0.15–0.35 on the deep ones, up to 0.8 on
  the bright ones); a pool kick keeps the old flat 1.0.
- **Note range.** The pool's 36–48 is 65–131 Hz. 48 is a tom, not a kick. The
  808-family presets sit at 27–38 (33–92 Hz).

### The bank, and the graveyard

Grouped by engine so that stepping walks a family at a time — auditioning
wants neighbours that compare.

Numbers are the shipped ones; the ✗ rows were built, auditioned and cut, and
are listed because "we tried engine 10 six ways" is worth not re-deriving.

| K | Name | Engine | What it is for |
|---|------|--------|----------------|
| 1 | 808 DEEP | 21 analog | The plain deep 808: dark, ~0.4 s ring |
| 2 | 808 SUB | 21 analog | The extreme end — 33 Hz, tail over a second |
| 3 | 808 DRIVE | 21 analog | h 0.88: the engine's own overdrive + full self-FM |
| 4 | 909 PUNCH | 21 synth | Classic 909, moderate sweep and click |
| 5 | 909 SWEEP | 21 synth | h 0.80 = full-depth pitch drop that takes its time |
| 6 | 909 CLICK | 21 synth | Bright and short — the one that cuts through |
| 7 | HYBRID | 21 both | Analog body, 909 transient, weighted to the body |
| 8 | 909+808 | 21 + 21 | Stacked kicks: 909 attack over an 808 sub (layered) |
| 9 | FOLD SUB | 9 Waveshaping | A sub with fold harmonics instead of FM ones |
| 10 | VCF BOOM | 0 VA+VCF | Resonant filter thump (no filter sweep — it held up anyway) |
| 11 | MODAL KNOCK | 20 Modal | Woody knock, at real CPU cost (see below) |
| ✗ | FM 1:1 · FM SUB 1:2 · FM AUX SUB · FM GRIT · FM SNAP · FM LONG | 10 | Ratio 1 / 0.5 / AUX sub / gritty feedback / high-index snap / long LPG. All six read as synth bass, not as drums |
| ✗ | SUB+CLICK | 21 + 17 | Noise transient over a deep 808 — crackled |
| ✗ | SUB+SNAP | 21 + 10 | FM snap over a deep 808 — sat too high |
| ✗ | SINE PURE | 12 Additive | Cut in the second pass — it masked the kit *and* held a voice for 7.4 s. See "The LPG tail trap" below |

FM ratios come from a quantizer LUT, not a linear map. Kept here even though
the FM presets are gone, because it is the thing that has to be looked up again
the moment anyone tries engine 10 for anything: h 0.00 = 0.5×, 0.24 = 1×,
0.47 = 2×, 0.65 = 3×, 0.75 = 4×, 1.00 = 8×.

### CPU and voice cost

Not measured on hardware — this is what the sources say to expect, and the
figures to check against. Everything is stated per sounding voice.

- **Engine 21 costs the same whatever `blend` is.** Both drums render every
  block regardless; blend is a mix applied afterwards. So the entire 808/909
  axis, including HYBRID, is free — and eight of the twelve presets are on it.
- **Engine 20 (MODAL KNOCK) is on `engine_is_heavy()`** — a bank of resonators,
  and now the only expensive entry in the bank. It earned its place on
  hardware; the cost is the reason to know which preset is loaded.
- **909+808 costs one extra voice of the six** for the length of the layer's
  own tail — and that tail is a deep 808 at morph 0.88, so roughly half a
  second, not the few tens of milliseconds the cut noise/FM layers cost. It is
  still the cheapest *kind* of layer: one engine, both of its drums, each with
  its own pitch and tail, so a second engine's render cost is avoided
  entirely. Since the randomizer can land on it, this is the one bank entry
  that changes the kit's voice budget, and it is 1 in 11 — worth listening for
  against a dense pattern (filed in `ROADMAP.md`).
- The layer fires on **voice id 24**, outside the pad range (0–6) and the drum
  range (16–22), so nothing NoteOffs it and it rings out as a one-shot.

### The LPG tail trap — why SINE PURE was cut

Reported after the starvation fix below: *"the kick that is still choking all
the other drums seems to be K09 SINE PURE"*. It was, and for a reason that
turns out to be a property of a whole class of preset rather than of that one.

**Eight of the twelve presets never touch the LPG.** Engines 21 and 20 are
`already_enveloped`, so Plaits bypasses the low-pass gate and the sound's
length is the engine's own envelope — bounded, and audible right up to its
end. The three "outside bet" engines (12, 9, 0) are not, so their tail is the
LPG in ping mode, and that has two properties nothing else in the drum path
has: **it ignores the gate entirely** (`ProcessPing` fires on the rising edge
and runs its own schedule — our one-shot timer does nothing to it), and its
fall is brutally non-linear in `patch.decay`. Simulated from
`LowPassGate::ProcessLP`, time for the gain to reach the pool's silence
threshold:

| decay | to −40 dBFS | to −80 dBFS (= voice released) |
|-------|-------------|-------------------------------|
| 0.30 | 0.77 s | 1.34 s |
| 0.35 | 0.94 s | 1.64 s |
| 0.45 | 1.41 s | 2.46 s |
| 0.60 | 2.55 s | 4.53 s |
| 0.72 | 4.12 s | **7.37 s** |

SINE PURE was authored at 0.72. So every kick masked the kit with four seconds
of 55 Hz sine *and* held one of six voices for seven — on a pattern with a kick
every beat, one voice was simply gone. Both halves of "choking" were literally
true, and the sound half is why no threshold change could have saved it.

**And the pool's own repair made it worse.** `find_free_or_steal()` had just
learned to take a *sleeping* voice before any sounding one, which is right in
general — but it meant the one voice that never slept was never reclaimed
either. The rule protected the worst offender by accident.

Three things came out of it:

- **SINE PURE is cut.** A 4 s audible sine is not a kick, and there is no
  parameter that makes engine 12 into one without making it into something
  else.
- **FOLD SUB and VCF BOOM are re-authored** from 0.60/0.45 down to 0.34/0.30.
  They had the same fault, just slower — 4.5 s and 2.5 s of voice apiece. The
  audible tail drops from ~2.5 s to ~0.9 s on FOLD SUB, which is still a long
  kick and no longer a drone. **Any future LPG-engine preset belongs at or
  below ~0.35**, and `synth/kick_presets.h` says so where the numbers are.
- **Drum one-shots now sleep at −60 dBFS, not −80** (`kDrumSilenceThresh`).
  A pitched voice is held by a finger and its release tail is something you are
  listening to, so following it to the noise floor is right; a drum's last
  20 dB is inaudible under a kit while the voice it holds is not. This is the
  safety net, not the fix — the presets are authored around the problem now,
  but the pool should not have depended on that.

### Kick priority — and the starvation it caused first

The second half of the roadmap's decision. `VoicePool::find_free_or_steal()`
stole the oldest voice, and on a dense pattern the oldest voice is very often
the kick — a long 808 tail is still allocated when the next bar's hats and
percussion arrive. Losing the downbeat is the one truncation that is always
audible. So kick voices (16 and its layer 24) are stolen **last**, not never:
if every voice is a protected kick the plain oldest-first rule still applies,
and the pool can never wedge. `ShedVoice()` gets the same preference.

**The first version of this was wrong in a way worth recording**, because the
mistake is one that looks like the obvious implementation. It keyed the
protection on `awake[i]` — protect the kick for as long as it is still making
sound. With the bank's presets that is 0.4–1.4 s, which on a four-on-the-floor
is most of the bar, so one or two of six voices became effectively *reserved*
and the other six drums thrashed what was left. It came back from hardware as
"the general tightness of all drums except the kick is super short", which
sounds like an S37 fault and is not one: `seq_tight_lk` was never touched.

The tell was the second half of the report — **a freshly randomized kit
sounded right for about a bar, then clamped down.** Nothing about a stuck knob
does that. Voice contention does exactly that: for the first bar the oldest
voices still belong to the *previous* kit and have already finished, so
stealing them is inaudible; from the second bar on, every steal cuts a live
tail. Two changes from this branch compounded — kicks that hold a voice for a
second (intended) and a protection with no end (not).

Two fixes, and the second is the bigger one:

- **The window is now 150 ms from the strike** (`kProtectChunks`), fixed, not
  the length of the sound. What the protection is for is the attack and body,
  the part whose loss reads as a dropped downbeat; the ring-out is not worth a
  hat. It also releases before the next kick at any usable tempo.
- **A sleeping voice is stolen before any sounding one.** This is independent
  of the kick and was a latent fault the whole time: drum voices never get a
  NoteOff, so their `pad_slot` stays set forever and the free-slot scan can
  never see them — the pool went straight to stealing the oldest *sounding*
  voice while a finished one sat unused beside it. With six voices and seven
  drum slots that happens constantly, and it is the single biggest cause of
  drum tails being cut short. Now a live tail is only ever truncated when all
  six voices are genuinely sounding.

Related, found while chasing the same report: the P0 release edge re-armed
S37's Rec pickup against the slot's **raw blend** rather than its position in
the preset's Body window (`arm_rec_slot_pickups()` had already been taught the
conversion; this second site had not). Since several presets sit at blend 0.0
that also armed a *rail*, which catches on any 3% nudge — so Body jumped the
moment S37 was touched after using P0 for anything, including the P0+P10/P11
stepper. That one really was an S37 bug, just not the one that was audible.

### Where the bank lives on the panel, and why

The roadmap decision floated replacing Seq mode with a preset finder. That
turned out to be unnecessary: **Recording mode already is the preset finder.**
It auditions the edited slot on a 0.5 s pulse against a stopped seq (or
force-fires it every other step against a running one), it has the whole
per-slot knob layer live and pickup-protected, and it has per-slot pitch on
P10/P11. What it lacked was a way to jump between known sounds instead of
randomizing toward them — one gesture, not a mode.

Three entry points, deliberately:

- **P0+P10/P11**, from Seq idle or from Rec on the kick pad — the fast walk.
  Chosen over the suggested P0+S31 because stepping a numbered bank wants
  discrete presses with an immediate sound, and every free knob is behind
  pickup protection, which is exactly the wrong behaviour for "play me the next
  one". Genuinely unbound in both states: Seq has no pitched octave for
  P0+P10/P11 to move, and Recording's drum-pitch branch already required P0 to
  be *up*.
- **P0+S35 → `KICK BANK`**, the 12th position of bank 0 on the kick pad, past
  the eleven engines. Top of the throw because that is the one boundary a hand
  finds without looking. Positions 0–10 keep their engines and only the
  divisions between them narrow, and only on this one pad — the alternative, a
  24th engine slot, would have made the same knob position mean different
  things on different pads across both banks.
- **S32**, once the bank is loaded — a 12-way quantizer. A `MoveCatch` rather
  than a `KnobPickup`, because the target is a position in a quantizer and not
  a stored float: there is nothing to cross, and requiring a deliberate ~3%
  nudge is what stops entering Rec from jumping the kick to whatever preset the
  pot happens to sit over. It re-arms the whole layer on each change (a new
  preset is a new set of values under every pot) *except its own catch*, which
  has to stay caught or one sweep would only ever advance one preset.

Everything not listed keeps its job: S30 Drive, S31 Decay, S36 Volume, P0+S37
Width, P10/P11 pitch. Nothing was taken away to make room — S37 in Rec was
already the OUT↔AUX blend, which on engine 21 *is* the 808↔909 body control;
the bank only gives it bounds and a better name.

Two behaviours are deliberately different once a preset is loaded, both so
that what the bank plays is what the bank was authored as:

- **S37 Tightness does not scale a preset's tail.** A knob quietly rescaling
  the thing under audition makes the audition meaningless (see finding 3
  above). Per-slot decay (S31 in Recording) is the right way to shorten one
  kick; tightness keeps its meaning on every other slot.
- **Editing a preset's knobs does not clear the preset index.** "Preset 7 with
  a shorter tail" is still preset 7 for the purpose of judging preset 7. The
  index is cleared only by pointing the pad at something outside the bank —
  hand-picking an engine in Rec, or copying another drum onto the kick pad.
  Note the test for that is on the *selection*, not the engine: a preset on
  engine 21 and a hand-picked engine 21 are the same number, and only the
  second should lose the preset's punch, layer and Tightness exemption.

### What to report back from an audition pass

Preset numbers are on the OLED (the confirm flash, S32's own value row, and the
Rec status row while the kick pad is being edited) and in the visualizer's
action log. The values behind them are visible live: the KIT frame publishes
each slot's engine / harmonics / timbre / morph / decay / note, and in
Recording the pots *are* the slot values once picked up — though on the kick
they are now window *positions*, so a Tone reading of 60% means 60% of that
preset's tone window, not 0.60 timbre. What is most useful back here is **which
numbers worked, and for the ones that nearly worked, which of Tone / Punch /
Body you moved and roughly where it landed** — the windows in
`synth/kick_presets.h` were derived from reading the engines, and the hardware
is the only thing that can say whether each one is drawn in the right place.

---

## Drum pattern system (2026-07-04)

Patterns live in **`synth/patterns/<genre>/NN_name.h`** — one file per pattern
in a genre subfolder (`techno/`, `electro/`, `idm/`), defining
`static constexpr uint8_t kPat_<genre>_NN_name[7][64]`. `tools/gen_patterns.py`
(run by `make` on every build, rewrites only on change; genre order hardcoded
to match SW1) registers them sorted by filename within each genre into the
generated `synth/patterns_gen.h` (gitignored): `kNumGenres`, `kNumPatterns`,
`kSeqPatterns[]`, `kGenrePatternCount[]`, `kGenrePatternIdx[][]`. Drop a new
file in a genre folder → next `make` embeds it. SW1 picks the genre
(`SetGenre`), S35 picks the pattern within it (`SetVariant`, pickup-protected,
knob range split evenly across the genre's pattern count).

**Step byte encoding 0xCW:** low nibble W = weight 0–4 (unchanged semantics);
high nibble C = chance rolled after the density threshold passes — 0 = always,
1 = 75%, 2 = 50%, 3 = 25%. Plain decimal 0–4 still means deterministic, so old
tables read unchanged; `0x23` = weight 3 at 50%. Implemented with xorshift32 in
`Sequencer::eval_step()`. First use: Electro clap echoes on steps 5+13 at 50%.

A step is audible when `weight + density >= 5`; S34 then scales the authored
chance nibble (`SetChance`). The full quantization contract — and the display
strings that have to stay in lockstep with it — is documented at the top of
`synth/sequencer.h`.

> **Feeds ROADMAP:** "weight → audio level" is a change to what the weight
> nibble *means* downstream. Today it is purely a density gate for audio and
> an accent for MIDI out (`drum_velocity()`); making ghosts quieter internally
> would change the shipped feel of every pattern in these files, which is why
> it's filed rather than done.

**Pattern editor webapp:** `tools/pattern_editor.html` (open locally in a
browser; no server needed). 7×4×16 grid, click = set/clear, drag ↑↓ = weight,
drag ←→ = chance (color lime→amber→orange→red), density-preview slider dims
sub-threshold steps, bar copy, localStorage library seeded with the three
built-ins, C-table import, and one-click export of a ready `synth/patterns/`
file. UI brainstorming lives in `tools/editornotes.md`.

---

## Upstream libDaisy — the SSD1306 column offset, and the fork topology (2026-08-08)

Reference for the two PRs filed against `electro-smith/libDaisy`. Open
actions are in `ROADMAP.md` → *Upstream — libDaisy PRs*.

**The rule the OLED bug turned on.** SSD1306 page writes are preceded by a
column-start address, split across two commands: lower nibble `0x00 | (off &
0x0F)`, higher nibble `0x10 | (off >> 4)`. The offset a panel needs is

> `offset = (controller RAM columns − panel width) / 2`

— 128 columns on the SSD1306/SSD1309, **132** on the SH1106. That gives 0 for
128×32 and 128×64, 32 for 64×32, 28 for 72×40, and 2 for an SH1106 128×64.
libDaisy already encodes the last one: `oled_sh1106.h` sends `0x02` as its
lower column start where the SSD1306 driver sends `0x00`. Nothing in the
SSD1306 datasheet documents per-module offsets — it's a glass-bonding choice,
so it lives in the module vendor's datasheet. u8g2 is the best practical
cross-reference; it carries per-panel init sequences with the offsets baked in.

**Why the bug existed.** `SSD130xDriver::Update()` keyed the offset on
`height` alone — `case 32: 0x12` — which is right for a 64×32 panel and wrong
for a 128×32 one. `git blame` puts that line in PR #326 (recursinging, May
2021), whose description names the motivation: the kxmx_bluemchen's **64×32**
I2C SSD1306. So `0x12` was never a mistake, just under-conditioned. `Init()`
already distinguishes the two inside `case 32:` via `if(width == 64)` for the
COM-pins command; the column-start line never got the same treatment. The fix
is geometry-derived (`(width == 64 && height == 32) ? 0x12 : 0x10`) and lives
in `Config` so odd panels need no library edit.

**TouchPlaited cannot test that fix.** `SSD1306DirtyDriver::Update()`
(`display/oled_dirty_driver.h`) fully overrides the base `Update()` and sends
its own hardcoded `0x10`, so the line the PR changes never executes here. The
app *does* exercise the PR's other commit — the batched I2C page write is in
`SendData()`, which the dirty driver calls straight into.

**The probe.** `oled_probe/` is a standalone sketch using the stock
`SSD130xI2c128x32Driver`, listed in `.git/info/exclude` so it stays invisible
to `git status` and can't leak into a commit. `BOOT_QSPI` like the app, so it
flashes the same way and leaves the bootloader alone. It alternates every 3 s
between `0x10` and `0x12`; the `0x12` phase is byte-for-byte what unpatched
upstream sends, so one build both reproduces the bug and proves the new
`Config` override reaches the panel. Hardware-verified 2026-08-08 on the
128×32 I2C build. Delete it (and its `.git/info/exclude` line) once #707 lands.

**Fork topology — three refs that disagree.** `lib/libDaisy` has
`origin = Synthux-Academy/libDaisy` and `upstream = electro-smith/libDaisy`.

| Ref | OLED | UART |
|---|---|---|
| `upstream/master` | unfixed | — |
| `origin/master` | flat `0x10` (fork PR #1) — **known-wrong for 64×32** | merged (#2) then **reverted** (#3) |
| `origin/touchplaited-pin` → the submodule pin | flat `0x10` | re-applied on top |
| `origin/fix/ssd1306-128x32-column-address` | final `Config` version | — |

The fork is 12 ahead / 3 behind upstream. Nothing shipped is exposed to the
64×32 regression — no Synthux board has one — but fork master shouldn't keep
it. Sync fork master **from upstream** once #707 merges rather than merging
the topic branch, so the fork stops accumulating parallel history.

---

## Where the history lives

Everything below was written up here and has since moved out. Listed so a
pointer from the code still lands somewhere.

**In `notesarchive/notes_archive_2026-07.md`:**
- *Open Decisions — resolved* (items 1–2) and *Known Issues — all fixed* —
  Six-Op audibility + gate/click fixes, seq pickup bugs, tightness direction,
  model-select-while-seq, Electro pattern redesign, the SSD1306 driver bugs
- *Voice expansion + MIDI — budget analysis (2026-07-03)* — the CPU/SRAM
  measurements behind the 4→6 voice bump, voice sleep, the load-shed guard,
  and the SDRAM→SRAM move. The remaining levers (ITCM placement, 7th voice)
  are `ROADMAP.md` Parking Lot items
- *MIDI implementation (2026-07-07) — phase 1 + CC map, both transports*,
  including the **MIDI mapping sketch** the channel-split comment in
  `TouchPlaited.cpp` refers to
- *Reverb / delay FX send* — the 2026-07-08 resource analysis and the
  2026-07-09 implementation (mirror knobs, Rings-derived reverb, per-group
  sends). Hardware-verified 2026-07-24
- *Playmode overhaul* — the original sketch that became Arp/Mel, referenced
  from `synth/arp.h`; the phase-by-phase log is in
  `notesarchive/arp-mel-plan-archive.md` §6–§9
- *Syncing* — the original CV clock in/out sketch. Implemented
  (Schmitt-triggered pulse-to-MIDI-clock bridge, MIDI-outranks-CV with timeout
  fallback); behaviour in `MANUAL.md` → *Clock sync*. **Hardware verification
  of the trigger thresholds is still open** — filed in `ROADMAP.md`
- *Visualizer mobile UX musing* (2026-07-23) — resolved the same day by the
  on-faceplate OLED and the device handle cluster

**In `notesarchive/notes_archive_2026-08.md`:**
- The OLED parity + held-combo progress bar write-up
- The whole 2026-08-04/05 round (kick curation, MIDI drum velocity, Chiptune
  closed as won't-ship, root readouts, OLED items A–E1, the hardware walks)
- *notes.md sweep (2026-08-06)* — the v1-era control reference, the build-out
  status table, and the per-role drum-pool tables this file used to carry

**Feature ideas** all moved to `ROADMAP.md` on 2026-07-08 and stay there: it
is the single owner of future work, and a roadmap item links back here when an
analysis exists.
