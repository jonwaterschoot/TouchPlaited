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
#include "../synth/patterns_gen.h"
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
    { "S33", "Timbre",    "Pattern density", "Swing", nullptr  },
    { "S34", "Morph",     "Step chance",     "Density", nullptr },
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

// Seq S35 (Pattern): name of the selected pattern + its position within the
// current genre, e.g. "fourfloor 1/6" — mirrors Sequencer::pattern_index()'s
// quantization (synth/sequencer.h) so the displayed slot always matches what
// actually plays.
template <size_t N>
void pattern_value(FixedCapStr<N>& out, int genre, float v01) {
    if (genre < 0 || genre >= kNumGenres) genre = 0;
    int n  = kGenrePatternCount[genre];
    int vi = static_cast<int>(v01 * static_cast<float>(n));
    if (vi >= n) vi = n - 1;
    int idx = kGenrePatternIdx[genre][vi];
    out.Clear();
    out.Append(kSeqPatternNames[idx]);
    out.Append(" ");
    out.AppendInt(vi + 1);
    out.Append("/");
    out.AppendInt(n);
}

// Seq S33 (Density): which weight layers survive at each stage. Named after
// the layers themselves rather than a mood word ("strong"/"ghosts" said how
// it sounds, not what the knob did). All four are <= 11 chars so the value
// row keeps one font across the whole sweep instead of resizing mid-turn.
// Same as pattern_value(), but from an already-resolved slot index (the
// sequencer's own `VariantSlot`, published as t.seq_pattern) rather than a
// knob position — what the status row needs, since S35's pot is behind a
// pickup and may not be where the playing pattern is.
template <size_t N>
void pattern_slot_value(FixedCapStr<N>& out, int genre, int slot) {
    if (genre < 0 || genre >= kNumGenres) genre = 0;
    const int n = kGenrePatternCount[genre];
    if (slot < 0 || slot >= n) slot = 0;
    out.Clear();
    out.Append(kSeqPatternNames[kGenrePatternIdx[genre][slot]]);
    out.Append(" ");
    out.AppendInt(slot + 1);
    out.Append("/");
    out.AppendInt(n);
}

const char* const kDensityWords[4] = {
    "layer 4", "layers 3-4", "layers 2-4", "layers 1-4",
};

// Mirrors Sequencer::SetDensity's quantization (synth/sequencer.h) exactly,
// so the displayed stage always matches what's actually playing.
template <size_t N>
void density_value(FixedCapStr<N>& out, float v01) {
    int d = 1 + static_cast<int>(v01 * 3.f + 0.5f);
    if (d < 1) d = 1;
    if (d > 4) d = 4;
    out.Clear();
    out.Append(kDensityWords[d - 1]);
}

// Seq S34 (Chance): a three-zone control, not a percentage. Printing the raw
// knob % read as "100% = always plays" when full right is in fact the
// *sparsest* setting — so name the zone instead. Mirrors eval_step()'s curve
// in synth/sequencer.h (miss rate x1 at centre, up to kChanceExtraMax = 3 at
// full right); all outputs are <= 11 chars, one font across the sweep.
template <size_t N>
void chance_value(FixedCapStr<N>& out, float v01) {
    constexpr float kDead = 0.02f;
    out.Clear();
    if (v01 <= kDead)         { out.Append("always fire"); return; }
    if (v01 <= 0.5f + kDead && v01 >= 0.5f - kDead) { out.Append("as authored"); return; }
    if (v01 < 0.5f) {
        // How far the authored miss rate has been pulled toward "always".
        out.Append("fuller ");
        out.AppendInt(static_cast<int>((0.5f - v01) * 200.f + 0.5f));
        out.Append("%");
        return;
    }
    // 1.0x .. 3.0x the authored miss rate, one decimal.
    const int tenths = static_cast<int>((1.f + (v01 - 0.5f) * 4.f) * 10.f + 0.5f);
    out.Append("sparse ");
    out.AppendInt(tenths / 10);
    out.Append(".");
    out.AppendInt(tenths % 10);
    out.Append("x");
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
    // Seq density / chance: name the stage or the zone instead of a raw %
    // (see density_value() / chance_value()).
    if (mode == 0 && !rec_active && i == 3) {
        density_value(value, v);
        return;
    }
    if (mode == 0 && !rec_active && i == 4) {
        chance_value(value, v);
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
        if (!p0 && !p2 && !rec_active) {
            pattern_value(value, t.sw1, v);
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

// The state P2+P10 just landed in, named as one thing instead of as whichever
// single flag moved. The combo means different things per sub-state
// (transport everywhere, capture arm while SW1 is in Rec) and the two are
// independent — "Armed" on its own never said whether anything was even
// looping, and "Play" never said whether it was capturing. All <= 11 chars,
// so the value row keeps one font across every case.
const char* melodic_state(int mode, int arp_sub, bool running, bool armed) {
    if (mode != 1)    return running ? "Mel play" : "Mel stopped";
    if (arp_sub == 2) {
        if (!running) return "Rec stopped";
        return armed ? "Rec + play" : "Play no rec";
    }
    // Hold is the same arpeggiator with latched input, so it shares Arp's
    // wording — the label row is what distinguishes the two.
    return running ? "Arp play" : "Arp stopped";
}

// ── Idle status row ─────────────────────────────────────────────────────────
// The home screen: what shows when nothing has been touched for a while, and
// the thing a finished hold hands the screen back to. Deliberately built only
// from slow-moving fields — the row this replaces was dropped because it
// included seq_step, which changes every block and so never redrew in time to
// be right. Everything here changes at most a few times a second.
void status_row(const TelemetryState& t, FixedCapStr<24>& label, FixedCapStr<24>& value) {
    label.Clear();
    value.Clear();

    // Seq recording: which pad is being edited, and what's loaded in it.
    if (t.mode == 0 && t.rec_slot < 7) {
        const int slot = t.rec_slot;
        label.Append("Rec P");
        label.AppendInt(slot + 3);
        if (kPads[slot + 3].seq_role) {
            label.Append(" ");
            label.Append(kPads[slot + 3].seq_role);
        }
        value.Append(model_name(t.kit[slot][0]));
        return;
    }
    if (t.mode == 0) {
        // Transport rides the label row so the value row can name the
        // pattern that's playing — the more useful of the two at a glance,
        // and t.seq_pattern is the sequencer's own slot, not S35's pot
        // (which sits behind a pickup and can be parked anywhere).
        label.Append(t.playing ? "Seq " : "Seq stop ");
        label.Append((t.sw1 <= 2) ? kSw1Seq[t.sw1] : "?");
        if (t.clock_src != 0) label.Append(t.clock_src == 1 ? " ext" : " cv");
        pattern_slot_value(value, t.sw1, t.seq_pattern);
        return;
    }
    if (t.mode == 1) {
        const int  sub     = t.arp_flags & 0x03; // 0 Arp, 1 Hold, 2 Rec
        const bool armed   = (t.arp_flags & 0x04) != 0;
        const bool running = (t.arp_flags & 0x08) != 0;
        label.Append("Arp/Mel ");
        label.Append(sub == 1 ? "Hold" : sub == 2 ? "Rec" : "Arp");
        if (t.snd_edit) label.Append(" edit");
        // In Rec, which state you're in matters more than the model: armed
        // and running are independent (a punched-out loop keeps playing), and
        // nothing on the panel distinguished them before. In Arp/Hold the
        // model is the more useful thing — except while stopped, which is the
        // one state where the mode looks broken rather than quiet.
        if (sub == 2 || !running) {
            value.Append(melodic_state(t.mode, sub, running, armed));
            return;
        }
        value.Append(model_name(t.model));
        return;
    }
    label.Append("Pitch ");
    label.Append((t.sw1 <= 2) ? kSw1Pitch[t.sw1] : "?");
    value.Append(model_name(t.model));
}

// ── Hold text ───────────────────────────────────────────────────────────────

template <size_t N>
void hold_label(FixedCapStr<N>& out, uint8_t kind, int mode, uint8_t rec_slot) {
    switch (kind) {
        case 1: set_label(out, "P0+P2", (mode == 0) ? "Re-randomize" : "Vary sound"); return;
        case 2: set_label(out, "P3-P9", "Rec entry");    return;
        case 3: set_label(out, "P2+pad", "Clear layer"); return;
        case 4: set_label(out, "Rec", "Copy layer");     return;
        case 5:
            // Name the pad being held, as asked for on hardware — "hold P5
            // to save" is the whole instruction, and the slot is right there
            // in the telemetry.
            out.Clear();
            out.Append("Hold ");
            if (rec_slot < 7) { out.Append("P"); out.AppendInt(rec_slot + 3); }
            else              { out.Append("pad"); }
            out.Append(" to save");
            return;
        case 6: set_label(out, "Rec", "Exit");           return;
        default: out.Clear(); out.Append("Hold");        return;
    }
}

// What crossing the *next* threshold will do — the note row under the bar
// (OledScreen::ShowProgress). A bar on its own says something is coming
// without ever saying what, and P0+P2's stages differ per playmode (see the
// hold block in TouchPlaited.cpp): Seq varies the current kit then replaces
// it, the pitched modes vary the sound twice, and Basic Pitch alone has a 3rd
// stage that drops the snapshots. `done` is how many have fired already.
const char* hold_note(uint8_t kind, int mode, uint8_t done) {
    switch (kind) {
        case 1:
            if (mode == 0) return (done == 0) ? "1s vary kit" : "2s new kit";
            if (done == 0) return "1s vary sound";
            if (done == 1) return (mode == 2) ? "2s vary more" : "2s vary more";
            return "3s back to live knobs";
        case 2: return "2s enter rec mode";
        case 3: return "clear this layer";
        case 4: return "copy slot to pad";
        case 5: return "keep these edits";
        default: return "";
    }
}

// What flashes the instant a threshold fires. Names the change rather than
// the stage number it used to print — "Stage 2" told you a threshold was
// crossed but not which of the three per-mode meanings it had.
template <size_t N>
void confirm_text(FixedCapStr<N>& out, uint8_t kind, int mode, uint8_t stage, uint8_t outcome) {
    out.Clear();
    switch (kind) {
        case 1:
            if (mode == 0)          out.Append(stage >= 2 ? "New kit" : "Kit varied");
            else if (stage >= 3)    out.Append("Live knobs");
            else                    out.Append(stage >= 2 ? "Varied more" : "Varied");
            return;
        case 2: out.Append("Recording"); return;
        case 3: out.Append(outcome == 2 ? "Empty" : "Cleared"); return;
        case 4: out.Append("Copied"); return;
        case 5: out.Append("Saved"); return;
        case 6: out.Append("Cancelled"); return;
        default: out.Append("OK"); return;
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
// A hold's confirm text needs to actually read as a flash, not a blip — held
// open well past the usual redraw throttle, in the same ballpark as the
// LED's own confirm blinks (blink_confirm() etc., TouchPlaited.cpp: 3 blinks
// at ~140ms each).
constexpr uint32_t kConfirmFlashMs = 220;
// How long the last touched control stays on screen before the status row
// takes over. Matches IDLE_MS in visualizer/src/panel/oled-mini.ts.
constexpr uint32_t kIdleMs = 2200;
} // namespace

void OledUi::Service(const TelemetryState& t, uint32_t now_ms, OledScreen& oled) {
    if (!has_last_) {
        last_       = t;
        has_last_   = true;
        // Start the idle clock here, not at 0: the screen already holds
        // OledBoot's restored/fresh status line when the main loop takes
        // over, and an idle timeout measured from reset would be long
        // expired, wiping it the moment this first runs.
        idle_at_ms_ = now_ms;
    }

    // `last_` intentionally stays at its last-drawn value while throttled —
    // it's the baseline the next allowed draw diffs against, so a pad still
    // held (or a knob settled at a new value) when the window clears still
    // reads as "new" instead of looking already-seen. A full press+release
    // that completes entirely inside the window is invisible either way —
    // an unavoidable consequence of sampling slower than the input, not
    // something eagerly updating `last_` here would fix.
    if (now_ms < next_draw_ms_) return;

    // Highest priority, above even a pad-down: a hold building toward a
    // threshold (TouchPlaited.cpp's compute_hold_telemetry(), same
    // precedence as the LED's own accelerating-blink family), shown as a
    // bar instead of a blink rate. Continuous — redraws every allowed cycle
    // while a hold is in progress, not just on change, so the bar visibly
    // fills; the moment it clears, the normal priority chain below resumes
    // from whatever `last_` it left behind.
    if (t.hold_kind != 0) {
        FixedCapStr<24> hl;
        hold_label(hl, t.hold_kind, t.mode, t.rec_slot);
        // A threshold just fired (hold_stage advanced since the last drawn
        // frame — same edge-detection `last_` already does for pads/switches)
        // gets a text flash instead of the bar, held open a little longer
        // than the usual throttle so it actually reads as a flash rather than
        // a blip. The firmware latches the confirm for kConfirmLatchMs
        // (TouchPlaited.cpp) precisely so this poll can't fall in a throttled
        // window and miss the edge; that latch is deliberately shorter than
        // the flash, so the bar can't reappear behind it either.
        const bool just_confirmed = t.hold_stage != 0
            && (t.hold_kind != last_.hold_kind || t.hold_stage != last_.hold_stage);
        if (just_confirmed) {
            FixedCapStr<24> confirm;
            confirm_text(confirm, t.hold_kind, t.mode, t.hold_stage, t.hold_outcome);
            oled.ShowLine(hl.Cstr(), confirm.Cstr());
            next_draw_ms_ = now_ms + kConfirmFlashMs;
        } else {
            oled.ShowProgress(hl.Cstr(), t.hold_progress,
                              hold_note(t.hold_kind, t.mode, t.hold_stage));
            next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        }
        last_         = t;
        idle_at_ms_   = now_ms;
        showing_status_ = false;
        return;
    }

    // A hold that just ended leaves its bar (or confirm flash) frozen on
    // screen: hold_kind drops to 0, nothing else in the chain below has
    // changed, and so nothing would redraw. Hand the screen back to the
    // status row instead — this is the second half of the "bar sticks at
    // ~98% after rec entry" bug, the first being the dropped confirm.
    const bool hold_ended = (last_.hold_kind != 0);

    FixedCapStr<24> label;
    FixedCapStr<24> value;
    bool draw = false;

    // Priority mirrors labels.ts: an explicit pad-down is the most specific
    // signal, then a switch flip, then a model change, then a knob move.
    // Whatever wins owns the screen until the idle timeout below hands it
    // back to the status row.
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
    } else if (((t.arp_flags ^ last_.arp_flags) & 0x0C) != 0) {
        // Melodic transport / Rec arm (both P2+P10, depending on sub-state).
        // Neither is a pad-down we can catch — P2+P10 is a combo whose pads
        // are usually already held — and they used to pass with no message
        // at all, which is the one gesture that silently changes whether
        // anything sounds.
        const bool running = (t.arp_flags & 0x08) != 0;
        const bool armed   = (t.arp_flags & 0x04) != 0;
        // Label names which of the combo's two meanings fired; the value is
        // the state that leaves you in, not the flag that moved (see
        // melodic_state()).
        set_label(label, "P2+P10",
                  (((t.arp_flags ^ last_.arp_flags) & 0x08) != 0) ? "Transport"
                                                                  : "Rec capture");
        value.Append(melodic_state(t.mode, t.arp_flags & 0x03, running, armed));
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
        next_draw_ms_   = now_ms + kMinRedrawIntervalMs;
        idle_at_ms_     = now_ms;
        showing_status_ = false;
        last_ = t;
        return;
    }

    // Nothing was touched. Fall back to the status row once the last callout
    // has had its time on screen — and keep it current afterwards, since the
    // things it reports (transport, model, edited slot) can change while no
    // control is being moved.
    FixedCapStr<24> sl, sv;
    status_row(t, sl, sv);
    const bool idle_now = hold_ended || (now_ms - idle_at_ms_) >= kIdleMs;
    const bool changed  = std::strcmp(sl.Cstr(), status_label_.Cstr()) != 0
                       || std::strcmp(sv.Cstr(), status_value_.Cstr()) != 0;
    if (idle_now && (!showing_status_ || changed)) {
        oled.ShowLine(sl.Cstr(), sv.Cstr());
        next_draw_ms_   = now_ms + kMinRedrawIntervalMs;
        showing_status_ = true;
        status_label_   = sl;
        status_value_   = sv;
    }

    last_ = t;
}

} // namespace synthux
