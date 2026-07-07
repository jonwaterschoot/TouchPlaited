#pragma once
#include <cstdint>

// ── Ambient / IDM (irregular, glitchy, per-bar mutation) ─────────────────────
// Each bar shifts the kick to a different unexpected position; snare off-grid;
// CHH uses E(5,16) fixed spacing; Perc is densest and most chaotic.
// Bar 1: kick on 0+12 (sparse but grounded).
// Bar 2: kick migrates to steps 0+7 — between beats, deeply unsettling.
// Bar 3: NO downbeat kick — only step 8 (the "and" of beat 3). Most disorienting.
// Bar 4: fill — kicks return to 0+12, perc explodes.
// Step byte encoding 0xCW: W = weight 0–4, C = chance 0=always 1=75% 2=50% 3=25%.
// Track order: 0=Kick 1=Snare 2=CHH 3=OHH 4=Clap 5=Tom 6=Perc.
static constexpr uint8_t kPat_idm_00_idm[7][64] = {
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
