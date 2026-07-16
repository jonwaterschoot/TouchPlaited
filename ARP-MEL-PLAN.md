# Arp / Mel playmode — design review & implementation plan

> **Status (2026-07-16, branch `arp-mel`)**: Phases 0–4a implemented and
> compiling — arp core (`synth/arp.h`), Hold, octave range (P1+P10/P11),
> melodic transport (P2+P10), swing, Euclid density, and the basic layered
> Rec recorder (`synth/note_rec.h`, fixed 2-bar loop, undo = P0+P10 in Rec).
> Confirmed decisions: per-note slot editing retired outside Seq (§2 C3);
> SW1 states are persistent + change-latched (never applied on mode entry —
> supersedes the "always enter plain Arp" wording in C1, see commit message).
> **First hardware round (15–16/07/26, see §6)**: arp sound is now fully
> independent of Basic Pitch — seeded from BP once on first-ever entry, then
> edited via the P0+P1 sound-edit layer (supersedes the C5 latch-on-every-
> BP→Arp-entry decision); arp/loop voices are `arp_owned` in the pool so BP's
> live knobs never reach them. Rec gained clear gestures: hold P0+P10 ~1.5s =
> clear all, P0+pad 1–4 = clear that layer. Loop length stays fixed at 2 bars
> (confirmed on hardware).
> Docs are current: README/MANUAL rewritten for Arp/Mel, notes.md sketch
> marked implemented, visualizer knob/SW1 labels updated for mode 1.
> NOT yet done: Rec knob morphs (4b), MIDI-in → pool, arp state in telemetry
> (the visualizer can't show the SW1 sub-state or pool yet).

Replaces the Random playmode (SW2 center). Design notes live in `notes.md` → "Playmode overhaul".
Last Random-mode firmware is preserved on tag `random-mode`.

---

## 1. Design recap (from notes.md)

- SW2: Down = Basic Pitch · Center = **Arp/Mel** (new, was Random) · Up = Seq
- SW1 inside Arp/Mel: three states — **Arp**, **Arp hold**, **Rec**
- Knobs: S30 Drive · S31 Division · S32 Swing · S33 Density (Euclid+chance / Euclid) ·
  S34 Decay · S35 Order (played/asc/desc/pingpong/random) · S36 Mix · P0/P2+S35 model
- P10/P11 base octave; +modifier = octave range
- Rec: layered note recording on the pads, undo = drop a layer, knobs morph the recording

---

## 2. Conflicts found in the current firmware (not in the notes)

These are things the code says that the design has to answer.

### C1 — SW1 is the scale switch (and it's positional)
Today SW1 = scale (Minor/Chromatic/Major) in pitched modes, genre in Seq, with the
**change-latch** policy (position acquired while serving another role is ignored until
the switch moves). Giving SW1 to Arp/Hold/Rec means:

- **Scale can't be changed inside Arp/Mel mode.** Same situation as Seq mode today
  (genre owns SW1 there) — the arp plays the latched `scale_lk`. → **Accept**, it's
  the established pattern. Change scale in Basic Pitch.
- **Entry state**: SW1 might physically sit on the "Rec" position when you flick SW2
  into Arp/Mel. Recommendation: **always enter in plain Arp regardless of SW1
  position**; SW1 takes effect on its next move (exact change-latch reuse). Position
  mapping recommendation: **Center = Arp** (resting default), Down = Arp hold,
  Up = Rec — so the common case (switch centered) matches the actual state.

### C2 — P0+P10/P11 is taken (root semitone)
The notes propose P0+P10/P11 = add/subtract arp octave range, but P0+P10/P11 is the
**root semitone** control everywhere. Recommendation: keep root where it is, put
**octave range on P1+P10/P11** (P1 currently only modifies knobs S30/S35; its pad
combos are free). Range 0–3 extra octaves.

### C3 — The 2-second pad-hold slot-recording collides with holding arp notes
`rec_entry_allowed = seq_mode_on || RANDOM`: today holding a pad 2 s in Random enters
per-slot sound editing. In Arp mode **holding pads is the primary playing gesture** —
this must be gated off in Arp/Mel. Consequence: with Random gone, **per-slot pitched
sound editing (and slot copy) disappears from the instrument entirely** (drum slot
editing in Seq stays). → Confirm this loss is acceptable. Naming note: the code's
existing `RecMode::RECORDING` (slot editing) is unrelated to the new Rec state —
rename to avoid confusion (`SlotEdit` vs `NoteRec`).

### C4 — The arp needs a clock, but the sequencer's clock only ticks while it plays
`Sequencer::Tick()` returns immediately when `!active_`. The arp must run with the
drum seq stopped. Also: **the new knob layout has no tempo knob in Arp mode**
(S31 = Division). Recommendation:
- One **master tempo** = the seq tempo (`seq_tempo_lk`, set in Seq mode / CC27 /
  external MIDI clock). Division is the arp's local rate against it.
- Give the arp its own block/ext-tick counter fed the same inputs (tempo value,
  F8 ticks), and re-phase it on the seq's step boundaries whenever the drum seq is
  running so the two can't drift.

### C5 — Arp sound source: `eff_*` params are pot-live, but the pots are repurposed
In Arp mode S31–S35 have new roles, yet `eff_h/t/m/d` follow the pots unconditionally
(same as in Seq — harmless there because drums use locked params). If the arp played
`eff_*` live, turning **Swing would change the timbre**. Recommendation: **latch the
arp's sound at mode entry** (`arp_h/t/m` captured from `eff_*`, engine =
`current_engine`), decay live from S34 via pickup, drive from S30 via pickup. Sound
design happens in Basic Pitch; the arp plays that sound (mode-memory doctrine).
P0/P2+S35 model change works inside Arp and re-latches. P0+P2 staged hold can reuse
`generate_soft_random`-style variation on the latched arp sound (stage 1 tight,
stage 2 wide) for consistency with Basic Pitch.

### C6 — Telemetry / visualizer / MIDI encode Random explicitly
`t.mode` (0=seq 1=random 2=basic) in `service_telemetry`, visualizer labels, ch1
MIDI note-on path (`PlayMode::RANDOM && slots_ready → pad_slots`), README/cheatsheet,
`tools/pattern_editor.html`(?). All need the Arp/Mel update in the same sweep.

---

## 3. Answers to the open questions in the notes

### Q: Clearing the arp — leave mode? P0+P2?
**Neither — make clearing structural.** In plain **Arp** the pool *is* the held pads:
release everything → silence. Nothing to clear. In **Hold**, re-touching a note
removes it (per the notes), and flicking **SW1 Hold → Arp clears the latched pool**
(what you hold is what plays — empty hands = cleared). No new gesture needed, and
P0+P2 stays "randomize the sound" everywhere.

### Q: Transport — always running? tied to play/stop? per-mode start?
**General approach: the arp is gate-driven, the loops are transport-driven.**
- Plain Arp: runs whenever the pool is non-empty. No transport at all.
- Latched arp (Hold) and the Rec loop: these are "the melodic sequencer" — mirror
  the drum seq exactly. **P2+P10 = melodic run/stop, P2+P11 = drum seq run/stop**
  (the notes' own suggestion — endorsed), both available from any playmode.
- Background play: a latched arp / Rec loop **keeps playing when you flick SW2 to
  another mode**, exactly like the drum seq does today. Symmetric doctrine, answers
  "arp without seq", "seq without arp", and "both" with zero special cases.
- Phase: when the drum seq runs, the arp aligns to its grid (C4); alone, it
  free-runs from the master tempo.

### Q: Held pads — do they also sound directly?
Not asked in the notes but it must be decided: **no** — in Arp mode pads only feed
the pool (standard arp behavior; direct + arp'd notes double up badly).

### Q: Rec — max layers?
**4 layers**, statically allocated (e.g. 64 events × 4 layers, ~8 bytes/event ≈ 2 KB).
Undo pops the newest layer. LIMIT blink when full.

### Q: Rec — loop length?
Not in the notes but load-bearing: recommend **the first layer defines the loop
length**, quantized up to the next whole bar of the master tempo (cap 4 bars);
subsequent layers overdub into that window. If the drum seq is running you're
recording against its grid and LED beat pulse; if not, the LED beat pulse is the
metronome (no click available).

### Q: Rec — octave mode: live notes or recorded seq?
**Live notes.** P10/P11 transpose what you play into the recording; the recording
itself is replayed as recorded. (Global octave still affects playback pitch via
compute_note-style resolution — decide in Phase 4 whether stored notes are absolute
or scale-degree-relative; recommendation: **absolute MIDI notes**, simplest and
root/scale changes don't retroactively warp a recorded melody.)

### Q: Rec — knob influence over the recording
As per the notes, with the pickup/move-catch idioms already in the code:
- S30 drive: live, global to arp+loop. ✓
- S31 division→**stretch**: MoveCatch (touch to engage), center = 1×, left slower,
  right faster, quantized ratios (½, ¾, 1, 1½, 2) so layers stay loop-locked.
- S32 swing: **yes** — applied at playback like the seq's shuffle. ✓
- S33 density→chance: MoveCatch; left = thin out (chance to drop notes), right = 100 %.
- S34 decay: live per-note while recording (stored per event). ✓
- S35 order→**timing nudge**: MoveCatch, center = as recorded, ± shifts events
  against the grid. Applies to the **newest layer** (most recent take is what you
  want to fix); undo still removes it whole.

### Q: Undo gesture?
Open. Recommendation: **P0+P10 = undo layer** while SW1 is in Rec (root-semitone-down
is meaningless mid-take; the combo is only overridden in the Rec state). Alternative:
long-hold P0 alone. Decide during Phase 4 bring-up.

---

## 4. New questions to decide (not blocking Phase 1)

1. **Gate length**: arp notes need a NoteOff. Recommend gate = ~55 % of the division
   step (swing-adjusted), tail shaped by S34 decay. Voice slots: round-robin a small
   dedicated range (e.g. 24–27) so overlapping release tails don't cut.
2. **Division values**: quantized detents — 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4;
   center detent = 1/16. LED MODEL-style blip on detent change?
3. **Euclid geometry**: fill over a fixed 16-step cycle (rotation 0), pool advances
   only on sounding steps vs. on every step? Recommend advance-on-sounding-steps
   (classic arp+euclid feel). S33 lower half adds the chance nibble idea from the
   drum tables (75/50/25 %) on top of the fill.
4. **MIDI in**: ch1 notes join the arp pool in Arp mode (that's what arps do).
   Cheap to add — Phase 5.
5. **Does Hold's latched pool survive SW2 round-trips** (mode memory) even when
   stopped with P2+P10? Recommend yes — consistent with everything else.

---

## 5. Implementation plan

Each phase compiles, flashes, and is playable on hardware; commit per phase on
branch `arp-mel`, merge to main when the mode is usable end-to-end.

### Phase 0 — carve out Random (small, mechanical)
- `PlayMode::RANDOM` → `PlayMode::ARP_MEL`; temporary behavior = Basic Pitch clone.
- Remove Random-only paths: `generate_full_random`, `pad_slots` random generation,
  `kFRAll`, Random branches in pad NoteOn / MIDI ch1 / P0+P2 stages / model select;
  gate slot-rec entry to `seq_mode_on` only (C3).
- Keep `generate_soft_random` (Basic Pitch snapshots use it).
- Telemetry `t.mode` value + visualizer label swap (minimal).

### Phase 1 — Arp core (SW1 = plain Arp only)
- New `synth/arp.h`: ordered note pool (add on touch, remove on release, played-order
  preserved), order modes (S35: played/asc/desc/pingpong/random — random default),
  own clock (C4: master tempo + division, ext-tick aware, seq-phase alignment),
  trigger output (note + gate length).
- Wire in `TouchPlaited.cpp`: pads→pool in Arp mode (no direct notes), latched sound
  (C5), S30 drive / S31 division / S34 decay / S35 order / S36 volume through the
  existing pickup idiom (`arp_*_lk` + `KnobPickup`, rearm on entry — clone of the
  seq block), S37 stays blend (+P0 width), P10/P11 base octave.
- Fire voices from AudioCallback beside `seq.Tick()`; MIDI out on ch1 per arp note.

### Phase 2 — Hold, octave range, transport
- SW1 change-latched tri-state (C1), Hold semantics (latch, re-touch removes,
  Hold→Arp clears), P1+P10/P11 octave range 0–3 (C2), pingpong/order across the
  expanded range.
- P2+P10 melodic run/stop; background play across SW2 modes (mirror seq's
  `seq_mode_on` vs `seq.IsActive()` split: `arp_mode_on` vs `arp.IsRunning()`).
- LED: reuse NUMBERED/CONFIRM grammar for SW1 state changes.

### Phase 3 — Swing + Density
- S32 swing (reuse the seq's odd-step delay model at the division rate).
- S33 Euclid fill + chance (bjorklund, 16-step cycle; §4.3 decisions).

### Phase 4 — Rec (note recorder)
- 4a: `synth/note_rec.h` — 4 layers × 64 events (note, tick offset, decay), first
  layer defines loop length (bar-quantized, cap 4 bars), overdub, undo (P0+P10),
  playback engine sharing the arp's clock + sound; SW1 Rec state wiring; entry
  starts listening immediately (per notes).
- 4b: recording morphs — S31 stretch, S33 chance, S34 per-note decay, S35 timing
  nudge (all MoveCatch, per §3).

### Phase 5 — Integration sweep
- MIDI ch1 in → pool (§4.4); decide/implement arp CCs if wanted.
- Telemetry: arp state (pool size, step, SW1 state) if the visualizer should show it;
  visualizer panel labels for the new knob roles.
- Docs: README, cheatsheet, notes.md cleanup; delete this file's answered questions
  or fold into the manual.

### Risks
- **ISR budget**: arp adds one trigger stream — negligible next to the drum seq.
  Voice pressure: arp is serial (1 note/step) + release tails; round-robin slots.
- **Gesture density**: P2+P10 vs P0+P10-undo vs octave taps all live on P10 —
  needs on-hardware feel testing (Phase 2/4 exit criteria).
- **Rec morphs (4b)** are the most speculative part of the design — build 4a first
  and re-evaluate; the notes' per-knob table may change after playing with 4a.


---

## 6. First hardware round (15–16/07/26) — problems & resolutions

Raw notes from the first build, with what was decided and implemented.

### Rec: no stop/clear (loop plays on regardless of SW1 state — by design)
Stop already existed (P2+P10 gates arp + loop together); clearing was
undo-only. **Implemented:**
- **Clear all**: hold **P0+P10 ~1.5 s** while SW1 is in Rec — wipes every
  layer and resets the clock (the tap stays single-step undo; the initial
  undo the tap fires is subsumed by the full clear). LED CONFIRM.
- **Clear one layer**: **P0 + playing pad 1–4** while SW1 is in Rec clears
  that committed layer (pad 1 = oldest). Under P0 the pads neither sound nor
  record. LED = layer number; LIMIT blink when there's no such layer.
- **Layer model kept as overdub passes** (each 2-bar pass = one layer), not
  bars-as-layers: pass-layers are what makes overdubbing and undo-my-last-
  take work; the pad combo makes them addressable, which was the point of
  the bar idea. **Loop length stays fixed at 2 bars** (confirmed).

### Sound entanglement between Basic Pitch and Arp/Mel (SW2)
The arp re-latched BP's sound on *every* BP→Arp flick, and BP's live global
knob writes reached background arp/loop voices (they're unlocked so they ride
the pitched group). The two modes were impossible to tell apart and fought
over the sound. **Implemented — the notes' own proposal:**
- `arp_snd` is seeded from the live BP sound **once, on the first Arp/Mel
  entry ever**, then fully independent.
- Arp/loop trigger voices are **`arp_owned`** in the voice pool: skipped by
  every global setter (like `locked`) but still mixed in the pitched group.
  BP's knobs can no longer morph a background arp, held Rec notes, or MIDI
  notes playing the arp sound.
- **Sound edit sub-mode**: hold **P0+P1 ~1 s** (without P2 — that's mutate)
  to toggle. The knobs swap to the BP layout on the arp's own model —
  S30 drive, S31 decay, S32 harmonics, S33 timbre, S34 morph — and the arp
  functions freeze until toggled back. The running arp is its own audition
  (entry also fires one). Exits on re-toggle, SW1 state change, or mode
  re-entry; all knob hand-offs go through pickups. P0/P2+S35 model select
  and P0+P2 mutate still work as before.
  Chosen over long-press P0/P1/P2 alone: P1 alone is the FX layer, P0 alone
  arms width/root combos — a solo long-press on either misfires while
  reaching for a combo; the two-finger chord can't happen accidentally and
  pads stay free for long held notes.
