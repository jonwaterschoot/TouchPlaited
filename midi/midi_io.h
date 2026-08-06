#pragma once

#include <cstddef>
#include <cstdint>

namespace synthux {

// MIDI I/O over two transports:
//   - TRS/DIN MIDI on USART1 (D13 TX / D14 RX, 31250 baud). Present only on
//     hardware-modded Simple Touch boards, but otherwise always initialized —
//     an idle UART costs nothing. Compiled out entirely by -DOLED_I2C4, which
//     hands D13/D14 to the display's own I2C4 bus; the two cannot coexist.
//   - USB device MIDI on the Seed's built-in port, only when built with
//     -DUSB_MIDI (mutually exclusive with USB serial logging / CPU meter).
//
// Events from both transports funnel through one set of handlers so TRS and
// USB behave identically. Handlers run inside a short IRQ-off critical
// section: they mutate voice/knob state the audio ISR also owns.
struct MidiHandlers {
    void (*note_on)(uint8_t channel, uint8_t note, uint8_t velocity);
    void (*note_off)(uint8_t channel, uint8_t note);
    void (*control_change)(uint8_t channel, uint8_t control, uint8_t value);
    void (*all_notes_off)();   // CC120 All Sound Off / CC123 All Notes Off (DAW stop)
    void (*clock)();           // F8 — 24 ppqn timing tick
    void (*start)();           // FA
    void (*cont)();            // FB ('continue' is a keyword)
    void (*stop)();            // FC
};

class MidiIO {
public:
    // Call once after hw.Init(), before StartAudio.
    void Init();

    // Main-loop only: pops received events into the handlers, then drains the
    // outgoing queue. Never call from the audio ISR — a UART message is ~1ms
    // of blocking TX, a quarter of the block budget.
    void Service(const MidiHandlers& handlers);

    // Send a complete SysEx frame (F0…F7) on the USB transport only — TRS at
    // 31250 baud would block ~0.3ms per byte, and telemetry is host-facing.
    // Main-loop only, sends immediately (no queue). No-op without -DUSB_MIDI.
    void SendSysexUsb(const uint8_t* bytes, size_t len);

    // Queue a message for both transports. Producers never overlap: pad/seq
    // events push from the audio ISR; the clock/transport echoes push from
    // the MIDI handlers, which run in the main loop with IRQs off.
    void SendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void SendNoteOff(uint8_t channel, uint8_t note);
    void SendClock();      // F8
    void SendStart();      // FA
    void SendContinue();   // FB
    void SendStop();       // FC

private:
    void push(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len);

    struct OutMsg {
        uint8_t len;
        uint8_t b[3];
    };
    static constexpr uint32_t kQueueSize = 64;   // power of two
    OutMsg            queue_[kQueueSize];
    volatile uint32_t q_head_ = 0;   // written by the audio ISR
    volatile uint32_t q_tail_ = 0;   // written by Service (main loop)
};

} // namespace synthux
