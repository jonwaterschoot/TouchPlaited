# TouchPlaited — Roadmap

Working prototype is running. Full implementation history is in `notesarchive/plan_archive.md`.

The description of the prompting approach is in `notesarchive/readme.md`

Open questions and feature details are in `notes.md`. 

---

## Priority 1 — Fix what's broken or confusing

- [ ] **Sequencer full hardware test** — tempo range, shuffle feel, genre distinguishability, density edge cases (0=silence, 4=max), manual trigger on top of running seq, P0+P2 re-randomize while seq plays
- [ ] **Decay in drum mode (seq)** — S37 does nothing for engines 21–23 (they use morph). In seq mode, remap S37 → `patch.morph` for drum engine slots. See notes.md Known Issues.
- [ ] **Recording mode confirm conflict** — decide which option (a/b/c/d) to implement (see notes.md Open Decisions §1) and fix. Leaning toward pad-alone ≥500ms = confirm.
- [ ] **Six-Op audition** — create a `kModelAuditionParams[]` table with known-good param values per engine for the audition tone. Six-Op silent at random values is a hard UX problem.

## Priority 2 — Quick improvements

- [ ] **Distortion in Basic Pitch mode** — route S31 soft-clip drive to Basic Pitch output. Already exists in drum path; small change.
- [ ] **LED blink on model load** — confirm the engine switched (brief single blink). Helps with Six-Op and any model that doesn't produce obvious sound at current params.
- [ ] **Per-pad stored params — S30/S31** — decide and implement (see notes.md Open Decisions §2). Store FM amount + drive per slot in pitched random modes.
- [ ] **Per-pad volume (S36 in rec mode)** — decide and implement (see notes.md Open Decisions §3). S36 = global volume normally, per-pad volume in recording mode.
- [ ] **Chiptune engine decision** — keep, remove from selectable list, or remap pots (see notes.md Open Decisions §5).

## Priority 3 — Larger features

- [ ] **Drum mode editing redesign** — last-touched pad as active editing slot (see notes.md Open Decisions §4). Decide model (last-touched quick edit + recording mode for deep edit, or full replacement).
- [ ] **Density + chance axis** — split S32 so center=normal, below=less density, above=more randomness/mutation.
- [ ] **Electro pattern redesign** — sketch the intended pattern manually before touching code.
- [ ] **More pattern variation** — rhythm mutation gesture (mutate weight tables, not just re-randomize sounds).

## Priority 4 — Explore / future

- [ ] Sequencer on any playmode (melodic mode)
- [ ] Live record into pattern (audio buffer vs note record — evaluate routes)
- [ ] Six-Op parameter check in Soft/Full Random (hardware verification pass)
- [ ] Phase 8F retry (controls out of ISR) — only if crackle returns

See `notes.md` Parking Lot for deferred hardware/feature additions (OLED, USB MIDI, persistent state, etc).
