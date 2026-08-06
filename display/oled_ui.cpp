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

// Same scale tables as controls-meta.ts's SCALES, indexed by the panel-mapped
// SW1 position — read through latched_scale() below, never off the lever.
const int kUiScales[3][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },  // Minor
    { 0, 1, 2, 3, 4, 5, 6  },  // Chromatic
    { 0, 2, 4, 5, 7, 9, 11 },  // Major
};
constexpr int kUiPitchBase = 60;

// SW1's latched roles, unpacked from t.sw1_latch (midi/telemetry.h) — both in
// panel order, so they compare directly against t.sw1. Everything that names
// a genre or a scale reads these, NOT the lever: SW1 only takes effect in the
// mode you move it in, so after flicking it elsewhere the lever routinely
// disagrees with what is loaded, and reading it made the screen claim a genre
// that wasn't playing (and pick pattern names out of the wrong genre's table).
// The old kUiScales comment called using the lever "a deliberate
// simplification" — it was a bug, and this is the fix.
inline int latched_genre(const TelemetryState& t) { return t.sw1_latch & 0x03; }
inline int latched_scale(const TelemetryState& t) { return (t.sw1_latch >> 2) & 0x03; }

// A latched role that no longer matches the lever gets a trailing marker, so
// "Techno*" reads as "Techno is loaded, and the switch is somewhere else" —
// which also tells you the flick that would re-sync it is available.
template <size_t N>
void mark_if_stale(FixedCapStr<N>& out, int latched, int lever) {
    if (latched != lever) out.Append("*");
}

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

// The root as a bare pitch class ("D#"), no octave — root_semitone is
// octave-less by design (the octave lives in active_octave()).
const char* root_name(int root) {
    return (root >= 0 && root < 12) ? kNoteNames[root] : "?";
}

// The seven pads as they actually sound from the current root, in order:
// "D# F F# G# A# B C#". compute_note() adds root_semitone *before* the scale
// degrees, so shifting the root transposes the whole scale — this row is what
// makes that visible, and answers "am I really in D# minor, or is only the
// first pad retuned". Pitch classes only, for the same reason as root_name().
// Worst case is 20 chars (seven names, five of them sharps, six spaces), just
// inside the 21-char Font_6x8 budget.
template <size_t N>
void scale_notes(FixedCapStr<N>& out, int sw1, int root) {
    if (sw1 < 0 || sw1 > 2) sw1 = 1;
    if (root < 0 || root > 11) root = 0;
    out.Clear();
    for (int d = 0; d < 7; d++) {
        if (d) out.Append(" ");
        out.Append(kNoteNames[(root + kUiScales[sw1][d]) % 12]);
    }
}

// The same seven pitch classes as scale_notes(), one per array slot instead
// of one string, for OledScreen::ShowPool's column layout — the markers under
// them have to line up per pad, which a single space-joined string can't do
// (the names are one char wide or two).
//
// Pitch classes are the right unit here for a second reason on this screen:
// the pool stores its notes octave-normalized (Arp::Touch, TouchPlaited.cpp)
// and the octave is added at fire time, so the pool's identity genuinely is a
// set of pitch classes — P10/P11 move the whole thing and this row stays true.
// The one case it can lie: leaving Arp/Mel with Hold latched, changing the
// root in Basic Pitch, and coming back. The latched notes keep the pitches
// they were captured at while this row follows the new root. Root and scale
// are Basic Pitch-only settings, so no shift reachable from inside Arp/Mel
// can do it.
void pool_names(const char* (&out)[7], int sw1, int root) {
    if (sw1 < 0 || sw1 > 2) sw1 = 1;
    if (root < 0 || root > 11) root = 0;
    for (int d = 0; d < 7; d++)
        out[d] = kNoteNames[(root + kUiScales[sw1][d]) % 12];
}

// Where an octave shift actually landed: the offset (−3..+3, the range
// P10/P11 clamps to) and the note the pads' root now sounds at. The label row
// only ever named the direction you pressed, so after a few taps — or after
// looking away — there was nothing on the screen saying where you were, only
// which way you had last moved. Octave is per-mode (Basic Pitch, the arp and
// Rec each keep their own), and t.octave is already the active one.
template <size_t N>
void octave_value(FixedCapStr<N>& out, const TelemetryState& t) {
    const int off = static_cast<int>(t.octave) - 3;
    out.Clear();
    if (off > 0) out.Append("+");
    out.AppendInt(off);
    out.Append(" ");
    FixedCapStr<8> n;
    note_name(n, kUiPitchBase + static_cast<int>(t.root) + off * 12);
    out.Append(n.Cstr());
}

// What P0+P10/P11 leaves you with. The range alone ("2") says little, because
// base octave and range COMPOSE: base +1 with range 2 means the arp climbs
// +1 -> +3, and that span is the thing you are actually setting.
//
// Only Arp and Hold can show the span, and the reason is worth keeping:
// t.octave is active_octave(), which in Rec is rec_octave — while the range
// still governs the ARP's climb from arp_octave. Pairing the range with an
// octave that isn't the one climbing would be a confident lie, so Rec reports
// the range on its own instead. Every variant is <= 11 chars so the value row
// holds one font across the whole sweep (item A3).
template <size_t N>
void append_signed(FixedCapStr<N>& out, int v) {
    if (v > 0) out.Append("+");
    out.AppendInt(v);
}

template <size_t N>
void arp_octave_span(FixedCapStr<N>& out, const TelemetryState& t) {
    const int range = (t.arp_flags >> 4) & 0x03;
    out.Clear();
    if ((t.arp_flags & 0x03) == 2) {          // Rec — base octave isn't the arp's
        if (range == 0) { out.Append("no extra"); return; }
        out.Append("+");
        out.AppendInt(range);
        out.Append(" extra");
        return;
    }
    const int base = static_cast<int>(t.octave) - 3;
    append_signed(out, base);
    if (range == 0) { out.Append(" only"); return; }
    out.Append("..");
    append_signed(out, base + range);
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
    // While a pot is armed behind a pickup its own position drives nothing —
    // the stored value is what's in effect, so that's what the value chain
    // below formats, in whatever units the knob normally speaks (BPM, a
    // layer count, a note name). The pot's actual position is shown
    // spatially instead, by OledScreen::ShowPickup's track.
    const bool armed = ((t.pickup_armed >> i) & 1u) != 0;
    const float v = (armed ? t.pickup_target[i] : t.controls[i]) / 127.f;

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
    // Model select is pitched-modes-only: process_model_select() returns
    // immediately on seq_mode_on (TouchPlaited.cpp), so in Seq these combos
    // do nothing and S35 keeps its pattern role. Labelling them here claimed
    // a control that isn't there (found while enumerating the combo lists).
    if (!handled && i == 5 && p0 && mode != 0) {
        set_label(label, "P0+S35", "Model select bank0");
        handled = true;
    }
    if (!handled && i == 5 && p2 && mode != 0) {
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
        // Seq: S35 keeps its pattern role under P0/P2 too — model select is
        // the combo that doesn't exist here, so there's nothing to yield to.
        if (!rec_active) {
            pattern_value(value, latched_genre(t), v);
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
            int sw_a = latched_scale(t);
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
        case 10:
        case 11: {
            // P10/P11 carry five different jobs depending on which modifier
            // is down, and a bare "Oct-/pitch-1" told you none of them —
            // the combos are the whole reason to touch these two.
            const bool up = (i == 11);
            const bool p0 = (t.pads & (1u << 0)) != 0;
            const bool p1 = (t.pads & (1u << 1)) != 0;
            const bool p2 = (t.pads & (1u << 2)) != 0;
            const int  arp_sub = t.arp_flags & 0x03;
            const bool in_rec  = (t.mode == 1) && (arp_sub == 2);
            if (p2) {
                if (up)          set_label(label, "P2+P11", "Drum transport");
                else if (in_rec) set_label(label, "P2+P10", "Rec capture");
                else             set_label(label, "P2+P10", "Mel transport");
            } else if (p1 && in_rec && !up) {
                set_label(label, "P1+P10", "Undo layer");
            } else if (p0) {
                if (t.mode == 1) {
                    // Octave range moved here from P1 (2026-08-05): P0 is the
                    // pitch modifier everywhere else, and range was the lone
                    // non-FX combo on P1.
                    set_label(label, up ? "P0+P11" : "P0+P10",
                              up ? "Arp octaves +" : "Arp octaves -");
                    arp_octave_span(value, t);
                } else if (t.mode == 2) {
                    // Root shifting is the one control on the panel with no
                    // audible landmark of its own — it clamps at C and B
                    // rather than wrapping (deliberate: the dead end IS the
                    // landmark for a unit with no screen), and without a
                    // note name you cannot tell a shifted scale from a
                    // retuned first pad. Name the root on the label row and
                    // spell the whole scale out underneath.
                    FixedCapStr<24> fn;
                    fn.Append(up ? "Root +" : "Root -");
                    fn.Append(" ");
                    fn.Append(root_name(t.root));
                    set_label(label, up ? "P0+P11" : "P0+P10", fn);
                    scale_notes(value, latched_scale(t), t.root);
                } else {
                    // "Root (Pitch only)" ran to 24 chars with the combo in
                    // front and truncated to `P0+P10 ROOT (PITCH ON`, which
                    // read as a fault rather than a restriction — and left
                    // the value row empty underneath it. Split the two.
                    set_label(label, up ? "P0+P11" : "P0+P10", "Root");
                    value.Append("Pitch mode only");
                }
            } else if (t.mode == 0 && t.rec_slot != 0x7F) {
                set_label(label, up ? "P11" : "P10", up ? "Drum pitch +1" : "Drum pitch -1");
                // Where the pitch landed, not just which way you pressed.
                if (t.rec_slot < 7) note_name(value, t.kit[t.rec_slot][5]);
            } else {
                set_label(label, up ? "P11" : "P10", up ? "Octave +" : "Octave -");
                octave_value(value, t);
            }
            return;
        }
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

// ── Modifier combo lists ────────────────────────────────────────────────────
// What holding P0 / P1 / P2 actually unlocks, per mode. Every row is verified
// against the handler that implements it, not against the manual — two things
// fell out of doing that:
//   - P0/P2 + S35 is dead in Seq. process_model_select() returns immediately
//     on seq_mode_on (TouchPlaited.cpp), so the drum kit is only reachable
//     per-slot from Recording. describe_control() labels it anyway; see the
//     `mode == 0` guard there now.
//   - the ± pairs (root, arp octaves) share one row. Listing them separately
//     was what pushed most of these lists past the four rows the screen has.
// Rows are self-labelling ("P1+S30 …"), so no header row is spent naming the
// modifier you are already holding.
constexpr int kMaxComboRows = 6;

// Every row <= 21 chars (Font_6x8 budget). Longest here is 20.
const char* const kP0Seq[]      = { "S37 drum width", "+P2 hold: vary kit" };
const char* const kP0SeqRec[]   = { "S35 slot model b0", "S37 slot width",
                                    "+P2 hold: vary pad" };
const char* const kP0Pitch[]    = { "S35 model bank 0", "S37 stereo width",
                                    "P10/P11 root -/+", "+P2 hold: randomize" };
// Arp and Arp Rec share one P0 list: since undo moved to P1 (2026-08-05) the
// two are identical, Rec's differences all living on P1 and P2 now.
const char* const kP0Arp[]      = { "S35 model bank 0", "S37 stereo width",
                                    "P10/P11 arp octaves", "+P1 hold: sound edit",
                                    "+P2 hold: vary sound" };
const char* const kP1Seq[]      = { "S30 reverb (drums)", "S35 delay (drums)" };
const char* const kP1SeqRec[]   = { "S30 slot reverb send", "S35 slot delay send" };
const char* const kP1Pitch[]    = { "S30 reverb", "S35 delay" };
const char* const kP1Arp[]      = { "S30 reverb", "S35 delay",
                                    "+P0 hold: sound edit" };
const char* const kP1ArpRec[]   = { "S30 reverb", "S35 delay",
                                    "P10 undo layer", "+P0 hold: sound edit" };
const char* const kP2Seq[]      = { "P10 mel transport", "P11 drum play/pause",
                                    "+P0 hold: vary kit" };
const char* const kP2SeqRec[]   = { "S35 slot model b1", "+P0 hold: vary pad" };
const char* const kP2Pitch[]    = { "S35 model bank 1", "P10 mel transport",
                                    "P11 drum play/pause", "+P0 hold: randomize" };
const char* const kP2Arp[]      = { "S35 model bank 1", "P10 mel transport",
                                    "P11 drum play/pause", "+P0 hold: vary sound" };
const char* const kP2ArpRec[]   = { "S35 model bank 1", "P10 rec cycle",
                                    "P11 drum play/pause", "P3-P7 layer gestures",
                                    "+P0 hold: vary sound" };

template <size_t N>
int pick_rows(const char* const (&tbl)[N], const char* const** out) {
    *out = tbl;
    return static_cast<int>(N);
}

// Returns the row count and points `rows` at them. mod is 0/1/2 (P0/P1/P2).
int combo_rows(const TelemetryState& t, int mod, const char* const** rows) {
    const bool seq_rec = (t.mode == 0) && (t.rec_slot != 0x7F);
    const bool arp_rec = (t.mode == 1) && ((t.arp_flags & 0x03) == 2);
    switch (mod) {
        case 0:
            if (seq_rec)      return pick_rows(kP0SeqRec, rows);
            if (t.mode == 0)  return pick_rows(kP0Seq,    rows);
            if (t.mode == 1)  return pick_rows(kP0Arp,    rows);
            return pick_rows(kP0Pitch, rows);
        case 1:
            if (seq_rec)      return pick_rows(kP1SeqRec, rows);
            if (t.mode == 0)  return pick_rows(kP1Seq,    rows);
            if (arp_rec)      return pick_rows(kP1ArpRec, rows);
            if (t.mode == 1)  return pick_rows(kP1Arp,    rows);
            return pick_rows(kP1Pitch, rows);
        default:
            if (seq_rec)      return pick_rows(kP2SeqRec, rows);
            if (t.mode == 0)  return pick_rows(kP2Seq,    rows);
            if (arp_rec)      return pick_rows(kP2ArpRec, rows);
            if (t.mode == 1)  return pick_rows(kP2Arp,    rows);
            return pick_rows(kP2Pitch, rows);
    }
}

// ── Idle status row ─────────────────────────────────────────────────────────
// The home screen: what shows when nothing has been touched for a while, and
// the thing a finished hold hands the screen back to. Deliberately built only
// from slow-moving fields — the row this replaces was dropped because it
// included seq_step, which changes every block and so never redrew in time to
// be right. Everything here changes at most a few times a second.
// How long each phase of the Rec status row's cycle stays up (below). Long
// enough to read a sentence and look away, short enough that you don't have
// to wait to find the one you wanted.
constexpr uint32_t kRecCycleMs = 2600;

void status_row(const TelemetryState& t, uint32_t now_ms,
                FixedCapStr<24>& label, FixedCapStr<24>& value) {
    label.Clear();
    value.Clear();

    // Seq recording: which pad is being edited, and what's loaded in it.
    // Rec is the one mode you can be *stuck* in — the two ways out are both
    // holds on pads you're not otherwise touching, and neither announces
    // itself. So the value row cycles instead of sitting on the model:
    // what's loaded, then how to keep it, then the other gesture that lives
    // here. Costs nothing extra to redraw — the row already repaints
    // whenever its own text changes (OledUi::Service's `changed` test), so
    // the cycle drives itself.
    if (t.mode == 0 && t.rec_slot < 7) {
        const int slot = t.rec_slot;
        label.Append("Rec P");
        label.AppendInt(slot + 3);
        if (kPads[slot + 3].seq_role) {
            label.Append(" ");
            label.Append(kPads[slot + 3].seq_role);
        }
        switch ((now_ms / kRecCycleMs) % 3) {
            case 1:
                value.Append("Hold P");
                value.AppendInt(slot + 3);
                value.Append(" save");
                break;
            case 2:
                value.Append("+pad copies");
                break;
            default:
                value.Append(model_name(t.kit[slot][0]));
                break;
        }
        return;
    }
    if (t.mode == 0) {
        // Transport rides the label row so the value row can name the
        // pattern that's playing — the more useful of the two at a glance,
        // and t.seq_pattern is the sequencer's own slot, not S35's pot
        // (which sits behind a pickup and can be parked anywhere).
        // The GENRE THAT IS LOADED, not the lever — flicking SW1 while in
        // Basic Pitch moves the scale, not the genre, so on returning to Seq
        // the lever routinely points somewhere the drums aren't. `*` marks
        // exactly that, and the pattern name comes out of the same genre's
        // table (reading the lever picked names from the wrong one).
        const int genre = latched_genre(t);
        label.Append(t.playing ? "Seq " : "Seq stop ");
        label.Append(kSw1Seq[genre]);
        mark_if_stale(label, genre, t.sw1);
        if (t.clock_src != 0) label.Append(t.clock_src == 1 ? " ext" : " cv");
        pattern_slot_value(value, genre, t.seq_pattern);
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
    // Same rule as Seq's genre above: the loaded scale, marked when the lever
    // has since been moved in another mode.
    const int scale = latched_scale(t);
    label.Append("Pitch ");
    label.Append(kSw1Pitch[scale]);
    mark_if_stale(label, scale, t.sw1);
    // Root belongs next to the scale, not on its own screen: together they
    // name the key the pads are in. 14 chars at worst ("Pitch Chromatic"
    // is 15 without it, 20 with) — inside the label budget.
    label.Append(" ");
    label.Append(root_name(t.root));
    value.Append(model_name(t.model));
}

// ── Hold text ───────────────────────────────────────────────────────────────

template <size_t N>
void hold_label(FixedCapStr<N>& out, const TelemetryState& t) {
    const uint8_t kind = t.hold_kind;
    const int     mode = t.mode;
    switch (kind) {
        case 1: set_label(out, "P0+P2", (mode == 0) ? "Re-randomize" : "Vary sound"); return;
        case 2: set_label(out, "P3-P9", "Rec entry");    return;
        case 3: set_label(out, "P2+pad", "Clear layer"); return;
        case 4: set_label(out, "Rec", "Copy layer");     return;
        case 6: set_label(out, "Rec", "Exit");           return;
        case 7: set_label(out, "P0+P1", "Sound edit");   return;
        // Same combo as kind 1, deliberately named for its scope instead:
        // in Rec it changes the one pad you are editing, not the kit.
        case 8:
            out.Clear();
            out.Append("P0+P2 ");
            if (t.rec_slot < 7) { out.Append("P"); out.AppendInt(t.rec_slot + 3); }
            else                { out.Append("pad"); }
            out.Append(" sound");
            return;
        case 5:
            // Name the pad being held, as asked for on hardware — "hold P5
            // to save" is the whole instruction, and the slot is right there
            // in the telemetry.
            out.Clear();
            out.Append("Hold ");
            if (t.rec_slot < 7) { out.Append("P"); out.AppendInt(t.rec_slot + 3); }
            else                { out.Append("pad"); }
            out.Append(" to save");
            return;
        default: out.Clear(); out.Append("Hold");        return;
    }
}

// What crossing the *next* threshold will do — the note row under the bar
// (OledScreen::ShowProgress). A bar on its own says something is coming
// without ever saying what, and P0+P2's stages differ per playmode (see the
// hold block in TouchPlaited.cpp): Seq varies the current kit then replaces
// it, the pitched modes vary the sound twice, and Basic Pitch alone has a 3rd
// stage that drops the snapshots. `done` is how many have fired already.
const char* hold_note(const TelemetryState& t) {
    const uint8_t done = t.hold_stage;
    switch (t.hold_kind) {
        // Times are kStageBlocks apart (2 s each, TouchPlaited.cpp) — these
        // strings still said 1s/2s/3s from before A5 doubled the stages.
        case 1:
            if (t.mode == 0) return (done == 0) ? "2s vary kit" : "4s new kit";
            if (done == 0) return "2s vary sound";
            if (done == 1) return "4s vary more";
            return "6s back to live knobs";
        case 2: return "2s enter rec mode";
        case 3: return "clear this layer";
        case 4: return "copy slot to pad";
        case 5: return "keep these edits";
        // Same combo both ways, so the note has to say which way this press
        // is going — it's the only warning you get before every knob in the
        // mode changes meaning.
        case 7: return t.snd_edit ? "back to arp knobs" : "knobs edit the sound";
        case 8: return (done == 0) ? "2s vary this pad" : "4s new sound, in role";
        default: return "";
    }
}

// What flashes the instant a threshold fires. Names the change rather than
// the stage number it used to print — "Stage 2" told you a threshold was
// crossed but not which of the three per-mode meanings it had.
template <size_t N>
void confirm_text(FixedCapStr<N>& out, const TelemetryState& t) {
    const uint8_t stage   = t.hold_stage;
    const uint8_t outcome = t.hold_outcome;
    out.Clear();
    switch (t.hold_kind) {
        case 1:
            if (t.mode == 0)        out.Append(stage >= 2 ? "New kit" : "Kit varied");
            else if (stage >= 3)    out.Append("Live knobs");
            else                    out.Append(stage >= 2 ? "Varied more" : "Varied");
            return;
        case 2: out.Append("Recording"); return;
        case 3: out.Append(outcome == 2 ? "Empty" : "Cleared"); return;
        case 4: out.Append("Copied"); return;
        case 5: out.Append("Saved"); return;
        case 6: out.Append("Cancelled"); return;
        // outcome: 1 entered sound edit, 2 left it. t.snd_edit is not
        // usable here — the flash is latched, so by the time it draws the
        // flag has already flipped.
        case 7: out.Append(outcome == 2 ? "Arp knobs" : "Sound edit"); return;
        case 8: out.Append(stage >= 2 ? "New sound" : "Pad varied"); return;
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
// Capture-indicator blink half-period. Slow enough to read as a deliberate
// pulse rather than a flicker, and it costs one extra redraw per phase — the
// screen is otherwise change-driven, so this is the only thing on it that
// generates I2C traffic on its own. Only runs while recording.
constexpr uint32_t kBlinkMs = 400;
// How long each window of an overflowing combo list stays up before it
// scrolls by one row. Only two lists in the whole device exceed the screen's
// four rows, and both by exactly one — see combo_rows().
constexpr uint32_t kListScrollMs = 1400;
} // namespace

namespace {
// What the persistent indicator block shows right now (oled_screen.h). Only
// Arp/Mel Rec has anything to say: whether capture is live, and the layer
// stack with the open take marked.
StatusIcons icons_for(const TelemetryState& t, uint32_t now_ms) {
    StatusIcons ic;
    // Seq slot editing gets the circle too — it has no layer stack to show,
    // but it is the other mode where the panel looks idle while the device
    // is in a state you have to leave deliberately. One persistent marker
    // that survives every callout, so "am I still in Rec?" is never a
    // question you have to press something to answer.
    if (t.mode == 0 && t.rec_slot < 7) {
        ic.blink = ((now_ms / kBlinkMs) & 1u) != 0;
        ic.rec   = true;
        return ic;
    }
    if (t.mode != 1 || (t.arp_flags & 0x03) != 2) return ic;  // Rec sub-state only
    const bool armed   = (t.arp_flags & 0x04) != 0;
    const bool running = (t.arp_flags & 0x08) != 0;
    ic.blink  = ((now_ms / kBlinkMs) & 1u) != 0;
    ic.rec    = armed && running;
    ic.layers = t.rec_layers > 5 ? 5 : t.rec_layers;
    ic.mute   = t.rec_mute;
    // The open take is the slot just past the committed ones — and only
    // while capture is actually live, since nothing is going into it
    // otherwise. At 5 the stack is full (LIMIT) and there is no open take.
    if (ic.rec && ic.layers < 5) ic.open = ic.layers;
    return ic;
}

// Whether this screen should be drawing the arp's note pool (ShowPool) rather
// than a plain value row. Arp and Hold only: Rec's pads play and record
// instead of feeding the pool, so there the same row would mark seven notes
// that have nothing to do with what you are hearing.
bool pool_view(const TelemetryState& t) {
    return t.mode == 1 && (t.arp_flags & 0x03) <= 1;
}
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
        hold_label(hl, t);
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
            confirm_text(confirm, t);
            oled.ShowLine(hl.Cstr(), confirm.Cstr());
            next_draw_ms_ = now_ms + kConfirmFlashMs;
        } else {
            oled.ShowProgress(hl.Cstr(), t.hold_progress, hold_note(t));
            next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        }
        last_         = t;
        idle_at_ms_   = now_ms;
        showing_status_ = false;
        pickup_knob_  = -1;
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
    // Which knob (if any) this frame should draw as a pickup rather than a
    // plain value row. Only the knob branch below sets it, so any more
    // specific callout — a pad, a switch, a transport change — takes the
    // screen back to normal, same as it takes it back from anything else.
    int pickup_now = -1;
    // Same idea for the pool row: only the musical-pad branch sets it.
    bool pool_now = false;

    // Priority mirrors labels.ts: an explicit pad-down is the most specific
    // signal, then a switch flip, then a model change, then a knob move.
    // Whatever wins owns the screen until the idle timeout below hands it
    // back to the status row.
    const uint16_t new_touches = t.pads & ~last_.pads;
    // Transport and capture changes outrank the pad-down that caused them.
    // P2+P10 and P2+P11 fire on the P10/P11 press edge, so the pad branch
    // below used to win the same frame and announce "P10 Oct-/pitch-1" —
    // the modifier's meaning, which is the whole point of the combo, never
    // reached the screen.
    if (((t.arp_flags ^ last_.arp_flags) & 0x0C) != 0) {
        const bool running = (t.arp_flags & 0x08) != 0;
        const bool armed   = (t.arp_flags & 0x04) != 0;
        // Label names which of the combo's two meanings fired; the value is
        // the state that leaves you in, not the flag that moved (see
        // melodic_state()).
        // Rec's cycle can move both flags in one press (stopped -> capturing
        // arms and starts). Capture is the headline when it changed.
        set_label(label, "P2+P10",
                  (((t.arp_flags ^ last_.arp_flags) & 0x04) != 0) ? "Rec capture"
                                                                 : "Transport");
        value.Append(melodic_state(t.mode, t.arp_flags & 0x03, running, armed));
        draw = true;
    } else if (t.playing != last_.playing) {
        // P2+P11, or a MIDI Start/Stop, or the seq's own first-entry
        // auto-start — all worth saying out loud for the same reason.
        set_label(label, "P2+P11", "Drum seq");
        value.Append(t.playing ? "Play" : "Stop");
        draw = true;
    } else if (new_touches != 0) {
        int i = 0;
        while (i < 12 && !((new_touches >> i) & 1u)) i++;
        if (i <= 2) {
            // A modifier just went down: list what it unlocks here rather
            // than printing "P0 modifier", which named the pad you are
            // already holding and nothing else. Owns the screen until it's
            // released or something more specific happens.
            list_mod_      = i;
            list_offset_   = 0;
            list_at_ms_    = now_ms;
            const char* const* rows;
            const int n = combo_rows(t, i, &rows);
            oled.ShowList(rows, n);
            next_draw_ms_   = now_ms + kMinRedrawIntervalMs;
            idle_at_ms_     = now_ms;
            showing_status_ = false;
            pickup_knob_    = -1;
            last_ = t;
            return;
        }
        describe_pad(i, t, label, value);
        // A musical pad in Arp/Hold answers a bigger question than the note it
        // just added: what the whole pool is now. The label row still names
        // the pad, so nothing is lost by giving the value row to the seven
        // markers instead of to this one note.
        if (pool_view(t) && i >= 3 && i <= 9) pool_now = true;
        draw = true;
    } else if (t.sw1 != last_.sw1) {
        const char* const* table = (t.mode == 0) ? kSw1Seq : (t.mode == 1) ? kSw1Arp : kSw1Pitch;
        set_label(label, "SW1", (t.sw1 >= 0 && t.sw1 <= 2) ? table[t.sw1] : "?");
        if (t.mode == 2) {
            // A scale is a scale *from somewhere* — "Minor" alone never said
            // which minor. Root is Basic Pitch-only, so it only rides here.
            label.Append(" - ");
            label.Append(root_name(t.root));
            scale_notes(value, latched_scale(t), t.root);
        }
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
                // An armed pot gets the pickup screen instead of the plain
                // value row: the value shown is the stored one (see
                // describe_control), and the track underneath is the only
                // thing saying the pot in your hand isn't connected to it
                // yet. Redraws on every reported pot move, so the marker
                // tracks the knob.
                if ((t.pickup_armed >> i) & 1u) pickup_now = i;
                break;
            }
        }
    }
    // Catching by proximity (KnobPickup's kNear window) or by the rails'
    // move-catch can land on a frame where the pot's *reported* position
    // hasn't moved a full LSB, so the branch above wouldn't fire and the
    // track would sit there implying the knob is still dead. Force the plain
    // value row once the arm clears — the track disappearing is the "you
    // have it" signal.
    if (!draw && pickup_knob_ >= 0 && ((t.pickup_armed >> pickup_knob_) & 1u) == 0) {
        describe_control(pickup_knob_, t, label, value);
        draw = true;
    }

    const StatusIcons icons = icons_for(t, now_ms);

    // A combo list holds the screen for as long as its modifier is down —
    // it's a reference you read, so the idle timeout must not pull it away
    // mid-sentence. Only the two lists that don't fit redraw, to scroll.
    if (list_mod_ >= 0) {
        const bool still_held = (t.pads & (1u << list_mod_)) != 0;
        if (still_held && !draw) {
            const char* const* rows;
            const int n   = combo_rows(t, list_mod_, &rows);
            const int max = n - OledScreen::kListRows;
            if (max > 0 && (now_ms - list_at_ms_) >= kListScrollMs) {
                list_offset_ = (list_offset_ + 1) % (max + 1);
                list_at_ms_  = now_ms;
                oled.ShowList(rows + list_offset_, n - list_offset_);
                next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
            }
            idle_at_ms_ = now_ms;   // hold off the status row while held
            last_ = t;
            return;
        }
        list_mod_ = -1;
    }

    if (draw) {
        pickup_knob_ = pickup_now;
        if (pool_now) {
            const char* names[7];
            pool_names(names, latched_scale(t), t.root);
            oled.ShowPool(label.Cstr(), names, t.arp_pool);
        } else if (pickup_knob_ >= 0)
            oled.ShowPickup(label.Cstr(), value.Cstr(),
                            t.controls[pickup_knob_], t.pickup_target[pickup_knob_]);
        else
            oled.ShowLine(label.Cstr(), value.Cstr(), icons);
        next_draw_ms_   = now_ms + kMinRedrawIntervalMs;
        idle_at_ms_     = now_ms;
        showing_status_ = false;
        blink_phase_    = icons.blink;
        last_label_     = label;   // a blink redraw has to repaint this
        last_value_     = value;
        last_ = t;
        return;
    }

    // Nothing was touched. Fall back to the status row once the last callout
    // has had its time on screen — and keep it current afterwards, since the
    // things it reports (transport, model, edited slot) can change while no
    // control is being moved.
    FixedCapStr<24> sl, sv;
    status_row(t, now_ms, sl, sv);
    // In Arp/Hold with anything in the pool, the home screen IS the pool. This
    // is the whole point of the row rather than a nicety: in Hold you cannot
    // inspect the pool by pressing a pad, because pressing a pad is what takes
    // notes out of it — so a view that only appeared on a pad-down would be
    // unreachable exactly where it matters. The label row still carries the
    // sub-state; what it costs is the model name, which is back the moment the
    // pool empties (and is a knob turn away regardless).
    // Not while the melodic transport is stopped, though: status_row() gives
    // the value row to "Arp stopped" there for a reason that outranks this one
    // — a silent pool is the state where the mode looks broken rather than
    // quiet, and seven markers would say what is loaded without saying that
    // none of it is playing. The pad-down callout still shows the pool while
    // stopped; it's only the home screen that yields.
    // 0xFF is "not a pool screen", so one comparison covers both the mask
    // changing and the screen changing kind.
    const bool arp_running = (t.arp_flags & 0x08) != 0;
    const uint8_t want_pool = (pool_view(t) && arp_running && (t.arp_pool & 0x7F) != 0)
                                  ? static_cast<uint8_t>(t.arp_pool & 0x7F) : 0xFF;
    const bool idle_now = hold_ended || (now_ms - idle_at_ms_) >= kIdleMs;
    const bool changed  = std::strcmp(sl.Cstr(), status_label_.Cstr()) != 0
                       || std::strcmp(sv.Cstr(), status_value_.Cstr()) != 0
                       || want_pool != status_pool_;
    if (idle_now && (!showing_status_ || changed)) {
        if (want_pool != 0xFF) {
            const char* names[7];
            pool_names(names, latched_scale(t), t.root);
            oled.ShowPool(sl.Cstr(), names, want_pool);
        } else {
            oled.ShowLine(sl.Cstr(), sv.Cstr(), icons);
        }
        next_draw_ms_   = now_ms + kMinRedrawIntervalMs;
        showing_status_ = true;
        pickup_knob_    = -1;   // the track went with the callout
        status_label_   = sl;
        status_value_   = sv;
        status_pool_    = want_pool;
        blink_phase_    = icons.blink;
    } else if (icons.animated() && icons.blink != blink_phase_) {
        // The one self-generated redraw on this screen: the capture
        // indicator has to pulse whether or not anything else moved. Redraw
        // whatever is already showing rather than reverting to the status
        // row, so a blink can't steal a callout mid-read. It repaints as a
        // plain label/value row, which would wipe a pool screen — it can
        // never land on one: icons_for() only animates in Rec (or Seq slot
        // editing), and pool_view() excludes both.
        if (showing_status_) oled.ShowLine(status_label_.Cstr(), status_value_.Cstr(), icons);
        else                 oled.ShowLine(last_label_.Cstr(), last_value_.Cstr(), icons);
        next_draw_ms_ = now_ms + kMinRedrawIntervalMs;
        blink_phase_  = icons.blink;
    }

    last_ = t;
}

} // namespace synthux
