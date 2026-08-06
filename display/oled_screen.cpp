#include "oled_screen.h"
#include "../i2c1_lock.h"
#include <cstring>
#include <cctype>
#include <algorithm>

using namespace synthux;
using namespace daisy;

#ifdef OLED_I2C4
// Loud on purpose: this define is a *hardware* claim, and building it against
// the unmoved wires produces a blank screen with no other symptom. It also
// silently removes TRS MIDI, so together with -DUSB_MIDI the board has no
// MIDI at all — which looks like a regression rather than a build choice.
#warning "OLED_I2C4: display must be rewired to D13/D14 (SCL/SDA); TRS MIDI is disabled in this build"
#endif

namespace {
constexpr size_t kBufLen = 22; // 21 chars (Font_6x8 budget, 128/6px) + NUL

// Indicator-block geometry, all on the label row (y 0..7). The rec circle
// owns the far right; the five layer dots sit to its left, so a label sharing
// the row has to be truncated to whatever is left of them.
constexpr uint8_t kRecCx = 123, kRecCy = 3, kRecR = 3;
constexpr uint8_t kDotX0 = 88, kDotPitch = 6, kDotW = 4;  // 5 dots -> x 88..115
constexpr size_t  kLabelCharsWithDots  = 14;   // 84 px, clear of kDotX0
constexpr size_t  kLabelCharsWithRec   = 19;   // 114 px, clear of the circle

// ShowPickup geometry: Font_7x10 value at y 12..21, pickup track at y 24..31.
constexpr uint8_t kPickupValueY = 12;

// ShowPool geometry. Seven fixed columns of 18 px (1..126) so the note row and
// the marker row line up under each other and under the pads themselves — the
// notes' own widths vary (one char or two), so laying them out by string
// position would put the markers under nothing in particular. Note row at
// y 12..19, markers at y 24..30; the row of blank pixels between each is what
// keeps them reading as two rows rather than one block.
constexpr uint8_t kPoolX0 = 1, kPoolColW = 18;
constexpr uint8_t kPoolNoteY = 12;
constexpr uint8_t kPoolMarkY0 = 24, kPoolMarkY1 = 30, kPoolMarkW = 9;

// Label row, shared by every screen here: Font_6x8, uppercased, truncated to
// `budget` chars (the 21-char Font_6x8 line, less anything drawn to its right).
void draw_label(daisy::OledDisplay<daisy::SSD130xI2c128x32Driver>& d,
                const char* label, size_t budget) {
    char buf[kBufLen];
    const size_t len = std::min({strlen(label), kBufLen - 1, budget});
    std::memcpy(buf, label, len);
    buf[len] = '\0';
    for (char* p = buf; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    d.SetCursor(1, 0);
    d.WriteString(buf, Font_6x8, true);
}
} // namespace

namespace {
// Windowed transfer stats — see OledScreen::LastFrameUs() and friends. Plain
// statics: every writer and reader is the main loop.
uint32_t g_frame_us_last = 0;
uint32_t g_frame_us_max  = 0;
uint32_t g_frame_count   = 0;
} // namespace

uint32_t OledScreen::LastFrameUs()  { return g_frame_us_last; }
uint32_t OledScreen::MaxFrameUs()   { return g_frame_us_max; }
uint32_t OledScreen::FrameCount()   { return g_frame_count; }
void     OledScreen::ResetFrameStats() {
    g_frame_us_max = 0;
    g_frame_count  = 0;
}

void OledScreen::PushFrame() {
#ifndef OLED_I2C4
    // Shared bus: held for the transfer only (see i2c1_lock.h) — Pads::Process()
    // skips its I2C poll in AudioCallback while this is up, so the transfer
    // can't get torn by the audio ISR landing mid-transaction. On I2C4 the
    // display has the bus to itself and there is nothing to interlock with.
    i2c1_bus_busy = true;
#endif
    const uint32_t t0 = System::GetUs();
    _display.Update();
    const uint32_t us = System::GetUs() - t0;
#ifndef OLED_I2C4
    i2c1_bus_busy = false;
#endif

    g_frame_us_last = us;
    if (us > g_frame_us_max) g_frame_us_max = us;
    g_frame_count++;
}

void OledScreen::Init(daisy::DaisySeed& hw) {
    (void)hw; // transport owns its own I2C init; kept for Pads::Init() symmetry

    OledDisplay<SSD130xI2c128x32Driver>::Config cfg;
    auto& i2c = cfg.driver_config.transport_config.i2c_config;
#ifdef OLED_I2C4
    // Dedicated bus: I2C4 on D13/D14 (PB6/PB7), which libDaisy drives at
    // AF6 — the same two pins USART1 uses for TRS MIDI, which is why
    // midi/midi_io.cpp drops the UART transport under this define. Nothing
    // else is on this bus, so the pads' 400kHz ceiling doesn't apply and the
    // driver's own 1MHz default (~886kHz in practice) stands.
    //
    // Two things this build depends on that the shared bus provided for free:
    //   - Pull-ups. libDaisy leaves I2C pins open-drain with GPIO_NOPULL, and
    //     unlike D11/D12 nothing on the Simple Touch board pulls D13/D14 up —
    //     so the OLED module's own resistors are the only ones. If frames
    //     come out garbled at 1MHz, that is the first thing to suspect; drop
    //     to 400kHz to confirm before blaming anything else.
    //   - Clock. libDaisy picks Init.Timing by assuming the peripheral runs
    //     off PCLK1, but I2C4 sits in the D3 domain and runs off PCLK4. Those
    //     happen to be equal here — sys/system.cpp derives both from HCLK
    //     with the same DIV2 — so the timing table lands right by luck, not
    //     design. Worth a scope on SCL once.
    i2c.periph         = I2CHandle::Config::Peripheral::I2C_4;
    i2c.pin_config.scl = seed::D13;
    i2c.pin_config.sda = seed::D14;
    i2c.speed          = I2CHandle::Config::Speed::I2C_1MHZ;
#else
    // MPR121 (touch/pads.cpp) already brought this same I2C1 bus up at
    // 400kHz. Whichever Init() runs last reprograms the peripheral's actual
    // clock — the driver's own default is 1MHz, which the pads aren't
    // guaranteed to tolerate, so pin it to match rather than relying on
    // init order between the two devices.
    i2c.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    // Address (0x3C), peripheral (I2C_1) and pins (SCL/SDA = D11/D12) are
    // already the driver's defaults — same bus the pads use, so no more to
    // set here.
#endif

    _display.Init(cfg);
    Clear();
}

void OledScreen::Clear() {
    _display.Fill(false);
    PushFrame();
}

void OledScreen::ShowProgress(const char* label, uint8_t progress, const char* note) {
    _display.Fill(false);

    draw_label(_display, label, kBufLen - 1);

    // Middle row: an outlined bar, filled left-to-right by progress/127 —
    // same geometry oled-mini.ts's showProgress() draws. Slimmer (and higher
    // up) than the full-height bar it started as, to leave the bottom row for
    // the note: a bar alone says a threshold is coming without ever saying
    // what it does.
    constexpr uint8_t kOutlineX1 = 1, kOutlineY1 = 12, kOutlineX2 = 126, kOutlineY2 = 21;
    _display.DrawRect(kOutlineX1, kOutlineY1, kOutlineX2, kOutlineY2, true, false);
    constexpr uint8_t kFillX1 = kOutlineX1 + 2, kFillY1 = kOutlineY1 + 2;
    constexpr uint8_t kFillY2 = kOutlineY2 - 2;
    constexpr uint8_t kFillMaxW = kOutlineX2 - 2 - kFillX1; // inner width at 100%
    const uint8_t fillW = static_cast<uint8_t>((static_cast<uint32_t>(kFillMaxW) * (progress & 0x7F)) / 127u);
    if (fillW > 0) {
        _display.DrawRect(kFillX1, kFillY1, static_cast<uint8_t>(kFillX1 + fillW - 1), kFillY2, true, true);
    }

    // Note row: Font_6x8 on the bottom 8 rows, same truncation rule as the
    // label row (it's the same font and the same 21-char budget).
    if (note != nullptr && *note != '\0') {
        char noteBuf[kBufLen];
        size_t noteLen = std::min(strlen(note), kBufLen - 1);
        std::memcpy(noteBuf, note, noteLen);
        noteBuf[noteLen] = '\0';
        for (char* p = noteBuf; *p; ++p)
            *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        _display.SetCursor(1, 32 - Font_6x8.FontHeight);
        _display.WriteString(noteBuf, Font_6x8, true);
    }

    PushFrame();
}

void OledScreen::ShowPickup(const char* label, const char* value,
                            uint8_t pot, uint8_t target) {
    _display.Fill(false);

    draw_label(_display, label, kBufLen - 1);

    // Value row: Font_7x10 at a fixed y, not ShowLine's shrink-to-fit stepping.
    // The value here is the stored one and doesn't change while you hunt for
    // it, so there is nothing for a font change to signal — and the track
    // below needs its own rows regardless, which rules out the 18px font.
    if (value != nullptr && *value != '\0') {
        constexpr size_t kValueChars = 128 / 7;   // Font_7x10 advance
        char valueBuf[kBufLen];
        size_t n = std::min({strlen(value), kValueChars, kBufLen - 1});
        std::memcpy(valueBuf, value, n);
        valueBuf[n] = '\0';
        _display.SetCursor(1, kPickupValueY);
        _display.WriteString(valueBuf, Font_7x10, true);
    }

    // Pickup track across the bottom: 0..127 mapped to the full width, with a
    // baseline so an empty stretch still reads as travel rather than blank.
    constexpr uint8_t kTrackX0 = 1, kTrackX1 = 126;
    constexpr uint8_t kSpan    = kTrackX1 - kTrackX0;
    auto track_x = [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(kTrackX0 + (static_cast<uint32_t>(v & 0x7F) * kSpan) / 127u);
    };
    _display.DrawLine(kTrackX0, 30, kTrackX1, 30, true);
    // Target: a full-height post — where the pot has to get to.
    const uint8_t tx = track_x(target);
    _display.DrawRect(tx, 24, static_cast<uint8_t>(tx + 1), 31, true, true);
    // Pot: a wider, shorter block riding the track — where it is now.
    const uint8_t px = track_x(pot);
    const uint8_t pl = px >= kTrackX0 + 2 ? static_cast<uint8_t>(px - 2) : kTrackX0;
    const uint8_t pr = px <= kTrackX1 - 2 ? static_cast<uint8_t>(px + 2) : kTrackX1;
    _display.DrawRect(pl, 27, pr, 29, true, true);

    PushFrame();
}

void OledScreen::ShowPool(const char* label, const char* const* names, uint8_t pool) {
    _display.Fill(false);

    draw_label(_display, label, kBufLen - 1);

    for (int i = 0; i < kPoolCols; i++) {
        const uint8_t col = static_cast<uint8_t>(kPoolX0 + i * kPoolColW);

        // Note row: centered in the column, so a one-char name ("F") and a
        // two-char one ("F#") both sit over their own marker.
        const char* nm = (names != nullptr && names[i] != nullptr) ? names[i] : "";
        char buf[4];
        const size_t len = std::min(strlen(nm), sizeof(buf) - 1);
        std::memcpy(buf, nm, len);
        buf[len] = '\0';
        const uint8_t w = static_cast<uint8_t>(len * Font_6x8.FontWidth);
        _display.SetCursor(static_cast<uint8_t>(col + (kPoolColW - w) / 2), kPoolNoteY);
        _display.WriteString(buf, Font_6x8, true);

        // Marker row: filled = in the pool, hollow = not. Same grammar as the
        // layer dots on the label row (ShowLine) — a filled shape is a thing
        // that is there, an outline is the slot it would occupy.
        const uint8_t mx = static_cast<uint8_t>(col + (kPoolColW - kPoolMarkW) / 2);
        _display.DrawRect(mx, kPoolMarkY0,
                          static_cast<uint8_t>(mx + kPoolMarkW - 1), kPoolMarkY1,
                          true, ((pool >> i) & 1) != 0);
    }

    PushFrame();
}

void OledScreen::ShowList(const char* const* rows, int n) {
    _display.Fill(false);
    const int shown = std::min(n, kListRows);
    for (int i = 0; i < shown; i++) {
        char buf[kBufLen];
        const size_t len = std::min(strlen(rows[i]), kBufLen - 1);
        std::memcpy(buf, rows[i], len);
        buf[len] = '\0';
        for (char* p = buf; *p; ++p)
            *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        _display.SetCursor(1, static_cast<uint8_t>(i * Font_6x8.FontHeight));
        _display.WriteString(buf, Font_6x8, true);
    }
    PushFrame();
}

void OledScreen::BeginFrame() {
    _display.Fill(false);
}

void OledScreen::SetPixel(uint8_t x, uint8_t y, bool on) {
    _display.DrawPixel(x, y, on);
}

void OledScreen::EndFrame() {
    PushFrame();
}

void OledScreen::ShowLine(const char* label, const char* value,
                          const StatusIcons& icons) {
    _display.Fill(false);

    // Indicator block first — it owns the right end of the label row, so the
    // label's budget depends on what's drawn here.
    size_t labelBudget = kBufLen - 1;
    if (icons.layers != 0xFF) {
        labelBudget = kLabelCharsWithDots;
        for (int i = 0; i < 5; i++) {
            const uint8_t x0 = static_cast<uint8_t>(kDotX0 + i * kDotPitch);
            const uint8_t x1 = static_cast<uint8_t>(x0 + kDotW - 1);
            if (i == icons.open) {
                // The take being recorded into pulses with the rec circle —
                // filled on the beat it is lit, hollow between, so which
                // layer is live reads at a glance without counting.
                _display.DrawRect(x0, 2, x1, 5, true, icons.blink);
            } else if (i < icons.layers) {
                // Committed: filled, or hollow while muted.
                _display.DrawRect(x0, 2, x1, 5, true, ((icons.mute >> i) & 1) == 0);
            } else {
                // Free slot: a 2px tick, just enough to count the slots.
                _display.DrawRect(static_cast<uint8_t>(x0 + 1), 3,
                                  static_cast<uint8_t>(x0 + 2), 4, true, true);
            }
        }
    } else if (icons.rec) {
        labelBudget = kLabelCharsWithRec;
    }
    if (icons.rec && icons.blink) _display.DrawCircle(kRecCx, kRecCy, kRecR, true);

    // Label row: Font_6x8, uppercased and truncated to the 21-char budget
    // (128px / 6px advance) — same rule as LABEL_CHARS in oled-mini.ts.
    draw_label(_display, label, labelBudget);

    if (value == nullptr || *value == '\0') {
        PushFrame();
        return;
    }

    // Value row: shrink through the three fixed bitmap fonts until the
    // string fits, largest first — it never truncates, it just gets
    // smaller (mirrors the emulator's fitFont()).
    size_t valueLen = strlen(value);
    const FontDef* font;
    size_t maxChars;
    if (valueLen <= 128 / Font_11x18.FontWidth) {
        font     = &Font_11x18;
        maxChars = 128 / Font_11x18.FontWidth;
    } else if (valueLen <= 128 / Font_7x10.FontWidth) {
        font     = &Font_7x10;
        maxChars = 128 / Font_7x10.FontWidth;
    } else {
        font     = &Font_6x8;
        maxChars = 128 / Font_6x8.FontWidth;
    }

    char valueBuf[kBufLen];
    size_t n = std::min({valueLen, maxChars, kBufLen - 1});
    std::memcpy(valueBuf, value, n);
    valueBuf[n] = '\0';
    _display.SetCursor(1, 32 - font->FontHeight);
    _display.WriteString(valueBuf, *font, true);

    PushFrame();
}
