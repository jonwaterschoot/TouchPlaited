#pragma once
#include "plaits_voice.h"
#include <cstdint>

namespace synthux {

class VoicePool {
public:
    static constexpr int kVoices = 4;

    void Init() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Init();
            pad_slot[i]  = -1;
            timestamp[i] = 0;
        }
        tick         = 0;
        audition_idx = -1;
    }

    void SetEngine(int e)      { for (auto& v : voices) v.SetEngine(e); }
    void SetHarmonics(float v) { for (auto& vv : voices) vv.SetHarmonics(v); }
    void SetTimbre(float v)    { for (auto& vv : voices) vv.SetTimbre(v); }
    void SetMorph(float v)     { for (auto& vv : voices) vv.SetMorph(v); }
    void SetDecay(float v)     { for (auto& vv : voices) vv.SetDecay(v); }

    void SetLPGColour(float v) { for (auto& vv : voices) vv.SetLPGColour(v); }
    void SetDrive(float v)     { for (auto& vv : voices) vv.SetDrive(v); }

    // Skip the audition voice — its FM stays at 0 for the full note duration.
    void SetFMAmount(float v) {
        for (int i = 0; i < kVoices; i++) {
            if (i != audition_idx) voices[i].SetFMAmount(v);
        }
    }

    // Mode 1: note only; patch params driven globally by knobs each block.
    void NoteOn(int slot, float note) {
        int idx = find_free_or_steal();
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]  = slot;
        timestamp[idx] = ++tick;
    }

    // Modes 2/3: note + full patch snapshot; params persist until stolen.
    void NoteOnWithParams(int slot, float note,
                          int engine, float harmonics, float timbre,
                          float morph, float decay) {
        int idx = find_free_or_steal();
        voices[idx].SetEngine(engine);
        voices[idx].SetHarmonics(harmonics);
        voices[idx].SetTimbre(timbre);
        voices[idx].SetMorph(morph);
        voices[idx].SetDecay(decay);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]  = slot;
        timestamp[idx] = ++tick;
    }

    void NoteOff(int slot) {
        for (int i = 0; i < kVoices; i++) {
            if (pad_slot[i] == slot) {
                voices[i].Trigger(false);
                pad_slot[i] = -1;
            }
        }
    }

    void AllNotesOff() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Trigger(false);
            pad_slot[i] = -1;
        }
        audition_idx = -1;
    }

    // One-shot preview with neutral params. FM=0 is maintained by SetFMAmount skipping this voice.
    void Audition(float note, int engine = -1) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        if (engine >= 0) voices[idx].SetEngine(engine);
        voices[idx].SetHarmonics(0.5f);
        voices[idx].SetTimbre(0.5f);
        voices[idx].SetMorph(0.5f);
        voices[idx].SetDecay(0.6f);
        voices[idx].SetFMAmount(0.0f);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]  = -1;
        timestamp[idx] = ++tick;
    }

    // Preview using a specific slot's patch — used for audible feedback during P0+P2 hold.
    void AuditionWithParams(float note, int engine,
                            float harmonics, float timbre, float morph, float decay) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        voices[idx].SetEngine(engine);
        voices[idx].SetHarmonics(harmonics);
        voices[idx].SetTimbre(timbre);
        voices[idx].SetMorph(morph);
        voices[idx].SetDecay(decay);
        voices[idx].SetFMAmount(0.0f);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]  = -1;
        timestamp[idx] = ++tick;
    }

    void Render(float* out_left, float* out_right, size_t size) {
        for (auto& v : voices) v.Render(out_left, out_right, size);
    }

    // Update params on the current audition voice without retriggering.
    void UpdateAuditionParams(float harmonics, float timbre, float morph, float decay) {
        if (audition_idx >= 0) {
            voices[audition_idx].SetHarmonics(harmonics);
            voices[audition_idx].SetTimbre(timbre);
            voices[audition_idx].SetMorph(morph);
            voices[audition_idx].SetDecay(decay);
        }
    }

    void UpdateAuditionEngine(int engine) {
        if (audition_idx >= 0) voices[audition_idx].SetEngine(engine);
    }

    PlaitsVoice voices[kVoices];

private:
    int      pad_slot[kVoices];
    uint32_t timestamp[kVoices];
    uint32_t tick;
    int      audition_idx;

    int find_free_or_steal() {
        // Prefer a genuinely free voice (not the active audition slot).
        for (int i = 0; i < kVoices; i++) {
            if (pad_slot[i] < 0 && i != audition_idx) return i;
        }
        // All real-note slots occupied — recycle the audition voice before stealing a note.
        if (audition_idx >= 0) {
            int idx      = audition_idx;
            audition_idx = -1;
            pad_slot[idx] = -1;
            return idx;
        }
        // Steal oldest note.
        int oldest = 0;
        for (int i = 1; i < kVoices; i++) {
            if (timestamp[i] < timestamp[oldest]) oldest = i;
        }
        if (oldest == audition_idx) audition_idx = -1;
        voices[oldest].Trigger(false);
        pad_slot[oldest] = -1;
        return oldest;
    }
};

} // namespace synthux
