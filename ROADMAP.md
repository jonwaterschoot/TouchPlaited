# TouchPlaited — Roadmap

**v1 stable — 2026-07-08.** The numbered implementation steps that built it
(Steps 1–15: SW2 mode restructure → recording redesign → soft-clip output →
mode memory → background drum seq → voice expansion + sleep + shed guard →
MIDI notes/CC/clock) are archived verbatim in
`notesarchive/roadmap_v1_archive.md`. The workflow story since the very first
prompt is in `notesarchive/readme.md`.

Design decisions, budget analyses, and implementation write-ups live in
`notes.md`. **This file is the single owner of future work** — ideas and
priorities live here and nowhere else; a roadmap item links to its analysis in
`notes.md` when one exists.

---

## Open verification (carried over from the v1 steps)

Hardware checks still unticked when the steps were archived. Daily use has
likely covered much of this — tick or delete as confirmed, the detailed
per-step lists are in the archive.

- [ ] MIDI clock (Step 13): DAW → TouchPlaited sync incl. shuffle feel under external clock; tempo knob dead while synced + fallback after clock stops; TouchPlaited → drum machine sync (clock out + Start/Stop); chained pass-through
- [ ] Soft-clip level (Step 6): single Basic Pitch voice at center is comfortable
- [ ] General verification sweep (Step 7 list in the archive): modes, recording, per-slot volume/drive, unified decay, Six-Op audibility

## Priority 2 — after the open verification

- [ ] **Per-track pattern variants — S35 in Seq mode** (feasibility decided 2026-07-03: **variant bank wins over generative**). S35 is free in Seq mode (model select disabled there). Hold a pad + turn S35 (quantized zones, deadzone pickup like the bank select) → pick that track's pattern variant from hand-authored rows: `kSeqWeights` grows a variant dimension, `Sequencer` gets `variant_[7]`. Flash cost trivial (~5 KB for 4 variants × 7 tracks × 3 genres); keeps genre feel; S35 position is repeatable. Phase authoring: 2 variants per track first. A generative variant (Euclidean E(k,16) + rotation) can be the last slot per track later. UI guard required: S35 move past deadzone while a pad is held must reset the rec-entry hold counter or it collides with 1.2 s Recording entry.
- [ ] **Density + chance axis** — S32 split: 0–0.45 = less density, 0.5 = normal, 0.55–1 = chance/mutation per step. Changes seq tick logic.
- [ ] **LED blink on model load** — brief single blink when engine changes via S35. Helps confirm Six-Op switches.

## Priority 3 — feature additions

- [ ] **Reverb/delay as per-slot FX send** (analysis 2026-07-08, see notes.md "Reverb / delay FX send — resource analysis"; **implement on a branch**). Resources: memory is a non-issue (64 MB SDRAM at 0 bytes used — put the reverb buffer there, DaisySP `ReverbSc` style; ~250 KB internal SRAM also free); CPU is the budget — delay ~1–2%, reverb ~6–10%, as a *fixed* per-block cost on top of peaks already at 96–98%, so expect more `shed N` under dense load unless paired with the ITCM code-placement lever (64 KB unused) and/or FX sleep. Design: `send` field on `PadSlot`/`VoiceParams` + `voice_send[i]`, second stereo bus accumulated in `VoicePool::Render`, per-group `send_seq`/`send_pitched` multipliers mirroring `vol_seq`/`vol_pitched` — gives dry-ish drums in Seq vs. long tail in Basic Pitch with mode-memory for free; reverb processed on the send bus in `AudioCallback` before the soft-clip. One shared reverb, per-group sends (not per-mode instances).
- [ ] **P1 = melodic seq trigger** — in Basic Pitch / Random modes, P1 toggle arms the drum sequencer to fire its patterns against the current scale/pad layout (each drum track → scale degree; drum weight tables apply to scale degrees). P1 is reserved for this and currently unused in those modes. Big change, design first.
- [ ] **Rhythm mutation gesture** — mutate the weight tables (changes rhythm, not just sounds). No current gesture available; needs a button combo or context-dependent trigger.
- [ ] **4-bar fill** — P1 held in seq mode triggers a max-density bar then returns to normal.
- [ ] **Live record into pattern** — two routes: audio buffer loop (complex controls) or live trigger capture (simpler; can't layer on top). Not designed yet.
- [ ] **MIDI drum pitch phase 2** — ch10 notes within ±6 of a slot's GM anchor play that slot transposed.

## Parking Lot

- **Bare S35 + P1 kept free — earmarked for melodic/arp or FX** (2026-07-09) — bare S35 does nothing in Basic Pitch / Random today. Rather than more model-select surface, spend these on the **melodic seq trigger** (P1 toggle, Priority 3) or the **reverb/delay FX send** (Priority 3) — e.g. bare S35 as send level in the pitched modes: continuous, non-destructive, and a natural knob-shaped parameter. **Ergonomics constraint: P1 is hard to reach without modding the device** — suit it to set-and-forget toggles only (mode arm, FX on/off), never held or performative gestures. ⚠ The *4-bar fill* idea above assumes holding P1 in Seq — reconsider that gesture. Shelved ideas for the record: all 24 engines on bare S35 (~4% travel per engine, jitter-prone, destructive in Random where an engine change regenerates all 7 slots), P1 + P10/P11 model ±1 stepping, hold-P1 + S35 as a third knob layer.
- **ITCM placement** — move the hottest Plaits render paths into ITCMRAM (64 KB, 0% used); code currently executes from QSPI. Enabler for both the FX send and a 7th voice.
- **Expand voice pool to 7** — after ITCM placement confirms the headroom on in-use engines.
- **OLED screen** — I2C 128×32/64 add-on; shows engine name, params, mode, root note. V2 hardware.
- **Persistent state** — libDaisy `PersistentStorage` for last model + pad slots across power cycles.
- **Chord mode** — single pad triggers multiple Plaits voices at scale intervals.
- **Mod-pad mirror on MIDI ch16** — remaining idea from the MIDI implementation.
- **Phase 8F retry** — controls out of ISR; needs `__disable_irq()` / `__enable_irq()` wrapping all `generate_*()` calls. Only if crackle returns at kBlockSize=192.
