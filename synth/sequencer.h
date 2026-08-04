#pragma once
#include <cstdint>
#include <cmath>
#include "patterns_gen.h"

// 16-step × 4-bar weight-matrix drum sequencer.
//
// Each step is one byte, hex-encoded 0xCW:
//   low nibble  W = weight 0–4:
//     0 = definite rest · 1–2 = ghost / light accent · 3 = medium · 4 = strong
//   high nibble C = chance, rolled only after the weight threshold passes:
//     0 = always fires · 1 = 75% · 2 = 50% · 3 = 25%
// Plain decimal 0–4 therefore still means "deterministic weight" — old tables
// read unchanged; 0x23 reads as "weight 3 at 50%".
//
// A step is audible when (weight + density) >= kThreshold (5). Density is
// clamped to 1–4 (SetDensity) — S33's travel never reaches a fully silent
// state:
//   density 1 → only weight=4 (strong) steps fire      — display: "1 strong"
//   density 2 → weight 3–4 fire (main pattern)          — display: "2 main"
//   density 3 → weight 2–4 fire (ghosts audible)         — display: "3 ghosts"
//   density 4 → weight 1–4 fire (everything)             — display: "4 full"
// (display/oled_ui.cpp's density_value() and the visualizer's
// densityValue() must stay in lockstep with SetDensity's quantization and
// these four words.)
//
// S34 (chance) rolls a step's own authored chance nibble (above) by default
// — SetChance(0.5), the pickup default, reproduces that exactly. Below 0.5
// it interpolates every chance<4-authored step toward always-fires; above
// 0.5 it scales the authored miss rate up (steps get sparser, up to
// kChanceExtraMax x at full right). Steps authored "always" (c=0, e.g. a
// four-on-the-floor kick) are never touched by this knob — it only adds or
// removes chance on steps the pattern already marked as probabilistic.
//
// Track order: 0=Kick 1=Snare 2=CHH 3=OHH 4=Clap 5=Tom 6=Perc
//   (matches pad_slots[0..6])
//
// Pattern tables are 64 steps (4 bars × 16), one file each in a genre
// subfolder of synth/patterns/ (techno/, electro/, idm/);
// tools/gen_patterns.py registers them into patterns_gen.h at build time.
// SW1 picks the genre (SetGenre), S35 picks the variant within that genre
// (SetVariant). Bar 4 is always the fill bar.
// Tick() must be called once per audio block from the audio ISR.
// Returns a 7-bit bitmask: bit i set → trigger track i this block.
// BeatFired() returns true on the block that step fired and step % 4 == 0 (quarter notes).

// ── Sequencer ─────────────────────────────────────────────────────────────────

class Sequencer {
public:
    static constexpr int    kSteps     = 16;
    static constexpr int    kBars      = 4;
    static constexpr int    kTracks    = 7;
    static constexpr int    kGenres    = kNumGenres;    // from patterns_gen.h
    static constexpr uint8_t kThreshold = 5;

    void Start() {
        active_       = true;
        step_         = 0;
        bar_          = 0;
        clock_        = 0;
        fire_at_      = 0;
        fire_pending_ = true;   // fire step 0 on the very first block/clock tick
        beat_fired_   = false;
        tick_in_step_      = 0;
        ext_ticks_pending_ = 0;
        // Master MIDI clock: emit a tick in the same block step 0 fires.
        midi_clk_acc_ = static_cast<float>(step_blocks_);
    }

    void Stop() {
        active_       = false;
        fire_pending_ = false;
        beat_fired_   = false;
        step_fired_   = false;
        ext_ticks_pending_ = 0;
    }

    // Resume from current step/bar position (does not reset to bar 0).
    // Fires the current step immediately so you hear the beat on unpause.
    void Resume() {
        if (!active_) {
            active_       = true;
            clock_        = 0;
            fire_at_      = 0;
            fire_pending_ = true;
            tick_in_step_      = 0;
            ext_ticks_pending_ = 0;
            midi_clk_acc_ = static_cast<float>(step_blocks_);
        }
    }

    // ── External MIDI clock (24 ppqn, 6 ticks per 16th step) ──────────────
    // While enabled, Tick() steps on received clock ticks instead of the
    // block counter — hard sync, no drift, and the tempo knob has no effect.
    void SetExternalClock(bool on) {
        if (ext_clock_ == on) return;
        ext_clock_         = on;
        ext_ticks_pending_ = 0;
        tick_in_step_      = 0;
        clock_             = 0;
        fire_at_           = 0;   // re-phase: pending step fires on next tick/block
    }
    bool ExternalClocked() const { return ext_clock_; }

    // One received F8. Called from the main loop with IRQs off. Only counts
    // while playing — ticks must not pile up during Stop and burst on Resume.
    void OnMidiClock() {
        if (ext_clock_ && active_) ext_ticks_pending_ = ext_ticks_pending_ + 1;
    }

    // Master-mode 24 ppqn generator on the step clock's own timebase (step =
    // 6 ticks of step_blocks_ blocks), so external gear can't drift against
    // the drums. Call once per block, active or not; skip when following an
    // external clock. Returns the number of clock messages to emit now.
    int MidiClockTick() {
        midi_clk_acc_ += 6.f;
        int n = 0;
        while (midi_clk_acc_ >= static_cast<float>(step_blocks_)) {
            midi_clk_acc_ -= static_cast<float>(step_blocks_);
            n++;
        }
        return n;
    }

    bool IsActive() const { return active_; }
    int  Step()    const { return step_; }
    int  Bar()     const { return bar_; }

    // Blocks per 16th step at the internal (knob/fallback) tempo — the
    // timebase for the tempo-synced delay. Under external MIDI clock this
    // still reflects the knob tempo, not the external one.
    uint32_t StepBlocks() const { return step_blocks_; }

    // S31 0..1 → 60–180 BPM
    void SetTempo(float v) {
        float bpm    = 60.f + v * 120.f;
        step_blocks_ = static_cast<uint32_t>(roundf(3750.f / bpm));
        if (step_blocks_ < 10) step_blocks_ = 10;  // clamp ~225 BPM max
    }

    // S32 0..1 → 0–50% swing delay on odd steps
    void SetShuffle(float v) { shuffle_ = v * 0.5f; }

    // S33 0..1 → integer density 1–4 (never fully silent). Quantization must
    // match display/oled_ui.cpp's density_value() and the visualizer's
    // densityValue() exactly, so the displayed stage always matches what's
    // actually playing.
    void SetDensity(float v) {
        int d = 1 + static_cast<int>(v * 3.f + 0.5f);
        if (d < 1) d = 1;
        if (d > 4) d = 4;
        density_ = static_cast<uint8_t>(d);
    }

    // S34 0..1 → chance layer over each step's authored chance nibble.
    // 0.5 (the pickup default) reproduces the pattern exactly as authored;
    // below that, probabilistic steps fire more often (toward "always" at
    // 0); above it, they fire less often (down to silent at 1). See the
    // header comment and eval_step() for the exact curve.
    void SetChance(float v) { chance_ = v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    // SW1 B() → genre (0=IDM 1=Techno 2=Electro, physical left/center/right;
    // order fixed in gen_patterns.py).
    void SetGenre(int g) {
        if (g >= 0 && g < kGenres) genre_ = g;
    }

    // S35 0..1 → variant slot within the current genre. Stored as the raw
    // knob value so switching to a genre with a different pattern count
    // re-quantizes automatically.
    void SetVariant(float v) {
        variant_ = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    }

    // Call once per audio block from the audio ISR.
    // Returns 7-bit trigger bitmask (bit i set = fire track i this block).
    // Check BeatFired() after Tick() to know if this block landed on a quarter note.
    uint8_t Tick() {
        if (!active_) return 0;

        beat_fired_       = false;
        step_fired_       = false;
        uint8_t triggered = 0;

        // External clock: consume received ticks; 6 per step, shuffle in
        // whole ticks (0–3 at max swing). The block counter is not consulted.
        if (ext_clock_) {
            while (ext_ticks_pending_ > 0) {
                ext_ticks_pending_ = ext_ticks_pending_ - 1;
                if (fire_pending_ && tick_in_step_ == fire_at_) {
                    fire_pending_ = false;
                    triggered    |= eval_step();
                    step_fired_   = true;
                    beat_fired_   = beat_fired_ || ((step_ % 4) == 0);
                }
                if (tick_in_step_ >= 5) {
                    tick_in_step_ = 0;
                    advance_step();
                } else {
                    tick_in_step_++;
                }
            }
            return triggered;
        }

        // Fire pending triggers at the scheduled clock offset.
        if (fire_pending_ && clock_ == fire_at_) {
            fire_pending_ = false;
            triggered     = eval_step();
            step_fired_   = true;
            // Quarter-note beat marker on steps 0, 4, 8, 12 — independent of
            // the density mask, so tempo is always visible in the LED.
            beat_fired_   = ((step_ % 4) == 0);
        }

        // Advance to next step once the block budget expires.
        if (clock_ >= step_blocks_ - 1) {
            clock_ = 0;
            advance_step();
        } else {
            clock_++;
        }

        return triggered;
    }

    bool BeatFired() const { return beat_fired_; }

    // True on the block where a 16th step fired (regardless of density mask).
    bool StepFired() const { return step_fired_; }

private:
    bool     active_       = false;
    bool     beat_fired_   = false;
    bool     step_fired_   = false;
    int      step_         = 0;
    int      bar_          = 0;
    int      genre_        = 1;  // Techno — matches seq_genre_lk's boot default
    float    variant_      = 0.f;
    uint32_t clock_        = 0;
    uint32_t step_blocks_  = 31;    // ~120 BPM at 4 ms/block
    uint32_t fire_at_      = 0;     // blocks internally; ticks under ext clock
    bool     fire_pending_ = false;
    bool     ext_clock_    = false;
    volatile uint32_t ext_ticks_pending_ = 0;   // written main loop (IRQ off), read ISR
    uint32_t tick_in_step_ = 0;     // 0–5 within the current 16th (ext clock)
    float    midi_clk_acc_ = 0.f;   // master 24 ppqn generator phase
    uint8_t  density_      = 2;
    float    shuffle_      = 0.f;
    float    chance_       = 0.5f;  // 0.5 = pattern's own chance, untouched
    uint32_t rng_          = 0x2545F491;  // xorshift32 state (any non-zero seed)

    uint32_t next_rand() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    void advance_step() {
        step_ = (step_ + 1) % kSteps;
        // Advance bar on every bar boundary.
        if (step_ == 0) bar_ = (bar_ + 1) % kBars;
        // Classic shuffle: even steps fire immediately, odd steps delayed —
        // in blocks internally, in whole clock ticks under external sync.
        if ((step_ & 1) == 0) {
            fire_at_ = 0;
        } else if (ext_clock_) {
            fire_at_ = static_cast<uint32_t>(shuffle_ * 6.f);
        } else {
            fire_at_ = static_cast<uint32_t>(shuffle_ * static_cast<float>(step_blocks_));
        }
        fire_pending_ = true;
    }

    // Resolve genre (SW1) + variant (S35) to a flat kSeqPatterns index.
    int pattern_index() const {
        int n  = kGenrePatternCount[genre_];
        int vi = static_cast<int>(variant_ * static_cast<float>(n));
        if (vi >= n) vi = n - 1;
        return kGenrePatternIdx[genre_][vi];
    }

    // Non-const: chance steps advance the RNG.
    uint8_t eval_step() {
        uint8_t mask = 0;
        int     idx  = bar_ * kSteps + step_;
        const uint8_t (*pat)[64] = kSeqPatterns[pattern_index()];
        for (int t = 0; t < kTracks; t++) {
            uint8_t v = pat[t][idx];
            uint8_t w = v & 0x0F;
            if (w == 0) continue;
            if (static_cast<uint32_t>(w) + density_ < kThreshold) continue;
            // Chance nibble: 0 = always (never touched by chance_ — see the
            // header comment), 1 = 75%, 2 = 50%, 3 = 25% as authored.
            // chance_ (S34) scales the miss rate around that authored value:
            // 0.5 leaves it exactly as authored; below stretches toward
            // always-fires; above stretches toward silent, up to
            // kChanceExtraMax x the authored miss rate.
            uint8_t c = v >> 4;
            if (c > 0) {
                constexpr float kChanceExtraMax = 3.f;
                float authored_p = static_cast<float>(4 - c) * 64.f;  // out of 256
                float miss       = 256.f - authored_p;
                float scale = (chance_ <= 0.5f)
                    ? (chance_ * 2.f)
                    : (1.f + (chance_ - 0.5f) * 2.f * (kChanceExtraMax - 1.f));
                float eff_p = 256.f - miss * scale;
                if (eff_p < 0.f) eff_p = 0.f;
                if (static_cast<float>(next_rand() & 0xFF) >= eff_p) continue;
            }
            mask |= static_cast<uint8_t>(1u << t);
        }
        return mask;
    }
};
