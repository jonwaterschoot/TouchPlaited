#include "oled_screen.h"
#include "../i2c1_lock.h"
#include <cstring>
#include <cctype>
#include <algorithm>

using namespace synthux;
using namespace daisy;

namespace {
constexpr size_t kBufLen = 22; // 21 chars (Font_6x8 budget, 128/6px) + NUL
}

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

void OledScreen::ShowProgress(const char* label, uint8_t progress) {
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

    // Value row: an outlined bar, filled left-to-right by progress/127 —
    // same geometry oled-mini.ts's showProgress() draws.
    constexpr uint8_t kOutlineX1 = 1, kOutlineY1 = 16, kOutlineX2 = 126, kOutlineY2 = 27;
    _display.DrawRect(kOutlineX1, kOutlineY1, kOutlineX2, kOutlineY2, true, false);
    constexpr uint8_t kFillX1 = kOutlineX1 + 2, kFillY1 = kOutlineY1 + 2;
    constexpr uint8_t kFillY2 = kOutlineY2 - 2;
    constexpr uint8_t kFillMaxW = kOutlineX2 - 2 - kFillX1; // inner width at 100%
    const uint8_t fillW = static_cast<uint8_t>((static_cast<uint32_t>(kFillMaxW) * (progress & 0x7F)) / 127u);
    if (fillW > 0) {
        _display.DrawRect(kFillX1, kFillY1, static_cast<uint8_t>(kFillX1 + fillW - 1), kFillY2, true, true);
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

void OledScreen::ShowLine(const char* label, const char* value) {
    // Held for the whole draw+transfer (see i2c1_lock.h) — Pads::Process()
    // skips its I2C poll in AudioCallback while this is up, so the ~20ms
    // Update() below can't get torn by the audio ISR landing mid-transfer.
    i2c1_bus_busy = true;
    _display.Fill(false);

    // Label row: Font_6x8, uppercased and truncated to the 21-char budget
    // (128px / 6px advance) — same rule as LABEL_CHARS in oled-mini.ts.
    char labelBuf[kBufLen];
    size_t labelLen = std::min(strlen(label), kBufLen - 1);
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
