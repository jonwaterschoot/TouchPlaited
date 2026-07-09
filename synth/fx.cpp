#include "fx.h"

#include "dev/sdram.h"   // DSY_SDRAM_BSS
#include "plaits/dsp/fx/fx_engine.h"

#include <cstring>

namespace synthux {

namespace {

// ── Buffers (SDRAM) ───────────────────────────────────────────────────────────
// .sdram_bss is NOLOAD — nothing zeroes it at startup, Init() must.
// A delay line is one sequential read + write per sample: cache-friendly, so
// SDRAM latency is not the per-sample tax it was for the physical-model
// engines (see notes.md FX resource analysis).
constexpr size_t kReverbBufSize = 32768;               // uint16 words, 64 KB
constexpr size_t kDelayBufSize  = 65536;               // floats/ch, ~1.36 s at 48 kHz
constexpr size_t kDelayMask     = kDelayBufSize - 1;

DSY_SDRAM_BSS uint16_t reverb_buffer[kReverbBufSize];
DSY_SDRAM_BSS float    delay_buffer_l[kDelayBufSize];
DSY_SDRAM_BSS float    delay_buffer_r[kDelayBufSize];

// FX sleep threshold — same figure as VoicePool voice sleep (−80 dBFS).
constexpr float kSilenceThresh = 1e-4f;

inline float peak_of(const float* a, const float* b, size_t size) {
    float p = 0.f;
    for (size_t i = 0; i < size; i++) {
        float x = a[i] < 0.f ? -a[i] : a[i];
        float y = b[i] < 0.f ? -b[i] : b[i];
        if (x > p) p = x;
        if (y > p) p = y;
    }
    return p;
}

// ── Reverb ────────────────────────────────────────────────────────────────────
// Griesinger topology from the Dattorro paper (4 AP diffusers on the input,
// then a loop of 2x 2AP+1Delay), adapted from Rings' dsp/fx/reverb.h
// (Emilie Gillet, MIT — same author/license as the vendored Plaits tree, and
// built on the FxEngine base class Plaits already ships). Changes: input is
// the send bus, wet is *added* to the main outs (send/return instead of
// Rings' in-place dry/wet crossfade).
class Reverb {
public:
    void Init(float sample_rate) {
        engine_.Init(reverb_buffer);
        engine_.Clear();
        engine_.SetLFOFrequency(plaits::LFO_1, 0.5f / sample_rate);
        engine_.SetLFOFrequency(plaits::LFO_2, 0.3f / sample_rate);
        input_gain_  = 0.2f;
        diffusion_   = 0.625f;
        reverb_time_ = 0.5f;
        lp_          = 0.7f;
        lp_decay_1_  = 0.f;
        lp_decay_2_  = 0.f;
        asleep_       = true;
        quiet_blocks_ = 0;
    }

    void set_time(float t) { reverb_time_ = t; }
    void set_lp(float lp)  { lp_ = lp; }

    void Process(const float* send_l, const float* send_r,
                 float* main_l, float* main_r, size_t size) {
        float in_peak = peak_of(send_l, send_r, size);
        if (asleep_) {
            if (in_peak < kSilenceThresh) return;
            asleep_       = false;
            quiet_blocks_ = 0;
        }

        typedef E::Reserve<150,
            E::Reserve<214,
            E::Reserve<319,
            E::Reserve<527,
            E::Reserve<2182,
            E::Reserve<2690,
            E::Reserve<4501,
            E::Reserve<2525,
            E::Reserve<2197,
            E::Reserve<6312> > > > > > > > > > Memory;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> dap2a;
        E::DelayLine<Memory, 8> dap2b;
        E::DelayLine<Memory, 9> del2;
        E::Context c;

        const float kap  = diffusion_;
        const float klp  = lp_;
        const float krt  = reverb_time_;
        const float gain = input_gain_;

        float lp_1     = lp_decay_1_;
        float lp_2     = lp_decay_2_;
        float wet_peak = 0.f;

        for (size_t i = 0; i < size; i++) {
            float wet;
            float apout = 0.0f;
            engine_.Start(&c);

            c.Read(send_l[i] + send_r[i], gain);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2 TAIL, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3 TAIL, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4 TAIL, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate(del2, 6261.0f, plaits::LFO_2, 50.0f, krt);
            c.Lp(lp_1, klp);
            c.Read(dap1a TAIL, -kap);
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b TAIL, kap);
            c.WriteAllPass(dap1b, -kap);
            c.Write(del1, 2.0f);
            c.Write(wet, 0.0f);

            main_l[i] += wet;
            float w = wet < 0.f ? -wet : wet;
            if (w > wet_peak) wet_peak = w;

            c.Load(apout);
            c.Interpolate(del1, 4460.0f, plaits::LFO_1, 40.0f, krt);
            c.Lp(lp_2, klp);
            c.Read(dap2a TAIL, kap);
            c.WriteAllPass(dap2a, -kap);
            c.Read(dap2b TAIL, -kap);
            c.WriteAllPass(dap2b, kap);
            c.Write(del2, 2.0f);
            c.Write(wet, 0.0f);

            main_r[i] += wet;
            w = wet < 0.f ? -wet : wet;
            if (w > wet_peak) wet_peak = w;
        }

        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;

        // Sleep once input and tail are both silent for longer than one pass
        // around the loop (del1+del2 ≈ 10.8 k samples), so nothing audible is
        // still circulating when rendering stops.
        if (in_peak < kSilenceThresh && wet_peak < kSilenceThresh) {
            if (++quiet_blocks_ >= kQuietBlocks) asleep_ = true;
        } else {
            quiet_blocks_ = 0;
        }
    }

private:
    typedef plaits::FxEngine<kReverbBufSize, plaits::FORMAT_16_BIT> E;
    E engine_;

    static constexpr uint32_t kQuietBlocks = 96;   // × 192 samples ≈ 380 ms

    float input_gain_;
    float reverb_time_;
    float diffusion_;
    float lp_;
    float lp_decay_1_;
    float lp_decay_2_;

    bool     asleep_;
    uint32_t quiet_blocks_;
};

// ── Stereo delay ──────────────────────────────────────────────────────────────
// Cross-feedback (ping-pong flavored) delay with one-pole damping in the
// loop. Time changes slew per-sample (tape-style pitch bend) unless the
// delay is asleep, in which case they snap — the character switch happens
// through the silent center of the mirror knob, so no artifact is audible.
class StereoDelay {
public:
    void Init(float sample_rate) {
        memset(delay_buffer_l, 0, sizeof(delay_buffer_l));
        memset(delay_buffer_r, 0, sizeof(delay_buffer_r));
        sample_rate_  = sample_rate;
        write_        = 0;
        time_         = 0.120f * sample_rate;
        target_time_  = time_;
        feedback_     = 0.f;
        damp_         = 0.8f;
        lp_l_ = lp_r_ = 0.f;
        asleep_       = true;
        quiet_blocks_ = 0;
    }

    void set_target_time(float samples) {
        if (samples < kMinTime) samples = kMinTime;
        if (samples > kMaxTime) samples = kMaxTime;
        target_time_ = samples;
        if (asleep_) time_ = samples;   // snap while silent
    }
    void set_feedback(float f) { feedback_ = f > 0.75f ? 0.75f : f; }
    void set_damp(float d)     { damp_ = d; }

    void Process(const float* send_l, const float* send_r,
                 float* main_l, float* main_r, size_t size) {
        float in_peak = peak_of(send_l, send_r, size);
        if (asleep_) {
            if (in_peak < kSilenceThresh) return;
            asleep_       = false;
            quiet_blocks_ = 0;
        }

        float out_peak = 0.f;
        for (size_t i = 0; i < size; i++) {
            time_ += kTimeSlew * (target_time_ - time_);

            // + kDelayBufSize keeps pos positive, so truncation is floor.
            float    pos  = static_cast<float>(write_ + kDelayBufSize) - time_;
            uint32_t ipos = static_cast<uint32_t>(pos);
            float    frac = pos - static_cast<float>(ipos);
            uint32_t p0   = ipos & kDelayMask;
            uint32_t p1   = (p0 + 1) & kDelayMask;

            float dl = delay_buffer_l[p0] + (delay_buffer_l[p1] - delay_buffer_l[p0]) * frac;
            float dr = delay_buffer_r[p0] + (delay_buffer_r[p1] - delay_buffer_r[p0]) * frac;

            // Damped, crossed feedback: L tail feeds R input and vice versa.
            lp_l_ += damp_ * (dl - lp_l_);
            lp_r_ += damp_ * (dr - lp_r_);
            delay_buffer_l[write_] = send_l[i] + lp_r_ * feedback_;
            delay_buffer_r[write_] = send_r[i] + lp_l_ * feedback_;
            write_ = (write_ + 1) & kDelayMask;

            main_l[i] += dl;
            main_r[i] += dr;
            float a = dl < 0.f ? -dl : dl;
            float b = dr < 0.f ? -dr : dr;
            if (a > out_peak) out_peak = a;
            if (b > out_peak) out_peak = b;
        }

        // Sleep only after input and output have been silent for a full delay
        // period — everything still queued in the line has then played out,
        // so waking later can't replay a stale tail.
        if (in_peak < kSilenceThresh && out_peak < kSilenceThresh) {
            uint32_t target = static_cast<uint32_t>(time_) / size + 8;
            if (++quiet_blocks_ >= target) asleep_ = true;
        } else {
            quiet_blocks_ = 0;
        }
    }

private:
    static constexpr float kMinTime  = 192.f;
    static constexpr float kMaxTime  = static_cast<float>(kDelayBufSize - 8);
    static constexpr float kTimeSlew = 0.0003f;   // ≈70 ms time constant at 48 kHz

    float    sample_rate_;
    uint32_t write_;
    float    time_;
    float    target_time_;
    float    feedback_;
    float    damp_;
    float    lp_l_, lp_r_;

    bool     asleep_;
    uint32_t quiet_blocks_;
};

Reverb     reverb_impl;
StereoDelay delay_impl;
float      fx_sample_rate = 48000.f;

} // namespace

// ── FxSection ─────────────────────────────────────────────────────────────────

FxSection fx;

void FxSection::Init(float sample_rate) {
    fx_sample_rate = sample_rate;
    reverb_impl.Init(sample_rate);
    delay_impl.Init(sample_rate);
}

void FxSection::SetReverbCharacter(int side, float amount) {
    if (side < 0) {
        // Room: short and damped; the tail opens slightly as wet grows.
        reverb_impl.set_time(0.35f + 0.25f * amount);
        reverb_impl.set_lp(0.45f);
    } else if (side > 0) {
        // Hall: long and bright. Capped below 1.0 — krt ≥ 1 never decays.
        reverb_impl.set_time(0.75f + 0.20f * amount);
        reverb_impl.set_lp(0.80f);
    }
    // side == 0: leave params; the send level is 0 so nothing feeds it.
}

void FxSection::SetDelayCharacter(int side, float amount, float synced_samples) {
    if (side < 0) {
        // Slapback: fixed short echo, feedback stays low.
        delay_impl.set_target_time(0.120f * fx_sample_rate);
        delay_impl.set_feedback(0.05f + 0.25f * amount);
        delay_impl.set_damp(0.85f);
    } else if (side > 0) {
        // Synced dotted 1/8: repeats grow with wet, darker dub-style tail.
        delay_impl.set_target_time(synced_samples);
        delay_impl.set_feedback(0.20f + 0.50f * amount);
        delay_impl.set_damp(0.45f);
    }
}

void FxSection::ProcessDelay(const float* send_l, const float* send_r,
                             float* main_l, float* main_r, size_t size) {
    delay_impl.Process(send_l, send_r, main_l, main_r, size);
}

void FxSection::ProcessReverb(const float* send_l, const float* send_r,
                              float* main_l, float* main_r, size_t size) {
    reverb_impl.Process(send_l, send_r, main_l, main_r, size);
}

} // namespace synthux
