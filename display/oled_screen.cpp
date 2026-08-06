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
void draw_label(daisy::OledDisplay<SSD1306DirtyDriver>& d,
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
uint32_t g_page_count    = 0;
} // namespace

uint32_t OledScreen::LastFrameUs()  { return g_frame_us_last; }
uint32_t OledScreen::MaxFrameUs()   { return g_frame_us_max; }
uint32_t OledScreen::FrameCount()   { return g_frame_count; }
uint32_t OledScreen::PageCount()    { return g_page_count; }
void     OledScreen::ResetFrameStats() {
    g_frame_us_max = 0;
    g_frame_count  = 0;
    g_page_count   = 0;
}

void OledScreen::PushFrame() {
    // The i2c1_bus_busy interlock lives inside the driver's Update(), held per
    // page rather than across the whole frame — a frame's wall-clock duration
    // measured up to 244ms under load, and blinding the pads for that long is
    // what the per-page release fixes. See display/oled_dirty_driver.h.
    // Timed in ticks, not System::GetUs(). GetUs() is GetTick()/(freq/1e6)
    // (lib/libDaisy/src/per/tim.cpp), so the divide happens after the 32-bit
    // tick wrap and the microsecond value rolls over every ~21.5s — a frame
    // straddling that produced a ~4.27e9 reading, which is where the negative
    // maxima in the 2026-08-06 captures came from. Tick subtraction wraps
    // correctly; convert afterwards.
    const uint32_t t0    = System::GetTick();
    _display.Update();
    const uint32_t ticks = System::GetTick() - t0;
    const uint32_t per_us = System::GetTickFreq() / 1000000u;
    const uint32_t us     = per_us ? ticks / per_us : 0u;

    // Wall clock, not bus time: the audio ISR preempts this transfer freely,
    // so under load the figure is dominated by how little of the main loop is
    // left rather than by the bus. That is the point — it is the latency a
    // frame actually experiences, and the 2026-08-06 I2C4 measurements showed
    // it running 7-40x the underlying transfer cost. See notes.md.
    g_frame_us_last = us;
    if (us > g_frame_us_max) g_frame_us_max = us;
    g_frame_count++;
    g_page_count += SSD1306DirtyDriver::PagesPushed();
}

void OledScreen::Init(daisy::DaisySeed& hw) {
    (void)hw; // transport owns its own I2C init; kept for Pads::Init() symmetry

    OledDisplay<SSD1306DirtyDriver>::Config cfg;
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
