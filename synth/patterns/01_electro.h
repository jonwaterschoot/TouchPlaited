#pragma once
#include <cstdint>

// ── Electro (breaks-style, transcribed from 6-pattern screenshot bank) ───────
// Replaces the earlier Anthony Rother table (in git history if ever wanted)
// as it was not the actual Rother vibe.
// Source: 6 chained patterns (1,2,1,2,3,4,3,4,5,6,...) = 2 kick variations
// (A: steps 0+6 / B: steps 0+6+10+13) × 3 intensity layers. The layers are
// encoded as weights so the density knob reproduces the original build:
//   density 1 → patterns 1/2 (kick + snare + OH, bars alternate A B A B)
//   density 2 → patterns 3/4 (+ broken 16th hats)
//   density 3 → patterns 5/6 (+ rimshot-style perc 16ths)
//   density 4 → full (+ ghost snares from the GH row)
// GH row mapped to weight-1 snare ghosts; RS row mapped to Perc at weight 2.
// Step byte encoding 0xCW: W = weight 0–4, C = chance 0=always 1=75% 2=50% 3=25%.
// Track order: 0=Kick 1=Snare 2=CHH 3=OHH 4=Clap 5=Tom 6=Perc.
static constexpr uint8_t kPat_01_electro[7][64] = {
    // Kick — A: 0+6; B: 0+6+10+13; bars alternate A B A B, bar 4 = B + ghosts
    { 4,0,0,0, 0,0,4,0, 0,0,0,0, 0,0,0,0,   // bar 1: pattern A
      4,0,0,0, 0,0,4,0, 0,0,4,0, 0,4,0,0,   // bar 2: pattern B
      4,0,0,0, 0,0,4,0, 0,0,0,0, 0,0,0,0,   // bar 3: pattern A
      4,0,0,0, 0,0,4,0, 0,0,4,0, 0,4,1,1 }, // bar 4: B + fill ghosts
    // Snare — 2&4 strong; GH row as weight-1 ghosts (2,3,7,9,11,15; +14 even bars)
    { 0,0,1,1, 4,0,0,1, 0,1,0,1, 4,0,0,1,
      0,0,1,1, 4,0,0,1, 0,1,0,1, 4,0,1,1,
      0,0,1,1, 4,0,0,1, 0,1,0,1, 4,0,0,1,
      0,0,1,1, 4,0,0,1, 0,1,0,1, 4,0,1,1 },
    // CHH — broken 16ths: 0,2,3,6,7,8,9,11,13,14,15 (avoids 1,4,5,10,12)
    { 3,0,3,3, 0,0,3,3, 3,3,0,3, 0,3,3,3,
      3,0,3,3, 0,0,3,3, 3,3,0,3, 0,3,3,3,
      3,0,3,3, 0,0,3,3, 3,3,0,3, 0,3,3,3,
      3,1,3,3, 0,1,3,3, 3,3,0,3, 0,3,3,3 },
    // OHH — doubles the snare on 2&4; strong (present in sparsest source pattern)
    { 0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0,
      0,0,0,0, 4,0,0,0, 0,0,0,0, 4,0,0,0 },
    // Clap — echo after the snare (5) and after beat 4 (13), chance-encoded:
    // 0x23 = weight 3 at 50%. Bar-4 step 13 = plain 4 so the fill clap is certain.
    { 0,0,0,0, 0,0x23,0,0, 0,0,0,0, 0,0x23,0,0,
      0,0,0,0, 0,0x23,0,0, 0,0,0,0, 0,0x23,0,0,
      0,0,0,0, 0,0x23,0,0, 0,0,0,0, 0,0x23,0,0,
      0,0,0,0, 0,0x23,0,0, 0,0,0,0, 0,4,0,0 },
    // Tom — bar 4 end fill only (added, not in source)
    { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,2,2 },
    // Perc — RS row: near-full 16ths avoiding 1,4,6,13 (rimshot shaker feel)
    { 2,0,2,2, 0,2,0,2, 2,2,2,2, 2,0,2,2,
      2,0,2,2, 0,2,0,2, 2,2,2,2, 2,0,2,2,
      2,0,2,2, 0,2,0,2, 2,2,2,2, 2,0,2,2,
      2,0,2,2, 0,2,0,2, 2,2,2,2, 2,0,2,2 },
};
