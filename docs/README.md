# TouchPlaited

A Synthux Simple Touch firmware based on Mutable Instruments Plaits (Émilie Gillet, MIT License): a 7-voice touch synth plus a 16-step generative drum sequencer, playable at the same time.

By [Jon Waterschoot](https://github.com/jonwaterschoot), firmware for the [Synthux Simple Touch](https://synthux.academy).

![TouchPlaited: what each control does](docs/touchplaited-controls.png)

## Controls

| Control | Function | + P00 | + P02 | + P01 |
|---|---|---|---|---|
| **S30** knob | Drive |  |  | Reverb |
| **S31** knob | Decay |  |  |  |
| **S32** knob | Harmonics |  |  |  |
| **S33** knob | Timbre |  |  |  |
| **S34** knob | Morph |  |  |  |
| **S35** knob | Model select | Model select | Model select | Delay |
| **S36** fader | Output level |  |  |  |
| **S37** fader | Model mix | Stereo width |  |  |
| **S09/S10** switch | Scale / Genre · min / chro / maj |  |  |  |
| **S07/S08** switch | Playmode · seq / arp / basic pitch |  |  |  |
| **IN** jack | in |  |  |  |
| **OUT** jack | out |  |  |  |
| **P03** pad | 1 · root · Kick |  |  |  |
| **P04** pad | 2 · Snare |  |  |  |
| **P05** pad | 3 · Cl. hat |  |  |  |
| **P06** pad | 4 · Op. hat |  |  |  |
| **P07** pad | 5 · Clap |  |  |  |
| **P08** pad | 6 · Tom |  |  |  |
| **P09** pad | 7 · Perc |  |  |  |
| **P10** pad | Octave down | Root semitone down |  |  |
| **P11** pad | Octave up | Root semitone up | Play / Pause |  |

- **+ P00**: hold P00 while you use the control.
- **+ P02**: hold P02 while you use the control.
- **+ P01**: hold P01 while you use the control.

## Install

Download the latest `.bin` from [Releases](https://github.com/jonwaterschoot/TouchPlaited/releases/latest), and connect the Simple Touch with a USB **data** cable.

### With the Daisy bootloader

This firmware runs from the QSPI flash, through the Daisy bootloader.

1. Once per unit, install the bootloader: see [Getting started with the Daisy bootloader](https://github.com/electro-smith/libDaisy/blob/master/doc/md/_a7_Getting-Started-Daisy-Bootloader.md).
2. Press **RESET**, then press **BOOT** while the LED pulses.
3. Flash the `.bin` with the [Daisy Web Programmer](https://flash.daisy.audio/), or from a terminal: `dfu-util -a 0 -s 0x90040000:leave -D touchplaited.bin -d ,0483:df11`

## Build from source

Built with [libDaisy](https://github.com/electro-smith/libDaisy) and [DaisySP](https://github.com/electro-smith/DaisySP), with the Arm toolchain from Electrosmith.

1. `git clone --recurse-submodules https://github.com/jonwaterschoot/TouchPlaited`
2. Build the libraries once: `make -C libDaisy` and `make -C DaisySP` <!-- TODO: their paths in this repository -->
3. `make`, then `make program-dfu` to flash (with `APP_TYPE = BOOT_QSPI` in the Makefile to run from the bootloader).

---

Control labels made with [Simple Touch labels](https://jonwaterschoot.github.io/simple-touch-labels/). To change them, open `simple-touch-labels.json` in the [designer](https://jonwaterschoot.github.io/simple-touch-labels/editor.html) (Open file…).
