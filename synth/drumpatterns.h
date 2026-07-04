    // Kick — A: 0+6; B: 0+6+10+13; bars alternate A B A B, bar 4 = B + ghosts
    { 4,0,0,0, 0,0,4,0, 4,0,4,0, 0,0,0,0,   // bar 1: pattern A
      4,0,4,0, 0,0,4,0, 4,0,0,0, 0,0,0,0,   // bar 2: pattern B
      4,0,0,0, 4,0,4,0, 4,4,0,0, 0,4,0,0,   // bar 3: pattern A
      4,0,0,0, 0,0,4,0, 0,1,4,0, 0,4,1,1 }, // bar 4: B + fill ghosts
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
    // Clap — echo after the snare (5) and after beat 4 (13), PROBABILISTIC:
    // this track+genre fires through a chance gate in eval_step() — the margin
    // of (weight+density) over the threshold sets the odds (25/50/75/100%).
    // Bar 4 step 13 upgraded to 4 so the fill clap is near-certain.
    { 0,0,0,0, 0,3,0,0, 0,0,0,0, 0,3,0,0,
      0,0,0,0, 0,3,0,0, 0,0,0,0, 0,3,0,0,
      0,0,0,0, 0,3,0,0, 0,0,0,0, 0,3,0,0,
      0,0,0,0, 0,3,0,0, 0,0,0,0, 0,4,0,0 },
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


      --

      When moving Density; at least for electro, Kick and snare should be the minimum density, and the other tracks should be the ones that increase in density. This is because the kick and snare are the main elements of the rhythm, and increasing their density would make the rhythm too busy and cluttered. The other tracks can be used to add variation and interest to the rhythm without overwhelming it.

    //  Kick 01:
    { 4,0,0,0, 0,0,4,0, 4,0,4,0, 0,0,0,0,   // bar 1: pattern A
      4,0,4,0, 0,0,4,0, 4,0,0,0, 0,0,0,0,   // bar 2: pattern B
      4,0,0,0, 4,0,4,0, 4,4,0,0, 0,4,0,0,   // bar 3: pattern A
      4,0,0,0, 0,0,4,0, 0,1,4,0, 0,4,1,1 }, // bar 4: B + fill ghosts

    
      
      Kick 02:
    { 4,0,0,0, 4,0,4,4, 0,0,4,0, 0,0,0,0,   // bar 1: pattern A
      4,0,0,0, 2,0,4,0, 0,0,0,0, 0,0,0,0,   // bar 2: pattern B
      4,0,0,0, 4,0,4,0, 4,4,0,0, 0,4,0,0,   // bar 3: pattern A
      4,0,0,0, 0,0,4,0, 0,1,4,0, 0,4,1,1 }, // bar 4: B + fill ghosts