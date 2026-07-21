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
