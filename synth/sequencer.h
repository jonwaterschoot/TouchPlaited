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
// A step is audible when (weight + density) >= kThreshold (5).
//   density 1 → only weight=4 (strong) steps fire
//   density 2 → weight 3–4 fire (main pattern)
//   density 3 → weight 2–4 fire (ghosts audible)
//   density 4 → weight 1–4 fire (everything)
//
// Track order: 0=Kick 1=Snare 2=CHH 3=OHH 4=Clap 5=Tom 6=Perc
//   (matches pad_slots[0..6])
//
// Pattern tables are 64 steps (4 bars × 16), one file each in synth/patterns/;
// tools/gen_patterns.py registers them into patterns_gen.h at build time.
// Bar 4 is always the fill bar.
// Tick() must be called once per audio block from the audio ISR.
// Returns a 7-bit bitmask: bit i set → trigger track i this block.
// BeatFired() returns true on the block that step fired and step % 4 == 0 (quarter notes).

// ── Sequencer ─────────────────────────────────────────────────────────────────

class Sequencer {
public:
    static constexpr int    kSteps     = 16;
    static constexpr int    kBars      = 4;
    static constexpr int    kTracks    = 7;
    static constexpr int    kGenres    = kNumPatterns;  // from patterns_gen.h
    static constexpr uint8_t kThreshold = 5;

    void Start() {
        active_       = true;
        step_         = 0;
        bar_          = 0;
        clock_        = 0;
        fire_at_      = 0;
        fire_pending_ = true;   // fire step 0 on the very first block
        beat_fired_   = false;
    }

    void Stop() {
        active_       = false;
        fire_pending_ = false;
        beat_fired_   = false;
        step_fired_   = false;
    }

    // Resume from current step/bar position (does not reset to bar 0).
    // Fires the current step immediately so you hear the beat on unpause.
    void Resume() {
        if (!active_) {
            active_       = true;
            clock_        = 0;
            fire_at_      = 0;
            fire_pending_ = true;
        }
    }

    bool IsActive() const { return active_; }
    int  Step()    const { return step_; }
    int  Bar()     const { return bar_; }

    // S31 0..1 → 60–180 BPM
    void SetTempo(float v) {
        float bpm    = 60.f + v * 120.f;
        step_blocks_ = static_cast<uint32_t>(roundf(3750.f / bpm));
        if (step_blocks_ < 10) step_blocks_ = 10;  // clamp ~225 BPM max
    }

    // S32 0..1 → 0–50% swing delay on odd steps
    void SetShuffle(float v) { shuffle_ = v * 0.5f; }

    // S33 0..1 → integer density 0–4
    void SetDensity(float v) {
        int d    = static_cast<int>(v * 4.f + 0.5f);
        density_ = static_cast<uint8_t>(d > 4 ? 4 : d);
    }

    // SW1 B() → pattern index (0=Techno 1=Electro 2=IDM with the stock files).
    // SW1 has 3 positions, so patterns beyond index 2 need another selector
    // (planned: S35 picker in Seq mode).
    void SetGenre(int g) {
        if (g >= 0 && g < kGenres) genre_ = g;
    }

    // Call once per audio block from the audio ISR.
    // Returns 7-bit trigger bitmask (bit i set = fire track i this block).
    // Check BeatFired() after Tick() to know if this block landed on a quarter note.
    uint8_t Tick() {
        if (!active_) return 0;

        beat_fired_       = false;
        step_fired_       = false;
        uint8_t triggered = 0;

        // Fire pending triggers at the scheduled clock offset.
        if (fire_pending_ && clock_ == fire_at_) {
            fire_pending_ = false;
            triggered     = eval_step();
            step_fired_   = true;
            // Quarter-note beat marker on steps 0, 4, 8, 12 — fires even at density=0
            // so tempo is always visible in the LED.
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
    int      genre_        = 0;
    uint32_t clock_        = 0;
    uint32_t step_blocks_  = 31;    // ~120 BPM at 4 ms/block
    uint32_t fire_at_      = 0;
    bool     fire_pending_ = false;
    uint8_t  density_      = 2;
    float    shuffle_      = 0.f;
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
        // Classic shuffle: even steps fire immediately, odd steps delayed.
        if ((step_ & 1) == 0) {
            fire_at_ = 0;
        } else {
            fire_at_ = static_cast<uint32_t>(shuffle_ * static_cast<float>(step_blocks_));
        }
        fire_pending_ = true;
    }

    // Non-const: chance steps advance the RNG.
    uint8_t eval_step() {
        uint8_t mask = 0;
        int     idx  = bar_ * kSteps + step_;
        for (int t = 0; t < kTracks; t++) {
            uint8_t v = kSeqPatterns[genre_][t][idx];
            uint8_t w = v & 0x0F;
            if (w == 0) continue;
            if (static_cast<uint32_t>(w) + density_ < kThreshold) continue;
            // Chance nibble: 0 = always, 1 = 75%, 2 = 50%, 3 = 25%.
            uint8_t c = v >> 4;
            if (c > 0 && (next_rand() & 0xFF) >= static_cast<uint32_t>(4 - c) * 64) {
                continue;
            }
            mask |= static_cast<uint8_t>(1u << t);
        }
        return mask;
    }
};
