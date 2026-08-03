#include "oled_boot.h"
#include "daisy_seed.h"
#include "util/oled_fonts.h"
#include <cmath>

using namespace synthux;
using namespace daisy;

namespace {

// "Plaited" is emphasized one font size above "Touch" (Font_6x8 vs
// Font_7x10 — adjacent entries in oled_fonts.h) — the two sit on a shared
// visual baseline (bottom edge) and the pair is centered as one block.
constexpr char kWord1[] = "Touch";
constexpr char kWord2[] = "Plaited";
constexpr int  kWord1Len = sizeof(kWord1) - 1; // 5
constexpr int  kWord2Len = sizeof(kWord2) - 1; // 7
constexpr int  kTextLen  = kWord1Len + kWord2Len; // 12

constexpr uint8_t kFont1W = 6, kFont1H = 8;
constexpr uint8_t kFont2W = 7, kFont2H = 10;
constexpr int      kWord1PxW = kWord1Len * kFont1W; // 30
constexpr int      kWord2PxW = kWord2Len * kFont2W; // 49
constexpr uint8_t   kX0      = (128 - (kWord1PxW + kWord2PxW)) / 2; // 24, centered
constexpr uint8_t   kWord2Y0 = (32 - kFont2H) / 2;                  // 11
constexpr uint8_t   kBaseline = kWord2Y0 + kFont2H;                 // 21, shared bottom edge
constexpr uint8_t   kWord1Y0  = kBaseline - kFont1H;                // 13

constexpr uint32_t kLetterStepMs   = 35;   // typewriter cadence while materializing
constexpr uint32_t kHoldMs         = 1000; // full word held before it scatters — halved from the original 2000ms
constexpr uint32_t kMessageHoldMs  = 2000; // status line held before Run() returns control to main()
// Explicit per-frame pacing for the scatter, on top of whatever the I2C
// transfer itself costs — makes the "another ~500ms" ask deterministic
// instead of riding on hardware transfer speed (52 frames * 10ms ~= 520ms).
constexpr uint32_t kFrameDelayMs   = 10;
// 512 covers "TouchPlaited" across the two fonts above with margin (measured
// worst case ~160 lit pixels at this pairing) — a live device would just
// draw fewer particles than exist if this were ever undersized, not
// overflow.
constexpr int       kMaxParticles  = 512;
// Half the linear speed of the original single-font version, run for twice
// as many frames so each particle still travels its full arc — reads as
// the same motion in slow motion rather than a shorter, choppier one.
constexpr int       kScatterFrames = 52;

// Slow blink while "loading" (materialize through scatter) so a unit with
// no screen attached still shows boot progress. Ticked in small time slices
// from every wait in Run() rather than off a hardware timer, since the
// whole sequence is already just one blocking call chasing wall-clock time.
constexpr uint32_t kBlinkPeriodMs = 300; // on 300ms / off 300ms
struct SlowBlink {
    void (*set_led)(bool);
    uint32_t acc_ms = 0;
    bool     state  = false;
    void Tick(uint32_t dt_ms) {
        acc_ms += dt_ms;
        if (acc_ms >= kBlinkPeriodMs) {
            acc_ms -= kBlinkPeriodMs;
            state = !state;
            set_led(state);
        }
    }
};

// System::Delay(ms) in small slices so SlowBlink keeps ticking through long
// waits (the 2s word-hold in particular) instead of freezing for the whole
// stretch.
void WaitBlinking(uint32_t ms, SlowBlink& blink) {
    constexpr uint32_t kStepMs = 20;
    while (ms > 0) {
        uint32_t step = ms < kStepMs ? ms : kStepMs;
        System::Delay(step);
        blink.Tick(step);
        ms -= step;
    }
}

// Self-contained LCG so this doesn't reach into TouchPlaited.cpp's static
// rand_f() — same constants (Numerical Recipes LCG), private instance
// seeded from the caller's boot-time clock read.
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}
    float Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.f;
    }
    float Range(float lo, float hi) { return lo + Next() * (hi - lo); }
};

struct Particle {
    float x, y, vx, vy;
    int   life;
};

struct CharSlot {
    char           ch;
    const FontDef* font;
    uint8_t        x0, y0; // top-left of this glyph's cell
};

// Index = 0..kTextLen-1 across both words, in reading order.
CharSlot SlotAt(int c) {
    if (c < kWord1Len) {
        return { kWord1[c], &Font_6x8,
                 static_cast<uint8_t>(kX0 + c * kFont1W), kWord1Y0 };
    }
    const int i = c - kWord1Len;
    return { kWord2[i], &Font_7x10,
             static_cast<uint8_t>(kX0 + kWord1PxW + i * kFont2W), kWord2Y0 };
}

// Whether font's bitmap for ch has a lit pixel at (row, col) within its
// character cell — same bit layout OneBitGraphicsDisplayImpl::WriteChar
// reads (MSB-first per row, see lib/libDaisy/src/hid/disp/display.h).
bool GlyphBit(const FontDef& font, char ch, int row, int col) {
    if (ch < 32 || ch > 126) return false;
    uint16_t bits = font.data[(ch - 32) * font.FontHeight + row];
    return (bits << col) & 0x8000;
}

} // namespace

void OledBoot::Run(OledScreen& oled, uint32_t seed, bool settings_restored,
                    void (*set_led)(bool)) {
    Rng rng(seed ^ 0x9E3779B9u);
    SlowBlink blink{set_led};
    set_led(false);

    // ── Materialize: reveal one letter per step ─────────────────────────
    oled.BeginFrame();
    for (int c = 0; c < kTextLen; ++c) {
        const CharSlot s = SlotAt(c);
        for (int row = 0; row < s.font->FontHeight; ++row) {
            for (int col = 0; col < s.font->FontWidth; ++col) {
                if (GlyphBit(*s.font, s.ch, row, col)) {
                    oled.SetPixel(s.x0 + col, s.y0 + row, true);
                }
            }
        }
        oled.EndFrame();
        WaitBlinking(kLetterStepMs, blink);
    }
    WaitBlinking(kHoldMs, blink);

    // ── Collect one particle per lit pixel, scattered outward from the
    //    word block's center with a little downward drift ───────────────
    static Particle particles[kMaxParticles];
    int n = 0;
    const float cx = kX0 + (kWord1PxW + kWord2PxW) * 0.5f;
    const float cy = (kWord2Y0 + kBaseline) * 0.5f;
    for (int c = 0; c < kTextLen && n < kMaxParticles; ++c) {
        const CharSlot s = SlotAt(c);
        for (int row = 0; row < s.font->FontHeight && n < kMaxParticles; ++row) {
            for (int col = 0; col < s.font->FontWidth && n < kMaxParticles; ++col) {
                if (!GlyphBit(*s.font, s.ch, row, col)) continue;
                Particle& p = particles[n++];
                p.x = static_cast<float>(s.x0 + col);
                p.y = static_cast<float>(s.y0 + row);
                float dx = p.x - cx, dy = p.y - cy;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f) len = 0.5f;
                float nx = dx / len, ny = dy / len;
                p.vx = nx * rng.Range(0.2f, 0.65f) + rng.Range(-0.125f, 0.125f);
                p.vy = ny * rng.Range(0.2f, 0.65f) - 0.1f + rng.Range(-0.1f, 0.1f);
                p.life = static_cast<int>(rng.Range(28.f, static_cast<float>(kScatterFrames)));
            }
        }
    }

    // ── Disintegrate: scatter + gentle gravity until every particle has
    //    either run out of life or drifted off-screen ────────────────────
    for (int frame = 0; frame < kScatterFrames; ++frame) {
        oled.BeginFrame();
        for (int i = 0; i < n; ++i) {
            Particle& p = particles[i];
            if (p.life <= 0) continue;
            p.x += p.vx;
            p.y += p.vy;
            p.vy += 0.02f; // gentle gravity so late frames fall rather than fly forever
            --p.life;
            if (p.x < 0.f || p.x >= 128.f || p.y < 0.f || p.y >= 32.f) {
                p.life = 0;
                continue;
            }
            oled.SetPixel(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), true);
        }
        oled.EndFrame();
        WaitBlinking(kFrameDelayMs, blink);
    }

    // ── Ready: one quick flash confirms loading finished, then settle into
    //    a status line reporting whether a prior session was restored ────
    set_led(false); // known state regardless of where the blink cycle left it
    set_led(true);
    System::Delay(80);
    set_led(false);

    oled.ShowLine("TouchPlaited",
                   settings_restored ? "Settings loaded" : "New - defaults");
    // Held here, blocking, before returning to main() — nothing else can
    // touch the OLED until Run() returns, so this is what actually
    // guarantees the message is readable rather than flashing by.
    System::Delay(kMessageHoldMs);
}
