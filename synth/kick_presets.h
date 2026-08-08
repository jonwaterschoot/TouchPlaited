#pragma once
#include <cstdint>

// ── Kick lab: the curated kick bank ───────────────────────────────────────────
//
// Twelve fixed, numbered kicks. They are what survived the 2026-08-08 hardware
// audition of the first twenty (`notes.md` -> "Kick lab" for the round in
// full, including what was cut and why): the whole 2-op FM family read as
// "regular synth" rather than as drums, and of the three layered entries only
// the one that layers engine 21 with itself survived.
//
// The bank now has two jobs, not one:
//   - it is the kick pad's **random pool** — `fill_drum_slot_from_pool(0)`
//     draws from here instead of from a parameter-range table, so a randomized
//     kit gets a kick that was chosen rather than rolled, and can be named;
//   - it is a **loadable model**: the last position of P0+S35 on the kick pad,
//     after which S32 selects within it and S33/S34/S37 tweak it.
//
// Three findings from reading the Plaits sources are baked into the numbers
// here and are not obvious from the knob names:
//
//  1. **Engine 21 renders two different drums.** OUT is the 808-style analog
//     bass drum (resonator + overdrive), AUX is the "inadvertently 909-ish"
//     synthetic one (`bass_drum_engine.cc`). `blend` crossfades between them,
//     so 0.0 and 1.0 are two instruments, not two mixes — and the old kit
//     randomizer pinned blend at 0.5, which is why every random kick sounded
//     like the same half-and-half compromise.
//  2. **`harmonics` on engine 21 is three controls in a row.** Analog side:
//     0.00-0.25 attack-FM depth, 0.25-0.50 self-FM, 0.50-1.00 overdrive.
//     Synthetic side: 0.00-0.50 pitch-sweep depth, 0.50-1.00 sweep length.
//     So h > 0.5 is where both the grit and the long pitch drops live, and the
//     old pool's 0.20-0.55 range never reached it.
//  3. **The tail wants morph 0.6+.** The analog resonator's Q is
//     `1500 * 2^(morph*80/12)`; the old pool's 0.05-0.30, then scaled again by
//     Tightness, could only ever make clicks.
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
// rings. Only one preset uses it, and deliberately: the two layers that paired
// engine 21 with a *different* engine both failed on hardware (a noise
// transient read as a crackle, an FM snap sat too high), while layering the
// bass drum engine with itself — its two drums, each with its own pitch and
// tail — is both the best-sounding and the cheapest of the three.

namespace synthux {

struct KickLayer {
    int   engine;      // -1 = no layer
    float harmonics, timbre, morph, decay, note, volume, blend;
};

// The three knobs that stay live on a loaded preset (S33 Tone, S34 Punch,
// S37 Body), as a window around what the preset was authored with rather than
// the raw 0..1 parameter. Two reasons the window and not the parameter:
//
//   - **Consistency.** The knobs are semantic — Tone is always "dark to
//     bright", Punch always "soft to hard", Body always "the drum's weight" —
//     but they land on the same three PadSlot fields (timbre, harmonics,
//     blend) whatever engine is underneath, and those fields mean wildly
//     different things per engine. The window is what makes one gesture mean
//     one thing across the bank.
//   - **Staying in kick territory.** Most of each engine's parameter space is
//     not a kick. Full-travel harmonics on engine 12 is a chord; full-travel
//     resonance on engine 0 is a whistle. The window is the part of the range
//     that still answers to the word "kick", so a knob cannot be turned out of
//     the instrument.
//
// The authored value always lies inside its own window, so loading a preset
// and then catching a knob never moves the sound before you turn it.
struct KickAxes {
    float tone_lo,  tone_hi;    // S33 -> timbre
    float punch_lo, punch_hi;   // S34 -> harmonics
    float body_lo,  body_hi;    // S37 -> blend
};

struct KickPreset {
    const char* name;  // <= 12 chars — drawn next to "Knn" on the 128px OLED
    int   engine;
    float harmonics, timbre, morph, decay, note;
    float blend;       // OUT<->AUX; on engine 21 this picks analog vs synthetic
    float width;       // 0 = mono. Kicks are mono unless the preset says why not
    float volume;
    float punch;       // share of S30 Drive that reaches timbre (see above)
    KickAxes axes;
    KickLayer layer;
};

static constexpr KickLayer kNoLayer = { -1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };

// Grouped by engine so stepping walks a family at a time rather than
// alternating characters — auditioning wants neighbours that compare. Eight
// on the bass drum engine, then the four outside bets that held up.
static const KickPreset kKickPresets[] = {
    // ── 21 Bass Drum, analog side (blend low) — the 808 lineage ────────────
    // Body windows stay analog-dominant: these presets are the 808, and
    // letting the knob cross into the synthetic drum would make three of the
    // twelve reach the same place.
    { "808 DEEP",    21, 0.10f, 0.15f, 0.5f, 0.78f, 31.f, 0.00f, 0.f, 0.95f, 0.35f,
      { 0.05f,0.50f, 0.02f,0.45f, 0.00f,0.35f }, kNoLayer },
    { "808 SUB",     21, 0.06f, 0.06f, 0.5f, 0.90f, 27.f, 0.00f, 0.f, 1.00f, 0.20f,
      { 0.02f,0.35f, 0.02f,0.35f, 0.00f,0.25f }, kNoLayer },
    // Punch window sits entirely in the overdrive third: this preset IS the
    // analog drum's own distortion, so the knob should trim it, not remove it.
    { "808 DRIVE",   21, 0.88f, 0.35f, 0.5f, 0.62f, 32.f, 0.10f, 0.f, 0.85f, 0.45f,
      { 0.15f,0.60f, 0.55f,1.00f, 0.00f,0.40f }, kNoLayer },

    // ── 21 Bass Drum, synthetic side (blend high) — the 909 lineage ────────
    // Punch is the pitch sweep here: depth up to 0.5, then length above it,
    // so the windows are wide — it is the most characterful knob on these
    // three and there is no part of it that stops being a kick.
    { "909 PUNCH",   21, 0.30f, 0.45f, 0.5f, 0.55f, 36.f, 1.00f, 0.f, 0.90f, 0.60f,
      { 0.15f,0.75f, 0.10f,0.90f, 0.65f,1.00f }, kNoLayer },
    { "909 SWEEP",   21, 0.80f, 0.28f, 0.5f, 0.72f, 38.f, 1.00f, 0.f, 0.90f, 0.40f,
      { 0.10f,0.60f, 0.50f,1.00f, 0.65f,1.00f }, kNoLayer },
    { "909 CLICK",   21, 0.22f, 0.88f, 0.5f, 0.32f, 42.f, 0.90f, 0.f, 0.80f, 0.80f,
      { 0.55f,1.00f, 0.05f,0.60f, 0.60f,1.00f }, kNoLayer },
    // The one preset whose Body knob is meant to cross the whole way: it is
    // the two drums together, and where between them is the point of it.
    { "HYBRID",      21, 0.38f, 0.30f, 0.5f, 0.66f, 33.f, 0.45f, 0.f, 0.90f, 0.50f,
      { 0.10f,0.65f, 0.10f,0.80f, 0.15f,0.85f }, kNoLayer },
    // Both drums as two voices rather than as a mix, each with its own pitch
    // and tail: 909 attack over an 808 sub. Body moves the main (909) voice
    // only — the 808 underneath is the layer and stays where it is, which is
    // what makes this a stack rather than a crossfade.
    { "909+808",     21, 0.72f, 0.35f, 0.5f, 0.60f, 36.f, 1.00f, 0.f, 0.85f, 0.45f,
      { 0.15f,0.70f, 0.35f,1.00f, 0.70f,1.00f },
      { 21, 0.05f, 0.08f, 0.5f, 0.88f, 28.f, 0.70f, 0.00f } },

    // ── Outside bets: engines that reach kick territory but weren't written
    //    for it. Their windows are the tightest in the bank, because the part
    //    of each engine that is a kick is a small corner of it.
    // 12 Additive with almost no harmonics is the cleanest sine kick the
    // device can make — the reference point the others are judged against.
    { "SINE PURE",   12, 0.02f, 0.00f, 0.25f, 0.72f, 33.f, 0.15f, 0.f, 1.00f, 0.20f,
      { 0.00f,0.25f, 0.00f,0.15f, 0.00f,0.40f }, kNoLayer },
    // 9 Waveshaping: harmonics is waveshape amount (0.5 = neutral), timbre is
    // the wavefolder. A sub with fold harmonics instead of FM ones.
    { "FOLD SUB",     9, 0.62f, 0.40f, 0.55f, 0.60f, 32.f, 0.20f, 0.f, 0.90f, 0.30f,
      { 0.15f,0.65f, 0.45f,0.80f, 0.00f,0.45f }, kNoLayer },
    // 0 VA+VCF: harmonics is resonance (not cutoff — the notes.md table had
    // these two swapped, fixed with this branch), timbre is cutoff. The filter
    // is not swept, so this is a resonant thump; below ~0.8 resonance it stops
    // being one, which is where the punch window starts.
    { "VCF BOOM",     0, 0.92f, 0.30f, 0.35f, 0.45f, 34.f, 0.00f, 0.f, 0.85f, 0.35f,
      { 0.18f,0.42f, 0.80f,1.00f, 0.00f,0.30f }, kNoLayer },
    // 20 Modal is on VoicePool::engine_is_heavy() — a woody knock, at real
    // cost. Kept because it earned it on hardware, not because it is free.
    { "MODAL KNOCK", 20, 0.22f, 0.28f, 0.5f, 0.30f, 36.f, 0.30f, 0.f, 0.85f, 0.40f,
      { 0.10f,0.50f, 0.10f,0.45f, 0.10f,0.50f }, kNoLayer },
};

static constexpr int kNumKickPresets =
    static_cast<int>(sizeof(kKickPresets) / sizeof(kKickPresets[0]));

// True if `e` is an engine the bank uses, layers included — the kick pad's
// "is this sound still in role" test now that the bank is its random pool.
inline bool kick_bank_has_engine(int e) {
    for (int i = 0; i < kNumKickPresets; i++) {
        if (kKickPresets[i].engine == e) return true;
        if (kKickPresets[i].layer.engine == e) return true;
    }
    return false;
}

} // namespace synthux
