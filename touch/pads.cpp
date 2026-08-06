#include "pads.h"
#include "../i2c1_lock.h"

using namespace synthux;
using namespace daisy;

void Pads::Init(DaisySeed& hw) {
    Mpr121I2C::Config cfg;
    _mpr.Init(cfg);
}

void Pads::Process() {
#ifndef OLED_I2C4
    // OLED mid-transfer on the shared I2C1 bus (see i2c1_lock.h) — skip this
    // poll rather than risk a torn read; _state just holds last block's
    // value until the bus frees up, a block or two later. With the display on
    // its own I2C4 bus there is no contention and the poll never skips.
    if (i2c1_bus_busy) return;
#endif

    uint16_t pad;
    bool is_touched;
    bool was_touched;
    auto state = _mpr.Touched();
    for (uint16_t i = 0; i < 12; i++) {
        pad = 1 << i;
        is_touched  = state & pad;
        was_touched = _state & pad;
        if (_on_touch != nullptr && is_touched && !was_touched) {
            _on_touch(i);
        } else if (_on_release != nullptr && was_touched && !is_touched) {
            _on_release(i);
        }
    }
    _state = state;
}
