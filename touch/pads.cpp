#include "pads.h"
#include "../i2c1_lock.h"

using namespace synthux;
using namespace daisy;

// The return value matters, and used to be thrown away. Mpr121::Init can bail
// out early — it reads CONFIG2 back after the soft reset and returns ERR if it
// isn't 0x24 — and everything after that point is skipped: the touch/release
// thresholds, every baseline-filter register, and the final ECR write that
// actually *starts* the chip. What's left is an MPR121 in stop mode with no
// electrodes enabled, whose touch register the chip never updates again. We
// then read undefined bits every audio block and play them as notes.
//
// That failure cost an entire evening in Aug 2026 (notes.md → "The stale
// libdaisy.a"), because "pads fire by themselves and real touches do nothing"
// looks like voice stealing, CPU overload, corrupted settings — anything but a
// touch controller that never came up. One retry, then remember the answer so
// boot can say so out loud.
void Pads::Init(DaisySeed& hw) {
    Mpr121I2C::Config cfg;
    _ready = _mpr.Init(cfg) == Mpr121I2C::Result::OK;
    if (!_ready) {
        // A second attempt costs ~1ms and covers the case where the first
        // CONFIG2 read raced the soft reset rather than genuinely failing.
        System::Delay(2);
        _ready = _mpr.Init(cfg) == Mpr121I2C::Result::OK;
    }
}

void Pads::Process() {
    // OLED mid-transfer on the shared I2C1 bus (see i2c1_lock.h) — skip this
    // poll rather than risk a torn read; _state just holds last block's
    // value until the bus frees up, a block or two later.
    if (i2c1_bus_busy) return;

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
