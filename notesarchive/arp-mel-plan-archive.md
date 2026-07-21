# Arp / Mel playmode — design review & implementation plan (archived 2026-07-21)

The design record for the Arp/Mel playmode (SW2 center): the arpeggiator,
Hold, and the layered Rec note recorder (§1–§6), plus two later follow-up
passes built on top once the mode was hardware-verified — per-mode
independence (§7–§8, Phases 6–12) and a round of fader/FX independence
work plus bug fixes (§9, Phases 13–16). Archived here once all 16 phases
were implemented and `MANUAL.md` fully reflected the shipped behavior.
**None of §7–§9 has had a hardware round yet** — treat "IMPLEMENTED" below
as "compiles and is believed correct," not "verified." The workflow story
around this file is in `readme.md` (this folder); the original design
sketch it answers lives in `notes.md` → "Playmode overhaul" (the
20/07/2026 and 21/07/2026 entries there are marked resolved, pointing back
here).

---

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
> NOT yet done: MIDI-in → pool, arp state in telemetry (the visualizer can't
> show the SW1 sub-state or pool yet).
> **Second design pass (20/07/26 notes, §7/§8 below) — implemented, not yet
> hardware-tested**: per-mode octave (Phase 6, plus a Basic Pitch-only gate on
> root note that didn't exist before); Rec's own sound (`rec_snd`) + its own
> P0+P1 sound-edit layer, boot-randomized (also fixed a bug where P0/P2+S35
> was silently re-linking Basic Pitch's engine to the arp's — Phase 7);
> per-mode volume + FX send via a `VoiceGroup` enum replacing the old
> locked/arp_owned bools in VoicePool (Phase 8 — this superseded and
> deleted the old Rec knob morphs (4b) sketch); Rec-only S32-S35 (Speed/
> Shift/Chance/Order, Phase 9 — note_rec.h grew an independent Speed-scaled
> playback clock alongside the real-time recording clock, plus a
> non-destructive pitch-shuffle for Order); 5-layer mute/clear via P2+pad,
> replacing the old P0+pad/P0+P10-hold gestures (Phase 10). MANUAL.md
> updated to match. P2+P10 transport intentionally NOT split per SW1
> sub-state (confirmed staying tied, see C12).
> **Third design pass (21/07/26 follow-ups, §9 below) — implemented, not yet
> hardware-tested**: drive (S30) and blend (S37) split 3-way independent
> (Basic Pitch / Arp / Rec — volume and FX send already were, from §7;
> Phase 13, which also caught two more stale-pickup bugs). Reverb and delay
> upgraded from one shared instance with a per-group send *amount* to
> **four fully independent instances**, each its own send *and* character
> (Basic Pitch/Arp/Rec/Drum, Phase 14 — ~2.3 MB SDRAM, trivial against the
> 64 MB budget; each sleeps independently, same as before). A real bug where
> `RecordNote` returning false just because Rec was unarmed (not because it
> was full) fired a LIMIT blink on every audition touch — fixed; the layer
> gestures' LED language redesigned (mute = instant, clear = the same
> accelerating countdown used everywhere else); telemetry + the visualizer's
> action log now report layer mute/clear/commit live (Phase 15). Last: a
> stale-pickup bug in the SW2→Arp/Mel entry path — it always re-armed the
> Arp knob layer even when mode memory had `arp_state` already sitting on
> Rec, which could snap Speed (or drive) to wherever S32/S30 happened to be
> resting the instant you flicked back — fixed by branching on `arp_state`
> the same way `apply_arp_sw1()` already did, and folding the shared decay
> pickup into `rearm_rec_pickups()` so this class of gap can't recur at a
> third call site (Phase 16).

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

---

## 7. Second design pass (20/07/2026) — per-playmode independence

Recap of the `notes.md` "20/07/2026" entry, checked against the shipped
Phase 0–4a + first-hardware-round build (§6). C-numbers continue from §2;
decisions below are confirmed, not open.

### C7 — Per-playmode volume + FX send (new state, new voice-pool group)
Today `pitched_vol_lk` / `fx_rev_pitched_lk` / `fx_dly_pitched_lk` are single
scalars shared by Basic Pitch **and** Arp/Mel (`VoicePool::vol_pitched` etc.);
only Seq/drums has its own group. The note wants three independent volumes
and three independent FX sends — Basic Pitch, Arp, and Rec each separate
("REC is considered separate VOL and FX").
- `VoicePool::arp_owned[]` currently tags **both** arp and Rec-loop voices
  together (comment: "arp/Rec-loop voice"). Split into a small group id per
  voice (`kBP / kArp / kRec / kDrum`, replacing the bool) so `Render` can pick
  the right volume/send multiplier per voice, mirroring how seq vs pitched
  already works.
- New locked values: `bp_vol_lk` (rename of `pitched_vol_lk`), `arp_vol_lk`,
  `rec_vol_lk`; `fx_rev_bp_lk/fx_dly_bp_lk`, `fx_rev_arp_lk/fx_dly_arp_lk`,
  `fx_rev_rec_lk/fx_dly_rec_lk`.
- S36 and P1+S30/S35 pickups clone per mode/SW1-state (same pattern as
  `seq_pu36` vs `pitch_pu_vol`): arm on entry into BP / Arp / Rec, exactly
  like the existing seq-pickup-on-re-entry doctrine.
- `VoicePool` gains `SetArpVolume`/`SetRecVolume` and matching
  `SetArpReverbSend` etc. alongside the existing `SetPitchedVolume`.

### C8 — Sound: Rec gets its own slot, Arp's shipped seeding is kept
The note asks for Basic Pitch, Arp, and Rec to each start on an unrelated
random model at boot. Arp's current behavior — seed `arp_snd` from Basic
Pitch **once**, then fully independent — was a hardware-tested fix for a
real entanglement bug (§6) and is kept as-is; revisit only if asked.
**New scope, resolved:** Rec gets its own sound entirely.
- New `PadSlot rec_snd` + `rec_snd_ready`, parallel to `arp_snd`/`arp_snd_ready`
  but seeded to a **random engine** at boot (not lazily from BP or from the
  arp) — `generate_soft_random`-style pick, independent thereafter.
- New sound-edit entry point cloning the P0+P1 layer (`arp_snd_edit`):
  reachable only while SW1=Rec, e.g. **P0+P1 hold ~1s while in Rec** toggles
  `rec_snd_edit` the same way it toggles `arp_snd_edit` in Arp/Hold — same
  knob relabel (S30 drive / S31 decay / S32 harmonics / S33 timbre /
  S34 morph), same freeze-the-mode-functions-while-editing rule, same
  pickup hand-off. P0/P2+S35 model select and P0+P2 mutate work on
  `rec_snd` the same way they work on `arp_snd` today.
- Small, low-risk addendum also requested by the note: Basic Pitch's
  `current_engine` currently boots fixed to engine 0 (`TouchPlaited.cpp:197`).
  Randomize it at boot too, matching the "random at boot" ask for all three.

### C9 — Independent octave per mode (new per-mode state)
`octave_offset` (`TouchPlaited.cpp:199`) is a single global written by bare
P10/P11 taps and read by `compute_note()` everywhere. Split into
`bp_octave`, `arp_octave`, `rec_octave` (arp *range*, `arp_oct_range`, is
already separate and unaffected). P10/P11 write whichever one matches
`current_mode` + `arp_state` at the time — no new gesture, no combo
collision, just three stores instead of one, following the same per-mode
doctrine already used for `bp_slots`/`pad_slots`/`drum_slots`.

### C10 — Rec-only override on S32–S35 (confirmed: Rec-only, not shared)
S30 Drive and S31 Decay already match what the note wants and need no
change. S32–S35 take on **Rec-only** meanings, active only while
SW1=Rec; Arp/Hold keep today's Division/Swing/Density/Order on the same
physical knobs — same pattern Seq already uses for S30/S31 meaning
something different in slot-recording:
- **S32 → Speed**: 0% = 1×, 100% = 8× playback rate of the recorded loop.
- **S33 → Shift**: moves all recorded events in time as a block, including
  sub-step offsets (e.g. steps 1,2,3…16 → 14,15,16,1,2…13); wraps at the
  loop boundary.
- **S34 → Chance**: single global per-note playback probability, replacing
  Density's Euclidean-fill semantics for the duration of Rec.
- **S35 → Order**: collapses from the 5-mode Arp walk to 2 zones — left of
  center = recorded order, right of center = randomized order.
Each needs its own pickup arm (`rec_pu32`…`rec_pu35`) re-armed on every
SW1 transition into/out of Rec, exactly like the seq pot-pickup rule, so
flipping SW1 never jumps a value. The stale §3 "Rec knob morphs (4b)"
sketch (S31→stretch, S33→chance, S35→timing-nudge) is superseded by this
and can be deleted once this lands.

### C11 — Layer clear/mute: P2 replaces P0 (confirmed)
`P0 + pad 1–4` (clear one layer) and `P0+P10` held 1.5s (clear all) are
retired from Rec. New gestures, scoped to `arp_state == ArpState::REC`
(mirrors the existing P0-branch gating in `SetOnTouch`, `TouchPlaited.cpp`
~line 2103):
- **P2 + pad tap** (P3–P7, one more pad than today → 5 layers, matching
  `kMaxLayers` going 4→5 in `note_rec.h`) = mute/unmute that layer.
- **P2 + pad held** = clear that layer (replaces `P0+pad` tap).
- **P2 + ≥2 of P3–P7 held together** = clear all (replaces `P0+P10` hold).
- `P0+P10` tap stays **undo** only (unchanged); freed of the hold-1.5s
  clear-all meaning, so a stray long P0+P10 in Rec no longer wipes anything.
- `note_rec.h` needs a per-layer mute bitmask read at playback (skip muted
  layers' events) — new, doesn't exist today.

### C12 — Transport: P2+P10 stays tied together (confirmed, no change)
Kept as shipped: `P2+P10` stops/starts the arp and the Rec loop as one unit
via the single `arp_run_on` flag. No split by SW1 sub-state. Nothing to
implement here — flagging closed so it isn't re-opened by a future reading
of "should work per arp and melrec" in the notes.

---

## 8. Implementation plan for §7 (continues §5's phase numbering)

Each phase should compile/flash/be testable on its own, same discipline as
Phases 0–4.

### Phase 6 — Per-mode octave + boot randomization (C9, part of C8) — IMPLEMENTED
Split `octave_offset` into `bp_octave`/`arp_octave`/`rec_octave`, dispatched
by `active_octave()`; the background arp reads `arp_octave` directly so it
doesn't follow whatever mode is currently in view. `current_engine`
randomized at boot (skipping Chiptune). Extra fix beyond the original scope:
root note (bare P0+P10/P11) is now gated to Basic Pitch only, matching the
notes' "scale and root can only be set in Basic Pitch" — it previously
worked from any mode. NOT yet hardware-tested.

### Phase 7 — Rec sound slot + edit layer (C8) — IMPLEMENTED
`rec_snd`/`rec_snd_ready` (boot-randomized, unlike `arp_snd`'s lazy
first-entry seed, which is kept as shipped), `rec_snd_edit` toggle (P0+P1
while SW1=Rec, same combo as Arp/Hold — disambiguated by `arp_state`),
`rec_params()` alongside `arp_params()`. Also fixed a latent bug found while
wiring this: `process_model_select` was writing `current_engine` (Basic
Pitch's engine) unconditionally even when selecting a model for the arp,
silently re-linking the two on every P0/P2+S35 turn in Arp/Mel — now each
of BP/arp/Rec only ever writes its own engine field. NOT yet
hardware-tested.

### Phase 8 — Per-playmode volume + FX send (C7) — IMPLEMENTED
`VoicePool`'s `locked`/`arp_owned` bools replaced with a `VoiceGroup` enum
(kBP/kArp/kRec/kDrum) driving both the global-setter skip and the
volume/FX-send group lookup in `Render`. `arp_vol_lk`/`rec_vol_lk` and
`fx_*_arp_lk`/`fx_*_rec_lk` alongside the existing `pitched_*` (Basic
Pitch's) and `seq_*` locks; `active_pitched_vol_lk()`/`active_fx_rev_lk()`/
`active_fx_dly_lk()` dispatch S36 and P1+S30/S35 to the right one by
mode/`arp_state`. Width/blend intentionally NOT split at this point (notes
only asked for volume + FX send) — **blend was split later, Phase 13**;
width remains shared, never asked for. CC26/85/87 now unambiguously address
Basic Pitch only
(previously ambiguous "pitched," shared with the arp) — MANUAL.md's MIDI
section updated to match. NOT yet hardware-tested; this is the
highest-blast-radius phase (touches `VoicePool::Render` for every voice).

### Phase 9 — Rec-only knob override (C10) — IMPLEMENTED
`rec_pu32`–`rec_pu35` pickups, re-armed (alongside `arp_pu*`) on every SW1
transition across the Rec boundary in `apply_arp_sw1`. `note_rec.h` grew a
second, independently-rateable playback clock (`play_tick_`/`play_tick_f_`,
scaled by `SetSpeed`) alongside the pre-existing real-time recording clock
(`cur_tick_`) — Speed only affects already-committed playback, live
recording input stays real-time. Shift offsets the playback comparison
tick (wraps). Chance rolls per-hit, per-pass. Order-random is a
non-destructive shadow-pitch shuffle (`shuf_note_`) so flipping back to
"original" always exactly recovers what was recorded. NOT yet
hardware-tested — Speed/Shift interacting with a live overdub in progress
is the part most likely to need a hardware round.

### Phase 10 — Layer mute + gesture migration (C11) — IMPLEMENTED
`kMaxLayers` 4→5 in `note_rec.h`, `mute_mask_` + `ToggleMute`/`IsMuted`,
`ClearLayer` keeps the mask in sync when compacting. P2+pad (P3-P7)
tracked with per-pad hold counters (`p2layer_hold`/`p2layer_fired`,
file-scope so both `AudioCallback` and the `SetOnRelease` touch callback can
see them): reaching the hold threshold fires ClearLayer or, if >=2 pads are
down at that exact moment, ClearAll; releasing before the threshold fires
ToggleMute instead. Old P0+pad / P0+P10-hold-1.5s gestures removed —
P0+P10 tap stays Undo-only. NOT yet hardware-tested — the tap/hold/
multi-hold disambiguation on 5 pads is the densest new gesture surface
here and should get real playtesting before calling it done, same caveat
as the original P0+P1 sound-edit gesture.

### Phase 11 — Docs sweep — PARTIALLY DONE
MANUAL.md updated: Arp/Mel control tables (both the glance table and the
detailed knob-function section), Rec-only knob table, layer-gesture table,
per-mode octave/volume/FX notes, MIDI CC descriptions, quick-tutorial S31→
S32 fix. NOT done: visualizer webapp **knob-layout** labels (still shows the
pre-refactor Arp/Hold knob layout while sitting in Rec, and has no per-mode
vol/FX display), README, telemetry (`t.model`/`t.snd_edit` don't yet
distinguish arp vs Rec sound for the visualizer — pre-existing gap, not
introduced by this pass but not closed either). Layer *state* (count + mute
mask) specifically was later closed by Phase 15 — the remaining gap here is
narrower than when this phase was written.

### Phase 12 — Rec capture arming (2026-07-21 follow-up) — IMPLEMENTED
Found during review, not in the original 20/07 notes: Rec's independent
sound (Phase 7) made SW1's old "entry starts listening immediately"
behavior actively harmful — auditioning an unfamiliar random model meant
touching a pad, which recorded a note whether you wanted it to or not.
Fix: new `rec_armed` bool, always false on a fresh SW1→Rec entry; pads
always sound (unchanged) but `NoteRec::RecordNote` only captures while
armed (already internally gated on `recording_`, so no call-site changes
needed there — only `note_rec.SetRecording()`'s call sites moved). **P2+P10,
scoped to SW1=Rec only, now arms/disarms instead of its usual transport
toggle** — a deliberate, narrow exception to C12 ("P2+P10 stays tied
together, not split per SW1 sub-state"): C12 was about whether P2+P10
targets the arp or the loop, not about giving Rec a second, unrelated
meaning for the same combo, so this doesn't reopen it. Disarming does NOT
stop playback (confirmed via user Q&A) — committed layers keep looping so
you can listen back before punching in again; only new capture stops, and
the open take commits. Arming force-sets `arp_run_on = true` so the clock
is guaranteed to actually advance. MANUAL.md updated (Arp/Mel control
tables, the sub-state description, both Transport sections, quick
tutorial). NOT yet hardware-tested.

---

## 9. Third design pass (21/07/26 follow-ups)

Four more requests/bugs surfaced in review after §7/§8 landed, all against
the shipped Phase 6–12 build. Numbering continues the phase sequence.

### Phase 13 — Fader independence: drive (S30) + blend (S37) — IMPLEMENTED
Volume and FX send were already Basic Pitch/Arp/Rec-independent (§7); drive
and blend weren't — drive was shared between Arp and Rec (`arp_drive_lk`),
blend was shared across all three pitched contexts (`pitched_blend_lk`).
- New `rec_drive_lk`, `arp_blend_lk`, `rec_blend_lk`; `active_blend_lk()`
  dispatcher mirrors `active_pitched_vol_lk()`. New pickups `rec_pu30`,
  `arp_pu_blend`, `rec_pu_blend`.
- `rearm_pitched_vol_pickup()` renamed `rearm_pitched_faders()` and now arms
  both volume and blend pickups together — the two call sites that used to
  arm blend separately (`rearm_seq_pickups`, the SW2 handler) were folded in.
- Two more stale-pickup bugs caught while wiring this: Rec's sound-edit S30
  (`rec_se30`) was arming/writing `arp_drive_lk` instead of `rec_drive_lk`;
  `exit_rec_snd_edit()` was calling `rearm_arp_pickups()` (the wrong knob
  set entirely) instead of `rearm_rec_pickups()`. Both existed since Phase 7
  and would have surfaced as knob jumps on any hardware round that actually
  exercised Rec's sound-edit layer.
- Decay (S31) and P0-held width stay the only genuinely shared pitched
  controls — width was never asked to split; decay is a deliberate choice
  (same knob, same meaning, in Arp/Hold and Rec alike).

### Phase 14 — Four independent FX instances — IMPLEMENTED
"Does that work?" — asked for reverb/delay *character*, not just send
amount, independent per Basic Pitch/Arp/Rec/Drum (previously: one shared
`Reverb`/`StereoDelay` pair with per-group send amount and a "last edit
wins" shared character — see the pre-§7 FX implementation notes in
`notes.md`).
- `synth/fx.h`: `FxGroup` enum (kBP/kArp/kRec/kDrum) + `FxSection` takes a
  group in its constructor; `extern FxSection fx_bp, fx_arp, fx_rec, fx_drum;`
  replaces the single `extern FxSection fx;`.
- `synth/fx.cpp`: `Reverb`'s 64 KB buffer and `StereoDelay`'s 2×256 KB
  buffers moved from file-scope globals to instance members, so
  `Reverb reverb_impl[4]` / `StereoDelay delay_impl[4]` (SDRAM) give each
  group fully independent state — including independent sleep, which
  already existed per-instance and just needed the 4x to carry over.
  `FxSection`'s methods dispatch through `reverb_impl[group_]` /
  `delay_impl[group_]`.
- `synth/voice_pool.h`: new `FxBuses` struct (4 send-bus pointer pairs each
  for reverb/delay); `Render()`'s signature collapsed the old 6 flat
  buffer args into `(out_l, out_r, const FxBuses&, size)` and routes each
  voice's contribution to `buses.rev_l[group]` etc. by `voice_group[i]`
  instead of one shared pair.
- `TouchPlaited.cpp`: `AudioCallback` now holds 4×4 send buffers
  (`rev_l[4][kBlockSize]` etc.), builds a fresh `FxBuses` per 24-sample
  render chunk, and calls `Process{Delay,Reverb}` on all four `fx_*`
  instances after `pool.Render`. The FX-decode block simplified — each
  group's own mirror-knob lock now decodes straight into that group's own
  `SetReverbCharacter`/`SetDelayCharacter`, so the old shared
  `fx_rev_char_lk`/`fx_dly_char_lk` locks were deleted entirely rather than
  ported forward.
- Cost: SDRAM 576 KB → 2.25 MB (4× the old single-instance footprint),
  trivial against the 64 MB budget. CPU is the real question — unmeasured
  on hardware. Each instance still sleeps independently when its own send
  is quiet (unchanged mechanism, just 4 instances of it now), and the
  existing load-shed guard is the safety net if a worst-case moment (all
  four wet at once, heavy engines) gets tight — same risk-acceptance
  pattern already used for the 6-voice expansion. **This is the one item
  from this pass most likely to need a real hardware CPU-meter round.**
- CC85–88 now unambiguously mean Basic Pitch/drums only (no more "last edit
  wins" ambiguity with the arp) — a side benefit, not a regression: the
  arp's and Rec's FX were already MIDI-unreachable, same as their sound.

### Phase 15 — LED bug fix, LED redesign, telemetry + visualizer log — IMPLEMENTED
Three related fixes/additions from one review pass:
- **Bug**: `RecordNote()` returning `false` while simply unarmed (not
  because the recorder was full) was read by the pad-touch handler as
  "limit reached" and fired a LIMIT blink on every audition touch in Rec —
  a direct regression from Phase 12's `rec_armed`. Fixed by only consulting
  `RecordNote`'s result while `rec_armed` is true; unarmed touches just
  sound the pad now, no LED reaction at all.
- **LED redesign**: mute/unmute (P2+pad tap) fires its NUMBERED blink
  instantly, no lead-up. Clear-that-layer and clear-all (P2+pad hold) now
  show the same accelerating-countdown language already used for every
  other hold gesture in this firmware (P0+P2 randomize, the old rec-entry
  countdown, copy-hold) while building, and diverge only at the finish —
  NUMBERED (layer number) for a single clear, CONFIRM (3 rapid blinks) for
  clear-all — so a tap and a hold-in-progress are unmistakable well before
  either resolves. New per-pad state (`p2layer_hold`/`p2layer_fired`, now
  `volatile` since the main loop reads them for the countdown) was already
  in place from Phase 10; this just added the live animation and stopped it
  claiming the loop the instant a hold actually fires, so the queued
  completion blink can dispatch through the normal path.
- **Telemetry**: STATE frame grew 19 → 21 payload bytes — `rec_layers`
  (NoteRec committed count, 0–5) and `rec_mute` (bitmask) — append-only,
  guarded by `p.length >= 21` in `protocol.ts`, no version bump, same
  pattern as every earlier STATE extension.
- **Visualizer** (`visualizer/src/`): `DeviceStore.setRecLayers()` carries
  both the new and previous (count, mask) so `labels.ts` can classify a
  layer-count drop as a clear (or "all layers cleared" from >1 straight to
  0), a rise as a layer committed, and an unchanged count with a flipped
  mask bit as a mute/unmute — without re-deriving state, and without
  fragile inference from LED timing alone. A small `N layers` status chip
  was added alongside the existing sound-edit/rec-slot indicators. Verified
  with `tsc --noEmit` and a full `vite build` (both clean) — not run in a
  browser. NOT done: a persistent per-layer indicator on the panel drawing
  itself (5 dots showing filled/muted/empty) — parked, would need
  `panel.ts`/SVG layout work. `visualizer/PLAN.md` §6e has the fuller
  writeup.

### Phase 16 — SW2→Arp/Mel re-entry knob-jump bug fix — IMPLEMENTED
Reported as "in Rec mode do not alter Speed until S32 is used to alter it."
Root cause: the SW2 mode-switch handler's Arp/Mel-entry block always called
`rearm_arp_pickups()` regardless of the *current* `arp_state` — fine the
first time you ever enter (arp_state defaults to `ArpState::ARP`), but
`arp_state` persists across SW2 round-trips (mode memory), so re-entering
Arp/Mel while already sitting in Rec left `rec_pu30`/`rec_pu32`–`35`
un-rearmed. If those pickups had already caught in an earlier Rec session,
they'd just keep tracking the live knob from then on — so flicking
Basic Pitch → Arp/Mel back into Rec could silently snap Speed (or Shift/
Chance/Order/drive) to wherever S32 (or S30/33/34/35) happened to be
resting, with no knob move at all.
- Fix: branch on `arp_state` at the SW2-entry site exactly like
  `apply_arp_sw1()` already does — `rearm_rec_pickups()` if `arp_state ==
  ArpState::REC`, else `rearm_arp_pickups()`. Also reset `rec_snd_edit =
  false` there (only `arp_snd_edit` was reset before), matching the "always
  enter in play" rule for both sound-edit layers.
- While fixing it, folded the shared decay pickup (`arp_pu31`) into
  `rearm_rec_pickups()` itself, since it's the one control genuinely shared
  between Arp/Hold and Rec and was getting armed piecemeal at each call
  site (this SW2 handler, `apply_arp_sw1`, `exit_rec_snd_edit`) — one bug of
  this exact shape (the Phase 13 `exit_rec_snd_edit()` fix) had already
  shown up once; centralizing removes the chance of a fourth call site
  repeating it.

### Risks
- **Phase 8** is the one likely to eat the CPU/voice-render budget most —
  it's a hot-path change (`VoicePool::Render`), not just new state; budget
  it alongside the reverb/delay headroom notes in "Reverb / delay FX send —
  resource analysis" above.
- **Phase 10**'s five-pad P2 gesture (mute vs clear vs clear-all,
  disambiguated by tap/hold/multi-hold) is the densest gesture surface
  added since P0+P1 sound-edit — needs hardware feel testing before
  calling it done, same caveat as §5's "Gesture density" risk.
- Phases 9 and 10 both touch `note_rec.h` — sequence them so Phase 10's
  `kMaxLayers` bump lands before Phase 9's Speed/Shift playback math is
  tuned against loop length, to avoid re-deriving timing constants twice.
- **Phase 14** now edges out Phase 8 as the top CPU risk: four independent
  FX instances is a real multiplier on top of an already-hot render path,
  even with per-instance sleep and the load-shed guard as a backstop. First
  hardware round for this pass should specifically try to get all four
  groups wet at once during a dense passage (Six-Op/Modal/String voices +
  a busy drum seq) and watch the `shed N` serial line.
- This whole third pass (Phases 13–16) shares the caveat every phase above
  it already carries: **implemented, not yet run on hardware.** Given how
  many of the bugs in this pass were stale-pickup issues invisible without
  actually turning the knobs in the affected sequence, a hardware round
  that specifically exercises mode round-trips (Basic Pitch ↔ Arp/Mel with
  `arp_state` parked on each of Arp/Hold/Rec) is worth prioritizing over a
  purely feature-by-feature pass.
