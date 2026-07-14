#include "midi_io.h"
#include "daisy_seed.h"

using namespace daisy;

namespace synthux {

// File-scope transports: each MidiHandler carries a 256-event FIFO and there
// is exactly one MidiIO in the app.
static MidiUartHandler uart_midi;
#ifdef USB_MIDI
static MidiUsbHandler usb_midi;
#endif

void MidiIO::Init() {
    // Defaults are exactly the TRS mod's wiring: USART1, D14 = RX, D13 = TX.
    MidiUartHandler::Config uart_cfg;
    uart_midi.Init(uart_cfg);
    uart_midi.StartReceive();

#ifdef USB_MIDI
    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::INTERNAL;
    // With no host attached every TX fails; each retry burns 100us in the
    // main loop for nothing.
    usb_cfg.transport_config.tx_retry_count = 1;
    usb_midi.Init(usb_cfg);
    usb_midi.StartReceive();
#endif
}

static void dispatch(MidiEvent ev, const MidiHandlers& h) {
    // Handlers touch pool/pickup state the audio ISR owns. Each event is a
    // few microseconds of plain stores — keep the ISR out for that long.
    __disable_irq();
    switch (ev.type) {
        case NoteOn: {   // vel-0 NoteOns already arrive as NoteOff (parser)
            NoteOnEvent e = ev.AsNoteOn();
            h.note_on(static_cast<uint8_t>(e.channel), e.note, e.velocity);
            break;
        }
        case NoteOff: {
            NoteOffEvent e = ev.AsNoteOff();
            h.note_off(static_cast<uint8_t>(e.channel), e.note);
            break;
        }
        case ControlChange: {
            ControlChangeEvent e = ev.AsControlChange();
            h.control_change(static_cast<uint8_t>(e.channel),
                             e.control_number, e.value);
            break;
        }
        case ChannelMode:   // CCs 120–127 arrive as this type, not ControlChange
            if (ev.cm_type == AllSoundOff || ev.cm_type == AllNotesOff)
                h.all_notes_off();
            break;
        case SystemRealTime:
            switch (ev.srt_type) {
                case TimingClock: h.clock(); break;
                case Start:       h.start(); break;
                case Continue:    h.cont();  break;
                case Stop:        h.stop();  break;
                default: break;
            }
            break;
        default: break;
    }
    __enable_irq();
}

void MidiIO::Service(const MidiHandlers& handlers) {
    uart_midi.Listen();   // restarts RX after a UART overrun error
    while (uart_midi.HasEvents()) dispatch(uart_midi.PopEvent(), handlers);
#ifdef USB_MIDI
    usb_midi.Listen();
    while (usb_midi.HasEvents()) dispatch(usb_midi.PopEvent(), handlers);
#endif

    // Bounded drain: UART TX is blocking (~1ms per 3-byte message at 31250
    // baud) and Service also runs inside LED delay loops — cap the stall,
    // leave the rest for the next call.
    int budget = 4;
    while (q_tail_ != q_head_ && budget--) {
        OutMsg& m = queue_[q_tail_ & (kQueueSize - 1)];
        uart_midi.SendMessage(m.b, m.len);
#ifdef USB_MIDI
        usb_midi.SendMessage(m.b, m.len);
#endif
        q_tail_ = q_tail_ + 1;
    }
}

void MidiIO::SendSysexUsb(const uint8_t* bytes, size_t len) {
#ifdef USB_MIDI
    // libDaisy's MidiUsbTransport::Tx packetizes SysEx natively (CIN 0x04/
    // 0x05-0x07) and copies into the endpoint buffer — non-blocking apart
    // from the single 100us retry configured in Init.
    usb_midi.SendMessage(const_cast<uint8_t*>(bytes), len);
#else
    (void)bytes;
    (void)len;
#endif
}

void MidiIO::push(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t len) {
    uint32_t head = q_head_;
    if (head - q_tail_ >= kQueueSize) return;   // full — drop, never block
    OutMsg& m = queue_[head & (kQueueSize - 1)];
    m.len  = len;
    m.b[0] = b0;
    m.b[1] = b1;
    m.b[2] = b2;
    // Element stores must be visible before the publish (ISR produces,
    // main loop consumes; same core, so a compiler barrier suffices).
    __asm volatile("" ::: "memory");
    q_head_ = head + 1;
}

void MidiIO::SendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    push(0x90 | (channel & 0x0F), note & 0x7F, velocity & 0x7F, 3);
}

void MidiIO::SendNoteOff(uint8_t channel, uint8_t note) {
    push(0x80 | (channel & 0x0F), note & 0x7F, 0, 3);
}

void MidiIO::SendClock()    { push(0xF8, 0, 0, 1); }
void MidiIO::SendStart()    { push(0xFA, 0, 0, 1); }
void MidiIO::SendContinue() { push(0xFB, 0, 0, 1); }
void MidiIO::SendStop()     { push(0xFC, 0, 0, 1); }

} // namespace synthux
