#pragma once
#include <cstdint>

// ── Kick lab: the curated kick bank ───────────────────────────────────────────
//
// The kick pad's pool (`kDrumKick`, TouchPlaited.cpp) is two engines wide and
// randomizes inside parameter *ranges*. That is right for "give me a kit", and
// wrong for "give me a kick I chose" — the two things a range can't do are land
// on a specific sound twice and be talked about by name. This table is the
// second thing: fixed, numbered, auditionable points, stepped with P0+P10/P11.
//
// Read `notes.md` -> "Kick lab" before editing values. Three findings from
// reading the Plaits sources are baked into the numbers here and are not
// obvious from the knob names:
//
//  1. **Engine 21 renders two different drums.** OUT is the 808-style analog
//     bass drum (resonator + overdrive), AUX is the "inadvertently 909-ish"
//     synthetic one (`bass_drum_engine.cc`). `blend` crossfades between them,
//     so 0.0 and 1.0 are two instruments, not two mixes — and the kit
//     randomizer pins blend at 0.5, which is why every random kick has sounded
//     like the same half-and-half compromise.
//  2. **Engine 10 plays two octaves below its note** (`fm_engine.cc`:
//     `note = parameters.note - 24`), and its AUX is a further octave down.
//     The pool's 36-48 note range therefore put the FM kick's fundamental at
//     16-33 Hz with a modulation index near zero: inaudible body, no
//     harmonics. That is the "low energy kick model". Notes here are 56-72.
//  3. **`harmonics` on engine 21 is three controls in a row.** Analog side:
//     0.00-0.25 attack-FM depth, 0.25-0.50 self-FM, 0.50-1.00 overdrive.
//     Synthetic side: 0.00-0.50 pitch-sweep depth, 0.50-1.00 sweep length.
//     So h > 0.5 is where both the grit and the long pitch drops live, and the
//     pool's 0.20-0.55 range never reached it.
//
// Fields map 1:1 onto PadSlot, so applying a preset is a plain copy. Which of
// `morph`/`decay` matters depends on the engine: engines 19-23 are
// morph-decay (`decay_via_morph()`), so their tail is `decay` routed to morph
// and the `morph` field is dead; every other engine here uses `morph` as its
// own third parameter and `decay` as the LPG fall time.
//
// `punch` is how much S30 Drive pushes the kick's timbre at trigger
// (`drum_params()`). It used to be a flat 1.0 for slot 0, which drags timbre
// toward 1.0 — on engine 21 that opens the tone lowpass *and* raises the
// synthetic click level, so turning drive up made every deep preset bright and
// clicky. Deep presets ask for a small share of it; bright ones keep most.
//
// `layer` is an optional second voice fired with the kick (voice id 24, one
// shot, drum group). It costs one extra voice for as long as its own tail
// rings, which is why every layer here is short: the point is a transient the
// main body can't make, not a second sustained drum.

namespace synthux {

struct KickLayer {
    int   engine;      // -1 = no layer
    float harmonics, timbre, morph, decay, note, volume, blend;
};

struct KickPreset {
    const char* name;  // <= 12 chars — drawn next to "Knn" on the 128px OLED
    int   engine;
    float harmonics, timbre, morph, decay, note;
    float blend;       // OUT<->AUX; on engine 21 this picks analog vs synthetic
    float width;       // 0 = mono. Kicks are mono unless the preset says why not
    float volume;
    float punch;       // share of S30 Drive that reaches timbre (see above)
    KickLayer layer;
};

static constexpr KickLayer kNoLayer = { -1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };

// Grouped by engine so stepping walks a family at a time rather than
// alternating characters — auditioning wants neighbours that compare.
static const KickPreset kKickPresets[] = {
    // ── 21 Bass Drum, analog side (blend 0) — the 808 lineage ──────────────
    // Tail is `decay`->morph: the resonator Q is 1500 * 2^(morph*80/12), so
    // long tails need 0.75+. 0.78 at 39 Hz is roughly a 0.4 s ring, 0.90 is
    // over a second.
    { "808 DEEP",    21, 0.10f, 0.15f, 0.5f, 0.78f, 31.f, 0.00f, 0.f, 0.95f, 0.35f, kNoLayer },
    { "808 SUB",     21, 0.06f, 0.06f, 0.5f, 0.90f, 27.f, 0.00f, 0.f, 1.00f, 0.20f, kNoLayer },
    // h 0.88: overdrive term ~0.76 and self-FM at maximum — the analog drum's
    // own distortion, not the output stage's.
    { "808 DRIVE",   21, 0.88f, 0.35f, 0.5f, 0.62f, 32.f, 0.10f, 0.f, 0.85f, 0.45f, kNoLayer },

    // ── 21 Bass Drum, synthetic side (blend 1) — the 909 lineage ───────────
    // Sweep depth is h*2 (capped at 1), sweep *length* is h*2-1, so 0.80 is a
    // full-depth drop that takes its time. Dirtiness is 0.4 - 0.25*morph^2,
    // i.e. the short presets are the gritty ones — that is the engine's rule,
    // not a choice made here.
    { "909 PUNCH",   21, 0.30f, 0.45f, 0.5f, 0.55f, 36.f, 1.00f, 0.f, 0.90f, 0.60f, kNoLayer },
    { "909 SWEEP",   21, 0.80f, 0.28f, 0.5f, 0.72f, 38.f, 1.00f, 0.f, 0.90f, 0.40f, kNoLayer },
    { "909 CLICK",   21, 0.22f, 0.88f, 0.5f, 0.32f, 42.f, 0.90f, 0.f, 0.80f, 0.80f, kNoLayer },
    // Both drums at once, weighted to the analog body.
    { "HYBRID",      21, 0.38f, 0.30f, 0.5f, 0.66f, 33.f, 0.45f, 0.f, 0.90f, 0.50f, kNoLayer },

    // ── 10 Two-Op FM — notes are +24 to undo the engine's own octave drop ───
    // Ratio comes from a quantizer LUT, not a linear map: h 0.00 = 0.5x
    // (modulator an octave below the carrier), 0.24 = 1x, 0.47 = 2x,
    // 0.70 = 3.46x, 1.00 = 8x. Index is 2*timbre^2. Morph is feedback,
    // centred at 0.5 = none; below centre feeds the modulator's phase (grit),
    // above centre feeds the modulator itself (brighter, saw-like).
    { "FM 1:1",      10, 0.24f, 0.42f, 0.50f, 0.55f, 60.f, 0.25f, 0.f, 0.90f, 0.35f, kNoLayer },
    { "FM SUB 1:2",  10, 0.00f, 0.55f, 0.50f, 0.60f, 64.f, 0.15f, 0.f, 0.90f, 0.30f, kNoLayer },
    // AUX is a sub an octave under the carrier, phase-modulated by it: with
    // the carrier at MIDI 48 the sub lands on 65 Hz and the carrier's own
    // harmonics come through the modulation rather than directly.
    { "FM AUX SUB",  10, 0.24f, 0.30f, 0.50f, 0.70f, 72.f, 1.00f, 0.f, 0.95f, 0.25f, kNoLayer },
    { "FM GRIT",     10, 0.47f, 0.62f, 0.10f, 0.42f, 58.f, 0.35f, 0.f, 0.85f, 0.45f, kNoLayer },
    { "FM SNAP",     10, 0.70f, 0.80f, 0.72f, 0.20f, 62.f, 0.20f, 0.f, 0.80f, 0.70f, kNoLayer },
    { "FM LONG",     10, 0.24f, 0.26f, 0.50f, 0.88f, 60.f, 0.45f, 0.f, 0.90f, 0.25f, kNoLayer },

    // ── Outside bets: engines that reach kick territory but weren't written
    //    for it. Kept together at the end so a walk of the bank hits the
    //    reliable families first.
    // 12 Additive with almost no harmonics is the cleanest sine kick the
    // device can make — the reference point the others are judged against.
    { "SINE PURE",   12, 0.02f, 0.00f, 0.25f, 0.72f, 33.f, 0.15f, 0.f, 1.00f, 0.20f, kNoLayer },
    // 9 Waveshaping: harmonics is waveshape amount (0.5 = neutral), timbre is
    // the wavefolder. A sub with fold harmonics instead of FM ones.
    { "FOLD SUB",     9, 0.62f, 0.40f, 0.55f, 0.60f, 32.f, 0.20f, 0.f, 0.90f, 0.30f, kNoLayer },
    // 0 VA+VCF: harmonics is resonance (not cutoff — the notes.md table had
    // these two swapped, fixed with this branch), timbre is cutoff, and the
    // filter is not swept, so this is a resonant thump rather than a drum.
    { "VCF BOOM",     0, 0.92f, 0.30f, 0.35f, 0.45f, 34.f, 0.00f, 0.f, 0.85f, 0.35f, kNoLayer },
    // 20 Modal is on VoicePool::engine_is_heavy() — a woody knock, at real
    // cost. Here to be judged against that cost, not because it is free.
    { "MODAL KNOCK", 20, 0.22f, 0.28f, 0.5f, 0.30f, 36.f, 0.30f, 0.f, 0.85f, 0.40f, kNoLayer },

    // ── Layered: two voices per hit ────────────────────────────────────────
    // Deep bodies can't also be crisp — the tone lowpass that makes them deep
    // is what removes the top. These pair a long body with a short second
    // voice that supplies only the transient, so the extra voice is awake for
    // tens of milliseconds, not for the whole tail.
    { "SUB+CLICK",   21, 0.08f, 0.10f, 0.5f, 0.85f, 28.f, 0.00f, 0.f, 0.95f, 0.15f,
      { 17, 0.35f, 0.55f, 0.50f, 0.10f, 72.f, 0.45f, 0.50f } },
    { "SUB+SNAP",    21, 0.10f, 0.12f, 0.5f, 0.82f, 29.f, 0.00f, 0.f, 0.95f, 0.15f,
      { 10, 0.75f, 0.70f, 0.50f, 0.12f, 84.f, 0.35f, 0.20f } },
    // The cheapest layer available: one engine, both of its drums, each with
    // its own pitch and tail. 909 attack over an 808 sub — the stacked-kick
    // trick, without a second engine's render cost.
    { "909+808",     21, 0.72f, 0.35f, 0.5f, 0.60f, 36.f, 1.00f, 0.f, 0.85f, 0.45f,
      { 21, 0.05f, 0.08f, 0.5f, 0.88f, 28.f, 0.70f, 0.00f } },
};

static constexpr int kNumKickPresets =
    static_cast<int>(sizeof(kKickPresets) / sizeof(kKickPresets[0]));

} // namespace synthux
