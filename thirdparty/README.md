# thirdparty/ — Dependencies

Two kinds of third-party code live here:

- **`thirdparty/plaits/`** — vendored (committed to this repo). Nothing to install.
- **`thirdparty/stmlib/`** and **`lib/libDaisy/`** — git submodules. One command to fetch (step 2).

---

## 1. Plaits DSP source (`thirdparty/plaits/`) — vendored, no setup needed

The Plaits DSP code is committed directly to this repository, so a fresh clone
already contains everything. Do **not** copy files from the eurorack repo over it.

### Provenance

- **Upstream:** https://github.com/pichenettes/eurorack (Mutable Instruments, discontinued — no further updates expected)
- **Vendored from commit:** `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4` (2023-08-16, final state of `master`)
- **Subtree copied:** `plaits/dsp/`, `plaits/resources.h`, `plaits/resources.cc`
- **Excluded:** `plaits.cc`, `ui.cc`, `settings.cc`, `drivers/` — STM32F37x
  hardware-specific code that does not apply to Daisy
- **Local modifications:** `user_data.h` only. The upstream version writes custom
  wavetables to STM32F37x internal flash; ours is a Daisy-compatible stub whose
  `ptr()` returns `nullptr`, so the engines fall back to their built-in wavetables.
  Every other file is byte-identical to upstream.

### License

The Plaits code is MIT-licensed by Emilie Gillet — see `plaits/LICENSE` and the
header in each source file. Note that "Mutable Instruments" is a registered
trademark and must not be used to name or market derivative works.

**Expected layout:**
```
thirdparty/plaits/
├── LICENSE
├── dsp/
│   ├── voice.h
│   ├── voice.cc
│   ├── engine/          (16 original engines)
│   ├── engine2/         (8 newer engines)
│   ├── speech/
│   └── physical_modelling/
├── resources.h
├── resources.cc
└── user_data.h          (Daisy stub — local modification)
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
