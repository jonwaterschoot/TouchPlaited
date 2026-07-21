#pragma once

#include "daisy_seed.h"
#include "../nocopy.h"
#include <array>

#ifdef __cplusplus

namespace synthux {

// AnalogControl with a guard-band rescale: the pots never reach the ADC
// rails (hardware tops out around 96-97% — drop on the analog rail feeding
// the pots, pot end resistance, ADC gain error), so raw [kLow, kHigh] maps
// to a clamped 0..1. Without this, rail-stored values are unreachable: full
// arp Density needs >= 98.4% for the top Euclidean fill bucket, and every
// continuous param silently loses its top ~3-4%. The band is fixed (no
// per-unit calibration): kHigh sits below the lowest ceiling observed on
// hardware, trading ~4% of pot travel per end for guaranteed rails.
class Knob {
public:
    void Init(uint16_t* adcptr, float sr) { _ctrl.Init(adcptr, sr); }

    float Process() {
        float v = (_ctrl.Process() - kLow) * kNorm;
        _val = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        return _val;
    }

    float Value() const { return _val; }

private:
    static constexpr float kLow  = 0.005f;
    static constexpr float kHigh = 0.95f;
    static constexpr float kNorm = 1.f / (kHigh - kLow);

    daisy::AnalogControl _ctrl;
    float _val = 0.f;
};

class Knobs {
public:
    Knobs() {}
    ~Knobs() {}

    void Init(daisy::DaisySeed& hw);

    Knob& s30() { return _knobs[0]; }
    Knob& s31() { return _knobs[1]; }
    Knob& s32() { return _knobs[2]; }
    Knob& s33() { return _knobs[3]; }
    Knob& s34() { return _knobs[4]; }
    Knob& s35() { return _knobs[5]; }
    Knob& s36() { return _knobs[6]; }
    Knob& s37() { return _knobs[7]; }

    // S43 clock-in jack (A11) — rides the same ADC DMA scan as the knobs
    // (hw.adc.Init can only run once). Raw counts, no AnalogControl: the
    // clock edge detector wants the unfiltered level, not a smoothed pot.
    uint16_t ClockInRaw() const { return _clock_in ? *_clock_in : 0; }

private:
    NOCOPY(Knobs)

    std::array<Knob, 8> _knobs;
    uint16_t* _clock_in = nullptr;
};

} // namespace synthux

#endif
