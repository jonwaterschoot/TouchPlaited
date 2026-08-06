#pragma once

#include "dev/oled_ssd130x.h"
#include <cstring>

#ifdef __cplusplus

namespace synthux {

// SSD1306 128x32 driver that only transmits the pages whose pixels changed.
//
// The stock SSD130xDriver::Update() always pushes all four pages — ~540 bytes
// of I2C traffic — no matter how little of the screen moved. Most of what this
// app draws moves very little: a progress bar animates two of the four pages,
// the blinking capture indicator only the top one, and a status row that
// hasn't changed pushes nothing at all.
//
// Why this matters more than the bus speed, which is the non-obvious part:
// the transfer runs in the main loop and the audio ISR preempts it freely, so
// the wall-clock cost of a frame is roughly (bus time / the main loop's share
// of the CPU). At 88% audio load that multiplier measured ~8x on hardware
// (2026-08-06, notes.md). Halving the bytes therefore halves a ~44ms stall,
// not a ~5ms one — and it shortens the i2c1_bus_busy window by the same
// factor, so the pads go unpolled for proportionally less time too. Both
// effects are why this beats moving the display to a faster dedicated bus,
// which was measured and produced no observable change.
//
// Implementation is a shadow copy of the last frame actually transmitted,
// compared per page. The comparison is 512 bytes of memcmp against SRAM —
// tens of microseconds against the milliseconds it saves. buffer_ and
// transport_ are protected in the base, which is what makes this a subclass
// rather than a fork of the submodule; OledDisplay holds its driver by value
// and calls Update() non-virtually, so this override is the one that runs.
class SSD1306DirtyDriver
    : public daisy::SSD130xDriver<128, 32, daisy::SSD130xI2CTransport> {
public:
    using Base = daisy::SSD130xDriver<128, 32, daisy::SSD130xI2CTransport>;

    static constexpr size_t kWidth  = 128;
    static constexpr size_t kPages  = 32 / 8;

    void Init(Base::Config config) {
        Base::Init(config);
        // Nothing has been transmitted yet, so the shadow describes nothing —
        // the first Update() must push every page whatever it happens to
        // contain, or a page that matches uninitialised shadow bytes by
        // coincidence would never be sent.
        shadow_valid_ = false;
        pages_pushed_ = 0;
    }

    void Update() {
        uint8_t pushed = 0;
        for (size_t page = 0; page < kPages; page++) {
            uint8_t* src = &buffer_[kWidth * page];
            uint8_t* dst = &shadow_[kWidth * page];
            if (shadow_valid_ && std::memcmp(src, dst, kWidth) == 0)
                continue;

            // Each page carries its own address commands, so skipping one
            // never leaves the controller pointing somewhere unexpected.
            transport_.SendCommand(static_cast<uint8_t>(0xB0 + page));
            transport_.SendCommand(0x00);
            transport_.SendCommand(0x10);
            transport_.SendData(src, kWidth);

            std::memcpy(dst, src, kWidth);
            pushed++;
        }
        shadow_valid_ = true;
        pages_pushed_ = pushed;
    }

    // Pages actually transmitted by the last Update(), 0..kPages. 0 means the
    // frame was identical to the one before it and cost nothing but the
    // memcmp — worth reporting, because a redraw path that keeps returning 0
    // is one that should not have been asked to redraw at all.
    //
    // Static because daisy::OledDisplay holds its driver privately and
    // forwards no accessor to it, and there is exactly one display in this
    // app (TouchPlaited.cpp's `oled`). Duplicating OledDisplay purely to add
    // one getter would cost more than this note does. A second instance would
    // silently share the counter — nothing else here would break.
    static uint8_t PagesPushed() { return pages_pushed_; }

    // Forces the next Update() to push everything. For a caller that has
    // reason to believe the panel and the shadow have diverged — a controller
    // reset, or a transfer that failed midway.
    void InvalidateShadow() { shadow_valid_ = false; }

private:
    uint8_t        shadow_[kWidth * kPages];
    bool           shadow_valid_ = false;
    static inline uint8_t pages_pushed_ = 0;
};

} // namespace synthux

#endif
