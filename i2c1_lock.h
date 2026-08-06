#pragma once

// Shared-bus interlock. Only exists in the default build; with -DOLED_I2C4
// (see the Makefile) the display has I2C4/D13/D14 to itself, nothing contends
// for I2C1, and every user of this flag compiles out.
#ifndef OLED_I2C4

// I2C1 (D11 SCL / D12 SDA) is shared: the MPR121 touch pads are polled
// every audio block from AudioCallback — ISR context (touch/pads.cpp) —
// while the OLED (display/oled_screen.cpp) pushes its ~500-byte frame
// buffer from the main loop as a run of blocking per-page I2C transmits.
// Neither side's transfer survives being interrupted mid-transaction by the
// other — the peripheral's shift register has no way to resume — so a
// preempted OLED update just leaves whatever's left of the old frame in
// place (this is why a redraw can show old and new characters mixed on the
// same row).
//
// The audio ISR can't block waiting on the main loop (that's a guaranteed
// glitch), so the fix runs the other way: the main loop's OLED transfer
// holds this flag up (OledScreen::PushFrame), and the ISR-side touch poll
// checks it and skips itself for that block (reusing the last known touch
// state) rather than risking a torn read on top of a torn display write.
//
// The cost is touch latency: a frame is several audio blocks long, and the
// pads go unread for all of them. Measuring exactly how many is what the
// oled-i2c4-no-trs branch is for — see notes.md.
inline volatile bool i2c1_bus_busy = false;

#endif // !OLED_I2C4
