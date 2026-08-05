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
// The idle/status fallback is back (2026-08-04), built to avoid what killed
// the first attempt: that one included seq_step, which changes every block —
// far faster than the screen can redraw, since each Update() is an I2C
// transfer — so it never caught most values and just looked broken.
// status_row() in oled_ui.cpp is built only from slow-moving fields
// (mode/genre/transport/model/edited slot), so a redraw is always still
// true by the time it lands. It takes over kIdleMs after the last callout,
// and immediately when a hold ends, so a finished progress bar can't sit
// frozen on the screen.
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
    uint32_t idle_at_ms_   = 0;      // when the last callout was drawn
    bool     showing_status_ = false; // status row currently owns the screen
    daisy::FixedCapStr<24> status_label_{};  // as last drawn, to skip redraws
    daisy::FixedCapStr<24> status_value_{};
    daisy::FixedCapStr<24> last_label_{};    // last callout, for blink repaints
    daisy::FixedCapStr<24> last_value_{};
    bool     blink_phase_  = false;  // phase the indicator block last drew at
    int      list_mod_     = -1;     // modifier whose combo list owns the screen
    int      list_offset_  = 0;      // first row shown, for lists over 4 rows
    uint32_t list_at_ms_   = 0;      // when the current window went up
};

} // namespace synthux

#endif
