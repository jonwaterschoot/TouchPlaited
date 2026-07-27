#pragma once

#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "../nocopy.h"

#ifdef __cplusplus

namespace synthux {

// 128x32 SSD1306, I2C1 on D11 (SCL) / D12 (SDA) — the same physical bus the
// MPR121 capacitive pads already use (touch/pads.h), just a different
// device address (0x3C vs the pads' 0x5A), so no extra wiring beyond power
// and the two data lines. See Init() for why the display's I2C speed is
// pinned to match the pads' instead of using its own faster default.
//
// Two rows, mirroring visualizer/src/panel/oled-mini.ts: a small label
// (Font_6x8, truncated to fit) and a value that never truncates — it steps
// down through Font_11x18 -> Font_7x10 -> Font_6x8 until it fits, the exact
// same fixed bitmap fonts and stepping rule oled-mini.ts blits (its font
// data is ported verbatim from lib/libDaisy/src/util/oled_fonts.c).
class OledScreen {
public:
    OledScreen() {}
    ~OledScreen() {}

    void Init(daisy::DaisySeed& hw);

    // Replaces the whole screen with one label + one value, like the
    // emulator's Oled.show() — there's no history on this screen either.
    void ShowLine(const char* label, const char* value);

    // Label row as ShowLine(), value row replaced by a filled bar —
    // progress/127 of the screen width — for a hold building toward a
    // threshold. Mirrors oled-mini.ts's showProgress().
    void ShowProgress(const char* label, uint8_t progress);

    void Clear();

private:
    NOCOPY(OledScreen)

    daisy::OledDisplay<daisy::SSD130xI2c128x32Driver> _display;
};

} // namespace synthux

#endif
