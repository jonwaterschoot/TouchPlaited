# Code-map page — original task brief (archived)

This was the handoff brief used to build the interactive hardware + code map
page. The page shipped in `4c7200a` ("Code map page: interactive hardware +
memory atlas at /codemap/") as `tools/codemap.html`, following the same
self-contained single-HTML-file pattern as `tools/pattern_editor.html`, and
is wired into `.github/workflows/pages.yml` and `visualizer/tools/build-site.mjs`
as `/codemap/`. `doc/codemap_brief.md` now just points here.

The hardware and memory-map tables below were verified against the code at
the time of writing; treat the source files they cite (`touch/knobs.cpp`,
`synth/settings_journal.{h,cpp}`, etc.) as the ground truth if they've since
diverged.

---

Handoff brief for building an **interactive hardware + code map** page: a literal
map of the instrument — the Daisy Seed and the MPR121 breakout drawn as boards,
pins annotated with what's connected, the links between chips explained, and an
overlay of *where the code runs* (which memory, which execution context). To be
integrated as its own page on the GitHub Pages site (like `/editor/` and
`/visualizer/`). Start by reading this file, then verify details against the
pointers below — the codebase is the ground truth.

## Hardware map (verified against code)

**Daisy Seed** (STM32H750 + 64 MB SDRAM + 8 MB QSPI NOR flash + audio codec on
one board) socketed on the **Synthux Simple Touch** PCB:

| Connection | Pins | Component / role |
|---|---|---|
| ADC A0–A7 | S30–S37 | 8 pots (per-mode knob layers) — `touch/knobs.cpp` |
| ADC A11 | S43 | CV clock-in jack, raw ADC counts (no smoothing — edge detector) — `touch/knobs.cpp` |
| GPIO D25 | S40 | CV clock-out jack, pulsed from the audio ISR — `TouchPlaited.cpp` (`cv_clock_out`) |
| GPIO D7/D6 | SW2 (right 3-pos) | playmode: Down=Basic Pitch, Center=Arp/Mel, Up=Seq — `touch/switches.cpp` |
| GPIO D9/D8 | SW1 (left 3-pos) | scale / genre / arp sub-state (change-latched) — `touch/switches.cpp` |
| I2C1: SCL=PB8 (D11), SDA=PB9 (D12) | MPR121 breakout @ 0x5A | 12 capacitive pads P0–P11 (P3–P9 musical, P0/P1/P2 modifiers, P10/P11 octave) — `touch/pads.cpp`, driver `lib/libDaisy/src/dev/mpr121.h` |
| USART1: D14 RX, D13 TX | TRS MIDI in/out mod | always active — `midi/midi_io.cpp` |
| USB (built-in) | USB MIDI **or** serial log | one port, one owner: `-DUSB_MIDI` in Makefile picks MIDI; comment it out for the CPU-meter/telemetry serial build |
| Onboard LED | | the only display; all blink patterns via `set_led`/`led_event` |
| Audio codec (on Seed) | audio out L/R + audio in | 48 kHz, 192-sample blocks |

## Memory & execution map (measured, this is the interesting overlay)

Boot chain: Daisy bootloader (internal 128 KB flash, first 256 KB of QSPI
reserved) → app runs **XIP from QSPI** at `0x90040000` (`APP_TYPE = BOOT_QSPI`).

| Memory | Size / used | What lives there |
|---|---|---|
| QSPI flash 0x90000000 | 8 MB, app ~360 KB | code + rodata (XIP); **settings journal** = 2×32 KB banks just below a 64 KB guard band at the top (base derived from DCR.FSIZE at boot) — `synth/settings_journal.{h,cpp}` |
| AXI SRAM 0x24000000 | 512 KB, ~53% | .data/.bss hot state, voice working set (moved from SDRAM for the 6-voice CPU win), **RAM-resident QSPI write routine** (`tp_qspi_ram_op`, in `.data` so it executes from SRAM while flash is offline) |
| SDRAM 0xC0000000 | 64 MB, ~2.3 MB | Plaits voice instances, FX delay/reverb buffers (`.sdram_bss`, NOLOAD) |
| SRAM1 (D2) 0x30000000 | 32 KB region | audio DMA buffers — MPU marks it non-cacheable |
| DTCM 0x20000000 | 128 KB | stack; mostly free |
| ITCM | 64 KB, unused | known future lever for a 7th voice |
| Backup SRAM | 4 KB | bootloader version stamp (this device reports < v6.0 — hence the manual `hw.led.Init()` workaround in `main()`) |

Execution contexts:
- **AudioCallback (ISR, 250 Hz / 4 ms blocks):** `touch.Process()` (I2C pad read
  + callbacks), knob processing + pickups, SW handling, seq/arp/note-rec ticks,
  CV clock in/out, 8×24-sample Plaits chunks through the voice pool, 4 FX
  groups, soft limiter. CPU meter + >90 % load-shed guard live here.
- **Main loop:** LED pattern state machine, MIDI service (UART+USB), CPU serial
  print (`sv N` = settings save count), `persist_tick()` debounced settings
  auto-save (250 ms compare / 3 s settle / one flash page per pass, gated on
  audio load < 85 %).
- **Hard-won H7 lessons already documented** (README "Settings memory", memory
  notes): (1) memory-mapped reads near the very top of the QSPI window trip the
  H7 prefetch-past-FSIZE erratum → boot hard fault — never map-read the last
  64 KB; (2) flash writes need an MPU no-access guard over the QSPI mapping or
  speculative prefetch stalls AXI permanently.

## Site integration (as planned)

- Pages are deployed by `.github/workflows/pages.yml` (GitHub Actions): the
  Vite visualizer builds, then `npm run build:site` (in `visualizer/`)
  assembles `_site/`: `/` (README), `/manual.html`, `/visualizer/`, `/editor/`
  (copies `tools/pattern_editor.html`). See `doc/SITE-PLAN.md`.
- A new `/codemap/` page should follow the `/editor/` pattern: a
  **self-contained single HTML file** (inline CSS/JS, no CDN), added to the
  `build:site` assembly + workflow paths + linked from README.
- Visual language: the pattern editor's dark/pixel style is the house look.

## Design decisions that were settled

- Level of interactivity: hover/click a pin or chip → explanation panel? An
  execution-context toggle (audio ISR vs main loop vs boot) highlighting the
  parts involved? Data-flow animation (touch → voice → FX → out)?
- Whether the map doubles as living documentation (deep-link anchors per
  component, links into source files on GitHub).
- SVG hand-drawn boards vs. schematic-style blocks (no real board photos in
  repo except `img/` shots — check what's usable).

(See `tools/codemap.html` for how these were ultimately resolved.)
