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
    bool triggered;
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
    impl_->triggered = false;

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
    impl_->modulations.trigger = gate_on ? 1.0f : 0.0f;
}

void PlaitsVoice::Render(float* out_left, float* out_right, size_t size) {
    if (!impl_) return;
    impl_->voice.Render(impl_->patch, impl_->modulations, impl_->frames, size);
    impl_->modulations.trigger = 0.0f;
    constexpr float kScale = 1.0f / 32768.0f;
    bool apply_drive = drive_ > 0.01f;
    for (size_t i = 0; i < size; i++) {
        float l = impl_->frames[i].out * kScale;
        float r = impl_->frames[i].aux * kScale;
        if (apply_drive) { l = distort(l, drive_); r = distort(r, drive_); }
        out_left[i]  += l;
        out_right[i] += r;
    }
}

} // namespace synthux
