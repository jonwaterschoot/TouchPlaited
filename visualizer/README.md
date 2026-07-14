# TouchPlaited Visualizer

Live on-screen mirror of the TouchPlaited panel for video tutorials: pads light
up when touched, knobs and faders move, the user LED blinks like the real one,
and contextual labels (P0, S35 · Timbre · 64%…) point at whatever is being used.
Design and protocol details: [PLAN.md](PLAN.md).

## Run

```sh
npm install
npm run dev        # http://localhost:5173
```

- **Demo** button: scripted state loop, no hardware needed.
- **Connect MIDI** button: live link to the device (Chrome/Edge; grant the SysEx
  permission). Requires firmware built with telemetry (`-DUSB_MIDI`, default).

## URL flags

| Flag | Effect |
|------|--------|
| `?demo` | autostart the scripted demo |
| `?midi` | autoconnect Web MIDI |
| `?transparent` | transparent background — for OBS browser-source overlays |
| `?bare` | hide the toolbar |
| `?view=pads` / `?view=panel` | crop to the pad field / the knob panel |
| `?zoom=1.5` | scale everything |
| `?drawer` | open the MIDI drawer (CC faders + piano) on load |

**OBS setup**: add a Browser Source pointing at
`http://localhost:5173/?midi&transparent&bare` — the panel floats over your
footage with no visible browser window.

The live-info panel (model / mode / step, the **last-4 action log** with smart
gesture names like `P0 + S35 · Model select · bank 0`, and the FX readout) is
**draggable** — put it wherever the shot needs it; position and size (corner
grip on hover) are remembered. Double-click resets both.

P10 and P11 are the two halves of the **TouCH logo** ("Tou" / "CH") — the
letters themselves light up when touched, like every other pad.

## MIDI *to* the device

- **Click the drum pads** (P3–P9) on the drawing: sends the pad's GM drum note
  on ch 10 — they flash blue to mark app-sent hits. (Pitched pads depend on
  scale/octave/root state the host can't know; use the piano for those.)
- **MIDI drawer** (toolbar button or `?drawer`): a fader for every CC the
  firmware listens to (20–31, 85–88; center-off FX CCs start at 64,
  double-click resets them) plus a 3-octave **piano** on ch 1 with octave
  shift.
- Incoming note traffic is used too: the device reports the *actual* pitch it
  plays (ch 1), and the app attaches it to the touched pad's label —
  `P5 · Play note · G4 · 67`.

## Panel asset

`assets/panel.svg` is generated — don't edit it by hand:

```sh
npm run panel      # re-derive from ../img/simpletouchdrawing_blank.svg
```

The script (`tools/build-panel-svg.mjs`) stamps stable ids onto the Illustrator
export by matching geometry, and fails loudly if the artwork moved.
