// Port of visualizer/src/core/controls-meta.ts + visualizer/src/panel/
// labels.ts's describeControl()/describePad()/switch & idle text, operating
// directly on TelemetryState's wire fields (controls[] 0..127, sw1/sw2
// panel-mapped positions, etc.) instead of the visualizer's 0..1-normalized
// DeviceState — same values, just not re-scaled, since nothing here needs
// float precision beyond what the knob already has.
//
// Font_6x8 only accepts ASCII 32-126 (OneBitGraphicsDisplayImpl::WriteChar
// rejects anything else and WriteString aborts the whole string on the
// first rejected char) so every string here is plain ASCII — the web
// version's "·", "→", "↔", "−" become "-", "->", "<>", "-".

#include "oled_ui.h"
#include <cstring>
#include <algorithm>

using namespace synthux;
using namespace daisy;

namespace {

// ── Static tables (visualizer/src/core/controls-meta.ts) ───────────────────

struct ControlMeta {
    const char* name;
    const char* main;
    const char* seq;
    const char* arp;
    const char* fx; // nullptr = not an FX-layer knob (only S30, S35 are)
};

// Index = S30..S37 (TelemetryState::controls[] order).
const ControlMeta kControls[8] = {
    { "S30", "Drive",     "Drive",     "Drive",      "Reverb" },
    { "S31", "Decay",     "Tempo",     "Decay",      nullptr  },
    { "S32", "Harmonics", "Swing",     "Division",   nullptr  },
    { "S33", "Timbre",    "Density",   "Swing",      nullptr  },
    { "S34", "Morph",     "Punch",     "Density",    nullptr  },
    { "S35", "Model sel", "Pattern",   "Order",      "Delay"  },
    { "S36", "Volume",    "Volume",    "Volume",     nullptr  },
    { "S37", "Blend",     "Tightness", "Blend",      nullptr  },
};

struct PadMeta {
    const char* name;
    const char* seq_role; // nullptr if none
};

// Index = P0..P11.
const PadMeta kPads[12] = {
    { "P0",  nullptr },
    { "P1",  nullptr },
    { "P2",  nullptr },
    { "P3",  "Kick" },
    { "P4",  "Snare" },
    { "P5",  "Closed hat" },
    { "P6",  "Open hat" },
    { "P7",  "Clap" },
    { "P8",  "Tom" },
    { "P9",  "Perc" },
    { "P10", nullptr },
    { "P11", nullptr },
};

const char* const kModels[24] = {
    "Virtual analog VCF", "Phase distortion", "Six-Op A", "Six-Op B",
    "Six-Op C", "Wave terrain", "String machine", "Chiptune",
    "Virtual analog", "Waveshaping", "FM 2-op", "Grain", "Additive",
    "Wavetable", "Chord", "Speech", "Swarm", "Noise", "Particle",
    "String", "Modal", "Bass drum", "Snare drum", "Hi-hat",
};

struct EngineKnobs {
    bool        present;
    const char* harmonics;
    const char* timbre;
    const char* morph; // nullptr = unassigned (dead) on this engine
    const char* decay;
    const char* aux;   // nullptr = blend/width unassigned on this engine
};

const EngineKnobs kSixOp = {
    true, "Patch select", "Mod level", nullptr, "Envelope time", nullptr,
};

// Index = engine 0..23. Index 7 (Chiptune) is intentionally absent
// (present=false) — mirrors ENGINE_KNOBS in controls-meta.ts, which has no
// entry for it either; a knob queried against it falls back to the generic
// per-mode label.
const EngineKnobs kEngineKnobs[24] = {
    /*0*/  { true, "Resonance / character", "Filter cutoff", "Waveform & sub", "Decay", "Highpass" },
    /*1*/  { true, "Distortion freq", "Distortion amount", "Asymmetry", "Decay", "Free-running" },
    /*2*/  kSixOp,
    /*3*/  kSixOp,
    /*4*/  kSixOp,
    /*5*/  { true, "Terrain", "Path radius", "Path offset", "Decay", "Alt path" },
    /*6*/  { true, "Chord", "Filter / chorus", "Waveform", "Decay", "Filtered mix" },
    /*7*/  { false, nullptr, nullptr, nullptr, nullptr, nullptr },
    /*8*/  { true, "Detune", "Square shape", "Saw shape", "Decay", "Synced variant" },
    /*9*/  { true, "Waveshaper", "Fold amount", "Asymmetry", "Decay", "Sine fold" },
    /*10*/ { true, "Freq ratio", "Mod index", "Feedback", "Decay", "Sub oscillator" },
    /*11*/ { true, "Freq ratio", "Formant freq", "Formant shape", "Decay", "Alt formant" },
    /*12*/ { true, "Spectrum bumps", "Main harmonic", "Bump width", "Decay", "High harmonics" },
    /*13*/ { true, "Bank", "Row", "Column", "Decay", "Lo-fi" },
    /*14*/ { true, "Chord type", "Inversion", "Waveform", "Decay", "Chord subset" },
    /*15*/ { true, "Synth mode", "Species", "Phoneme / word", "Decay", "Unfiltered" },
    /*16*/ { true, "Detune spread", "Grain rate", "Grain duration", "Decay", "Sine grains" },
    /*17*/ { true, "Filter response", "Clock freq", "Resonance", "Decay", "Bandpass" },
    /*18*/ { true, "Freq spread", "Density", "Filter / diffusion", "Decay", "Raw particles" },
    /*19*/ { true, "Inharmonicity", "Excitation brightness", nullptr, "Damping", "Raw exciter" },
    /*20*/ { true, "Material", "Excitation brightness", nullptr, "Damping", "Raw exciter" },
    /*21*/ { true, "Attack / overdrive", "Brightness", nullptr, "Tail", "Alt model" },
    /*22*/ { true, "Tone-noise mix", "Brightness", nullptr, "Tail", "Alt model" },
    /*23*/ { true, "Noise colour", "HPF cutoff", nullptr, "Tail", "Alt model" },
};

const char* const kChordNames[11] = {
    "Oct", "5th", "sus4", "m", "m7", "m9", "m11", "69", "M9", "M7", "M",
};

const char* const kArpOrders[5] = { "Played", "Up", "Down", "Ping-pong", "Random" };

const char* const kSw1Seq[3]   = { "IDM", "Techno", "Electro" };
const char* const kSw1Pitch[3] = { "Minor", "Chromatic", "Major" };
const char* const kSw1Arp[3]   = { "Hold", "Arp", "Rec" };  // panel position order
const char* const kSw2[3]      = { "Seq", "Arp/Mel", "Pitch" };

const char* const kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

// Same scale tables as controls-meta.ts's SCALES, indexed by the
// panel-mapped SW1 position (t.sw1) — an approximation of the actual
// pitched note (the firmware change-latches the scale; this uses the live
// switch), same deliberate simplification the visualizer makes.
const int kUiScales[3][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },  // Minor
    { 0, 1, 2, 3, 4, 5, 6  },  // Chromatic
    { 0, 2, 4, 5, 7, 9, 11 },  // Major
};
constexpr int kUiPitchBase = 60;

// ── Small formatters ────────────────────────────────────────────────────────

const char* model_name(int n) {
    return (n >= 0 && n < 24) ? kModels[n] : "Model ?";
}

template <size_t N>
void model_value(FixedCapStr<N>& out, int model) {
    out.Clear();
    out.Append(model_name(model));
    out.Append(" #");
    out.AppendInt(model);
}

template <size_t N>
void pct_value(FixedCapStr<N>& out, float v01) {
    out.Clear();
    out.AppendInt(static_cast<int>(v01 * 100.f + 0.5f));
    out.Append("%");
}

// formatKnobValue() port: quantized selectors show the selected item
// instead of a %. param matches controls[] index minus one (i-1 for
// i=1..4, i.e. S31..S34): 0=decay 1=harmonics 2=timbre 3=morph — only
// harmonics (param==1) is ever quantized (Six-Op patch / chord name).
template <size_t N>
void format_knob_value(FixedCapStr<N>& out, int engine, int param, float v01) {
    if (param == 1) {
        if (engine >= 2 && engine <= 4) {
            int idx = std::min(31, static_cast<int>(v01 * 1.02f * 32.f));
            out.Clear();
            out.Append("Patch ");
            out.AppendInt(idx + 1);
            out.Append("/32");
            return;
        }
        if (engine == 6 || engine == 14) {
            int idx = std::min<int>(10, static_cast<int>(v01 * 1.02f * 11.f));
            out.Clear();
            out.Append(kChordNames[idx]);
            return;
        }
    }
    pct_value(out, v01);
}

const char* arp_order_name(float v01) {
    int idx = static_cast<int>(v01 * 5.f);
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;
    return kArpOrders[idx];
}

// fxValueLabel() port: center-off mirror-knob decode, dead zone 0.06 either
// side of center — mirrors fx_decode() in TouchPlaited.cpp exactly.
template <size_t N>
void fx_value_label(FixedCapStr<N>& out, bool is_reverb, float v01) {
    constexpr float kDeadZone = 0.06f;
    const float half = 0.5f - kDeadZone;
    const char* a = is_reverb ? "Room" : "Slapback";
    const char* b = is_reverb ? "Hall" : "Dotted 1/8";
    out.Clear();
    if (v01 < half) {
        int pct = static_cast<int>(((half - v01) / half) * 100.f + 0.5f);
        out.Append(a); out.Append(" "); out.AppendInt(pct); out.Append("%");
    } else if (v01 > 1.f - half) {
        int pct = static_cast<int>(((v01 - (1.f - half)) / half) * 100.f + 0.5f);
        out.Append(b); out.Append(" "); out.AppendInt(pct); out.Append("%");
    } else {
        out.Append("Off");
    }
}

template <size_t N>
void note_name(FixedCapStr<N>& out, int n) {
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    out.Clear();
    out.Append(kNoteNames[n % 12]);
    out.AppendInt(n / 12 - 1);
}

// blendInfo() port: what S37/Blend targets on a given engine — aux==nullptr
// means the engine's AUX output equals OUT, so blend/width does nothing.
template <size_t N>
bool blend_info(FixedCapStr<N>& fn, int engine, bool slot_prefix) {
    fn.Clear();
    fn.Append(slot_prefix ? "Slot blend" : "Blend");
    const bool valid = engine >= 0 && engine < 24 && kEngineKnobs[engine].present;
    const char* aux = valid ? kEngineKnobs[engine].aux : nullptr;
    if (aux) {
        fn.Append(" OUT<>");
        fn.Append(aux);
        return false; // not dead
    }
    return true; // dead
}

// engineKnobLabel() port. i: 1=S31(decay) 2=S32(harmonics) 3=S33(timbre)
// 4=S34(morph). Returns false when the control isn't one of these four or
// the engine has no table entry (falls back to the generic per-mode label).
template <size_t N>
bool engine_knob_label(FixedCapStr<N>& fn, int i, int engine, bool* dead) {
    if (engine < 0 || engine >= 24 || !kEngineKnobs[engine].present) return false;
    const EngineKnobs& ek = kEngineKnobs[engine];
    auto pair = [&](const char* generic, const char* specific) {
        fn.Clear();
        fn.Append(generic);
        if (std::strcmp(generic, specific) != 0) {
            fn.Append(" - ");
            fn.Append(specific);
        }
    };
    switch (i) {
        case 1: *dead = false; pair("Decay", ek.decay); return true;
        case 2: *dead = false; pair("Harmonics", ek.harmonics); return true;
        case 3: *dead = false; pair("Timbre", ek.timbre); return true;
        case 4:
            if (ek.morph == nullptr) { *dead = true; fn.Clear(); fn.Append("Morph"); return true; }
            *dead = false; pair("Morph", ek.morph); return true;
        default: return false;
    }
}

template <size_t N>
void set_label(FixedCapStr<N>& label, const char* combo, const char* fn) {
    label.Clear();
    label.Append(combo);
    label.Append(" ");
    label.Append(fn);
}

template <size_t N, size_t M>
void set_label(FixedCapStr<N>& label, const char* combo, const FixedCapStr<M>& fn) {
    label.Clear();
    label.Append(combo);
    label.Append(" ");
    label.Append(fn.Cstr());
}

// ── describeControl() port ──────────────────────────────────────────────────
// Same priority order as visualizer/src/panel/labels.ts's describeControl()
// + the value chain from apply()'s 'control' case — see oled_ui.h for the
// two documented simplifications (flatter Rec drum-slot labels, shortened
// dead-knob message).
void describe_control(int i, const TelemetryState& t,
                       FixedCapStr<24>& label, FixedCapStr<24>& value) {
    const ControlMeta& meta = kControls[i];
    const bool p0 = (t.pads & (1u << 0)) != 0;
    const bool p1 = (t.pads & (1u << 1)) != 0;
    const bool p2 = (t.pads & (1u << 2)) != 0;
    const int  mode = t.mode; // 0 Seq, 1 Arp/Mel, 2 Pitch
    const bool snd_edit = t.snd_edit;
    const int  arp_sub = t.arp_flags & 0x03; // 0 Arp, 1 Hold, 2 Rec
    const float v = t.controls[i] / 127.f;

    const bool rec_active  = (mode == 0) && (t.rec_slot != 0x7F);
    const int  rec_engine  = (rec_active && t.rec_slot < 7) ? t.kit[t.rec_slot][0] : -1;

    bool dead         = false;
    int  value_engine = -1; // set only when i is 1..4 and an engine table applies

    bool handled = false;

    if (rec_active) {
        if (p1 && meta.fx) {
            set_label(label, (i == 0) ? "P1+S30" : "P1+S35",
                      (i == 0) ? "Slot reverb send" : "Slot delay send");
            handled = true;
        } else if (i == 5 && p0) {
            set_label(label, "P0+S35", "Slot model bank0");
            handled = true;
        } else if (i == 5 && p2) {
            set_label(label, "P2+S35", "Slot model bank1");
            handled = true;
        } else if (i == 7 && p0) {
            set_label(label, "P0+S37", "Slot width");
            handled = true;
        } else if (i == 0) {
            set_label(label, "S30", "Slot drive");
            handled = true;
        } else if (i == 5) {
            set_label(label, "S35", "Slot model select");
            handled = true;
        } else if (i == 6) {
            set_label(label, "S36", "Slot volume");
            handled = true;
        } else if (i == 7) {
            FixedCapStr<24> fn;
            dead = blend_info(fn, rec_engine, true);
            set_label(label, "S37", fn);
            handled = true;
        } else {
            FixedCapStr<24> fn;
            if (engine_knob_label(fn, i, rec_engine, &dead)) {
                set_label(label, meta.name, fn);
                value_engine = rec_engine;
                handled = true;
            }
        }
    }

    if (!handled && p1 && meta.fx) {
        FixedCapStr<8> combo;
        combo.Append("P1+");
        combo.Append(meta.name);
        FixedCapStr<24> fn;
        fn.Append(meta.fx);
        fn.Append(mode == 0 ? " drums" : " pitched");
        set_label(label, combo.Cstr(), fn);
        fx_value_label(value, i == 0, v);
        return; // value fully resolved here, matches the web app's early break
    }
    if (!handled && i == 5 && p0) {
        set_label(label, "P0+S35", "Model select bank0");
        handled = true;
    }
    if (!handled && i == 5 && p2) {
        set_label(label, "P2+S35", "Model select bank1");
        handled = true;
    }
    if (!handled && i == 7 && mode != 0) {
        FixedCapStr<24> fn;
        bool bdead = blend_info(fn, t.model, false);
        if (p0) { set_label(label, "P0+S37", "Stereo width"); dead = bdead; }
        else    { set_label(label, "S37", fn); dead = bdead; }
        handled = true;
    }
    if (!handled && i == 7 && p0) {
        set_label(label, "P0+S37", "Stereo width");
        handled = true;
    }
    if (!handled && mode == 1 && !snd_edit && arp_sub == 2) {
        if (i == 0)      { set_label(label, "S30", "Drive (Rec)"); handled = true; }
        else if (i == 2) { set_label(label, "S32", "Speed 1x-8x"); handled = true; }
        else if (i == 3) { set_label(label, "S33", "Shift in time"); handled = true; }
        else if (i == 4) { set_label(label, "S34", "Chance"); handled = true; }
        else if (i == 5) { set_label(label, "S35", "Order"); handled = true; }
    }
    if (!handled && mode == 0 && !rec_active && i == 1 && t.clock_src != 0) {
        set_label(label, "S31", (t.clock_src == 1) ? "Tempo ext MIDI" : "Tempo ext CV");
        value.Clear();
        value.Append("ext");
        return; // knob's muted under an external clock — % of its position is meaningless
    }
    if (!handled && mode == 1 && !snd_edit && i == 1) {
        FixedCapStr<24> fn;
        if (engine_knob_label(fn, 1, t.model, &dead)) {
            set_label(label, meta.name, fn);
            value_engine = t.model;
            handled = true;
        }
    }
    if (!handled && (mode == 2 || (mode == 1 && snd_edit))) {
        FixedCapStr<24> fn;
        if (engine_knob_label(fn, i, t.model, &dead)) {
            set_label(label, meta.name, fn);
            value_engine = t.model;
            handled = true;
        } else if (mode == 1) {
            if (i == 0)      { set_label(label, "S30", "Drive (sound edit)"); handled = true; }
            else if (i == 5) { set_label(label, "S35", "Order (frozen)"); handled = true; }
        }
    }
    if (!handled) {
        const char* fn = (mode == 0) ? meta.seq : (mode == 1) ? meta.arp : meta.main;
        if (!fn) fn = meta.main;
        set_label(label, meta.name, fn);
    }

    // ── value chain ──────────────────────────────────────────────────────
    if (dead) {
        value.Clear();
        value.Append("no effect");
        return;
    }
    if (value_engine != -1 && i >= 1 && i <= 4) {
        format_knob_value(value, value_engine, i - 1, v);
        return;
    }
    // Seq tempo: a raw % of the knob position is meaningless on its own —
    // show the actual BPM instead (the ext-clock/muted case already
    // returned above with value="ext"). Matches the BPM formula the web
    // app's knob-map panel uses (visualizer/src/panel/labels.ts
    // renderModel()): 60-180 BPM over the knob's full travel.
    if (mode == 0 && !rec_active && i == 1) {
        value.Clear();
        value.AppendInt(static_cast<int>(60.f + v * 120.f + 0.5f));
        value.Append(" BPM");
        return;
    }
    if (std::strcmp(meta.name, "S35") == 0) {
        if (mode == 1 && arp_sub == 2 && !snd_edit && !p0 && !p2) {
            value.Clear();
            value.Append(v < 0.5f ? "as recorded" : "shuffled");
            return;
        }
        if (mode == 1 && !p0 && !p2) {
            value.Clear();
            value.Append(arp_order_name(v));
            return;
        }
        if (mode != 0) {
            model_value(value, t.model);
            return;
        }
    }
    pct_value(value, v);
}

// ── describePad() port ──────────────────────────────────────────────────────
void describe_pad(int i, const TelemetryState& t,
                   FixedCapStr<24>& label, FixedCapStr<24>& value) {
    label.Clear();
    value.Clear();
    if (i >= 3 && i <= 9) {
        const PadMeta& pm = kPads[i];
        label.Append(pm.name);
        label.Append(" ");
        if (t.mode == 0 && pm.seq_role) label.Append(pm.seq_role);
        else                            label.Append("Play note");
        if (t.mode != 0) {
            int sw_a = t.sw1;
            if (sw_a < 0 || sw_a > 2) sw_a = 1;
            int degree = i - 3;
            int oct    = static_cast<int>(t.octave) - 3;
            int note   = kUiPitchBase + t.root + kUiScales[sw_a][degree] + oct * 12;
            note_name(value, note);
        }
        return;
    }
    switch (i) {
        case 0:  label.Append("P0 modifier"); return;
        case 1:  label.Append("P1 FX layer"); return;
        case 2:  label.Append("P2 modifier"); return;
        case 10: label.Append("P10 Oct-/pitch-1"); return;
        case 11: label.Append("P11 Oct+/pitch+1"); return;
        default: return;
    }
}

} // namespace

// ── OledUi ────────────────────────────────────────────────────────────────

namespace synthux {

namespace {
// Bus transfer cost bounds how often we can afford to redraw — a full
// Update() is now ~16 I2C transactions (batched per page, see the SendData
// fix in lib/libDaisy/src/dev/oled_ssd130x.h) instead of ~524, roughly
// 30-40ms down to an estimated well under 15ms. 80ms leaves a healthy
// margin over that (touch polling only pauses for the actual transfer —
// see i2c1_lock.h — so the ratio here is how much of the time it's
// paused) while still reading as prompt on a knob sweep. Tune down further
// if it still feels laggy once measured on real hardware.
constexpr uint32_t kMinRedrawIntervalMs = 80;
} // namespace

void OledUi::Service(const TelemetryState& t, uint32_t now_ms, OledScreen& oled) {
    if (!has_last_) {
        last_     = t;
        has_last_ = true;
    }

    // `last_` intentionally stays at its last-drawn value while throttled —
    // it's the baseline the next allowed draw diffs against, so a pad still
    // held (or a knob settled at a new value) when the window clears still
    // reads as "new" instead of looking already-seen. A full press+release
    // that completes entirely inside the window is invisible either way —
    // an unavoidable consequence of sampling slower than the input, not
    // something eagerly updating `last_` here would fix.
    if (now_ms < next_draw_ms_) return;

    FixedCapStr<24> label;
    FixedCapStr<24> value;
    bool draw = false;

    // Priority mirrors labels.ts: an explicit pad-down is the most specific
    // signal, then a switch flip, then a model change, then a knob move.
    // No idle fallback — the screen just holds whatever was last touched
    // (see the class comment in oled_ui.h for why).
    const uint16_t new_touches = t.pads & ~last_.pads;
    if (new_touches != 0) {
        int i = 0;
        while (i < 12 && !((new_touches >> i) & 1u)) i++;
        describe_pad(i, t, label, value);
        draw = true;
    } else if (t.sw1 != last_.sw1) {
        const char* const* table = (t.mode == 0) ? kSw1Seq : (t.mode == 1) ? kSw1Arp : kSw1Pitch;
        set_label(label, "SW1", (t.sw1 >= 0 && t.sw1 <= 2) ? table[t.sw1] : "?");
        draw = true;
    } else if (t.sw2 != last_.sw2) {
        set_label(label, "SW2", (t.sw2 >= 0 && t.sw2 <= 2) ? kSw2[t.sw2] : "?");
        draw = true;
    } else if (t.model != last_.model) {
        label.Clear();
        label.Append("Model");
        model_value(value, t.model);
        draw = true;
    } else {
        // First changed control wins; if two moved in the same throttle
        // window the other one's change gets folded into `last_` unseen
        // once we redraw below (rare — simultaneous two-knob moves — and
        // the next real move on that knob still shows normally).
        for (int i = 0; i < 8; i++) {
            if (t.controls[i] != last_.controls[i]) {
                describe_control(i, t, label, value);
                draw = true;
                break;
            }
        }
    }

    if (draw) {
        oled.ShowLine(label.Cstr(), value.Cstr());
        next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
    }

    last_ = t;
}

} // namespace synthux
