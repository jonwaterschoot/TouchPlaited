#pragma once

#include <cstddef>

namespace synthux {

// Shared FX section: one reverb + one stereo delay, each fed by its own send
// bus accumulated in VoicePool::Render (per-group send levels live there).
// The mirror-knob decode (center = off, each side = a character, wet grows
// outward) happens in TouchPlaited.cpp; this class receives the decoded
// side/amount. Buffers live in SDRAM — cleared in Init() because .sdram_bss
// is NOLOAD (the startup code never zeroes it).
//
// All Plaits/stmlib headers are confined to fx.cpp, same rule as
// plaits_voice.h — see the note there about stmlib's global `namespace impl`.
class FxSection {
public:
    // Call from main() after hw.Init() (SDRAM must be up). Clears all buffers.
    void Init(float sample_rate);

    // side: -1 = left half of the knob, +1 = right half, 0 = center (off).
    // amount: 0..1, distance from the center dead zone (drives wet-coupled
    // character params; the wet level itself is the send gain in VoicePool).
    // Reverb: left = room (short, damped), right = hall (long, bright).
    void SetReverbCharacter(int side, float amount);
    // Delay: left = slapback (~120 ms), right = tempo-synced dotted 1/8.
    // synced_samples: current dotted-1/8 length in samples (from the seq clock).
    void SetDelayCharacter(int side, float amount, float synced_samples);

    // Add the FX return into main_l/r. Send buses are read-only. Each FX
    // sleeps (skips rendering entirely) once its input AND its own tail have
    // been silent long enough — same trick as voice sleep, so an unused FX
    // costs nothing.
    void ProcessDelay(const float* send_l, const float* send_r,
                      float* main_l, float* main_r, size_t size);
    void ProcessReverb(const float* send_l, const float* send_r,
                       float* main_l, float* main_r, size_t size);
};

extern FxSection fx;

} // namespace synthux
