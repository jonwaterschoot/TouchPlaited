# TouchPlaited Visualizer

Live on-screen mirror of the TouchPlaited panel for video tutorials: pads light
up when touched, knobs and faders move, the user LED blinks like the real one,
whatever you're using glows, and an **OLED-style screen** on the faceplate
(the free zone between the knobs) names it — `S33 · Timbre · 64%`.
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
| `?bare` | hide the toolbar and the settings bar |
| `?view=pads` / `?view=panel` | crop to the pad field / the knob panel |
| `?zoom=1.5` | scale everything |
| `?drawer` | open the MIDI drawer (CC faders + piano) on load |

**OBS setup**: add a Browser Source pointing at
`http://localhost:5173/?midi&transparent&bare` — the panel floats over your
footage with no visible browser window.

The live-info panel (model / mode / step, a collapsible **knob map** showing
what every pot/fader does right now with engine-aware values, the drum kit in
Seq, the **last-4 action log** with smart gesture names like `P0 + S35 · Model
select · bank 0`, and the FX readout) is **draggable** — put it wherever the
shot needs it. Grab its **⠿ info** title bar to move it; the corner grip
resizes the box (text wraps and scrolls, it never scales). **Hovering a
knob-map or kit row highlights that control on the drawing.**

The **device drawing** moves too: drag any non-pad area, pinch or scroll to
zoom, or use the **⠿ grip** pinned to its top-left corner (there is no
double-click reset — double-tapping a pad plays it, nothing else).

**Settings bar** (top-left, ⚙ collapses it — collapsed by default on phones):
every display setting in one place, rather than the two rows these used to be
split across. The label-overlay mode, **A− / A+** for the faceplate label &
screen text, **A− / A+** for the info panel's font (both 0.6–2.2× in the same
steps), and one **⟲** that resets the drawing's position and zoom, the info
panel's position, size and font, and both text scales.

**The OLED screen** sits between the knob columns — a true-to-hardware 128×32
emulation, drawn dot by dot from the firmware's own bitmap fonts. It rides the
drawing at every size: drag or zoom the panel and the screen goes with it.

**The expanded display** (menu → *Expanded display*) is the optional companion
on the Daisy silhouette. Top row: model · mode · transport (the classic yellow
strip). Below it, the last three actions, newest at the bottom — the control
that moved also glows on the drawing, so nothing pops up next to the controls
to overlap anymore. Values are pinned to the screen's right edge; quantized
settings show their names (Six-Op patch x/32, chord types, arp Order
Played/Up/Down/Ping-pong/Random), and modifier holds (P0/P1FX/P2) show their
hint without spamming the action log. It stops short of the board's right end
so the **user LED stays visible** — that LED carries the limit and state
blinks, and they read nowhere else on the drawing.

**Label overlays** — the first button in the settings bar cycles three modes: `dyn`
(default, no static labels — glow + screen only), `S#` (permanent designators
S30…S37 / P0…P11 / SW1-2 on the panel), and `Aa` (permanent full labels of
the current mode and model in a condensed faceplate font — pads show their
drum role in Seq or their live note name in the pitched modes). Knob
designators sit inside the knob caps; the labels follow the drawing when you
drag, zoom or resize it.

P10 and P11 are the two halves of the **TouCH logo** ("Tou" / "CH") — the
letters themselves light up when touched, like every other pad.

## On a phone

The drawing sizes itself to the viewport — no pinching to get at it. In
portrait it sits at the top of the screen and the info panel takes the strip
below it. Two menu entries matter here:

- **Keep screen awake** — a Screen Wake Lock, which is the only thing that
  stops the phone sleeping mid-session. It needs a tap to take (browsers won't
  grant one on load), and it's re-taken every time the page comes back to the
  front; the setting is remembered. Safari has it from iOS 16.4; below that the
  entry reads *n/a* and there is no substitute — Fullscreen doesn't hold the
  screen up, and iPhone Safari has no Fullscreen API at all.
- **Fit to screen** — drops any pan/zoom and re-centres. Turning the phone
  does this on its own, since a pan saved in the other orientation points
  somewhere that no longer exists.

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
