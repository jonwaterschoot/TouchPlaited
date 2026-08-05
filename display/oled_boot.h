#pragma once

#include <cstdint>
#include "oled_screen.h"

#ifdef __cplusplus

namespace synthux {

// One-shot boot animation, run once from main() right after
// OledScreen::Init and before hw.StartAudio() (see TouchPlaited.cpp) —
// nothing else is touching the I2C bus or the audio ISR yet, so it's free
// to block for its several-second runtime without the redraw-throttle
// concerns OledUi has to observe on every other screen update.
//
// "TouchPlaited" materializes letter by letter — "Plaited" emphasized one
// font size above "Touch" — holds for 2s, then disintegrates into particles
// that scatter outward and fall away; once the screen is clear it settles
// into a status line via OledScreen::ShowLine reporting whether
// SettingsJournal found and restored a prior session or this is a
// fresh/reset unit, held for another 2s before Run() returns. It survives a
// while longer after that: OledUi starts its idle clock on its first
// Service() call rather than at reset, so the line gets one full kIdleMs on
// screen before the device's own status row takes over (or sooner, if
// something is touched first).
//
// The whole sequence also drives the single user LED (slow blink while
// "loading", one quick flash once the status line is about to show) so a
// unit with no screen attached still shows boot progress and a ready
// signal.
class OledBoot {
  public:
    // seed: pass System::GetNow() so the scatter differs boot to boot.
    // settings_restored: same bool main() already computed from
    // SettingsJournal::Init(), reused verbatim for the final status line.
    // set_led: TouchPlaited.cpp's set_led(bool) — routed through a function
    // pointer rather than #including the .cpp's globals, and kept as the
    // one path to the LED so telemetry's LED-shadow mirroring (see
    // set_led's own comment) still sees the boot blink.
    static void Run(OledScreen& oled, uint32_t seed, bool settings_restored,
                     void (*set_led)(bool));

  private:
    OledBoot() = delete;
};

} // namespace synthux

#endif
