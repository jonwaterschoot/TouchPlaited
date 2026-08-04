# Notes archive — August 2026

Resolved/historical material moved out of `ROADMAP.md`/`notes.md` during
August 2026, per the archiving convention described at the top of
`ROADMAP.md`. Earlier material: `notesarchive/notes_archive_2026-07.md`.

---

## OLED screen — parity + held-combo progress bar (moved from `ROADMAP.md` 2026-08-04)

**Parity with the visualizer — status (2026-07-24):**

- Text content (label/value strings): `display/oled_ui.cpp`'s `describe_control()`/
  `describe_pad()` is a line-by-line port of `visualizer/src/panel/labels.ts`'s
  `describeControl()`/`describePad()`, operating on the same `TelemetryState`
  wire fields the visualizer decodes — verified by inspection, not just belief.
  Three **deliberate** differences, documented in `display/oled_ui.h` and
  `oled_ui.cpp` at the point each happens — not bugs, don't "fix" without
  deciding the UX first:
  - Rec drum-slot editing uses flatter "Slot X" labels vs. the web's full
    per-knob nesting (128×32 char budget).
  - The dead-knob message is "no effect" instead of "no effect on \<model>".
  - Hardware has no idle/status fallback (still open — tracked in
    `ROADMAP.md` "OLED screen").
- Rendering (fonts/pixels): fixed 2026-07-24 — `oled-mini.ts` now blits the
  actual firmware bitmap fonts (`visualizer/src/panel/oled-font-data.ts`,
  ported from `lib/libDaisy/src/util/oled_fonts.c`) with the same discrete
  Font_11x18 → Font_7x10 → Font_6x8 stepping `display/oled_screen.cpp`'s
  `ShowLine()` uses, instead of a system font rasterized then thresholded.
  Small text was illegible before this — see
  `notesarchive/notes_archive_2026-07.md` → "SSD1306 128×32 OLED" for the
  bring-up bugs that motivated the overhaul.

**Held-combo progress bar — implemented 2026-07-24, confirm flash added same
day.** `TelemetryState` gained `hold_kind`/`hold_progress`/`hold_stage`/
`hold_outcome` (`midi/telemetry.h`, SysEx STATE payload bytes 23-26,
`visualizer/PLAN.md` §2 + `protocol.ts` updated in lockstep).
`TouchPlaited.cpp`'s `compute_hold_telemetry()` mirrors the LED loop's own
precedence exactly (P0+P2 hold > rec entry > layer clear > layer copy) so the
number on the wire never disagrees with what the LED is already blinking.
`OledScreen::ShowProgress()` / `oled-mini.ts`'s `showProgress()` draw the same
outlined-then-filled bar geometry on both sides, and a hold owns the screen
unconditionally while active (`OledUi::Service` returns early on `hold_kind
!= 0`; `oled-mini.ts`'s `show()` no-ops while `progressMode` is set) — no
knob/pad callout can steal the bar mid-hold on either side.

Each threshold crossing gets a distinct confirm: `hold_stage` counts
confirms fired so far for the current hold (0 while building, then
increments and stays there for as long as the gesture stays held) — both
sides edge-detect a rise against the previous frame and swap the bar for
text (`ShowLine`/`confirmFlash()`, held open ~220ms — `kConfirmFlashMs` /
`CONFIRM_FLASH_MS`) before releasing back to the bar or the normal chain.
P0+P2 is genuinely multi-stage (2 segments normally, 3 in Pitch mode) — its
progress is rescaled *per stage* so every segment fills its own 0..100%
independently, fixing a real bug where it used to stall around 67% in the
common 2-stage case (was scaled against the 3-stage-only 750-block ceiling).
Each stage flashes "Stage N"; the other three gestures are single-stage
("Recording", "Copied", "Cleared"/"Empty"). Layer clear's `hold_outcome`
distinguishes a real clear from a no-op (holding a pad with nothing
recorded) — the LED already tells these apart (NUMBERED vs LIMIT blink) so
showing "Empty" as if it were a success would have been actively
misleading. `entry_just_confirmed`/`copy_just_confirmed`/`p2layer_outcome[]`
(`TouchPlaited.cpp`) exist because rec-entry and layer-copy reset their hold
counters in the same ISR call that fires them — a level-polling read would
never observe the exact completion frame without a latched flag.

Deliberately **not covered**: the rec-pad confirm hold (`rec_hold_count`) and
the P0+P1 sound-edit toggle (`rec_snd_edit`) — unlike the four gestures
above, neither has an existing LED build-up animation to key off (both fire
a single confirm blink with no preceding countdown), so there's no
established "this is what building-up looks like" to extend. Tracked as
"Missing confirmations" in `ROADMAP.md` → "OLED screen" → per-mode list.

---

## Density + chance axis via S32 — superseded, not implemented (moved from `ROADMAP.md` 2026-08-04)

The original idea (`ROADMAP.md` Priority 2) proposed splitting S32 into a
0–0.45 "less density" / 0.5 "normal" / 0.55–1 "chance/mutation per step"
range, changing seq tick logic. By the time this was revisited, Seq mode's
knob layout had already settled with S32 = shuffle and S33 = density as two
independent knobs (`TouchPlaited.cpp` "Seq layout" comment, ~line 1996) —
there's no free S32 range left to repurpose this way, and density already
has its own dedicated knob. The underlying goal (less repetitive patterns
without hand-authoring dozens of variants) is still tracked in `ROADMAP.md`
Priority 2 under "a less repetitive feel, and freeing up S34" — via
per-step chance and a rethought S34, not a knob split.
