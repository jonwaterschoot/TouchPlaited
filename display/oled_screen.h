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
// Right-aligned indicator block on the label row — the only thing on this
// screen that persists across whatever callout is showing, because it answers
// two questions you need continuously while recording rather than at the
// moment you press something: is capture live, and which layer is it going
// into. Everything else here replaces the whole screen.
struct StatusIcons {
    bool    rec    = false;   // capture live: a circle, top right, blinking
    bool    blink  = false;   // current blink phase, supplied by the caller
    uint8_t layers = 0xFF;    // committed NoteRec layers, 0xFF = draw no dots
    uint8_t mute   = 0;       // bit i = layer i muted
    uint8_t open   = 0xFF;    // layer being recorded into, 0xFF = none

    bool any() const { return rec || layers != 0xFF; }
    // Whether anything in this block changes with `blink` — the screen only
    // redraws on change, so a caller has to know when to force one.
    bool animated() const { return rec || open != 0xFF; }
};

class OledScreen {
public:
    OledScreen() {}
    ~OledScreen() {}

    void Init(daisy::DaisySeed& hw);

    // Replaces the whole screen with one label + one value, like the
    // emulator's Oled.show() — there's no history on this screen either.
    void ShowLine(const char* label, const char* value,
                  const StatusIcons& icons = StatusIcons{});

    // Four rows of Font_6x8 — the whole 32 px height, no label/value split.
    // Used for the combo list a held modifier pad shows; `rows` must hold at
    // least `n` entries and only the first 4 are drawn, so a caller with more
    // pages by advancing the pointer.
    static constexpr int kListRows = 32 / 8;
    void ShowList(const char* const* rows, int n);

    // Label row as ShowLine(), value row replaced by a filled bar —
    // progress/127 of the screen width — for a hold building toward a
    // threshold, plus a small note row under it saying what crossing that
    // threshold will do (`note` may be null/empty for just the bar).
    // Mirrors oled-mini.ts's showProgress().
    void ShowProgress(const char* label, uint8_t progress, const char* note);

    // A pot that is armed behind a pickup: it isn't driving anything until it
    // reaches the stored value, and printing its raw position (what this
    // screen used to do) actively lies about that. Label row as ShowLine();
    // `value` is the STORED value in its own units — the one actually in
    // effect, formatted exactly as it would be normally, so nothing has to be
    // re-expressed as a bare percentage just because it's mid-handover. Under
    // it, a track carrying both positions: a tall post at `target` and a
    // slider block at `pot`, both 0..127 across the full width. Drive the
    // block onto the post and the pot takes over — no arithmetic, and it
    // works the same whether the value is a BPM, a note name or a word.
    void ShowPickup(const char* label, const char* value,
                    uint8_t pot, uint8_t target);

    // Label row as ShowLine(), then the seven musical pads as two aligned
    // rows: their note names in fixed 18 px columns, and under each a marker
    // — filled if that pad is in the arp's pool, hollow if it isn't. `names`
    // must hold 7 short strings (pitch classes, <= 3 chars); bit i of `pool`
    // is pad P(3+i). Mirrors oled-mini.ts's showPool().
    //
    // The markers are drawn rather than written for a reason worth keeping:
    // Font_6x8 only carries ASCII 32-126, and WriteString abandons the whole
    // string at the first character outside that range — so a filled block
    // can't be a glyph here, however convenient that would be.
    static constexpr int kPoolCols = 7;
    void ShowPool(const char* label, const char* const* names, uint8_t pool);

    void Clear();

    // Frame-level primitives for the one-shot boot animation
    // (display/oled_boot.cpp) — the only other caller; everything above
    // this replaces the whole screen in a single call, so these are the
    // only way anything outside this file gets pixel-level control.
    // BeginFrame/SetPixel only touch the local buffer (no I2C traffic);
    // EndFrame is the one that actually transfers it, so it's the only one
    // that needs the i2c1_bus_busy bracket the other Show*() methods use.
    void BeginFrame();
    void SetPixel(uint8_t x, uint8_t y, bool on);
    void EndFrame();

private:
    NOCOPY(OledScreen)

    daisy::OledDisplay<daisy::SSD130xI2c128x32Driver> _display;
};

} // namespace synthux

#endif
