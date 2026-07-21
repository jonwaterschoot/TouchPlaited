#pragma once

#include <cstddef>

namespace synthux {

// Four independent FX groups (21/07/26 follow-up to the 20/07/26 notes:
// Basic Pitch, the arp, Rec and the drum seq each get their own reverb AND
// delay — not just their own send amount, their own *character* too, since
// one shared instance can't be room and hall at once). Each group owns its
// own reverb + delay DSP instance (and SDRAM buffers, ~570 KB per group —
// trivial against the 64 MB budget) and sleeps independently when its send
// is quiet, so an unused group's FX costs ~0 regardless of the others.
enum class FxGroup : int { kBP = 0, kArp = 1, kRec = 2, kDrum = 3 };
constexpr int kFxGroupCount = 4;

// The mirror-knob decode (center = off, each side = a character, wet grows
// outward) happens in TouchPlaited.cpp; this class receives the decoded
// side/amount. Buffers live in SDRAM — cleared in Init() because .sdram_bss
// is NOLOAD (the startup code never zeroes it).
//
// All Plaits/stmlib headers are confined to fx.cpp, same rule as
// plaits_voice.h — see the note there about stmlib's global `namespace impl`.
class FxSection {
public:
    explicit FxSection(FxGroup group) : group_(group) {}

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

private:
    FxGroup group_;
};

extern FxSection fx_bp, fx_arp, fx_rec, fx_drum;

} // namespace synthux
