#pragma once

// I2C1 (D11 SCL / D12 SDA) is shared: the MPR121 touch pads are polled
// every audio block from AudioCallback — ISR context (touch/pads.cpp) —
// while the OLED (display/oled_screen.cpp) pushes its ~500-byte frame
// buffer from the main loop as a long run of blocking single-byte I2C
// transmits (tens of ms). Neither side's transfer survives being
// interrupted mid-transaction by the other — the peripheral's shift
// register has no way to resume — so a preempted OLED update just leaves
// whatever's left of the old frame in place (this is why a redraw can
// show old and new characters mixed on the same row).
//
// The audio ISR can't block waiting on the main loop (that's a guaranteed
// glitch), so the fix runs the other way: the main loop's OLED transfer
// holds this flag up, and the ISR-side touch poll checks it and skips
// itself for that block (reusing the last known touch state) rather than
// risking a torn read on top of a torn display write.
inline volatile bool i2c1_bus_busy = false;
