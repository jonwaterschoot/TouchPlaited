#pragma once
#include "plaits_voice.h"
#include <cstdint>

namespace synthux {

// Per-group (Basic Pitch / arp / Rec / drum, matching VoiceGroup below) send
// buses for Render() — 4 independent reverb + 4 independent delay sends
// (21/07/26 follow-up), indexed by VoiceGroup's underlying int. A struct of
// arrays rather than 16 positional params, same reasoning as VoiceParams.
struct FxBuses {
    float* rev_l[4];
    float* rev_r[4];
    float* dly_l[4];
    float* dly_r[4];
};

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

// Which per-group volume/width/FX-send set a voice draws from in Render, and
// whether it's skipped by the global per-block setters (see skip()). kBP is
// the only group those setters still reach — kArp/kRec/kDrum voices carry
// their sound as a snapshot/trigger param instead (arp_snd, rec_snd, or a
// drum slot), so a live BP knob write must never reach them.
enum class VoiceGroup : uint8_t { kBP, kArp, kRec, kDrum };

class VoicePool {
public:
    // Must match kMaxVoices in plaits_voice.cpp. 6 voices ≈ 180 KB SRAM.
    // Sleep keeps allocated-but-silent voices free; the CPU risk is only in
    // many *sounding* voices on expensive engines at once — watch the meter.
    static constexpr int kVoices = 6;

    // Max simultaneously-held Basic Pitch notes, per engine. Gate-held voices
    // are never touched by ShedVoice(), so once the held set alone exceeds the
    // shed threshold the guard is disabled by construction — crackle for as
    // long as the notes stay down, with nothing the pool can do about it.
    //
    // 4 was the confirmed-stable ceiling, but it was confirmed on cheap
    // engines. Measured on hardware 2026-08-07 with Six-Op C (engine 4), one
    // group, no FX: idle 16%, then 41 / 58 / 75 / 92% for one to four held
    // voices — ~17% per voice, so four held sits at 92-95% against a 90%
    // shed threshold that cannot fire. Worse, a fifth note makes it briefly
    // five *awake* voices: cap_bp_voices() releases the oldest but its tail
    // keeps rendering, so 92% + 17% ≈ 109%, against peaks of 101-121%
    // measured. That transient is the crackle, and it is why the fault seems
    // intermittent — it depends on whether a release tail is still sounding
    // when the next note lands. See notes.md, "Six-Op crackle".
    //
    // Capping the expensive engines at 3 leaves ~15% of headroom, about what
    // one overlapping release tail costs.
    static constexpr int kBPMaxHeld      = 4;   // default
    static constexpr int kBPMaxHeldHeavy = 3;   // expensive engines

    // Which engines get the lower cap. Six-Op C (4) is the measured one; the
    // rest are the engines the 2026-07-03 budget analysis flagged as
    // expensive, applied by inference rather than measurement. If one of them
    // turns out to be cheap it costs a fourth held note, not stability, so
    // this errs deliberately toward the safe side. Engine numbers per the
    // model table in notes.md.
    static bool engine_is_heavy(int e) {
        return e == 2 || e == 3 || e == 4   // Six-Op A / B / C
            || e == 15                      // Speech
            || e == 18                      // Particle
            || e == 19                      // String
            || e == 20;                     // Modal
    }

    void Init() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Init();
            pad_slot[i]     = -1;
            voice_engine[i] = 0;
            timestamp[i]    = 0;
            voice_volume[i] = 1.0f;
            voice_blend[i]  = 0.5f;
            voice_width[i]  = 1.0f;
            voice_rev_send[i] = 1.0f;
            voice_dly_send[i] = 1.0f;
            voice_group[i]  = VoiceGroup::kBP;
            awake[i]        = false;
            gate_held[i]    = false;
            quiet_chunks[i] = 0;
            gate_chunks[i]  = 0;
            protect_chunks[i] = 0;
        }
        tick         = 0;
        audition_idx = -1;
    }

    // Global setters skip param-locked voices (background drum-seq triggers) so
    // the active playmode's knobs can't stomp a drum sound mid-decay, and
    // arp-owned voices (arp/Rec-loop triggers), whose params are per-trigger
    // only — a background arp must not morph under the Basic Pitch knobs.
    // Each value is cached so NoteOn can rehydrate a reused voice — a voice
    // that was skipped when a global setter ran still holds stale params.
    // voice_engine is kept in step here as well as at trigger time: a live BP
    // voice really does change engine under this setter, and cap_bp_voices()
    // reads it to decide the ceiling.
    void SetEngine(int e)      { g_engine = e; for (int i = 0; i < kVoices; i++) if (!skip(i)) { voices[i].SetEngine(e); voice_engine[i] = static_cast<uint8_t>(e); } }
    void SetHarmonics(float v) { g_harm = v;   for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetHarmonics(v); }
    void SetTimbre(float v)    { g_timbre = v; for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetTimbre(v); }
    void SetMorph(float v)     { g_morph = v;  for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetMorph(v); }
    void SetDecay(float v)     { g_decay = v;  for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetDecay(v); }
    void SetBlend(float v)     { g_blend = v;  for (int i = 0; i < kVoices; i++) if (!skip(i)) voice_blend[i] = v; }

    void SetLPGColour(float v) { g_lpg = v;    for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetLPGColour(v); }
    void SetDrive(float v)     { g_drive = v;  for (int i = 0; i < kVoices; i++) if (!skip(i)) voices[i].SetDrive(v); }

    // Group volumes: Basic Pitch, the arp, Rec and the drum seq each have
    // separate output levels (20/07/26 notes: "each playmode has its own
    // volume... REC is considered separate"), so S36 in one never touches
    // another's level.
    void SetSeqVolume(float v)     { vol_seq = v; }
    void SetPitchedVolume(float v) { vol_pitched = v; }   // Basic Pitch
    void SetArpVolume(float v)     { vol_arp = v; }
    void SetRecVolume(float v)     { vol_rec = v; }

    // Group stereo width (see Render): 1 = natural OUT/AUX split, 0 = mono.
    // Not split per the 20/07/26 notes (volume + FX send only) — the arp and
    // Rec share Basic Pitch's width, same as before.
    void SetSeqWidth(float v)     { width_seq = v; }
    void SetPitchedWidth(float v) { width_pitched = v; }

    // Per-group FX send levels (see Render): each voice's post-volume/width
    // output also feeds the reverb and delay send buses, scaled by its
    // group's send × the voice's own send trim — dry drums under a wet
    // synth for free, like the volume/width pairs. Basic Pitch, the arp and
    // Rec each get their own (20/07/26 notes: separate send per playmode).
    void SetSeqReverbSend(float v)     { rev_send_seq = v; }
    void SetPitchedReverbSend(float v) { rev_send_pitched = v; }   // Basic Pitch
    void SetArpReverbSend(float v)     { rev_send_arp = v; }
    void SetRecReverbSend(float v)     { rev_send_rec = v; }
    void SetSeqDelaySend(float v)      { dly_send_seq = v; }
    void SetPitchedDelaySend(float v)  { dly_send_pitched = v; }   // Basic Pitch
    void SetArpDelaySend(float v)      { dly_send_arp = v; }
    void SetRecDelaySend(float v)      { dly_send_rec = v; }

    // Skip the audition voice — its FM stays at 0 for the full note duration.
    void SetFMAmount(float v) {
        for (int i = 0; i < kVoices; i++) {
            if (i != audition_idx && !skip(i)) voices[i].SetFMAmount(v);
        }
    }

    // Mode 1: note only; patch params driven globally by knobs each block.
    // Re-applies all cached globals first: the voice may have been a locked
    // drum-seq voice that missed every global setter since its trigger.
    void NoteOn(int slot, float note) {
        cap_bp_voices(g_engine);
        int idx = find_free_or_steal();
        voice_engine[idx] = static_cast<uint8_t>(g_engine);
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
        voice_group[idx]  = VoiceGroup::kBP;
        gate_chunks[idx]  = 0;   // gate held until the pad's NoteOff
        protect_chunks[idx] = 0;
        wake(idx, true);
    }

    // Modes 2/3: note + full patch snapshot; params persist until stolen.
    // grp selects both the render group (volume/FX-send set, see Render) and
    // whether the voice is isolated from the global per-block setters:
    // kBP is the only group those still reach. kDrum additionally pins
    // LPG/FM to drum defaults (one-shot triggers, no gate held). kArp/kRec
    // take LPG colour (CC25) from the cache at trigger time instead — their
    // sound is a snapshot (arp_snd/rec_snd) refreshed per trigger, never the
    // live knobs.
    void NoteOnWithParams(int slot, float note, const VoiceParams& p,
                          VoiceGroup grp = VoiceGroup::kBP) {
        bool lock_params = grp == VoiceGroup::kDrum;
        if (grp == VoiceGroup::kBP) cap_bp_voices(p.engine);
        int idx = find_free_or_steal();
        voice_engine[idx] = static_cast<uint8_t>(p.engine);
        voices[idx].SetEngine(p.engine);
        voices[idx].SetHarmonics(p.harmonics);
        voices[idx].SetTimbre(p.timbre);
        voices[idx].SetMorph(p.morph);
        voices[idx].SetDecay(p.decay);
        voices[idx].SetDrive(p.drive);
        if (lock_params) {
            voices[idx].SetLPGColour(0.5f);
            voices[idx].SetFMAmount(0.0f);
        } else if (grp != VoiceGroup::kBP) {
            // Isolated from the per-block globals, so take LPG colour (CC25)
            // from the cache at trigger time; FM stays off like any pitched voice.
            voices[idx].SetLPGColour(g_lpg);
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
        voice_group[idx] = grp;
        // Locked (drum-seq) voices are one-shots: gate drops on a decay-scaled
        // timer instead of a NoteOff, and they're free to sleep as soon as
        // their tail decays.
        gate_chunks[idx]  = lock_params ? one_shot_gate_chunks(p.decay) : 0;
        protect_chunks[idx] = is_kick_voice(slot) ? kProtectChunks : 0;
        wake(idx, !lock_params);
    }

    void NoteOff(int slot) {
        for (int i = 0; i < kVoices; i++) {
            if (pad_slot[i] == slot) {
                voices[i].Trigger(false);
                pad_slot[i]  = -1;
                // Only a drum-seq voice returns to the global setters' reach
                // here — an arp/Rec voice's isolation outlives its NoteOff
                // (matches the pre-refactor locked/arp_owned split: NoteOff
                // only ever cleared "locked", never "arp_owned").
                if (voice_group[i] == VoiceGroup::kDrum) voice_group[i] = VoiceGroup::kBP;
                gate_held[i] = false;   // release tail may now decay to sleep
                gate_chunks[i] = 0;
                protect_chunks[i] = 0;
            }
        }
    }

    void AllNotesOff() {
        for (int i = 0; i < kVoices; i++) {
            voices[i].Trigger(false);
            pad_slot[i]  = -1;
            voice_group[i] = VoiceGroup::kBP;
            gate_held[i] = false;
            gate_chunks[i] = 0;
            protect_chunks[i] = 0;
        }
        audition_idx = -1;
    }

    // One-shot preview with neutral params. FM=0 is maintained by SetFMAmount skipping this voice.
    void Audition(float note, int engine = -1, VoiceGroup grp = VoiceGroup::kBP) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        voice_group[idx] = grp;
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
        protect_chunks[idx] = 0;
        wake(idx, false);   // auditions are one-shots — sleep after decay
    }

    // Preview using a specific slot's patch — P0+P2 hold feedback and rec-mode
    // auditions. volume defaults to full; rec passes the slot's stored volume
    // so S36 edits are audible while recording, not only after confirm.
    // grp routes the voice through that group's volume/width/FX sends in
    // Render — e.g. drum-slot auditions (kDrum) must sound like seq triggers,
    // and the pitched fader may be sitting at zero; arp/Rec sound-edit
    // auditions (kArp/kRec) must preview at that group's own level. Any
    // non-kBP group also shields the voice from the global setters (like any
    // locked voice). Drive is taken from the params, not the global cache:
    // for kBP auditions the per-block SetDrive overwrites it anyway, and a
    // locked/owned audition must keep the drive its params were shaped with.
    void AuditionWithParams(float note, const VoiceParams& p, VoiceGroup grp = VoiceGroup::kBP) {
        int idx = find_free_or_steal();
        audition_idx = idx;
        voice_group[idx] = grp;
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
        protect_chunks[idx] = 0;
        wake(idx, false);   // auditions are one-shots — sleep after decay
    }

    // Sleeping voices are skipped entirely — this is what makes a bigger pool
    // affordable: an allocated-but-silent voice costs nothing. A voice sleeps
    // after kQuietChunks consecutive chunks below kSilenceThresh with its gate
    // off, and wakes on the next trigger. Gate-held voices never sleep, so a
    // held pad on a quiet engine region still responds to knob sweeps.
    void Render(float* out_left, float* out_right,
                const FxBuses& buses, size_t size) {
        static float tmp_l[24], tmp_r[24];
        for (int i = 0; i < kVoices; i++) {
            // One-shot gate countdown runs even for sleeping voices so a
            // silent-while-gated voice can't keep a stale gate forever.
            if (gate_chunks[i] && --gate_chunks[i] == 0) {
                voices[i].Trigger(false);
            }
            // Runs for sleeping voices too, like the gate countdown above: a
            // kick that decayed inside its own protection window must not stay
            // protected once it is silent.
            if (protect_chunks[i]) --protect_chunks[i];
            if (!awake[i]) continue;
            __builtin_memset(tmp_l, 0, size * sizeof(float));
            __builtin_memset(tmp_r, 0, size * sizeof(float));
            voices[i].Render(tmp_l, tmp_r, size);
            // Volume and FX send are per-group (Basic Pitch / arp / Rec /
            // drum seq, each independent); width stays a two-way drum-vs-
            // everything-else split, unchanged from before this group split.
            float g_vol, rs, ds;
            switch (voice_group[i]) {
                case VoiceGroup::kDrum: g_vol = vol_seq;     rs = rev_send_seq;     ds = dly_send_seq;     break;
                case VoiceGroup::kArp:  g_vol = vol_arp;     rs = rev_send_arp;     ds = dly_send_arp;     break;
                case VoiceGroup::kRec:  g_vol = vol_rec;     rs = rev_send_rec;     ds = dly_send_rec;     break;
                default:                g_vol = vol_pitched; rs = rev_send_pitched; ds = dly_send_pitched; break;
            }
            float vol  = voice_volume[i] * g_vol;
            // Plaits renders two different signals per engine (OUT and AUX).
            // Blend picks the mono mix between them; width crossfades from
            // that mono mix (0) toward the raw OUT-left/AUX-right split (1).
            // Per-voice and group widths multiply, so a slot set to mono
            // stays dead center whatever the group width does.
            float b    = voice_blend[i];
            bool  is_drum = voice_group[i] == VoiceGroup::kDrum;
            float w    = voice_width[i] * (is_drum ? width_seq : width_pitched);
            float peak = 0.f;
            float rsv = rs * voice_rev_send[i] * vol;
            float dsv = ds * voice_dly_send[i] * vol;
            int g = static_cast<int>(voice_group[i]);
            float* rl = buses.rev_l[g]; float* rr = buses.rev_r[g];
            float* dl = buses.dly_l[g]; float* dr = buses.dly_r[g];
            for (size_t s = 0; s < size; s++) {
                float m = tmp_l[s] + b * (tmp_r[s] - tmp_l[s]);
                float l = m + w * (tmp_l[s] - m);
                float r = m + w * (tmp_r[s] - m);
                out_left[s]  += l * vol;
                out_right[s] += r * vol;
                rl[s] += l * rsv;
                rr[s] += r * rsv;
                dl[s] += l * dsv;
                dr[s] += r * dsv;
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
        // Same kick preference as find_free_or_steal(): shedding the downbeat
        // to save a hat is the wrong trade, so the kick is the last thing
        // fading early — but it is still sheddable if it is all there is.
        int victim = -1;
        for (int pass = 0; pass < 2 && victim < 0; pass++) {
            for (int i = 0; i < kVoices; i++) {
                if (!awake[i] || gate_held[i]) continue;
                if (pass == 0 && is_kick_voice(pad_slot[i])) continue;
                if (victim < 0 || timestamp[i] < timestamp[victim]) victim = i;
            }
        }
        if (victim < 0) return false;
        voices[victim].Trigger(false);
        awake[victim]    = false;
        pad_slot[victim] = -1;
        voice_group[victim] = VoiceGroup::kBP;
        gate_chunks[victim] = 0;
        protect_chunks[victim] = 0;
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
    uint8_t  voice_engine[kVoices];  // engine 0..23, for cap_bp_voices()
    uint32_t timestamp[kVoices];
    float    voice_volume[kVoices];
    float    voice_blend[kVoices];
    float    voice_width[kVoices];
    float    voice_rev_send[kVoices];
    float    voice_dly_send[kVoices];
    VoiceGroup voice_group[kVoices]; // render group + global-setter isolation, see VoiceGroup
    bool     awake[kVoices];
    bool     gate_held[kVoices];
    uint32_t quiet_chunks[kVoices];
    uint16_t gate_chunks[kVoices];   // one-shot gate countdown; 0 = no timer
    // Kick steal-protection countdown, in chunks; 0 = not protected. See
    // kProtectChunks — this is a window after the strike, deliberately NOT the
    // length of the sound.
    uint16_t protect_chunks[kVoices];
    uint32_t tick;
    int      audition_idx;

    // How long a kick voice is protected from being stolen: 150 ms, fixed.
    //
    // The first version of this keyed on `awake[i]`, i.e. protected the kick
    // for as long as it was still making sound. With the kick lab's presets
    // that is 0.4-1.4 s, which on a four-on-the-floor is most of the bar — so
    // one or two of six voices became effectively *reserved*, and the other
    // six drums thrashed what was left. Reported from hardware as "everything
    // except the kick is super short", with the tell that a freshly randomized
    // kit sounded right for about a bar first: for that bar the oldest voices
    // still belonged to the previous kit and had already finished, so stealing
    // them was free.
    //
    // What the protection is actually for is the *strike* — the attack and
    // body, the part whose loss reads as a dropped downbeat. The ring-out is
    // not worth a hat. 150 ms covers the transient of every preset in the bank
    // and releases well before the next kick at any usable tempo.
    static constexpr uint16_t kProtectChunks = 300;   // 150 ms at 0.5 ms/chunk

    // Voices the global setters must not touch — anything but kBP.
    bool skip(int i) const { return voice_group[i] != VoiceGroup::kBP; }

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

    // Per-group output levels (see SetSeqVolume/SetPitchedVolume/etc.).
    float vol_seq = 1.0f, vol_pitched = 1.0f, vol_arp = 1.0f, vol_rec = 1.0f;
    // Per-group stereo width (see SetSeqWidth/SetPitchedWidth) — only the
    // drum-vs-everything-else split exists; arp/Rec share width_pitched.
    float width_seq = 1.0f, width_pitched = 1.0f;
    // Per-group FX send levels (see SetSeqReverbSend etc.). 0 = dry.
    float rev_send_seq = 0.0f, rev_send_pitched = 0.0f, rev_send_arp = 0.0f, rev_send_rec = 0.0f;
    float dly_send_seq = 0.0f, dly_send_pitched = 0.0f, dly_send_arp = 0.0f, dly_send_rec = 0.0f;

    // Releases the oldest held Basic Pitch voice when a new one would push
    // the held count past the engine's ceiling — same effect as that voice getting a
    // NoteOff early. Called before find_free_or_steal() so the incoming note
    // never counts as the release candidate.
    // `incoming` is the engine of the note about to be triggered — the cap has
    // to account for it, since it is the voice that pushes the block over.
    // The held set is scanned for heavy engines too: with bp_slots_active each
    // pad carries its own snapshot, so a chord can mix engines, and one heavy
    // voice already sounding is enough to want the lower ceiling.
    void cap_bp_voices(int incoming) {
        bool heavy = engine_is_heavy(incoming);
        int  count = 0;
        for (int i = 0; i < kVoices; i++) {
            if (gate_held[i] && voice_group[i] == VoiceGroup::kBP) {
                count++;
                if (engine_is_heavy(voice_engine[i])) heavy = true;
            }
        }
        const int cap = heavy ? kBPMaxHeldHeavy : kBPMaxHeld;

        // A loop, not a single release: the cap is no longer a constant, so
        // switching from a cheap engine to a heavy one mid-chord can leave
        // more voices held than the new ceiling allows, and releasing one
        // would still leave it over.
        while (count >= cap) {
            int oldest = -1;
            for (int i = 0; i < kVoices; i++) {
                if (gate_held[i] && voice_group[i] == VoiceGroup::kBP
                    && (oldest < 0 || timestamp[i] < timestamp[oldest]))
                    oldest = i;
            }
            if (oldest < 0) break;
            voices[oldest].Trigger(false);
            gate_held[oldest] = false;
            count--;
        }
    }

    // The kick's own voice id (16 + slot 0) and its optional layered second
    // voice — see the kick lab in TouchPlaited.cpp.
    static bool is_kick_voice(int slot) { return slot == 16 || slot == 24; }

    // Oldest voice matching a predicate, or -1.
    template <typename Pred>
    int oldest_where(Pred ok) const {
        int best = -1;
        for (int i = 0; i < kVoices; i++) {
            if (!ok(i)) continue;
            if (best < 0 || timestamp[i] < timestamp[best]) best = i;
        }
        return best;
    }

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
        // A voice that has finished and gone to sleep is silent, so taking it
        // costs nothing — and there is usually one. Drum voices never get a
        // NoteOff, so their pad_slot stays set forever and the free-slot scan
        // above can never see them; without this pass the pool went straight
        // to stealing the oldest *sounding* voice while a finished one sat
        // there unused. That is the single biggest cause of drum tails being
        // cut, and it is independent of the kick.
        int victim = oldest_where([&](int i) { return !awake[i]; });
        // Then the oldest voice that is not a kick inside its protection
        // window (below). Only if every voice is a protected kick does the
        // plain oldest-first rule take over, so the pool can never wedge.
        if (victim < 0) victim = oldest_where([&](int i) {
            return !(is_kick_voice(pad_slot[i]) && protect_chunks[i]);
        });
        if (victim < 0) victim = oldest_where([](int) { return true; });
        if (victim == audition_idx) audition_idx = -1;
        voices[victim].Trigger(false);
        pad_slot[victim] = -1;
        return victim;
    }
};

} // namespace synthux
