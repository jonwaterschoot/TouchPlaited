#pragma once

#include <cstdint>

namespace synthux {

class MidiIO;

// Panel-state snapshot for the visualizer webapp (protocol: visualizer/PLAN.md
// §2 and visualizer/src/core/protocol.ts — keep the three in lockstep).
// Fields are already in wire format: 7-bit values only.
struct TelemetryState {
    uint16_t pads;         // bit i = pad Pi touched (P0..P11)
    uint8_t  controls[8];  // S30..S37, 0..127
    uint8_t  sw1;          // panel position: 0 left/min · 1 center · 2 right/maj
    uint8_t  sw2;          // panel position: 0 up/Seq · 1 center/Random · 2 down/Pitch
    uint8_t  led;          // user LED, 0 or 127
    uint8_t  model;        // engine 0..23
    uint8_t  mode;         // 0 Seq, 1 Random, 2 Pitch
    bool     playing;      // drum sequencer transport
    bool     snd_edit;     // Arp/Mel sound-edit knob layer active (P0+P1 toggle)
    uint8_t  seq_step;     // 0..63, 0x7F = seq inactive
    uint8_t  octave;       // octave_offset + 3, 0..6 (center = 3)
    uint8_t  root;         // root_semitone, 0..11
    uint8_t  fx_drive;     // active mode's drive / send values, 0..127
    uint8_t  fx_reverb;
    uint8_t  fx_delay;
    uint8_t  rec_slot;     // slot being edited in Recording, 0x7F = not recording
    uint8_t  kit[7][6];    // per drum slot: engine, harmonics, timbre, morph,
                           // decay, MIDI note — the app's info screen / rec labels
    uint8_t  rec_layers;   // NoteRec committed layer count, 0..5 (Arp/Mel Rec)
    uint8_t  rec_mute;     // bit i = NoteRec layer i muted
    uint8_t  clock_src;    // master clock: 0 internal, 1 MIDI, 2 CV
    uint8_t  arp_flags;    // bits0-1 Arp/Mel sub-state (0 Arp, 1 Hold, 2 Rec —
                           // the change-latched state, NOT the live lever),
                           // bit2 Rec capture armed (P2+P10), bit3 melodic
                           // transport running (arp_run_on — P2+P10's other
                           // meaning, outside Rec). Armed and running are
                           // independent: a punched-out Rec keeps looping.
    uint8_t  seq_pattern;  // slot within the current genre that's actually
                           // playing, 0-based (Sequencer::VariantSlot) — S35
                           // sits behind a pickup, so its pot position is not
                           // a safe stand-in for what's on the drums
    uint8_t  hold_kind;    // which build-toward-threshold hold is in progress,
                           // 0 none, matches the LED's own accelerating-blink
                           // family and precedence (display/oled_ui.cpp):
                           // 1 P0+P2 (re-randomize/vary sound), 2 rec entry,
                           // 3 layer clear, 4 layer copy, 5 rec save,
                           // 6 rec cancel (no build-up — confirm only),
                           // 7 P0+P1 sound-edit toggle
    uint8_t  hold_progress; // 0..127, fraction of the way to hold_kind's next
                            // threshold; 0 when hold_kind is 0. For hold_kind
                            // 1 (P0+P2) this is relative to the CURRENT stage
                            // only — each stage fills 0..127 on its own.
    uint8_t  hold_stage;    // count of thresholds already confirmed for the
                            // current hold: 0 while still building, then
                            // increments the instant a threshold fires (1, 2,
                            // 3 for P0+P2's stages; 0->1 for the single-stage
                            // holds). Edge-detect against the previous frame
                            // to catch the confirm — it stays at its fired
                            // value for as long as the gesture stays held.
    uint8_t  hold_outcome;  // which of two results a confirm had, at the
                            // instant hold_stage becomes 1. kind 3 (layer
                            // clear): 1 success, 2 empty (nothing to clear).
                            // kind 7 (sound edit): 1 entered, 2 left — the
                            // same combo both ways, and snd_edit has already
                            // flipped by the time the flash draws. 0 = n/a.
};

// Emits full-state SysEx frames over USB MIDI, rate-limited:
//   - STATE on any change, at most every 33 ms (~30 Hz), LED included — the
//     visualizer reproduces blink patterns from this instead of re-implementing
//     them
//   - STATE heartbeat every 500 ms regardless, so a visualizer joining
//     mid-stream syncs without a request round-trip
//   - FX frame when the send values change, at most every 100 ms
//   - KIT frame when the drum kit changes, at most every 100 ms, with a 2 s
//     heartbeat (larger frame, slower-moving data)
//
// Main-loop only (called via service_midi); never from the audio ISR.
class Telemetry {
public:
    void Service(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);

private:
    void SendState(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);
    void SendFx(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);
    void SendKit(const TelemetryState& s, uint32_t now_ms, MidiIO& midi);

    TelemetryState last_state_{};   // as of the last STATE frame
    TelemetryState last_fx_{};      // as of the last FX frame
    TelemetryState last_kit_{};     // as of the last KIT frame
    uint32_t state_ms_ = 0;
    uint32_t fx_ms_    = 0;
    uint32_t kit_ms_   = 0;
    bool     started_  = false;
};

} // namespace synthux
