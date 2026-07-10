#include "plaits_voice.h"
#include "daisy_seed.h"
#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"

namespace {

constexpr int kMaxVoices  = 6;   // must match VoicePool::kVoices
constexpr int kScratchSize = 16384;

// Padé approximant for tanh — accurate to < 0.01 for |x| < 3.5, then clamped.
static inline float tanh_fast(float x) {
    if (x >  3.5f) return  1.0f;
    if (x < -3.5f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Staged warm→gritty saturator.
//   drive 0.0 – 0.5: tanh saturation (warm, tube-like), gain 1→4
//   drive 0.5 – 1.0: blend tanh into hard clip (fuzz), gain 4→9
// Peak-bounded to ±1, plus loudness makeup: saturation raises RMS sharply even
// with peak compensation, so the output is pulled down as drive increases
// (−6 dB at full drive). Tune the makeup slope by ear on hardware.
static inline float distort(float x, float drive) {
    float gain, fuzz;
    if (drive <= 0.5f) {
        gain = 1.0f + drive * 6.0f;   // 1..4
        fuzz = 0.0f;
    } else {
        gain = 4.0f + (drive - 0.5f) * 10.0f;  // 4..9
        fuzz = (drive - 0.5f) * 2.0f;            // 0..1
    }
    float makeup = 1.0f / (1.0f + drive);   // 0 dB at drive 0 → −6 dB at drive 1
    float comp   = 1.0f / tanh_fast(gain);
    float sat    = tanh_fast(x * gain) * comp;
    if (fuzz < 0.001f) return sat * makeup;
    float hard = x * gain;
    hard = hard >  1.0f ?  1.0f : hard < -1.0f ? -1.0f : hard;
    hard *= comp;
    return (sat + fuzz * (hard - sat)) * makeup;
}

// Scratch and impl storage as raw bytes — no C++ constructors run here;
// PlaitsImpl is built via placement new inside Init().
// Placed in internal D1 SRAM (plain .bss), NOT SDRAM: the physical-modeling
// engines walk these buffers every sample, and external SDRAM latency made
// that the dominant CPU cost (measured ~44% idle at 4 voices from SDRAM).
// 4 voices ≈ 100 KB of the 512 KB SRAM.
uint8_t scratch[kMaxVoices][kScratchSize];

} // namespace

namespace synthux {

struct PlaitsImpl {
    plaits::Voice voice;
    plaits::Patch patch;
    plaits::Modulations modulations;
    plaits::Voice::Frame frames[24];
    bool gate;          // desired gate level: pad held / one-shot hold running
    bool edge_pending;  // note-on requested since the last render
    bool trig_high;     // trigger level written on the previous render
    uint8_t ac_count;   // anti-click gain sequence countdown (0 = inactive)
    bool ac_faded;      // fade-out block of the sequence already rendered
};

// Raw aligned storage in internal SRAM (see note on `scratch` above).
// No constructor runs here; placement new in Init() builds each PlaitsImpl in place.
alignas(PlaitsImpl) static uint8_t impl_storage[kMaxVoices * sizeof(PlaitsImpl)];

static int next_slot = 0;  // in regular .bss — safe to access before main()

PlaitsVoice::PlaitsVoice() : slot_(next_slot < kMaxVoices ? next_slot++ : -1), impl_(nullptr) {}

PlaitsVoice::~PlaitsVoice() {
    if (impl_) impl_->~PlaitsImpl();
}

void PlaitsVoice::Init() {
    if (slot_ < 0 || slot_ >= kMaxVoices) return;

    // Placement new: run PlaitsImpl's constructor now, with SDRAM ready.
    void* storage = &impl_storage[slot_ * sizeof(PlaitsImpl)];
    impl_ = new (storage) PlaitsImpl();
    impl_->gate         = false;
    impl_->edge_pending = false;
    impl_->trig_high    = false;
    impl_->ac_count     = 0;
    impl_->ac_faded     = false;

    stmlib::BufferAllocator alloc(scratch[slot_], kScratchSize);
    impl_->voice.Init(&alloc);

    auto& p = impl_->patch;
    p.note                         = 60.0f;
    p.harmonics                    = 0.5f;
    p.timbre                       = 0.5f;
    p.morph                        = 0.5f;
    p.frequency_modulation_amount  = 0.0f;
    p.timbre_modulation_amount     = 0.0f;
    p.morph_modulation_amount      = 0.0f;
    p.engine                       = 0;
    p.decay                        = 0.5f;
    p.lpg_colour                   = 0.5f;

    auto& m = impl_->modulations;
    m                  = {};
    m.trigger_patched  = true;
    m.level            = 1.0f;
}

void PlaitsVoice::SetNote(float midi_note)  { if (impl_) impl_->patch.note = midi_note; }
void PlaitsVoice::SetHarmonics(float v)     { if (impl_) impl_->patch.harmonics = v; }
void PlaitsVoice::SetTimbre(float v)        { if (impl_) impl_->patch.timbre = v; }
void PlaitsVoice::SetMorph(float v)         { if (impl_) impl_->patch.morph = v; }
void PlaitsVoice::SetDecay(float v)         { if (impl_) impl_->patch.decay = v; }
void PlaitsVoice::SetEngine(int engine)     { if (impl_) impl_->patch.engine = engine; }
void PlaitsVoice::SetFMAmount(float v)      { if (impl_) impl_->patch.frequency_modulation_amount = v; }
void PlaitsVoice::SetLPGColour(float v)    { if (impl_) impl_->patch.lpg_colour = v; }
void PlaitsVoice::SetLevel(float v)         { if (impl_) impl_->modulations.level = v; }
void PlaitsVoice::SetDrive(float v)         { drive_ = v; }

void PlaitsVoice::Trigger(bool gate_on) {
    if (!impl_) return;
    if (gate_on) impl_->edge_pending = true;
    impl_->gate = gate_on;
}

void PlaitsVoice::Render(float* out_left, float* out_right, size_t size) {
    if (!impl_) return;
    // Gate sequencing. Plaits detects note-ons from the rising edge of
    // modulations.trigger, and the six-op FM engines (2-4) additionally hold
    // their DX7 envelopes keyed while the line stays high — so the gate must
    // span the whole press, not one block. A retrigger while the line is
    // still high (voice steal, one-shot repeat) gets one forced-low block
    // first so the edge detector fires; a tap released before its first
    // render still produces a one-block pulse.
    const int engine = impl_->patch.engine;
    const bool fm = engine >= 2 && engine <= 4;
    float trig;
    if (impl_->edge_pending && impl_->trig_high) {
        trig = 0.0f;                    // gap block; edge fires next render
        // Anti-click (six-op only): the engine won't see the new edge for
        // kTriggerDelay (5) blocks, and at key-on it resets operator phases /
        // loads the new patch — a hard discontinuity if this voice still
        // carries the previous note's tail. Fade the tail out now, mute the
        // 5 stale-tail blocks, ramp back in on the block where the key-on
        // lands. Gap path spans 7 blocks, fresh-edge path 6.
        if (fm && impl_->ac_count == 0) {
            impl_->ac_count = 7;
            impl_->ac_faded = false;
        }
    } else if (impl_->edge_pending) {
        trig = 1.0f;
        impl_->edge_pending = false;
        if (fm && impl_->ac_count == 0) {
            impl_->ac_count = 6;
            impl_->ac_faded = false;
        }
    } else {
        trig = impl_->gate ? 1.0f : 0.0f;
    }
    impl_->modulations.trigger = trig;
    impl_->trig_high = trig > 0.5f;
    impl_->voice.Render(impl_->patch, impl_->modulations, impl_->frames, size);
    constexpr float kScale = 1.0f / 32768.0f;
    // Six-op FM (engines 2-4) is the hottest thing in Plaits: registered at
    // out_gain 1.0 with the LPG bypassed (already_enveloped), and its
    // internal soft-clip pins dense DX7 patches near full scale for the
    // whole (now held) gate. Every other melodic engine is 0.6-0.8 through
    // a decaying LPG. Pad it to a comparable level so polyphonic FM doesn't
    // saturate the output stage. Tune by ear on hardware.
    const float scale = fm ? kScale * 0.20f : kScale;
    // Anti-click gain profile for this block: fade-out on the block that
    // starts the sequence, silence while the engine still renders the stale
    // tail, one-block ramp-in as the new note keys on. ~3 ms total, engine
    // attack character untouched (the ramp ends as the envelope starts).
    enum AcMode { AC_NONE, AC_FADE, AC_MUTE, AC_RAMP };
    AcMode ac = AC_NONE;
    if (impl_->ac_count > 0) {
        if (!impl_->ac_faded) { ac = AC_FADE; impl_->ac_faded = true; }
        else if (impl_->ac_count > 1) { ac = AC_MUTE; }
        else { ac = AC_RAMP; }
        impl_->ac_count--;
    }
    const float ac_step = 1.0f / static_cast<float>(size);
    bool apply_drive = drive_ > 0.01f;
    for (size_t i = 0; i < size; i++) {
        float l = impl_->frames[i].out * scale;
        float r = impl_->frames[i].aux * scale;
        if (apply_drive) { l = distort(l, drive_); r = distort(r, drive_); }
        if (ac != AC_NONE) {
            float g = ac == AC_MUTE ? 0.0f
                    : ac == AC_FADE ? 1.0f - ac_step * static_cast<float>(i + 1)
                                    : ac_step * static_cast<float>(i + 1);
            l *= g;
            r *= g;
        }
        out_left[i]  += l;
        out_right[i] += r;
    }
}

} // namespace synthux
