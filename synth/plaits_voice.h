#pragma once

#include <cstddef>
#include <cstdint>

namespace synthux {

// Forward-declare the Pimpl struct.
// All Plaits and stmlib headers are confined to plaits_voice.cpp —
// this prevents stmlib's global `namespace impl` from leaking into
// any translation unit that includes this header.
struct PlaitsImpl;

class PlaitsVoice {
public:
    PlaitsVoice();
    ~PlaitsVoice();

    void Init();

    // Patch parameters (0..1 unless noted)
    void SetNote(float midi_note);   // MIDI note number, e.g. 60.0 = C4
    void SetHarmonics(float v);
    void SetTimbre(float v);
    void SetMorph(float v);
    void SetDecay(float v);
    void SetEngine(int engine);      // 0..23
    void SetFMAmount(float v);
    void SetLPGColour(float v);
    void SetLevel(float v);
    void SetDrive(float v);    // 0=bypass, 1=full fuzz; only audible on drum stages

    // One-shot trigger: call with true on pad press, false on release
    void Trigger(bool gate_on);

    // Accumulates rendered samples into out_left/out_right (does not zero them).
    // size must be <= 12 (Plaits internal block size limit).
    void Render(float* out_left, float* out_right, size_t size);

    // Returns true if this voice has been Init()'d
    bool IsReady() const { return impl_ != nullptr; }

private:
    int      slot_;   // index into impl_storage/scratch; set at construction, never changes
    PlaitsImpl* impl_;  // null until Init() is called (after hw.Init())
    float    drive_ = 0.0f;
};

} // namespace synthux
