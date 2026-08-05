#include "oled_screen.h"
#include "../i2c1_lock.h"
#include <cstring>
#include <cctype>
#include <algorithm>

using namespace synthux;
using namespace daisy;

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
} // namespace

void OledScreen::Init(daisy::DaisySeed& hw) {
    (void)hw; // transport owns its own I2C init; kept for Pads::Init() symmetry

    OledDisplay<SSD130xI2c128x32Driver>::Config cfg;
    // MPR121 (touch/pads.cpp) already brought this same I2C1 bus up at
    // 400kHz. Whichever Init() runs last reprograms the peripheral's actual
    // clock — the driver's own default is 1MHz, which the pads aren't
    // guaranteed to tolerate, so pin it to match rather than relying on
    // init order between the two devices.
    cfg.driver_config.transport_config.i2c_config.speed
        = I2CHandle::Config::Speed::I2C_400KHZ;
    // Address (0x3C), peripheral (I2C_1) and pins (SCL/SDA = D11/D12) are
    // already the driver's defaults — same bus the pads use, so no more to
    // set here.

    _display.Init(cfg);
    Clear();
}

void OledScreen::Clear() {
    i2c1_bus_busy = true;
    _display.Fill(false);
    _display.Update();
    i2c1_bus_busy = false;
}

void OledScreen::ShowProgress(const char* label, uint8_t progress, const char* note) {
    i2c1_bus_busy = true;
    _display.Fill(false);

    // Label row: identical to ShowLine's.
    char labelBuf[kBufLen];
    size_t labelLen = std::min(strlen(label), kBufLen - 1);
    std::memcpy(labelBuf, label, labelLen);
    labelBuf[labelLen] = '\0';
    for (char* p = labelBuf; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    _display.SetCursor(1, 0);
    _display.WriteString(labelBuf, Font_6x8, true);

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

    _display.Update();
    i2c1_bus_busy = false;
}

void OledScreen::ShowPickup(const char* label, const char* value,
                            uint8_t pot, uint8_t target) {
    i2c1_bus_busy = true;
    _display.Fill(false);

    // Label row: identical to ShowLine's.
    char labelBuf[kBufLen];
    size_t labelLen = std::min(strlen(label), kBufLen - 1);
    std::memcpy(labelBuf, label, labelLen);
    labelBuf[labelLen] = '\0';
    for (char* p = labelBuf; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    _display.SetCursor(1, 0);
    _display.WriteString(labelBuf, Font_6x8, true);

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

    _display.Update();
    i2c1_bus_busy = false;
}

void OledScreen::ShowList(const char* const* rows, int n) {
    i2c1_bus_busy = true;
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
    _display.Update();
    i2c1_bus_busy = false;
}

void OledScreen::BeginFrame() {
    _display.Fill(false);
}

void OledScreen::SetPixel(uint8_t x, uint8_t y, bool on) {
    _display.DrawPixel(x, y, on);
}

void OledScreen::EndFrame() {
    i2c1_bus_busy = true;
    _display.Update();
    i2c1_bus_busy = false;
}

void OledScreen::ShowLine(const char* label, const char* value,
                          const StatusIcons& icons) {
    // Held for the whole draw+transfer (see i2c1_lock.h) — Pads::Process()
    // skips its I2C poll in AudioCallback while this is up, so the ~20ms
    // Update() below can't get torn by the audio ISR landing mid-transfer.
    i2c1_bus_busy = true;
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
    char labelBuf[kBufLen];
    size_t labelLen = std::min({strlen(label), kBufLen - 1, labelBudget});
    std::memcpy(labelBuf, label, labelLen);
    labelBuf[labelLen] = '\0';
    for (char* p = labelBuf; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    _display.SetCursor(1, 0);
    _display.WriteString(labelBuf, Font_6x8, true);

    if (value == nullptr || *value == '\0') {
        _display.Update();
        i2c1_bus_busy = false;
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

    _display.Update();
    i2c1_bus_busy = false;
}
