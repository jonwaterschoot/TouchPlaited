#pragma once

#include "daisy_seed.h"
#include "dev/mpr121.h"
#include <stdint.h>
#include <functional>
#include "../nocopy.h"

#ifdef __cplusplus

namespace synthux {

class Pads {
public:
    Pads(): _state { 0 } {}
    ~Pads() {}

    void Init(daisy::DaisySeed& hw);
    void Process();

    // Did the MPR121 come up? False means Init bailed out partway and the
    // chip is NOT running — see the comment in pads.cpp. Worth surfacing at
    // boot: a silent failure here reads as random notes playing themselves,
    // which looks like anything except a dead touch controller
    // (notes.md → "The stale libdaisy.a").
    bool Ready() const { return _ready; }

    void SetOnTouch(std::function<void(uint16_t)> on_touch) {
        _on_touch = on_touch;
    }
    void SetOnRelease(std::function<void(uint16_t)> on_release) {
        _on_release = on_release;
    }

    bool IsTouched(uint16_t pad) { return _state & (1 << pad); }
    bool HasTouch()              { return _state > 0; }

private:
    NOCOPY(Pads)

    uint16_t _state;
    bool     _ready = false;
    daisy::Mpr121I2C _mpr;

    std::function<void(uint16_t pad)> _on_touch;
    std::function<void(uint16_t pad)> _on_release;
};

} // namespace synthux

#endif
