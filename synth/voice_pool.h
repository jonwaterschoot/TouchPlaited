#pragma once
#include "plaits_voice.h"
#include <cstdint>

namespace synthux {

// Full per-voice patch snapshot for NoteOnWithParams / AuditionWithParams.
// Passed as a struct — the positional-parameter version grew past ten args
// and call sites were becoming error-prone.
struct VoiceParams {
    int   engine    = 0;
    float harmonics = 0.5f;
    float timbre    = 0.5f;
    float morph     = 0.5f;
    float decay     = 0.5f;
    float volume    = 1.0f;
    float drive     = 0.0f;
    float blend     = 0.5f;   // OUT↔AUX mono mix: 0 = OUT only, 1 = AUX only
    float width     = 1.0f;   // this voice's share of the group stereo width
    float rev_send  = 1.0f;   // per-voice trim on the group FX sends —
    float dly_send  = 1.0f;   // multiplies like volume/width; 1 = follow fully
};

class VoicePool {
public:
    // Must match kMaxVoices in plaits_voice.cpp. 6 voices ≈ 180 KB SRAM.
    // Sleep keeps allocated-but-silent voices free; the CPU risk is only in
    // many *sounding* voices on expensive engines at once — watch the meter.
    static constexpr int kVoices = 6;

    void Init() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Init();
            pad_slot[i]    = -1;
            timestamp[i]   = 0;
            voice_volume[i] = 1.0f;
            voice_blend[i]  = 0.5f;
            voice_width[i]  = 1.0f;
            voice_rev_send[i] = 1.0f;
            voice_dly_send[i] = 1.0f;
            locked[i]       = false;
            awake[i]        = false;
            gate_held[i]    = false;
            quiet_chunks[i] = 0;
            gate_chunks[i]  = 0;
        }
        tick         = 0;
        audition_idx = -1;
    }

    // Global setters skip param-locked voices (background drum-seq triggers) so
    // the active playmode's knobs can't stomp a drum sound mid-decay. Each value
    // is cached so NoteOn can rehydrate a reused voice — a voice that was locked
    // when a global setter ran still holds stale (drum) params otherwise.
    void SetEngine(int e)      { g_engine = e; for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetEngine(e); }
    void SetHarmonics(float v) { g_harm = v;   for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetHarmonics(v); }
    void SetTimbre(float v)    { g_timbre = v; for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetTimbre(v); }
    void SetMorph(float v)     { g_morph = v;  for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetMorph(v); }
    void SetDecay(float v)     { g_decay = v;  for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetDecay(v); }
    void SetBlend(float v)     { g_blend = v;  for (int i = 0; i < kVoices; i++) if (!locked[i]) voice_blend[i] = v; }

    void SetLPGColour(float v) { g_lpg = v;    for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetLPGColour(v); }
    void SetDrive(float v)     { g_drive = v;  for (int i = 0; i < kVoices; i++) if (!locked[i]) voices[i].SetDrive(v); }

    // Group volumes: locked voices (drum seq) and pitched voices have separate
    // output levels, so S36 in Seq mode balances the drums against the synth
    // without touching the pitched level, and vice versa.
    void SetSeqVolume(float v)     { vol_seq = v; }
    void SetPitchedVolume(float v) { vol_pitched = v; }

    // Group stereo width (see Render): 1 = natural OUT/AUX split, 0 = mono.
    void SetSeqWidth(float v)     { width_seq = v; }
    void SetPitchedWidth(float v) { width_pitched = v; }

    // Per-group FX send levels (see Render): each voice's post-volume/width
    // output also feeds the reverb and delay send buses, scaled by its
    // group's send × the voice's own send trim — dry drums under a wet
    // synth for free, like the volume/width pairs.
    void SetSeqReverbSend(float v)     { rev_send_seq = v; }
    void SetPitchedReverbSend(float v) { rev_send_pitched = v; }
    void SetSeqDelaySend(float v)      { dly_send_seq = v; }
    void SetPitchedDelaySend(float v)  { dly_send_pitched = v; }

    // Skip the audition voice — its FM stays at 0 for the full note duration.
    void SetFMAmount(float v) {
        for (int i = 0; i < kVoices; i++) {
            if (i != audition_idx && !locked[i]) voices[i].SetFMAmount(v);
        }
    }

    // Mode 1: note only; patch params driven globally by knobs each block.
    // Re-applies all cached globals first: the voice may have been a locked
    // drum-seq voice that missed every global setter since its trigger.
    void NoteOn(int slot, float note) {
        int idx = find_free_or_steal();
        voices[idx].SetEngine(g_engine);
        voices[idx].SetHarmonics(g_harm);
        voices[idx].SetTimbre(g_timbre);
        voices[idx].SetMorph(g_morph);
        voices[idx].SetDecay(g_decay);
        voices[idx].SetLPGColour(g_lpg);
        voices[idx].SetDrive(g_drive);
        voices[idx].SetFMAmount(0.0f);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]    = slot;
        timestamp[idx]   = ++tick;
        voice_volume[idx] = 1.0f;
        voice_blend[idx]  = g_blend;
        voice_width[idx]  = 1.0f;
        voice_rev_send[idx] = 1.0f;
        voice_dly_send[idx] = 1.0f;
        locked[idx]       = false;
        gate_chunks[idx]  = 0;   // gate held until the pad's NoteOff
        wake(idx, true);
    }

    // Modes 2/3: note + full patch snapshot; params persist until stolen.
    // lock_params=true isolates the voice from all global setters (drum-seq
    // triggers playing behind a pitched mode) and pins LPG/FM to drum defaults.
    void NoteOnWithParams(int slot, float note, const VoiceParams& p,
                          bool lock_params = false) {
        int idx = find_free_or_steal();
        voices[idx].SetEngine(p.engine);
        voices[idx].SetHarmonics(p.harmonics);
        voices[idx].SetTimbre(p.timbre);
        voices[idx].SetMorph(p.morph);
        voices[idx].SetDecay(p.decay);
        voices[idx].SetDrive(p.drive);
        if (lock_params) {
            voices[idx].SetLPGColour(0.5f);
            voices[idx].SetFMAmount(0.0f);
        }
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]    = slot;
        timestamp[idx]   = ++tick;
        voice_volume[idx] = p.volume;
        voice_blend[idx]  = p.blend;
        voice_width[idx]  = p.width;
        voice_rev_send[idx] = p.rev_send;
        voice_dly_send[idx] = p.dly_send;
        locked[idx]       = lock_params;
        // Locked (drum-seq) voices are one-shots: gate drops on a decay-scaled
        // timer instead of a NoteOff, and they're free to sleep as soon as
        // their tail decays.
        gate_chunks[idx]  = lock_params ? one_shot_gate_chunks(p.decay) : 0;
        wake(idx, !lock_params);
    }

    void NoteOff(int slot) {
        for (int i = 0; i < kVoices; i++) {
            if (pad_slot[i] == slot) {
                voices[i].Trigger(false);
                pad_slot[i]  = -1;
                locked[i]    = false;
                gate_held[i] = false;   // release tail may now decay to sleep
                gate_chunks[i] = 0;
            }
        }
    }

    void AllNotesOff() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Trigger(false);
            pad_slot[i]  = -1;
            locked[i]    = false;
            gate_held[i] = false;
            gate_chunks[i] = 0;
        }
        audition_idx = -1;
    }

    // One-shot preview with neutral params. FM=0 is maintained by SetFMAmount skipping this voice.
    void Audition(float note, int engine = -1) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        locked[idx]  = false;
        // engine < 0 = "current global engine" — never trust the recycled
        // voice's own engine, it may be a stale drum-seq voice.
        voices[idx].SetEngine(engine >= 0 ? engine : g_engine);
        voices[idx].SetHarmonics(0.5f);
        voices[idx].SetTimbre(0.5f);
        voices[idx].SetMorph(0.5f);
        voices[idx].SetDecay(0.6f);
        voices[idx].SetLPGColour(g_lpg);
        voices[idx].SetDrive(g_drive);
        voices[idx].SetFMAmount(0.0f);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]    = -1;
        timestamp[idx]   = ++tick;
        voice_volume[idx] = 1.0f;
        voice_blend[idx]  = g_blend;
        voice_width[idx]  = 1.0f;
        voice_rev_send[idx] = 1.0f;
        voice_dly_send[idx] = 1.0f;
        gate_chunks[idx]  = one_shot_gate_chunks(0.6f);  // matches SetDecay above
        wake(idx, false);   // auditions are one-shots — sleep after decay
    }

    // Preview using a specific slot's patch — P0+P2 hold feedback and rec-mode
    // auditions. volume defaults to full; rec passes the slot's stored volume
    // so S36 edits are audible while recording, not only after confirm.
    // seq_group routes the voice through the drum group's volume/width/FX
    // sends in Render — drum-slot auditions must sound like seq triggers, and
    // the pitched fader may be sitting at zero. It rides on `locked`, which
    // also shields the voice from the global setters (like any drum voice).
    // Drive is taken from the params, not the global cache: for unlocked
    // (pitched) auditions the per-block SetDrive overwrites it anyway, and a
    // locked drum audition must keep the drive its params were shaped with.
    void AuditionWithParams(float note, const VoiceParams& p, bool seq_group = false) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        locked[idx]  = seq_group;
        voices[idx].SetEngine(p.engine);
        voices[idx].SetHarmonics(p.harmonics);
        voices[idx].SetTimbre(p.timbre);
        voices[idx].SetMorph(p.morph);
        voices[idx].SetDecay(p.decay);
        voices[idx].SetLPGColour(g_lpg);
        voices[idx].SetDrive(p.drive);
        voices[idx].SetFMAmount(0.0f);
        voices[idx].SetNote(note);
        voices[idx].Trigger(true);
        pad_slot[idx]    = -1;
        timestamp[idx]   = ++tick;
        voice_volume[idx] = p.volume;
        voice_blend[idx]  = p.blend;
        voice_width[idx]  = p.width;
        voice_rev_send[idx] = p.rev_send;
        voice_dly_send[idx] = p.dly_send;
        gate_chunks[idx]  = one_shot_gate_chunks(p.decay);
        wake(idx, false);   // auditions are one-shots — sleep after decay
    }

    // Sleeping voices are skipped entirely — this is what makes a bigger pool
    // affordable: an allocated-but-silent voice costs nothing. A voice sleeps
    // after kQuietChunks consecutive chunks below kSilenceThresh with its gate
    // off, and wakes on the next trigger. Gate-held voices never sleep, so a
    // held pad on a quiet engine region still responds to knob sweeps.
    void Render(float* out_left, float* out_right,
                float* rev_left, float* rev_right,
                float* dly_left, float* dly_right, size_t size) {
        static float tmp_l[24], tmp_r[24];
        for (int i = 0; i < kVoices; i++) {
            // One-shot gate countdown runs even for sleeping voices so a
            // silent-while-gated voice can't keep a stale gate forever.
            if (gate_chunks[i] && --gate_chunks[i] == 0) {
                voices[i].Trigger(false);
            }
            if (!awake[i]) continue;
            __builtin_memset(tmp_l, 0, size * sizeof(float));
            __builtin_memset(tmp_r, 0, size * sizeof(float));
            voices[i].Render(tmp_l, tmp_r, size);
            float vol  = voice_volume[i] * (locked[i] ? vol_seq : vol_pitched);
            // Plaits renders two different signals per engine (OUT and AUX).
            // Blend picks the mono mix between them; width crossfades from
            // that mono mix (0) toward the raw OUT-left/AUX-right split (1).
            // Per-voice and group widths multiply, so a slot set to mono
            // stays dead center whatever the group width does.
            float b    = voice_blend[i];
            float w    = voice_width[i] * (locked[i] ? width_seq : width_pitched);
            float rs   = locked[i] ? rev_send_seq : rev_send_pitched;
            float ds   = locked[i] ? dly_send_seq : dly_send_pitched;
            float peak = 0.f;
            float rsv = rs * voice_rev_send[i] * vol;
            float dsv = ds * voice_dly_send[i] * vol;
            for (size_t s = 0; s < size; s++) {
                float m = tmp_l[s] + b * (tmp_r[s] - tmp_l[s]);
                float l = m + w * (tmp_l[s] - m);
                float r = m + w * (tmp_r[s] - m);
                out_left[s]  += l * vol;
                out_right[s] += r * vol;
                rev_left[s]  += l * rsv;
                rev_right[s] += r * rsv;
                dly_left[s]  += l * dsv;
                dly_right[s] += r * dsv;
                float a = l < 0.f ? -l : l;
                float b = r < 0.f ? -r : r;
                if (a > peak) peak = a;
                if (b > peak) peak = b;
            }
            if (!gate_held[i] && peak < kSilenceThresh) {
                if (++quiet_chunks[i] >= kQuietChunks) awake[i] = false;
            } else {
                quiet_chunks[i] = 0;
            }
        }
    }

    // Emergency load shed: force-sleep the oldest awake voice that isn't
    // gate-held. Called when the previous audio block ran too close to the
    // budget — an early tail fade instead of a buffer overrun.
    bool ShedVoice() {
        int victim = -1;
        for (int i = 0; i < kVoices; i++) {
            if (awake[i] && !gate_held[i]) {
                if (victim < 0 || timestamp[i] < timestamp[victim]) victim = i;
            }
        }
        if (victim < 0) return false;
        voices[victim].Trigger(false);
        awake[victim]    = false;
        pad_slot[victim] = -1;
        locked[victim]   = false;
        gate_chunks[victim] = 0;
        if (victim == audition_idx) audition_idx = -1;
        return true;
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

    // Live blend/width edits on the current audition voice (rec mode S37 / P0+S37).
    void UpdateAuditionBlend(float b) {
        if (audition_idx >= 0) voice_blend[audition_idx] = b;
    }
    void UpdateAuditionWidth(float w) {
        if (audition_idx >= 0) voice_width[audition_idx] = w;
    }
    void UpdateAuditionSends(float rev, float dly) {
        if (audition_idx >= 0) {
            voice_rev_send[audition_idx] = rev;
            voice_dly_send[audition_idx] = dly;
        }
    }

    PlaitsVoice voices[kVoices];

private:
    // Sleep tuning: chunks are 24 samples (0.5ms at 48kHz); 64 quiet chunks =
    // 32ms of sustained silence below -80dBFS before a voice stops rendering.
    static constexpr float    kSilenceThresh = 1e-4f;
    static constexpr uint32_t kQuietChunks   = 64;

    int      pad_slot[kVoices];
    uint32_t timestamp[kVoices];
    float    voice_volume[kVoices];
    float    voice_blend[kVoices];
    float    voice_width[kVoices];
    float    voice_rev_send[kVoices];
    float    voice_dly_send[kVoices];
    bool     locked[kVoices];
    bool     awake[kVoices];
    bool     gate_held[kVoices];
    uint32_t quiet_chunks[kVoices];
    uint16_t gate_chunks[kVoices];   // one-shot gate countdown; 0 = no timer
    uint32_t tick;
    int      audition_idx;

    void wake(int idx, bool hold_gate) {
        awake[idx]        = true;
        gate_held[idx]    = hold_gate;
        quiet_chunks[idx] = 0;
    }

    // Gate hold for voices that never get a NoteOff (drum-seq triggers,
    // auditions). Only the six-op FM engines (2-4) read gate *length* — real
    // DX7 key-on/key-off — so this shapes their note duration; every other
    // engine only uses the rising edge and ignores the timer. Chunks are
    // 24 samples (0.5 ms at 48 kHz): 20 ms floor + up to 380 ms with decay.
    static uint16_t one_shot_gate_chunks(float decay) {
        return static_cast<uint16_t>((20.0f + decay * 380.0f) * 2.0f);
    }

    // Cached global params — last value passed to each global setter.
    int   g_engine = 0;
    float g_harm  = 0.5f, g_timbre = 0.5f, g_morph = 0.5f, g_decay = 0.5f;
    float g_lpg   = 0.5f, g_drive  = 0.0f, g_blend = 0.5f;

    // Per-group output levels (see SetSeqVolume/SetPitchedVolume).
    float vol_seq = 1.0f, vol_pitched = 1.0f;
    // Per-group stereo width (see SetSeqWidth/SetPitchedWidth).
    float width_seq = 1.0f, width_pitched = 1.0f;
    // Per-group FX send levels (see SetSeqReverbSend etc.). 0 = dry.
    float rev_send_seq = 0.0f, rev_send_pitched = 0.0f;
    float dly_send_seq = 0.0f, dly_send_pitched = 0.0f;

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
