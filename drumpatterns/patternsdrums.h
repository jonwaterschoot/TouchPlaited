// =============================================================================
// drum_pattern_generator.h
// Generative drum pattern engine for 7-voice drum mode (Daisy / plaits clone)
//
// Voices: P3 Kick · P4 Snare · P5 CHH · P6 OHH · P7 Clap · P8 Tom · P9 Perc
// GM ref (future MIDI alignment): 36=Kick · 38=Snare · 42=CHH · 46=OHH · 39=Clap · 41=LowTom
//
// Contains:
//   1. GenreWeights      - per-genre, per-instrument 16-step x4-bar weight tables (0-4 scale)
//   2. Euclid            - classic Bjorklund/Toussaint euclidean rhythm generator
//   3. GenreEuclid       - blends euclidean placement with genre weight constraints
//   4. DrumPatternGenerator - top-level class: call RegenerateBar() to get a new pattern
//
// Usage sketch:
//   DrumPatternGenerator gen;
//   gen.SetGenre(&kAnthonyRotherStyle);
//   gen.RegenerateBar(0, rngSeed);     // regenerate bar 0 of 4
//   bool hit = gen.GetStep(TRACK_KICK, 0, stepIndex);
//
// =============================================================================

#pragma once
#include <cstdint>
#include <vector>
#include <array>

// -----------------------------------------------------------------------------
// 1. GENRE WEIGHT TABLES
//    0 = never · 1-2 = ghost (only fires at high density) · 3 = normal hit · 4 = always
//    64 steps = 4 bars x 16 steps. Index = bar*16 + step.
// -----------------------------------------------------------------------------

struct GenreWeights {
    uint8_t kick[64];
    uint8_t snare[64];
    uint8_t chh[64];
    uint8_t ohh[64];
    uint8_t clap[64];
    uint8_t tom[64];
    uint8_t perc[64];
};

// --- Anthony Rother / oldskool 808 electro style -----------------------------
// Syncopated kick (no four-on-floor), snare+clap stacked on 2&4, sparse cowbell-perc,
// ghost echo on clap approximating a 2/16-step delay bounce, bar4 = fill/turnaround.
static const GenreWeights kAnthonyRotherStyle = {
    .kick = {
        4,0,0,0, 4,0,3,0, 4,0,0,0, 4,0,0,0,
        4,0,0,0, 4,0,3,0, 4,0,0,0, 4,0,0,0,
        4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,2,0,
        4,0,0,2, 4,0,3,0, 4,0,0,0, 4,0,3,3,
    },
    .snare = {
        0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
        0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
        0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
        0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,1,
    },
    .chh = {
        3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
        3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
        3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
        3,0,3,0, 3,0,3,0, 3,0,3,0, 3,1,3,1,
    },
    .ohh = {
        0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,2,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,2,0,
    },
    .clap = {
        0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
        0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
        0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
        0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,3,2,
    },
    .tom = {
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 2,0,3,3,
    },
    .perc = {
        0,0,2,0, 0,0,0,0, 0,3,0,0, 0,0,0,0,
        0,0,2,0, 0,0,0,0, 0,3,0,0, 0,0,0,0,
        0,0,0,0, 0,0,2,0, 0,3,0,0, 0,0,0,0,
        0,0,2,0, 0,0,0,0, 0,3,0,2, 0,0,0,0,
    },
};

// --- Techno (four-on-floor, off-beat hat) ------------------------------------
static const GenreWeights kTechnoStyle = {
    .kick = {
        4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,
        4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,
        4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,
        4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,
    },
    .snare = {
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,1,
    },
    .chh = {
        0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
        0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
        0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
        0,0,3,0, 0,0,3,0, 0,0,3,0, 0,1,3,1,
    },
    .ohh = {
        0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
    },
    .clap = {
        0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
        0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
    },
    .tom = {
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,2,
    },
    .perc = {
        0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
        0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
        0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
        0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
    },
};

// --- Ambient / IDM (sparse, irregular, glitchy) ------------------------------
static const GenreWeights kAmbientIdmStyle = {
    .kick = {
        4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,0,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,0,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,0,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,0,1,
    },
    .snare = {
        0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,1,0,
    },
    .chh = {
        0,0,2,0, 0,2,0,2, 0,0,0,0, 2,0,0,2,
        0,0,2,0, 0,2,0,2, 0,0,0,0, 2,0,0,2,
        0,0,2,0, 0,2,0,2, 0,0,0,0, 2,0,0,2,
        0,0,2,0, 0,2,0,2, 0,0,0,0, 2,0,0,2,
    },
    .ohh = {
        0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
    },
    .clap = {
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    },
    .tom = {
        4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
        4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
    },
    .perc = {
        0,2,0,0, 0,2,0,2, 0,0,2,0, 0,0,0,2,
        0,2,0,0, 0,2,0,2, 0,0,2,0, 0,0,0,2,
        0,2,0,0, 0,2,0,2, 0,0,2,0, 0,0,0,2,
        0,2,0,0, 0,2,0,2, 0,0,2,0, 0,0,0,2,
    },
};

// -----------------------------------------------------------------------------
// 2. EUCLIDEAN RHYTHM GENERATOR (Bjorklund / Toussaint)
//    Distributes k hits as evenly as possible across n steps.
//    Origin: E. Bjorklund's pulse-spacing algorithm for accelerator timing
//    systems, shown by Toussaint (2005) to generate traditional world rhythms
//    and to be equivalent to the Euclidean GCD algorithm. Used internally by
//    Mutable Instruments Grids and most euclidean step sequencers.
// -----------------------------------------------------------------------------

class Euclid {
public:
    // Returns a bitmask (bit i = step i) with k hits spread over n steps.
    static uint32_t Generate(int k, int n, int rotation = 0) {
        if (k <= 0 || n <= 0 || k > n) return 0;

        std::vector<std::vector<bool>> groups;
        for (int i = 0; i < k; i++) groups.push_back({true});
        for (int i = 0; i < n - k; i++) groups.push_back({false});

        while (true) {
            std::vector<std::vector<bool>> trues, falses;
            for (auto& g : groups) {
                if (g.front()) trues.push_back(g); else falses.push_back(g);
            }
            int pairs = std::min((int)trues.size(), (int)falses.size());
            if (pairs <= 1) break;

            std::vector<std::vector<bool>> merged;
            for (int i = 0; i < pairs; i++) {
                std::vector<bool> combo = trues[i];
                combo.insert(combo.end(), falses[i].begin(), falses[i].end());
                merged.push_back(combo);
            }
            for (int i = pairs; i < (int)trues.size(); i++) merged.push_back(trues[i]);
            for (int i = pairs; i < (int)falses.size(); i++) merged.push_back(falses[i]);

            groups = merged;
            if (groups.size() <= 1) break;
        }

        std::vector<bool> flat;
        for (auto& g : groups)
            for (bool b : g)
                flat.push_back(b);

        uint32_t mask = 0;
        int len = (int)flat.size();
        for (int i = 0; i < len; i++) {
            int idx = (i + rotation) % len;
            if (flat[idx]) mask |= (1u << i);
        }
        return mask;
    }
};

// -----------------------------------------------------------------------------
// 3. GENRE-CONSTRAINED EUCLIDEAN BLENDING
//    Places k euclidean hits, but if a hit lands on a genre-forbidden step
//    (weight==0), nudges it to the nearest allowed step. Preserves hit count
//    (good for predictable density scaling) while respecting genre identity.
// -----------------------------------------------------------------------------

class GenreEuclid {
public:
    // weights: pointer to 16 values (one bar) from a GenreWeights field
    static uint16_t Generate(const uint8_t weights[16], int k, int rotation = 0) {
        uint32_t euclidMask = Euclid::Generate(k, 16, rotation);
        uint16_t result = 0;

        for (int step = 0; step < 16; step++) {
            if (!((euclidMask >> step) & 1)) continue;

            if (weights[step] > 0) {
                result |= (1 << step);
            } else {
                int nearest = FindNearestAllowed(weights, step);
                if (nearest >= 0) result |= (1 << nearest);
            }
        }
        return result;
    }

private:
    static int FindNearestAllowed(const uint8_t weights[16], int from) {
        for (int dist = 1; dist <= 8; dist++) {
            int right = (from + dist) % 16;
            int left  = (from - dist + 16) % 16;
            if (weights[right] > 0) return right;
            if (weights[left] > 0) return left;
        }
        return -1;
    }
};

// -----------------------------------------------------------------------------
// 4. TOP-LEVEL DRUM PATTERN GENERATOR
//    Kick/Snare/Clap use fixed genre weight-table threshold triggering
//    (positional identity matters most for these -> keep them genre-locked).
//    CHH/OHH/Tom/Perc use density-driven GenreEuclid placement for variation.
//
//    Call RegenerateBar() per bar (or per full 4-bar cycle) with a fresh seed
//    to get new variations that still sound like the chosen genre.
// -----------------------------------------------------------------------------

enum DrumTrack {
    TRACK_KICK = 0,
    TRACK_SNARE,
    TRACK_CHH,
    TRACK_OHH,
    TRACK_CLAP,
    TRACK_TOM,
    TRACK_PERC,
    TRACK_COUNT
};

class DrumPatternGenerator {
public:
    void SetGenre(const GenreWeights* genre) { genre_ = genre; }

    // Simple xorshift PRNG, seed from e.g. ADC noise / floating pin on Daisy
    uint32_t NextRand() {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return rngState_;
    }
    void SeedRng(uint32_t seed) { rngState_ = seed ? seed : 0xA5A5A5A5u; }

    // Threshold-trigger tracks (kick/snare/clap): weight + density >= threshold
    static bool ShouldTrigger(uint8_t weight, uint8_t density, uint8_t threshold = 5) {
        return (weight + density) >= threshold;
    }

    // Regenerate one bar (0-3) of the pattern using current genre + rng
    void RegenerateBar(int bar, int densityKick = 2, int densitySnareClap = 2) {
        if (!genre_) return;
        int base = bar * 16;

        // --- Kick / Snare / Clap: genre-locked threshold triggering ---
        for (int s = 0; s < 16; s++) {
            pattern_[TRACK_KICK][base + s]  = ShouldTrigger(genre_->kick[base + s],  densityKick);
            pattern_[TRACK_SNARE][base + s] = ShouldTrigger(genre_->snare[base + s], densitySnareClap);
            pattern_[TRACK_CLAP][base + s]  = ShouldTrigger(genre_->clap[base + s],  densitySnareClap);
        }

        // --- CHH / OHH / Tom / Perc: euclidean placement, randomized hit counts ---
        uint32_t r = NextRand();
        int chhHits  = 4 + (r % 5);          // 4-8 hits
        int ohhHits  = (r >> 4) % 3;          // 0-2 hits
        int tomHits  = (r >> 8) % 3;          // 0-2 hits
        int percHits = 1 + ((r >> 12) % 4);   // 1-4 hits

        SetEuclidTrack(TRACK_CHH,  base, genre_->chh,  chhHits);
        SetEuclidTrack(TRACK_OHH,  base, genre_->ohh,  ohhHits);
        SetEuclidTrack(TRACK_TOM,  base, genre_->tom,  tomHits);
        SetEuclidTrack(TRACK_PERC, base, genre_->perc, percHits);
    }

    void RegenerateAllBars(int densityKick = 2, int densitySnareClap = 2) {
        for (int bar = 0; bar < 4; bar++)
            RegenerateBar(bar, densityKick, densitySnareClap);
    }

    bool GetStep(DrumTrack track, int bar, int step) const {
        return pattern_[track][bar * 16 + step];
    }

private:
    void SetEuclidTrack(DrumTrack track, int base, const uint8_t* weightsField, int hits) {
        if (hits <= 0) {
            for (int s = 0; s < 16; s++) pattern_[track][base + s] = false;
            return;
        }
        uint16_t mask = GenreEuclid::Generate(&weightsField[base], hits);
        for (int s = 0; s < 16; s++)
            pattern_[track][base + s] = (mask >> s) & 1;
    }

    const GenreWeights* genre_ = &kAnthonyRotherStyle;
    bool pattern_[TRACK_COUNT][64] = {};
    uint32_t rngState_ = 0xDEADBEEFu;
};

// -----------------------------------------------------------------------------
// Example usage:
//
//   DrumPatternGenerator gen;
//   gen.SetGenre(&kAnthonyRotherStyle);   // or &kTechnoStyle / &kAmbientIdmStyle
//   gen.SeedRng(GetSomeHardwareNoise());
//   gen.RegenerateAllBars(/*densityKick=*/2, /*densitySnareClap=*/2);
//
//   // in your sequencer clock callback:
//   int bar = currentBar;   // 0-3
//   int step = currentStep; // 0-15
//   if (gen.GetStep(TRACK_KICK, bar, step))  TriggerVoice(P3);
//   if (gen.GetStep(TRACK_SNARE, bar, step)) TriggerVoice(P4);
//   if (gen.GetStep(TRACK_CHH, bar, step))   TriggerVoice(P5);
//   if (gen.GetStep(TRACK_OHH, bar, step))   TriggerVoice(P6);
//   if (gen.GetStep(TRACK_CLAP, bar, step))  TriggerVoice(P7);
//   if (gen.GetStep(TRACK_TOM, bar, step))   TriggerVoice(P8);
//   if (gen.GetStep(TRACK_PERC, bar, step))  TriggerVoice(P9);
//
//   // call gen.RegenerateBar(bar, ...) on a button press / fill trigger
//   // for fresh variations that stay genre-faithful.
// -----------------------------------------------------------------------------