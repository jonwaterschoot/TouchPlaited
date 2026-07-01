# thirdparty/ — Setup Required Before Building

The project will not compile until these two source trees are present.

---

## 1. Plaits DSP source (`thirdparty/plaits/`)

Clone the Mutable Instruments eurorack repo and copy the `plaits/` subtree here,
excluding hardware-specific files that don't apply to Daisy.

Run these in a **bash** shell (Git Bash on Windows — brace expansion won't work in plain `sh`):

```bash
# Clone somewhere temporarily
git clone https://github.com/pichenettes/eurorack.git /tmp/eurorack

# Create destination and copy in one block
mkdir -p thirdparty/plaits && \
cp -r /tmp/eurorack/plaits/dsp thirdparty/plaits/dsp && \
cp /tmp/eurorack/plaits/resources.{h,cc} thirdparty/plaits/

# Excludes plaits.cc, ui.cc, settings.cc, drivers/ — STM32F37x-specific
# user_data.h is NOT copied — a Daisy-compatible stub is already in thirdparty/plaits/
```

**Expected layout after copy:**
```
thirdparty/plaits/
├── dsp/
│   ├── voice.h
│   ├── voice.cc
│   ├── engine/          (16 original engines)
│   ├── engine2/         (8 newer engines)
│   ├── speech/
│   └── physical_modelling/
├── resources.h
└── resources.cc
```

---

## 2. Submodules — stmlib + libDaisy

Both are git submodules. `--recursive` is required because libDaisy itself has submodules (STM HAL, CMSIS, USB stack):

```bash
git submodule update --init --recursive
```

Then build libDaisy once (produces `lib/libDaisy/build/libdaisy.a`):
```bash
cd lib/libDaisy && make
```

## 3. Build TouchPlaited

From the project root:
```bash
cd ~/Documents/GitHub/TouchPlaited && make
```

This produces `build/TouchPlaited.bin`.

---

## 4. Boot mode

The firmware uses `APP_TYPE = BOOT_QSPI` — the binary exceeds the 186 KB Daisy SRAM limit.
The Daisy bootloader must be flashed on the device first:

```bash
# Flash the Daisy bootloader (only needed once per device)
make program-boot

# Then flash the app via DFU
make program-dfu
```

Or use the VSCode task **build_and_program_dfu**.
