#pragma once

#include <cstdint>
#include "oled_screen.h"
#include "../midi/telemetry.h"
#include "../nocopy.h"

#ifdef __cplusplus

namespace synthux {

// Drives OledScreen from the exact same panel-state snapshot the visualizer
// decodes (TelemetryState — see midi/telemetry.h and
// visualizer/src/core/state.ts, which is a 1:1 field mirror of it). The
// label/value text is a port of visualizer/src/panel/labels.ts's
// describeControl()/describePad()/etc. against that struct's wire fields
// directly, so the panel and the physical screen read the same thing from
// the same data rather than needing to be kept in sync by hand.
//
// Two known simplifications vs. the web app (documented in oled_ui.cpp
// where they happen): drum-slot editing (Seq recording) uses a flatter set
// of "Slot X" labels than the web's full per-knob nesting, and the "no
// effect on <model>" dead-knob message is shortened to "no effect" — both
// for the 128x32 screen's char budget, not because the underlying logic
// differs.
//
// Deliberately no idle/status fallback: it used to fall back to a
// model+mode+seq-step row whenever nothing had just changed, but a running
// sequencer changes step every block, far faster than the screen can
// redraw (each Update() is an I2C transfer) — so it never caught most step
// values and just looked broken. The screen now simply holds the last
// touched control indefinitely; there is nothing to show until something
// is actually touched.
class OledUi {
public:
    OledUi() {}
    ~OledUi() {}

    // Call once per main-loop pass with the same TelemetryState the
    // Telemetry class just sent (or is about to) — main-loop-only, like
    // Telemetry::Service, never from the audio ISR.
    void Service(const TelemetryState& t, uint32_t now_ms, OledScreen& oled);

private:
    NOCOPY(OledUi)

    TelemetryState last_{};
    bool     has_last_     = false;
    uint32_t next_draw_ms_ = 0;
};

} // namespace synthux

#endif
