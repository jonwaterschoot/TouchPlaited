# TouchPlaited — Roadmap

**Wrapped up on 2026-11-14.** No new features and no optimising are planned.
The firmware, the manual, the Play page and the code map describe the same
build.

The roadmap as it stood that day, with every parked idea and its analysis, is
in `notesarchive/roadmap_2026-11_archive.md`.

## Where it ended

- **The full firmware is a lot to play.** The drum sequencer under a melody
  runs out of voices, and the stealing is audible. The control surface takes
  the manual and the OLED to learn. The README recommends the simple way in:
  Basic Pitch only, P0 or P2 + S35 for the model.
- **A light version** (melody only, or drums only) is the better instrument.
  It would be a new firmware, not a change to this one.
- **Kick bank**: listened to on hardware, the Tone/Punch/Body ranges stay.
- **Code map**: checked against the source and the build's map file on
  2026-11-14.
- **Play page**: linked to the latest library version, live link included.
  The board stays read-only from the page.

## Still open

Not feature work. These stay here until they close.

- [ ] **libDaisy #707, SSD1306 128×32 column start.** Open upstream, waiting
      on review since 2026-08-08. Nothing to do but wait.
- [ ] **libDaisy #708, `HAL_UART_ErrorCallback` freezes the main loop.** v3
      pushed 2026-08-09, waiting on review. The hardware results, the
      mechanism and the re-test builds are in the archived roadmap, section
      *Upstream — libDaisy PRs*.
- [ ] **Fork sync, after #707 merges.** Merge `upstream/master` into the
      Synthux fork's `master`, rebase `touchplaited-pin` onto it, re-pin the
      submodule here, then `make -C lib/libDaisy`. Background: `notes.md` →
      *Upstream libDaisy*.

## Known limits, not planned

- **CV clock thresholds** (S43 in, S40 out) were never checked against a real
  CV source.
- **The OLED and the LED froze once** while audio, pads and knobs kept
  running (2026-08-10). Never reproduced. `notes.md` → *Bugs*.
- **Decay is the only envelope control.** The ADSR analysis is in the
  archived roadmap, section *Sound*.
- **Ghost notes play at full level** on the internal drums. Only MIDI out
  carries the accents.
- **MIDI Start/Stop always drives the drum sequencer.** There is no way to
  ignore it.
- **OLED**: `P1+P10 UNDO LAYER` shows an empty value row, the arp's pool row
  redraws late on pad touch, and Basic Pitch doesn't show which held note was
  dropped.

## History

- `notes.md`: design decisions, measurements and the working log.
- `notesarchive/`: finished rounds. `readme.md` there tells the story from
  the first prompt, `roadmap_v1_archive.md` has the steps that built v1.
