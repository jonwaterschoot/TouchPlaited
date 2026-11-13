#include "telemetry.h"
#include "midi_io.h"
#include "../display/oled_mirror.h"
#include <cstring>

namespace synthux {

namespace {

// F0 7D 54 50 <ver> <type> <payload…> F7 — mfr 0x7D (educational), "TP"
// Type 0x06 is the live link's knobs frame, 0x7E its request (page → board).
constexpr uint8_t kFrameState = 0x01;
constexpr uint8_t kFrameFx    = 0x04;
constexpr uint8_t kFrameKit   = 0x05;

constexpr uint32_t kStateMinIntervalMs = 33;   // ~30 Hz on change
constexpr uint32_t kHeartbeatMs        = 500;
constexpr uint32_t kFxMinIntervalMs    = 100;
constexpr uint32_t kKitMinIntervalMs   = 100;
constexpr uint32_t kKitHeartbeatMs     = 2000; // big frame, slow-moving data

// Live link
constexpr uint8_t  kFrameKnobs  = 0x06;
constexpr uint8_t  kLinkRequest[5] = { 0x7D, 0x54, 0x50, 0x01, 0x7E };
constexpr uint32_t kLinkMs      = 10000;  // frames stop this long after a request
constexpr uint32_t kLinkBeatMs  = 500;    // Live and knobs sent again, changed or not
constexpr uint32_t kLinkFrameMs = 33;     // fastest rate per frame type
constexpr uint32_t kLinkOledMs  = 8;      // per half page: a full screen in ~64 ms

bool StateChanged(const TelemetryState& a, const TelemetryState& b) {
    if (a.pads != b.pads || a.sw1 != b.sw1 || a.sw2 != b.sw2) return true;
    if (a.led != b.led || a.model != b.model || a.mode != b.mode) return true;
    if (a.playing != b.playing || a.snd_edit != b.snd_edit) return true;
    if (a.seq_step != b.seq_step) return true;
    if (a.octave != b.octave || a.root != b.root) return true;
    if (a.rec_slot != b.rec_slot) return true;
    if (a.rec_layers != b.rec_layers || a.rec_mute != b.rec_mute) return true;
    if (a.clock_src != b.clock_src || a.arp_flags != b.arp_flags) return true;
    if (a.arp_pool != b.arp_pool) return true;
    if (a.hold_kind != b.hold_kind || a.hold_progress != b.hold_progress) return true;
    if (a.hold_stage != b.hold_stage || a.hold_outcome != b.hold_outcome) return true;
    if (a.seq_pattern != b.seq_pattern) return true;
    if (a.sw1_latch != b.sw1_latch) return true;
    if (a.kick_preset != b.kick_preset) return true;
    if (a.pickup_armed != b.pickup_armed) return true;
    for (int i = 0; i < 8; i++)
        if (a.controls[i] != b.controls[i]) return true;
    // Targets only matter where a pot is actually armed — an unarmed slot's
    // stale target must not keep the frame rate pinned at the 33 ms floor.
    for (int i = 0; i < 8; i++)
        if ((a.pickup_armed >> i) & 1)
            if (a.pickup_target[i] != b.pickup_target[i]) return true;
    return false;
}

bool FxChanged(const TelemetryState& a, const TelemetryState& b) {
    return a.fx_drive != b.fx_drive || a.fx_reverb != b.fx_reverb
        || a.fx_delay != b.fx_delay;
}

bool KitChanged(const TelemetryState& a, const TelemetryState& b) {
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 6; j++)
            if (a.kit[i][j] != b.kit[i][j]) return true;
    return false;
}

} // namespace

void Telemetry::Service(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    const bool first = !started_;
    started_ = true;
    bool sent = false;

    const uint32_t state_age = now_ms - state_ms_;
    if (first || state_age >= kHeartbeatMs
        || (StateChanged(s, last_state_) && state_age >= kStateMinIntervalMs)) {
        SendState(s, now_ms, midi);
        sent = true;
    }

    const uint32_t fx_age = now_ms - fx_ms_;
    if (first || (FxChanged(s, last_fx_) && fx_age >= kFxMinIntervalMs)) {
        SendFx(s, now_ms, midi);
        sent = true;
    }

    const uint32_t kit_age = now_ms - kit_ms_;
    if (first || kit_age >= kKitHeartbeatMs
        || (KitChanged(s, last_kit_) && kit_age >= kKitMinIntervalMs)) {
        SendKit(s, now_ms, midi);
        sent = true;
    }

    ServiceLink(s, now_ms, midi, sent);
}

bool Telemetry::OnSysex(const uint8_t* data, uint8_t len) {
    if (len != sizeof(kLinkRequest)
        || std::memcmp(data, kLinkRequest, sizeof(kLinkRequest)) != 0)
        return false;
    link_asked_ = true;
    return true;
}

void Telemetry::ServiceLink(const TelemetryState& s, uint32_t now_ms,
                            MidiIO& midi, bool sent) {
    if (link_asked_) {
        link_asked_ = false;
        link_on_    = true;
        link_ms_    = beat_ms_ = now_ms;
        live_valid_ = knobs_valid_ = false;
        OledMirror::All();
    }
    if (!link_on_) return;
    if (now_ms - link_ms_ >= kLinkMs) {
        link_on_ = false;
        return;
    }
    if (now_ms - beat_ms_ >= kLinkBeatMs) {
        beat_ms_    = now_ms;
        live_valid_ = knobs_valid_ = false;
    }
    if (sent) return;

    // s.sw1 is 0 left, 1 centre, 2 right. The Live frame counts S09_S10 from
    // the right. s.sw2 already matches S07_S08: 0 up, 1 centre, 2 down.
    const uint8_t live[4] = {
        static_cast<uint8_t>(s.sw1 <= 2 ? 2 - s.sw1 : 1),
        static_cast<uint8_t>(s.sw2 <= 2 ? s.sw2 : 1),
        static_cast<uint8_t>(s.pads & 0x7F),
        static_cast<uint8_t>((s.pads >> 7) & 0x1F),
    };
    if ((!live_valid_ || std::memcmp(live, sent_live_, sizeof(live)) != 0)
        && (!live_valid_ || now_ms - live_ms_ >= kLinkFrameMs)) {
        uint8_t f[10] = { 0xF0, 0x7D, 0x53, 0x54, 0x01 };
        std::memcpy(f + 5, live, sizeof(live));
        f[9] = 0xF7;
        midi.SendSysexUsb(f, sizeof(f));
        std::memcpy(sent_live_, live, sizeof(live));
        live_valid_ = true;
        live_ms_    = now_ms;
        return;
    }

    if ((!knobs_valid_ || std::memcmp(s.controls, sent_knobs_, 8) != 0)
        && (!knobs_valid_ || now_ms - knobs_ms_ >= kLinkFrameMs)) {
        uint8_t f[15] = { 0xF0, 0x7D, 0x54, 0x50, 0x01, kFrameKnobs };
        for (int i = 0; i < 8; i++) f[6 + i] = s.controls[i] & 0x7F;
        f[14] = 0xF7;
        midi.SendSysexUsb(f, sizeof(f));
        std::memcpy(sent_knobs_, s.controls, 8);
        knobs_valid_ = true;
        knobs_ms_    = now_ms;
        return;
    }

    if (now_ms - oled_ms_ >= kLinkOledMs) {
        uint8_t   f[OledMirror::kFrameBytes];
        const int n = OledMirror::Frame(f);
        if (n > 0) {
            midi.SendSysexUsb(f, static_cast<size_t>(n));
            oled_ms_ = now_ms;
        }
    }
}

void Telemetry::SendState(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    uint8_t f[47];
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x54; f[3] = 0x50; f[4] = 0x01;
    f[5]  = kFrameState;
    f[6]  = static_cast<uint8_t>(s.pads & 0x7F);         // P0..P6
    f[7]  = static_cast<uint8_t>((s.pads >> 7) & 0x1F);  // P7..P11
    for (int i = 0; i < 8; i++) f[8 + i] = s.controls[i] & 0x7F;
    f[16] = s.sw1;
    f[17] = s.sw2;
    f[18] = s.led & 0x7F;
    f[19] = s.model & 0x7F;
    f[20] = static_cast<uint8_t>((s.mode & 0x03) | (s.playing ? 0x04 : 0x00)
                                 | (s.snd_edit ? 0x08 : 0x00));
    f[21] = s.seq_step & 0x7F;
    f[22] = s.octave & 0x7F;
    f[23] = s.root & 0x7F;
    f[24] = s.rec_slot & 0x7F;
    f[25] = s.rec_layers & 0x7F;   // NoteRec committed layer count, 0..5
    f[26] = s.rec_mute & 0x7F;     // bit i = NoteRec layer i muted
    f[27] = s.clock_src & 0x7F;    // 0 internal, 1 MIDI, 2 CV
    f[28] = s.arp_flags & 0x7F;    // bits0-1 sub-state, bit2 Rec armed, bit3 melodic transport
    f[29] = s.hold_kind & 0x7F;    // 0 none, 1 P0+P2, 2 rec entry, 3 layer clear,
                                   // 4 layer copy, 5 rec save, 6 rec cancel,
                                   // 7 P0+P1 sound edit
    f[30] = s.hold_progress & 0x7F;
    f[31] = s.hold_stage & 0x7F;   // confirms fired so far for the current hold
    f[32] = s.hold_outcome & 0x7F; // kind 3: 1 cleared, 2 empty · kind 7: 1 entered, 2 left
    f[33] = s.seq_pattern & 0x7F;  // playing slot within the genre, 0-based
    f[34] = s.pickup_armed & 0x7F; // bit i = S3(0+i) waiting for pickup
    for (int i = 0; i < 8; i++) f[35 + i] = s.pickup_target[i] & 0x7F;
    f[43] = s.sw1_latch & 0x7F;    // bits0-1 latched genre, bits2-3 latched scale
    f[44] = s.arp_pool & 0x7F;     // bit i = pad P(3+i) in the arp's pool
    f[45] = s.kick_preset & 0x7F;  // 1-based kick lab preset, 0 = none (fw v12)
    f[46] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
    last_state_ = s;
    state_ms_   = now_ms;
}

void Telemetry::SendFx(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    // payload: drive, reverb, delay, nTrims (per-slot trims reserved for v2)
    uint8_t f[11];
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x54; f[3] = 0x50; f[4] = 0x01;
    f[5]  = kFrameFx;
    f[6]  = s.fx_drive & 0x7F;
    f[7]  = s.fx_reverb & 0x7F;
    f[8]  = s.fx_delay & 0x7F;
    f[9]  = 0;
    f[10] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
    last_fx_ = s;
    fx_ms_   = now_ms;
}

void Telemetry::SendKit(const TelemetryState& s, uint32_t now_ms, MidiIO& midi) {
    // payload: nSlots, then per slot: engine, harmonics, timbre, morph,
    // decay, MIDI note (all 7-bit)
    uint8_t f[7 + 7 * 6 + 1];
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x54; f[3] = 0x50; f[4] = 0x01;
    f[5] = kFrameKit;
    f[6] = 7;
    int w = 7;
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 6; j++)
            f[w++] = s.kit[i][j] & 0x7F;
    f[w] = 0xF7;
    midi.SendSysexUsb(f, sizeof(f));
    last_kit_ = s;
    kit_ms_   = now_ms;
}

} // namespace synthux
