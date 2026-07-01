#pragma once
#include <cstdint>
#include <cmath>

// 16-step × 4-bar weight-matrix drum sequencer.
//
// Weight values 0–4 per step:
//   0 = definite rest
//   1–2 = ghost / light accent
//   3 = medium accent
//   4 = strong hit
//
// A step fires when (weight + density) >= kThreshold (5).
//   density 1 → only weight=4 (strong) steps fire
//   density 2 → weight 3–4 fire (main pattern)
//   density 3 → weight 2–4 fire (ghosts audible)
//   density 4 → weight 1–4 fire (everything)
//
// Track order: 0=Kick 1=Snare 2=CHH 3=OHH 4=Clap 5=Tom 6=Perc
//   (matches pad_slots[0..6])
//
// Genre tables are 64 steps (4 bars × 16). Bar 4 is always the fill bar.
// Tick() must be called once per audio block from the audio ISR.
// Returns a 7-bit bitmask: bit i set → trigger track i this block.
// BeatFired() returns true on the block that step fired and step % 4 == 0 (quarter notes).

// ── Weight tables ─────────────────────────────────────────────────────────────
// static = internal linkage; safe to define in a header.
// Layout: [genre][track][bar * 16 + step], bars 0–3.

// ── Genre 0: Techno (four-on-floor, off-beat hat, bar 4 = fill) ──────────────
// Source: standard 909/808 techno template, bar-by-bar variation.
// Bars 1–3 identical grid; bar 4 adds ghost snare/hat and tom fill.
static constexpr uint8_t kWeightsTechno[7][64] = {
    // Kick — four-on-floor every bar
    { 4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,   // bar 1
      4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,   // bar 2
      4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0,   // bar 3
      4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,0,0 }, // bar 4
    // Snare — silent bars 1–3, ghost fill on bar 4
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,1 },
    // CHH — off-beat 8ths; bar 4 ghosts on the 16ths
    { 0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
      0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
      0,0,3,0, 0,0,3,0, 0,0,3,0, 0,0,3,0,
      0,0,3,0, 0,0,3,0, 0,0,3,0, 0,1,3,1 },
    // OHH — beat 4 (step 12) every bar
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 3,0,0,0 },
    // Clap — beats 2 & 4
    { 0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0,
      0,0,0,0, 3,0,0,0, 0,0,0,0, 3,0,0,0 },
    // Tom — bar 4 end fill only
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,2 },
    // Perc — sparse syncopated accent (every 4 steps offset)
    { 0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
      0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
      0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0,
      0,0,0,2, 0,0,0,0, 0,0,0,2, 0,0,0,0 },
};

// ── Genre 1: Electro (Anthony Rother / oldskool 808 style) ───────────────────
// Syncopated kick (not four-on-floor), snare+clap on 2&4, clap ghost echo 2
// steps later, straight 8th hats at weight 3 (slightly softer than techno),
// sparse cowbell perc. Bar 4 is the fill/turnaround.
// Source: kAnthonyRotherStyle in drumpatterns/patternsdrums.h
static constexpr uint8_t kWeightsElectro[7][64] = {
    // Kick
    { 4,0,0,0, 4,0,3,0, 4,0,0,0, 4,0,0,0,   // bar 1
      4,0,0,0, 4,0,3,0, 4,0,0,0, 4,0,0,0,   // bar 2
      4,0,0,0, 4,0,0,0, 4,0,0,0, 4,0,2,0,   // bar 3 — ghost on step 14
      4,0,0,2, 4,0,3,0, 4,0,0,0, 4,0,3,3 }, // bar 4 fill — extra hits at end
    // Snare
    { 0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,1 },
    // CHH — straight 8ths at weight 3 (softer than weight 4, ghostable)
    { 3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
      3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
      3,0,3,0, 3,0,3,0, 3,0,3,0, 3,0,3,0,
      3,0,3,0, 3,0,3,0, 3,0,3,0, 3,1,3,1 },
    // OHH — bar accent on step 10 (off-position vs techno)
    { 0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,2,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,3,0, 0,0,2,0 },
    // Clap — beat 2 (step 4) + ghost echo 2 steps later (step 6): 808 delay bounce
    { 0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
      0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
      0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,2,0,
      0,0,0,0, 4,0,2,0, 0,0,0,0, 4,0,3,2 },
    // Tom — mostly absent; bar 2 end ghost, bar 4 fill
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 2,0,3,3 },
    // Perc — sparse cowbell/syncopated offset, varies bars 1–4
    { 0,0,2,0, 0,0,0,0, 0,3,0,0, 0,0,0,0,
      0,0,2,0, 0,0,0,0, 0,3,0,0, 0,0,0,0,
      0,0,0,0, 0,0,2,0, 0,3,0,0, 0,0,0,0,
      0,0,2,0, 0,0,0,0, 0,3,0,2, 0,0,0,0 },
};

// ── Genre 2: Ambient / IDM (irregular, glitchy, per-bar mutation) ─────────────
// Each bar shifts the kick to a different unexpected position; snare off-grid;
// CHH uses E(5,16) fixed spacing; Perc is densest and most chaotic.
// Bar 1: kick on 0+12 (sparse but grounded).
// Bar 2: kick migrates to steps 0+7 — between beats, deeply unsettling.
// Bar 3: NO downbeat kick — only step 8 (the "and" of beat 3). Most disorienting.
// Bar 4: fill — kicks return to 0+12, perc explodes.
static constexpr uint8_t kWeightsIdm[7][64] = {
    // Kick
    { 4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,0,0,   // bar 1: steps 0 + 12
      4,0,0,0, 0,0,0,3, 0,0,0,0, 0,0,0,0,   // bar 2: steps 0 + 7 (shifted)
      0,0,0,0, 0,0,0,0, 4,0,0,0, 0,0,0,0,   // bar 3: only step 8 — no downbeat
      4,0,0,0, 0,0,0,0, 0,0,0,0, 4,0,3,0 }, // bar 4 fill
    // Snare — appears on step 6 (off-position), extra ghost bar 3
    { 0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,3,0, 0,0,2,0, 0,0,0,0,
      0,0,0,0, 0,0,3,0, 0,0,0,0, 0,0,3,0 },
    // CHH — E(5,16) fixed: steps 2,5,7,11,14 (same every bar, fill adds ghosts)
    { 0,0,4,0, 0,3,0,4, 0,0,0,3, 0,0,4,0,
      0,0,4,0, 0,3,0,4, 0,0,0,3, 0,0,4,0,
      0,0,4,0, 0,3,0,4, 0,0,0,3, 0,0,4,0,
      0,0,4,0, 0,3,0,4, 0,0,0,3, 0,1,4,1 },
    // OHH — single sparse hit per bar, shifts each bar
    { 0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,2,
      0,0,0,0, 0,0,0,2, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,2, 0,0,0,0 },
    // Clap — unused (zeroes throughout)
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 },
    // Tom — doubles kick bar 1, sporadic bar 4
    { 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,0 },
    // Perc — most chaotic track, different pattern every bar
    { 0,2,0,0, 0,2,0,2, 0,0,2,0, 0,0,0,2,   // bar 1: E(5,16) offset
      2,0,0,2, 0,0,2,0, 0,2,0,0, 0,0,2,0,   // bar 2: shifted
      0,0,2,0, 2,0,0,0, 0,2,0,2, 0,0,0,0,   // bar 3: another shift
      2,0,2,0, 0,2,0,0, 2,0,0,2, 0,2,0,0 }, // bar 4: densest / fill
};

// Master table: [genre][track][bar * 16 + step]
static constexpr const uint8_t* kSeqWeights[3][7] = {
    { kWeightsTechno[0], kWeightsTechno[1], kWeightsTechno[2], kWeightsTechno[3],
      kWeightsTechno[4], kWeightsTechno[5], kWeightsTechno[6] },
    { kWeightsElectro[0], kWeightsElectro[1], kWeightsElectro[2], kWeightsElectro[3],
      kWeightsElectro[4], kWeightsElectro[5], kWeightsElectro[6] },
    { kWeightsIdm[0], kWeightsIdm[1], kWeightsIdm[2], kWeightsIdm[3],
      kWeightsIdm[4], kWeightsIdm[5], kWeightsIdm[6] },
};

// ── Sequencer ─────────────────────────────────────────────────────────────────

class Sequencer {
public:
    static constexpr int    kSteps     = 16;
    static constexpr int    kBars      = 4;
    static constexpr int    kTracks    = 7;
    static constexpr int    kGenres    = 3;
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

    // S30 0..1 → 60–180 BPM
    void SetTempo(float v) {
        float bpm    = 60.f + v * 120.f;
        step_blocks_ = static_cast<uint32_t>(roundf(3750.f / bpm));
        if (step_blocks_ < 10) step_blocks_ = 10;  // clamp ~225 BPM max
    }

    // S31 0..1 → 0–50% swing delay on odd steps
    void SetShuffle(float v) { shuffle_ = v * 0.5f; }

    // S32 0..1 → integer density 0–4
    void SetDensity(float v) {
        int d    = static_cast<int>(v * 4.f + 0.5f);
        density_ = static_cast<uint8_t>(d > 4 ? 4 : d);
    }

    // SW1 B() → genre 0=Techno 1=Electro 2=IDM
    void SetGenre(int g) {
        if (g >= 0 && g < kGenres) genre_ = g;
    }

    // Call once per audio block from the audio ISR.
    // Returns 7-bit trigger bitmask (bit i set = fire track i this block).
    // Check BeatFired() after Tick() to know if this block landed on a quarter note.
    uint8_t Tick() {
        if (!active_) return 0;

        beat_fired_       = false;
        uint8_t triggered = 0;

        // Fire pending triggers at the scheduled clock offset.
        if (fire_pending_ && clock_ == fire_at_) {
            fire_pending_ = false;
            triggered     = eval_step();
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

private:
    bool     active_       = false;
    bool     beat_fired_   = false;
    int      step_         = 0;
    int      bar_          = 0;
    int      genre_        = 0;
    uint32_t clock_        = 0;
    uint32_t step_blocks_  = 31;    // ~120 BPM at 4 ms/block
    uint32_t fire_at_      = 0;
    bool     fire_pending_ = false;
    uint8_t  density_      = 2;
    float    shuffle_      = 0.f;

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

    uint8_t eval_step() const {
        uint8_t mask = 0;
        int     idx  = bar_ * kSteps + step_;
        for (int t = 0; t < kTracks; t++) {
            uint8_t w = kSeqWeights[genre_][t][idx];
            if (w > 0 && (static_cast<uint32_t>(w) + density_) >= kThreshold) {
                mask |= static_cast<uint8_t>(1u << t);
            }
        }
        return mask;
    }
};
