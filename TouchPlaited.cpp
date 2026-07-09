#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "touch/touch.h"
#include "synth/voice_pool.h"
#include "synth/sequencer.h"
#include "synth/fx.h"
#include "midi/midi_io.h"
#include "log.h"
#include <algorithm>
#include <cmath>

using namespace daisy;
using namespace synthux;

DaisySeed hw;
Touch touch;
VoicePool pool;
Sequencer seq;
MidiIO midi;

// ISR load measurement — groundwork for expanding the voice pool. Printed over
// serial every 2s from the main loop (visible via `make debug` / USB serial).
static CpuLoadMeter cpu_meter;

// Load shedding: 6 worst-case voices can exceed the 4ms block budget (measured
// 138% peak). If the previous block ran past the threshold, force-sleep the
// oldest non-held voice at the start of the next block — an early tail fade
// instead of a DMA overrun.
static float blk_ticks_inv   = 0.f;   // set in main()
static float last_block_load = 0.f;
static volatile uint32_t shed_count = 0;   // diagnostics, printed with CPU load
static constexpr float kShedThreshold = 0.90f;

// seq_mode_on = SW2 Up: pads are drums, seq knobs live, drum recording available.
// The sequencer itself (seq.IsActive()) is independent — it can keep playing in
// the background while SW2 sits on Basic Pitch or Random. P2+P11 toggles it.
static bool seq_mode_on      = false;
static bool seq_entered_once = false;   // first SW2-Up entry auto-starts the seq

// Seq settings, stored separately from the pots. They only follow a pot after
// it picks up (crosses the stored value) inside Seq mode — so using the same
// pot in another mode, or re-entering Seq, never jumps a setting.
// Defaults match the Sequencer class defaults (120 BPM, no shuffle, density 2).
static float seq_tempo_lk = 0.5f;
static float seq_shuf_lk  = 0.0f;
static float seq_dens_lk  = 0.5f;
static float seq_punch_lk = 0.0f;
static float seq_tight_lk = 0.5f;
static float seq_drive_lk = 0.0f;
static float seq_var_lk   = 0.0f;   // S35 in Seq = pattern variant within genre
static int   seq_genre_lk = 0;      // SW1 in Seq = genre; change-latched (see SW1 handler)
static float seq_vol_lk   = 1.0f;   // S36 in Seq = drum group volume
static float seq_width_lk = 0.0f;   // P0+S37 in Seq = drum-group stereo width (0 = mono)

// Pitched-mode fader state. Volume (S36) and blend (S37) go through pickup:
// S37 doubles as the width control while P0 is held, and recording borrows
// both faders — a raw live read would jump the setting on every hand-off.
// Width defaults to 0: the mix is mono until the width control opens it.
static float pitched_vol_lk   = 1.0f;
static float pitched_blend_lk = 0.5f;
static float pitched_width_lk = 0.0f;

// FX mirror knobs (P1+S30 = reverb, P1+S35 = delay), stored per group like
// the volume/width pairs so drums and synth keep independent wet levels.
// Encoding: 0.5 = off (center dead zone); left of center = character A, wet
// grows toward 0.0; right of center = character B, wet grows toward 1.0.
// Reverb: room | hall. Delay: slapback | synced dotted 1/8.
// The *_char values remember the last edit from either group — the shared
// FX instance takes its character (side + coupled params) from there.
// Drum recording adds a per-slot trim on top of the drum-group send (PadSlot
// rev_send/dly_send, edited with the same P1 combo): group send × slot trim,
// so the mirror knob stays the master wet and the trims fine-balance the kit.
static float fx_rev_seq_lk = 0.5f, fx_rev_pitched_lk = 0.5f, fx_rev_char_lk = 0.5f;
static float fx_dly_seq_lk = 0.5f, fx_dly_pitched_lk = 0.5f, fx_dly_char_lk = 0.5f;

// Mirror-knob decode: side -1/0/+1 and wet 0..1 (distance from the dead zone).
static constexpr float kFxDeadZone = 0.06f;
static int fx_decode(float v, float* wet) {
    const float half = 0.5f - kFxDeadZone;
    if (v < half)        { *wet = (half - v) / half;         return -1; }
    if (v > 1.f - half)  { *wet = (v - (1.f - half)) / half; return  1; }
    *wet = 0.f;
    return 0;
}

// Unified long-hold threshold for recording confirm / copy in every mode.
// 1200 ms: long enough that holding a sustained note in Random doesn't trip
// it by accident.
static constexpr uint32_t kLongHoldBlocks = 300;  // 1200 ms at 4 ms/block

// Recording *entry* is longer still: 2 s, with an accelerating LED countdown
// (main loop) from ~0.2 s in so the wait reads as intentional, not dead.
static constexpr uint32_t kRecEntryHoldBlocks = 500;  // 2000 ms at 4 ms/block
static constexpr uint32_t kRecEntryAnimStart  = 50;   // countdown visible from 200 ms

// ─── Random ───────────────────────────────────────────────────────────────────
static uint32_t rng = 1;
static float rand_f() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng >> 8) / 16777216.0f;
}
static float rand_range(float lo, float hi) { return lo + rand_f() * (hi - lo); }
static float clampf(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

// Width fader bottom = mono: snap the last few percent to exactly 0 so "fader
// completely down" reliably means mono, not width 0.02.
static float snap_width(float v) { return v < 0.03f ? 0.f : v; }

// ─── Pad slots ────────────────────────────────────────────────────────────────
static constexpr int kPadSlots = 7;

struct PadSlot {
    int   engine    = 0;
    float harmonics = 0.5f;
    float timbre    = 0.5f;
    float morph     = 0.5f;
    float decay     = 0.5f;
    float note      = 60.0f;
    float volume    = 1.0f;
    float drive     = 0.0f;
    float blend     = 0.5f;   // OUT↔AUX mono mix: 0 = OUT only, 1 = AUX only
    float width     = 1.0f;   // this slot's share of the group stereo width
    float rev_send  = 1.0f;   // per-slot trim on the group FX sends (P1+S30 /
    float dly_send  = 1.0f;   // P1+S35 in drum recording); 1 = follow fully
};

// Engines whose internal decay lives on MORPH: String and Modal route it to
// the physical model's damping, the drum engines (21–23) to their tail. The
// Decay knob owns morph for these; S34 has no effect on them.
static bool morph_is_decay(int e) { return e >= 19 && e <= 23; }

// Build render params from a slot, routing decay→morph for morph-decay
// engines. tight >= 0 additionally scales that tail (Seq S37 tightness).
static VoiceParams slot_params(const PadSlot& s, float tight = -1.f) {
    VoiceParams p;
    p.engine    = s.engine;
    p.harmonics = s.harmonics;
    p.timbre    = s.timbre;
    p.morph     = s.morph;
    p.decay     = s.decay;
    p.volume    = s.volume;
    p.drive     = s.drive;
    p.blend     = s.blend;
    p.width     = s.width;
    // Squared like the group sends (audio taper — see the fx_decode block).
    p.rev_send  = s.rev_send * s.rev_send;
    p.dly_send  = s.dly_send * s.dly_send;
    if (morph_is_decay(s.engine))
        p.morph = (tight < 0.f) ? s.decay : s.decay * (0.2f + tight * 0.8f);
    return p;
}
// Per-mode slot arrays — each playmode keeps its own state so flicking SW2
// back and forth restores the last sound (mode memory). Only P0+P2 re-randomizes.
static PadSlot pad_slots[kPadSlots];        // Random mode
static bool    slots_ready  = false;
static PadSlot drum_slots[kPadSlots];       // Seq mode drum kit
static bool    drum_kit_ready = false;      // generated once; refreshed only by P0+P2 stage 2
static bool    is_drum_mode = false;

// Basic Pitch P0+P2 randomize snapshots. While active, pads play these instead
// of the live global knobs; grabbing any timbral knob returns to live mode.
static PadSlot bp_slots[kPadSlots];
static bool    bp_slots_active = false;
static float   bp_ref_h = 0.f, bp_ref_t = 0.f, bp_ref_m = 0.f, bp_ref_d = 0.f;

// Slot array being edited in recording mode: drums in Seq, pitched slots in Random.
static PadSlot* live_slots() { return is_drum_mode ? drum_slots : pad_slots; }

static float last_h = 0.5f, last_t = 0.5f, last_m = 0.5f, last_d = 0.5f;

// Effective pitched params — what actually sounds. Each follows its pot once
// the pot has picked up (caught from boot), and holds a MIDI CC write until
// the pot crosses that value. last_* stay raw pot reads: the Basic Pitch
// snapshot escape watches pot *movement*, which a CC write must not fake.
static float eff_h = 0.5f, eff_t = 0.5f, eff_m = 0.5f, eff_d = 0.5f;
static float eff_drive = 0.0f;
// LPG colour has no pot anymore (retired for the unified Decay) — CC25 is its
// only writer; 0.5 is the old hardcoded neutral.
static float lpg_colour = 0.5f;

static int current_engine = 0;

static int octave_offset        = 0;
static int root_semitone        = 0;
static constexpr int kPitchBase = 60;

static void generate_soft_random(PadSlot* slots, int engine,
                                 float h, float t, float m, float d, float spread) {
    for (int i = 0; i < kPadSlots; i++) {
        slots[i].engine    = engine;
        slots[i].harmonics = clampf(h + rand_range(-spread, spread));
        slots[i].timbre    = clampf(t + rand_range(-spread, spread));
        slots[i].morph     = clampf(m + rand_range(-spread, spread));
        slots[i].decay     = clampf(d + rand_range(-spread, spread));
        // Blend anchors to the current S37 fader, like the other centers —
        // otherwise snapshots jump to 50/50 no matter where the mix was.
        slots[i].blend     = pitched_blend_lk;
    }
}

// Full-random engine pool: everything except Chiptune (7) — autonomous
// arpeggiator plays without gate. Drum engines 21–23 are included; they play
// at scale pitches like any other engine.
static const int kFRAll[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 }; // 23
static int rand_engine_any() { return kFRAll[static_cast<int>(rand_f() * 23.f) % 23]; }

// Six-Op A/B/C (engines 2–4) are nearly silent over most of their parameter
// space: harmonics is a quantized DX7 patch selector and timbre is the FM
// modulator level (quiet near zero). These presets land on audible spots —
// used for the model-change audition AND as the anchor for random generation.
struct AudPreset { float h, t, m; };
static const AudPreset kSixOpAud[3] = {
    { 0.15f, 0.75f, 0.40f },  // 2 Six-Op A
    { 0.20f, 0.70f, 0.45f },  // 3 Six-Op B
    { 0.10f, 0.80f, 0.30f },  // 4 Six-Op C
};

// Per-role drum options: { engine, morph_lo,hi, timbre_lo,hi, harm_lo,hi, decay_lo,hi, note_lo,hi }
struct DrumOpt { int e; float mlo,mhi, tlo,thi, hlo,hhi, dlo,dhi, nlo,nhi; };

static const DrumOpt kDrumKick[]  = {
    { 21, 0.05f,0.30f, 0.20f,0.65f, 0.20f,0.55f, 0.4f,0.8f, 36.f,48.f },
    { 10, 0.10f,0.30f, 0.00f,0.30f, 0.10f,0.40f, 0.3f,0.6f, 36.f,48.f },
};
static const DrumOpt kDrumSnare[] = {
    { 22, 0.10f,0.60f, 0.30f,0.80f, 0.30f,0.70f, 0.4f,0.8f, 48.f,60.f },
    { 17, 0.05f,0.20f, 0.55f,0.90f, 0.30f,0.70f, 0.3f,0.6f, 48.f,60.f },
    { 18, 0.05f,0.20f, 0.40f,0.80f, 0.10f,0.50f, 0.3f,0.6f, 48.f,60.f },
};
// Hats live high: note is the pitch center (engine 23) / filter center (17).
// 60–84 sat in the melodic register and read as tonal noise, not metal.
static const DrumOpt kDrumCHH[]   = {
    { 23, 0.02f,0.12f, 0.50f,0.90f, 0.40f,0.80f, 0.4f,0.8f, 84.f,100.f },
    { 17, 0.02f,0.10f, 0.65f,0.95f, 0.45f,0.75f, 0.3f,0.6f, 84.f,100.f },
};
static const DrumOpt kDrumOHH[]   = {
    { 23, 0.35f,0.60f, 0.45f,0.85f, 0.40f,0.80f, 0.4f,0.8f, 80.f,96.f },
};
static const DrumOpt kDrumClap[]  = {
    { 22, 0.55f,0.90f, 0.65f,0.95f, 0.50f,0.90f, 0.4f,0.7f, 48.f,62.f },
    { 17, 0.05f,0.20f, 0.70f,1.00f, 0.40f,0.80f, 0.3f,0.5f, 55.f,70.f },
    { 18, 0.05f,0.20f, 0.50f,0.90f, 0.10f,0.50f, 0.3f,0.5f, 55.f,70.f },
};
static const DrumOpt kDrumTom[]   = {
    { 21, 0.30f,0.65f, 0.10f,0.40f, 0.30f,0.60f, 0.4f,0.8f, 48.f,72.f },
    { 20, 0.30f,0.70f, 0.20f,0.60f, 0.10f,0.50f, 0.4f,0.8f, 48.f,72.f },
};
// Perc: short transient hits only. String (19) is out — Karplus-Strong reads
// as a loud melodic pluck, not percussion; Modal's tail is capped well below
// its old 0.90 for the same reason. Snare (22) pitched high + body-heavy
// timbre gives a rim/wood tick.
static const DrumOpt kDrumPerc[]  = {
    { 20, 0.10f,0.30f, 0.20f,0.55f, 0.30f,0.60f, 0.4f,0.8f, 60.f,84.f },
    { 23, 0.10f,0.30f, 0.50f,0.90f, 0.50f,1.00f, 0.4f,0.7f, 76.f,96.f },
    { 18, 0.10f,0.30f, 0.30f,0.70f, 0.30f,0.70f, 0.3f,0.6f, 60.f,80.f },
    { 22, 0.05f,0.20f, 0.05f,0.35f, 0.30f,0.60f, 0.3f,0.5f, 66.f,80.f },
};

static void fill_drum_slot(PadSlot& s, const DrumOpt* opts, int n) {
    const DrumOpt& o = opts[static_cast<int>(rand_f() * n) % n];
    s.engine    = o.e;
    s.morph     = 0.5f;                      // fixed; drum engines use decay→morph routing at trigger
    s.timbre    = rand_range(o.tlo, o.thi);
    s.harmonics = rand_range(o.hlo, o.hhi);
    s.decay     = rand_range(o.mlo, o.mhi);  // tail time stored in decay; mlo/mhi are the tail ranges
    s.note      = rand_range(o.nlo, o.nhi);
}

static void generate_drum_random() {
    fill_drum_slot(drum_slots[0], kDrumKick,  2); drum_slots[0].volume = 0.90f;
    fill_drum_slot(drum_slots[1], kDrumSnare, 3); drum_slots[1].volume = 0.80f;
    fill_drum_slot(drum_slots[2], kDrumCHH,   2); drum_slots[2].volume = 0.55f;
    fill_drum_slot(drum_slots[3], kDrumOHH,   1); drum_slots[3].volume = 0.65f;
    fill_drum_slot(drum_slots[4], kDrumClap,  3); drum_slots[4].volume = 0.75f;
    fill_drum_slot(drum_slots[5], kDrumTom,   2); drum_slots[5].volume = 0.70f;
    fill_drum_slot(drum_slots[6], kDrumPerc,  4); drum_slots[6].volume = 0.50f;
    // slot.drive is a ratio of the overall S30 drive in seq mode; 1.0 = follow fully.
    // Blend/width/FX-send trims reset with the kit: a mono flag, AUX-only blend
    // or dry-trimmed send from an old kit shouldn't silently reshape whatever
    // new engine lands on the slot.
    for (int i = 0; i < kPadSlots; i++) {
        drum_slots[i].drive    = 1.0f;
        drum_slots[i].blend    = 0.5f;
        drum_slots[i].width    = 1.0f;
        drum_slots[i].rev_send = 1.0f;
        drum_slots[i].dly_send = 1.0f;
    }
    drum_kit_ready = true;
}

// Render params for drum slot i exactly as a seq trigger shapes them (tail→
// morph via tightness, kick punch, overall drive × slot ratio). Shared by
// triggers and rec-mode auditions, so tweaking a drum against the paused seq
// sounds the same as it will when the pattern runs.
static VoiceParams drum_params(int i) {
    const PadSlot& s = drum_slots[i];
    VoiceParams p = slot_params(s, seq_tight_lk);
    if (i == 0) p.timbre = p.timbre + seq_punch_lk * (1.0f - p.timbre);
    p.drive = clampf(seq_drive_lk * s.drive);
    return p;
}

// Fire drum slot i — used by both seq steps and manual pad hits in Seq mode.
// Slot id 16+i keeps drum voices out of reach of pad NoteOffs (slots 0–6), and
// lock_params=true shields them from the active playmode's global knob writes.
static void trigger_drum(int i, float vel = 1.0f) {
    VoiceParams p = drum_params(i);
    p.volume *= vel;   // MIDI velocity; pads and seq steps pass 1.0
    pool.NoteOnWithParams(16 + i, drum_slots[i].note, p, true);
}

// Seq P0+P2 stage 1: nudge params of the current kit — same engines, same notes.
static void mutate_drum_soft() {
    for (int i = 0; i < kPadSlots; i++) {
        auto& s = drum_slots[i];
        s.harmonics = clampf(s.harmonics + rand_range(-0.10f, 0.10f));
        s.timbre    = clampf(s.timbre    + rand_range(-0.10f, 0.10f));
        s.decay     = clampf(s.decay     + rand_range(-0.08f, 0.08f));
    }
}

// Random mode P0+P2: every slot gets a random engine + params from kFRAll.
// Stage 1 locks decay to the current S37 value; stage 2 spreads decay around it.
static void generate_full_random(bool spread_decay) {
    float root = static_cast<float>(kPitchBase + root_semitone + octave_offset * 12);
    for (int i = 0; i < kPadSlots; i++) {
        int e = rand_engine_any();
        pad_slots[i].engine = e;
        if (e >= 2 && e <= 4) {
            // Six-Op: random values usually land silent — vary around the
            // known-audible preset instead.
            const auto& p = kSixOpAud[e - 2];
            pad_slots[i].harmonics = clampf(p.h + rand_range(-0.08f, 0.08f));
            pad_slots[i].timbre    = clampf(p.t + rand_range(-0.15f, 0.15f));
            pad_slots[i].morph     = clampf(p.m + rand_range(-0.15f, 0.15f));
        } else {
            pad_slots[i].harmonics = rand_range(0.2f, 0.8f);
            pad_slots[i].timbre    = rand_range(0.2f, 0.8f);
            pad_slots[i].morph     = rand_range(0.2f, 0.8f);
        }
        // Anchor to the effective decay (pot or MIDI CC23), like the other centers.
        pad_slots[i].decay = spread_decay ? clampf(eff_d + rand_range(-0.25f, 0.25f))
                                          : eff_d;
        pad_slots[i].note  = root;
    }
    slots_ready = true;
}

// ─── Playmode ─────────────────────────────────────────────────────────────────
// Switch3: 0=CENTER, 1=UP, 2=DOWN
// A() = SW2 (right, _switch_7_8): Init(D7, D6) → D7=Down(pos1), D6=Up(pos2)
// B() = SW1 (left, _switch_9_10): Init(D9, D8) → D9=left-flick(pos1), D8=right-flick(pos2)
enum class PlayMode { BASIC_PITCH, RANDOM };
static volatile PlayMode current_mode = PlayMode::BASIC_PITCH;

// A() polarity verified on hardware: sw=2=Down→Basic, sw=0=Center→Soft, sw=1=Up→Full
static PlayMode sw2_to_mode(int sw) {
    if (sw == 0) return PlayMode::RANDOM;
    return PlayMode::BASIC_PITCH;
}

// SW1 (B()): pos1=left-flick=3 blinks, center=2, pos2=right-flick=1 blink
static int sw1_blink_count(int sw) {
    return (sw == 2) ? 1 : (sw == 0) ? 2 : 3;
}
// SW2 (A()): sw=1=Up=3 blinks, center=2, sw=2=Down=1 blink
static int sw2_blink_count(int sw) {
    return (sw == 2) ? 1 : (sw == 0) ? 2 : 3;
}

// ─── Recording mode ───────────────────────────────────────────────────────────
// Active in modes 2 and 3 only; P0+pad tap enters RECORDING, P0+other pad enters COPY.
enum class RecMode { IDLE, RECORDING };
static volatile RecMode rec_mode = RecMode::IDLE;
static int              rec_slot = -1;      // 0–6: slot being edited
static PadSlot          rec_backup;          // saved state, restored on cancel

// Recording entry: hold pad 3–9 for kRecEntryHoldBlocks (AudioCallback-driven,
// not touch callback). The count is read by the main loop for the accelerating
// entry-countdown LED animation. entry_wait_clear latches counting off after a
// rec exit until every musical pad is released — the hold that confirmed a
// save must not bleed into a fresh 2 s entry.
static int               entry_hold_pad    = -1;
static volatile uint32_t entry_hold_count  = 0;
static bool              entry_wait_clear  = false;

// Secondary pad tracking for cancel / copy while in recording. copy_hold_anim
// mirrors the copy hold progress (only while the source pad is also down) for
// the main-loop LED: the entry countdown animation restarts during a copy.
static int               cancel_pad     = -1;
static uint32_t          cancel_count   = 0;
static volatile uint32_t copy_hold_anim = 0;

// Set on rec entry; the main loop plays a short LED burst before settling
// into the recording heartbeat.
static volatile bool rec_entry_flash = false;

// Recording heartbeat trigger: set whenever the slot being edited actually
// sounds (seq step or paused-seq audition); the main loop answers each hit
// with one fast double blink, so the LED is locked to the audio.
static volatile bool rec_hit_flash = false;

// Knob pickup — the pot takes effect once it reaches or crosses the stored
// value. Inclusive comparison + a near-window, so targets at the pot extremes
// (0.0 / 1.0) are reachable — a strict crossing test can never fire there,
// which is how S30/S34 went dead after seq re-entry with drive/punch at zero.
struct KnobPickup {
    static constexpr float kNear = 0.01f;
    float thresh = 0.f;
    float prev   = 0.f;
    bool  caught = false;
    // Arm against a stored value: the pot takes over when it reaches it.
    void arm_to(float target, float current_val) { thresh = target; prev = current_val; caught = false; }
    void force_catch(float current_val) { thresh = current_val; prev = current_val; caught = true; }
    bool update(float curr) {
        if (!caught) {
            bool crossed = (prev <= thresh && curr >= thresh) ||
                           (prev >= thresh && curr <= thresh);
            bool near    = (curr - thresh <= kNear) && (thresh - curr <= kNear);
            if (crossed || near) caught = true;
        }
        prev = curr;
        return caught;
    }
};
static KnobPickup rec_k30, rec_k31, rec_k32, rec_k33, rec_k34, rec_k36, rec_k37;
// P1 held in drum recording: S30/S35 become the slot's FX send trims (level
// only — the character stays on the global mirror knobs). Armed on the P1
// press edge; the bare roles (drive / model select) re-arm on release.
static KnobPickup rec_k30fx, rec_k35fx;
static bool       rec_p1_last = false;   // press/release edge tracking

// CC pickups gate the pots behind the eff_* params. Force-caught at boot so
// the pots are live from the start; a MIDI CC write re-arms them.
static KnobPickup cc_pu_h, cc_pu_t, cc_pu_m, cc_pu_d, cc_pu_drive;

// Width controls catch on *movement*, not value-crossing. Stored width is
// usually 1.0 (fader top), so a crossing pickup demanded riding the fader to
// the very top while holding P0 before it engaged — the control felt dead.
// Instead: hold P0 and nudge the fader ~3%; from then on (until re-armed by a
// P0 release) the fader position IS the width, absolutely.
struct MoveCatch {
    static constexpr float kDeadZone = 0.03f;
    float ref    = 0.f;
    bool  caught = false;
    void arm(float current_val) { ref = current_val; caught = false; }
    bool update(float curr) {
        if (!caught) {
            float d = curr - ref;
            if (d < 0.f) d = -d;
            if (d >= kDeadZone) caught = true;
        }
        return caught;
    }
};
static MoveCatch rec_k37w;    // P0+S37 in recording = slot stereo width
static MoveCatch seq_puw;     // P0+S37 in Seq = drum-group stereo width
static MoveCatch pitch_pu_w;  // P0+S37 in pitched modes = pitched-group width
static MoveCatch fx_mc_rev;   // P1+S30 = reverb mirror knob (any mode)
static MoveCatch fx_mc_dly;   // P1+S35 = delay mirror knob (any mode)

// Pickups for the seq settings — armed on every Seq entry (see SW2 handling)
// and re-armed after recording (rec mode borrows most of these pots).
static KnobPickup seq_pu30, seq_pu31, seq_pu32, seq_pu33, seq_pu34, seq_pu35, seq_pu36,
                  seq_pu37;
// Pitched-mode faders — armed at boot and on every return to a pitched mode.
static KnobPickup pitch_pu_vol;     // S36 volume
static KnobPickup pitch_pu_blend;   // S37 OUT↔AUX blend

static void rearm_seq_pickups() {
    auto& kn = touch.knobs();
    seq_pu30.arm_to(seq_drive_lk, kn.s30().Value());
    seq_pu31.arm_to(seq_tempo_lk, kn.s31().Value());
    seq_pu32.arm_to(seq_shuf_lk,  kn.s32().Value());
    seq_pu33.arm_to(seq_dens_lk,  kn.s33().Value());
    seq_pu34.arm_to(seq_punch_lk, kn.s34().Value());
    seq_pu35.arm_to(seq_var_lk,   kn.s35().Value());
    seq_pu36.arm_to(seq_vol_lk,   kn.s36().Value());
    seq_pu37.arm_to(seq_tight_lk, kn.s37().Value());
    seq_puw.arm(kn.s37().Value());
    // Recording borrows S36/S37 in Random mode too — re-arm the pitched set.
    pitch_pu_vol.arm_to(pitched_vol_lk,     kn.s36().Value());
    pitch_pu_blend.arm_to(pitched_blend_lk, kn.s37().Value());
    pitch_pu_w.arm(kn.s37().Value());
}

// ─── MIDI (mapping design in notes.md → "MIDI mapping sketch") ────────────────
// Channel split: ch1 = pitched — the note number IS the pitch, bypassing the
// pad scale/root/octave logic — ch10 = drums via the GM map. CC20–31 and
// 85–88 map to *functions*, not pots; a CC write re-arms that pot's pickup so
// the pot must cross the value to take over (same rule as mode hand-offs).
// Handlers run from MidiIO::Service (main loop) inside an IRQ-off section.
static constexpr uint8_t kMidiPitchCh  = 0;    // ch1
static constexpr uint8_t kMidiDrumCh   = 9;    // ch10
static constexpr int     kMidiSlotBase = 32;   // pool slot ids: pads 0–6, drums 16–22, MIDI 32+note

// GM percussion → drum slot (kick, snare, CHH, OHH, clap, tom, perc). Phase 1:
// the note picks the slot, the slot's stored pitch plays — same as a pad hit.
static int gm_to_drum_slot(uint8_t note) {
    switch (note) {
        case 35: case 36:                               return 0;  // kicks
        case 38: case 40:                               return 1;  // snares
        case 42: case 44:                               return 2;  // closed + pedal hat
        case 46:                                        return 3;  // open hat
        case 39:                                        return 4;  // clap
        case 41: case 43: case 45: case 47: case 48:
        case 50:                                        return 5;  // toms
        case 37: case 54: case 56: case 75: case 76:    return 6;  // rim/tamb/cowbell/clave/woodblock
        default:                                        return -1;
    }
}
// GM note sent out per drum slot (pad hits in Seq mode + seq steps).
static const uint8_t kDrumSlotGm[kPadSlots] = { 36, 38, 42, 46, 39, 45, 37 };

// Pitched note sent out per pad slot, remembered so the NoteOff matches even
// if octave/root changed while the pad was held. -1 = nothing sent.
static int midi_pad_note_out[kPadSlots] = { -1, -1, -1, -1, -1, -1, -1 };

static void on_midi_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    float gain = static_cast<float>(vel) * (1.0f / 127.0f);

    if (ch == kMidiDrumCh) {
        int slot = gm_to_drum_slot(note);
        if (slot < 0) return;
        if (!drum_kit_ready) generate_drum_random();
        trigger_drum(slot, gain);
        return;
    }
    if (ch != kMidiPitchCh) return;

    // Pitched: play the current mode's sound. Slots are multi-timbral in
    // Random / BP-snapshot modes — cycle the 7 slots by note so each key has
    // a stable sound. In live Basic Pitch the per-block global setters keep
    // updating these voices, so knobs stay live on held MIDI notes too.
    VoiceParams p;
    if (current_mode == PlayMode::RANDOM && slots_ready) {
        p = slot_params(pad_slots[note % kPadSlots]);
    } else if (current_mode == PlayMode::BASIC_PITCH && bp_slots_active) {
        p = slot_params(bp_slots[note % kPadSlots]);
    } else {
        p.engine    = current_engine;
        p.harmonics = eff_h;
        p.timbre    = eff_t;
        p.morph     = morph_is_decay(current_engine) ? eff_d : eff_m;
        p.decay     = eff_d;
        p.drive     = eff_drive;
        p.blend     = pitched_blend_lk;
    }
    p.volume *= gain;   // scale, don't replace — Random slots carry recorded volumes
    pool.NoteOnWithParams(kMidiSlotBase + note, static_cast<float>(note), p);
}

static void on_midi_note_off(uint8_t ch, uint8_t note) {
    // Drums are one-shots — only pitched notes gate off.
    if (ch == kMidiPitchCh) pool.NoteOff(kMidiSlotBase + note);
}

static void on_midi_cc(uint8_t /*ch*/, uint8_t cc, uint8_t val) {
    float v = static_cast<float>(val) * (1.0f / 127.0f);
    auto& kn = touch.knobs();
    switch (cc) {
        // Pitched sound (pots live again once they cross the CC value):
        case 20: eff_h = v;  cc_pu_h.arm_to(v, kn.s32().Value());  break;
        case 21: eff_t = v;  cc_pu_t.arm_to(v, kn.s33().Value());  break;
        case 22: eff_m = v;  cc_pu_m.arm_to(v, kn.s34().Value());  break;
        case 23: eff_d = v;  cc_pu_d.arm_to(v, kn.s31().Value());  break;
        // Drive is one function that exists in both knob layouts — a CC write
        // sets the pitched drive and the seq drum drive together.
        case 24: eff_drive = v;    cc_pu_drive.arm_to(v, kn.s30().Value());
                 seq_drive_lk = v; seq_pu30.arm_to(v, kn.s30().Value());     break;
        case 25: lpg_colour = v;                                             break;
        case 26: pitched_vol_lk = v; pitch_pu_vol.arm_to(v, kn.s36().Value()); break;
        // Seq settings — also pushed to the sequencer directly, so tempo etc.
        // respond while it plays in the background of a pitched mode (where
        // the per-block seq updates are gated off):
        case 27: seq_tempo_lk = v; seq_pu31.arm_to(v, kn.s31().Value());
                 seq.SetTempo(v);                                            break;
        case 28: seq_shuf_lk = v;  seq_pu32.arm_to(v, kn.s32().Value());
                 seq.SetShuffle(v);                                          break;
        case 29: seq_dens_lk = v;  seq_pu33.arm_to(v, kn.s33().Value());
                 seq.SetDensity(v);                                          break;
        case 30: seq_punch_lk = v; seq_pu34.arm_to(v, kn.s34().Value());     break;
        case 31: seq_tight_lk = v; seq_pu37.arm_to(v, kn.s37().Value());     break;
        // FX mirror values, same center-off encoding as the P1 knob layer
        // (64 ≈ off; below = character A, above = character B, wet grows
        // outward). The pot writes to whichever group is active; over MIDI
        // each group gets its own CC so a DAW can automate the drum sends
        // while playing a pitched mode. Character is shared — last edit from
        // either group wins, exactly like the knob. Re-arming the movement-
        // catch means a caught pot must be nudged again to take back over.
        case 85: fx_rev_pitched_lk = v; fx_rev_char_lk = v;
                 fx_mc_rev.arm(kn.s30().Value());                            break;
        case 86: fx_rev_seq_lk = v;     fx_rev_char_lk = v;
                 fx_mc_rev.arm(kn.s30().Value());                            break;
        case 87: fx_dly_pitched_lk = v; fx_dly_char_lk = v;
                 fx_mc_dly.arm(kn.s35().Value());                            break;
        case 88: fx_dly_seq_lk = v;     fx_dly_char_lk = v;
                 fx_mc_dly.arm(kn.s35().Value());                            break;
        default: break;
    }
}

// DAW stop sends CC123 — release MIDI voices only; pad-held notes stay.
static void on_midi_all_notes_off() {
    for (int n = 0; n < 128; n++) pool.NoteOff(kMidiSlotBase + n);
}

// ── MIDI clock ────────────────────────────────────────────────────────────────
// External clock (F8 on either port) hard-syncs the seq — 6 ticks per 16th —
// and mutes the tempo knob/CC27 (the block counter isn't consulted). While
// external clock is present, incoming clock + transport pass through to the
// output; without it, the seq generates 24 ppqn itself (see AudioCallback)
// and local start/stop send FA/FB/FC. 500ms without a tick = clock lost,
// tempo falls back to the knob.
static volatile uint32_t  midi_clock_last_ms = 0;
static volatile bool      midi_ext_clock     = false;
static constexpr uint32_t kExtClockTimeoutMs = 500;

static void on_midi_clock() {
    midi_clock_last_ms = System::GetNow();
    if (!midi_ext_clock) {
        midi_ext_clock = true;
        seq.SetExternalClock(true);
    }
    seq.OnMidiClock();
    midi.SendClock();   // pass-through
}

static void on_midi_start() {
    if (!drum_kit_ready) generate_drum_random();
    seq_entered_once = true;   // like P2+P11: a started seq counts as entered
    seq.Start();
    midi.SendStart();
}

static void on_midi_continue() {
    if (!drum_kit_ready) generate_drum_random();
    seq_entered_once = true;
    seq.Resume();
    midi.SendContinue();
}

static void on_midi_stop() {
    seq.Stop();
    midi.SendStop();
}

static const MidiHandlers kMidiHandlers = { on_midi_note_on, on_midi_note_off,
                                            on_midi_cc, on_midi_all_notes_off,
                                            on_midi_clock, on_midi_start,
                                            on_midi_continue, on_midi_stop };

static void service_midi() { midi.Service(kMidiHandlers); }

// Delay that keeps MIDI flowing — the LED helpers below block the main loop
// for up to seconds at a time, which would starve MIDI in AND out.
static void delay_serviced(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        service_midi();
        System::Delay(1);
    }
}

// Model-select pickup for recording mode (separate from global bank pickup).
static bool  rec_bank_caught[2]  = { false, false };
static float rec_bank_thresh[2]  = { 0.f,   0.f   };

// Hold timer: rec_pad alone held ≥ kLongHoldBlocks (800ms) → confirm.
static uint32_t rec_hold_count    = 0;
static bool     rec_entry_released = false;  // must release pad once after entry before confirm can fire

// Steady re-audition pulse while editing (only when the seq isn't already
// force-firing the slot): one retrigger every 500ms, from entry onward.
static uint32_t rec_tick         = 0;   // increments each audio block in rec mode
static uint32_t rec_retrig_tick  = 0;
static constexpr uint32_t kRetrigBlocks = 125;  // 500ms at 4ms/block

// ─── Scale system ─────────────────────────────────────────────────────────────
// SW1: DOWN=Minor | CENTER=Chromatic | UP=Major
static const int kScales[3][7] = {
    { 0, 1, 2, 3,  4,  5,  6  },
    { 0, 2, 4, 5,  7,  9,  11 },
    { 0, 2, 3, 5,  7,  8,  10 },
};

// SW1 is change-latched per role (scale here, genre in Seq): a switch has no
// value to "cross", so the pickup equivalent is ignoring the position it
// acquired while serving the other role until it moves again (see SW1 handler).
static int scale_lk = 0;

static float compute_note(int pad) {
    int degree = pad - 3;
    int note   = kPitchBase + root_semitone + kScales[scale_lk][degree] + octave_offset * 12;
    return static_cast<float>(std::max(0, std::min(127, note)));
}

static float root_note_f() {
    return static_cast<float>(kPitchBase + root_semitone + octave_offset * 12);
}

// ─── Guard: is any musical pad currently held? ────────────────────────────────
static bool any_musical_pad_held() {
    for (int i = 3; i <= 9; i++) {
        if (touch.pads().IsTouched(i)) return true;
    }
    return false;
}

// ─── LED events (declared early — used by process_model_select below) ─────────
enum class LedEvent { NONE, NUMBERED, LIMIT, CONFIRM, MODEL, BEAT };
static volatile LedEvent led_event      = LedEvent::NONE;
static volatile int      led_event_data = 0;

// Beat-pulse hold-off: the pulse only shows when the LED isn't needed for
// anything else. Armed on every rec exit (the confirm/cancel blink must stay
// readable) and by the main loop whenever it dispatches a real blink; counted
// down per audio block. 2 s covers the longest blink (~1.4 s) plus a gap.
static constexpr uint32_t kBeatLedHoldBlocks = 500;   // 2 s at 4 ms/block
static volatile uint32_t  beat_led_hold      = 0;

// ─── Model selection ──────────────────────────────────────────────────────────
// (Six-Op audition presets kSixOpAud are defined with the random generators above.)
static int   bank_engine[2] = { 0, 12 };
static float bank_thresh[2] = { 0.0f, 0.0f };
static bool  bank_caught[2] = { false, false };
static constexpr float kBankDeadZone = 0.03f;

static void process_model_select(float s35_val) {
    if (seq_mode_on) return;
    if (any_musical_pad_held())                return;

    bool p0 = touch.pads().IsTouched(0);
    bool p2 = touch.pads().IsTouched(2);
    if (!p0 && !p2) return;

    int bank = p0 ? 0 : 1;

    if (!bank_caught[bank]) {
        float dist = s35_val - bank_thresh[bank];
        if (dist < 0.f) dist = -dist;
        if (dist < kBankDeadZone) return;
        bank_caught[bank] = true;
    }

    int new_engine;
    if (bank == 0) {
        // 11 engines in P0 bank — skip Chiptune (7); map 0..1 → {0..6, 8..11}
        int idx    = std::min(10, static_cast<int>(s35_val * 10.5f));
        new_engine = (idx >= 7) ? idx + 1 : idx;
    } else {
        new_engine = 12 + std::min(11, static_cast<int>(s35_val * 11.5f));
    }
    if (new_engine != current_engine) {
        current_engine    = new_engine;
        bank_engine[bank] = new_engine;
        bank_thresh[bank] = s35_val;
        if (current_mode == PlayMode::BASIC_PITCH) {
            pool.SetEngine(new_engine);
            bp_slots_active = false;   // explicit model choice returns to live knobs
        } else {
            generate_soft_random(pad_slots, new_engine, eff_h, eff_t, eff_m, eff_d, 0.25f);
            slots_ready = true;
        }
        if (new_engine >= 2 && new_engine <= 4) {
            const auto& a = kSixOpAud[new_engine - 2];
            VoiceParams vp;
            vp.engine    = new_engine;
            vp.harmonics = a.h;
            vp.timbre    = a.t;
            vp.morph     = a.m;
            vp.decay     = 0.6f;
            vp.blend     = pitched_blend_lk;   // preview with the current mix
            pool.AuditionWithParams(root_note_f(), vp);
        } else {
            pool.Audition(root_note_f(), new_engine);
        }
        led_event = LedEvent::MODEL;
    }
}

// Model select that only updates the recording slot's engine (used while in RECORDING mode).
static void process_rec_model_select(float s35_val) {
    if (rec_slot < 0) return;

    bool p0 = touch.pads().IsTouched(0);
    bool p2 = touch.pads().IsTouched(2);
    if (!p0 && !p2) return;

    int bank = p0 ? 0 : 1;

    if (!rec_bank_caught[bank]) {
        float dist = s35_val - rec_bank_thresh[bank];
        if (dist < 0.f) dist = -dist;
        if (dist < kBankDeadZone) return;
        rec_bank_caught[bank] = true;
    }

    int new_engine;
    if (bank == 0) {
        int idx    = std::min(10, static_cast<int>(s35_val * 10.5f));
        new_engine = (idx >= 7) ? idx + 1 : idx;
    } else {
        new_engine = 12 + std::min(11, static_cast<int>(s35_val * 11.5f));
    }
    if (new_engine != live_slots()[rec_slot].engine) {
        live_slots()[rec_slot].engine = new_engine;
        rec_bank_thresh[bank]         = s35_val;
        pool.UpdateAuditionEngine(new_engine);
    }
}

// ─── LED blink helpers ────────────────────────────────────────────────────────

// N medium blinks — mode/scale confirmation. (All blink delays are serviced:
// they run in the main loop and must not starve MIDI.)
static void blink_numbered(int n) {
    hw.SetLed(false); delay_serviced(200);
    for (int i = 0; i < n; i++) {
        hw.SetLed(true);  delay_serviced(200);
        hw.SetLed(false); delay_serviced(200);
    }
}

static void blink_limit() {
    for (int i = 0; i < 3; i++) {
        hw.SetLed(true);  delay_serviced(60);
        hw.SetLed(false); delay_serviced(80);
    }
}

// 3 rapid blinks — recording mode confirm/copy stored.
static void blink_confirm() {
    for (int i = 0; i < 3; i++) {
        hw.SetLed(true);  delay_serviced(80);
        hw.SetLed(false); delay_serviced(60);
    }
}

// ─── Recording mode helpers ───────────────────────────────────────────────────
// Called from AudioCallback (ISR context) — no blocking calls.
static void enter_rec_mode(int slot) {
    rec_mode   = RecMode::RECORDING;
    rec_slot   = slot;
    rec_backup = live_slots()[slot];
    rec_hold_count     = 0;
    rec_entry_released = false;
    rec_tick           = 0;
    rec_retrig_tick    = 0;
    cancel_pad         = -1;
    cancel_count       = 0;
    copy_hold_anim     = 0;
    rec_p1_last        = false;   // P1 already down at entry = fresh press edge
    rec_entry_flash    = true;    // main loop: entry burst before the heartbeat
    rec_hit_flash      = false;

    // Arm knob pickups to the slot's actual values: each pot takes effect only
    // when it reaches the value it is editing — no jumps, works from either
    // direction (arming at the pot's own position made upward moves dead).
    const auto& sl = live_slots()[slot];
    rec_k30.arm_to(sl.drive,     touch.knobs().s30().Value());
    rec_k31.arm_to(sl.decay,     touch.knobs().s31().Value());
    rec_k32.arm_to(sl.harmonics, touch.knobs().s32().Value());
    rec_k33.arm_to(sl.timbre,    touch.knobs().s33().Value());
    rec_k34.arm_to(sl.morph,     touch.knobs().s34().Value());
    rec_k36.arm_to(sl.volume,    touch.knobs().s36().Value());
    rec_k37.arm_to(sl.blend,     touch.knobs().s37().Value());
    rec_k37w.arm(touch.knobs().s37().Value());

    // Arm model-select pickup.
    float s35 = touch.knobs().s35().Value();
    rec_bank_caught[0]  = rec_bank_caught[1] = false;
    rec_bank_thresh[0]  = rec_bank_thresh[1] = s35;

    pool.AllNotesOff();
    // While the seq is running it force-fires this slot every step, so a
    // sustained audition voice would just double the sound — skip it. Drum
    // auditions use the seq-trigger param shaping and ride the drum group's
    // volume/sends (seq_group=true) — the pitched fader may be at zero.
    if (!(seq_mode_on && seq.IsActive())) {
        if (is_drum_mode) {
            pool.AuditionWithParams(drum_slots[slot].note, drum_params(slot), true);
        } else {
            pool.AuditionWithParams(root_note_f(), slot_params(live_slots()[slot]));
        }
        rec_hit_flash = true;
    }
}

static void cancel_rec_mode() {
    if (rec_slot >= 0) live_slots()[rec_slot] = rec_backup;
    rec_mode       = RecMode::IDLE;
    rec_slot       = -1;
    rec_hold_count = 0;
    cancel_pad     = -1;
    cancel_count   = 0;
    copy_hold_anim = 0;
    entry_wait_clear = true;   // held pads must clear before a new entry hold
    pool.AllNotesOff();
    rearm_seq_pickups();   // rec borrowed these pots; require fresh pickup
    beat_led_hold = kBeatLedHoldBlocks;
}

static void confirm_rec_mode() {
    rec_mode       = RecMode::IDLE;
    rec_slot       = -1;
    rec_hold_count = 0;
    cancel_pad     = -1;
    cancel_count   = 0;
    copy_hold_anim = 0;
    entry_wait_clear = true;   // the confirming hold must not become a new entry
    pool.AllNotesOff();
    rearm_seq_pickups();   // rec borrowed these pots; require fresh pickup
    beat_led_hold = kBeatLedHoldBlocks;
    led_event = LedEvent::CONFIRM;
}

// ─── P0+P2 hold state (ISR-writable, main-loop-readable) ──────────────────────
// Block size 192 at 48kHz = 4ms/block.  500 blocks = 2000ms.
// Stages at 1s and 2s in every playmode; Basic Pitch adds a 3rd at 3s that
// drops the randomize snapshots and restores the clean live-knob sound.
static volatile uint32_t p0p2_hold_count  = 0;
static volatile uint32_t p0p2_stage_fired = 0;
static volatile bool     p0p2_all_done    = false;
static bool p0p2_fired1 = false, p0p2_fired2 = false, p0p2_fired3 = false;

static void fire_hold_stage(int stage) {
    // In Seq the running sequencer plays the new sounds; no extra audition.
    if (!seq_mode_on) {
        const PadSlot& s = (current_mode == PlayMode::RANDOM) ? pad_slots[0] : bp_slots[0];
        pool.AuditionWithParams(root_note_f(), slot_params(s));
    }
    p0p2_stage_fired = static_cast<uint32_t>(stage);
}

// ─── Switch tracking ──────────────────────────────────────────────────────────
// Initialised to -1 so the first AudioCallback block sets mode silently.
static int last_sw1 = -1;
static int last_sw2 = -1;

// ─── Audio callback ───────────────────────────────────────────────────────────
static constexpr size_t kBlockSize = 192;
static constexpr size_t kChunkSize = 24;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    uint32_t blk_start = System::GetTick();
    cpu_meter.OnBlockStart();
    if (last_block_load > kShedThreshold) {
        if (pool.ShedVoice()) shed_count = shed_count + 1;
        // A block that actually overran (>100%) gets a second shed — a seq step
        // can wake several drums at once, and one-per-block lags behind.
        if (last_block_load > 1.0f && pool.ShedVoice()) shed_count = shed_count + 1;
    }
    // Touch callbacks (NoteOn, NoteOff, enter_rec_mode) fire here at ISR priority.
    touch.Process();

    auto& k = touch.knobs();
    k.s30().Process(); k.s31().Process(); k.s32().Process(); k.s33().Process();
    k.s34().Process(); k.s35().Process(); k.s36().Process(); k.s37().Process();

    float drive = k.s30().Value();
    last_h    = k.s32().Value();
    last_t    = k.s33().Value();
    last_m    = k.s34().Value();
    last_d    = k.s31().Value();   // unified Decay lives on S31 (LPG colour retired)

    // P1 = FX modifier: held → S30 edits the reverb, S35 the delay. Their
    // bare roles (drive / pattern select) freeze for the duration. Disabled
    // while recording (rec borrows S30 — in drum recording the same combo
    // edits the slot's send trims instead, see the rec knob block) and under
    // P0/P2 (model select owns S35 there).
    bool p1_fx = touch.pads().IsTouched(1) && !touch.pads().IsTouched(0)
                 && !touch.pads().IsTouched(2) && rec_mode == RecMode::IDLE;

    // Pots feed the effective params through the CC pickups: live by default,
    // gated after a MIDI CC write until the pot crosses the CC value.
    if (cc_pu_h.update(last_h))     eff_h     = last_h;
    if (cc_pu_t.update(last_t))     eff_t     = last_t;
    if (cc_pu_m.update(last_m))     eff_m     = last_m;
    if (cc_pu_d.update(last_d))     eff_d     = last_d;
    if (!p1_fx && cc_pu_drive.update(drive))  eff_drive = drive;

    // One-time fader arm: volume and blend follow their pots live from boot —
    // the pickups only start gating after a mode hand-off or P0 width edit.
    static bool faders_armed = false;
    if (!faders_armed) {
        faders_armed = true;
        pitched_vol_lk = k.s36().Value();
        pitch_pu_vol.force_catch(pitched_vol_lk);
        pitched_blend_lk = k.s37().Value();
        pitch_pu_blend.force_catch(pitched_blend_lk);
        pitch_pu_w.arm(k.s37().Value());
        // CC pickups start caught: pots drive eff_* until the first CC write.
        eff_h = last_h;        cc_pu_h.force_catch(last_h);
        eff_t = last_t;        cc_pu_t.force_catch(last_t);
        eff_m = last_m;        cc_pu_m.force_catch(last_m);
        eff_d = last_d;        cc_pu_d.force_catch(last_d);
        eff_drive = drive;     cc_pu_drive.force_catch(drive);
    }

    // SW1 / scale or genre — change-latched: the position only takes effect
    // when the switch moves while its role is active, so a position it
    // acquired while serving the other role never jumps the setting on a
    // playmode flick (the switch equivalent of the knob pickups).
    int sw1 = touch.switches().B();
    if (sw1 != last_sw1) {
        if (last_sw1 >= 0) {
            if (seq_mode_on) seq_genre_lk = sw1;
            else             scale_lk     = sw1;
            led_event      = LedEvent::NUMBERED;
            led_event_data = sw1_blink_count(sw1);
        } else {
            // Boot: scale follows the physical position. The genre keeps its
            // Techno default until SW1 moves inside Seq (same policy as the
            // S35 pattern variant).
            scale_lk = sw1;
        }
        last_sw1 = sw1;
    }

    // SW2 / mode — Up = Seq (enters immediately). Center = Random. Down = Basic Pitch.
    int sw2 = touch.switches().A();
    if (sw2 != last_sw2) {
        bool is_boot = (last_sw2 < 0);
        last_sw2 = sw2;
        if (rec_mode != RecMode::IDLE) cancel_rec_mode();
        if (sw2 == 1) {
            // SW2 Up → enter Seq mode. Kit is generated only once (mode memory);
            // after that the same kit returns on every re-entry until P0+P2 stage 2.
            // The seq auto-starts on the very first entry (incl. booting into Seq);
            // later entries keep whatever play/pause state P2+P11 last set.
            if (!drum_kit_ready) generate_drum_random();
            is_drum_mode = true;
            pool.AllNotesOff();
            p0p2_hold_count  = 0;
            p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
            p0p2_stage_fired = 0;
            p0p2_all_done    = false;
            // Arm the seq-knob pickups: on the very first entry the pots are
            // live immediately; on re-entry each pot must cross its stored
            // setting first (it served another mode in the meantime).
            if (!seq_entered_once) {
                seq_entered_once = true;
                seq_drive_lk = k.s30().Value();  seq_pu30.force_catch(seq_drive_lk);
                seq_tempo_lk = k.s31().Value();  seq_pu31.force_catch(seq_tempo_lk);
                seq_shuf_lk  = k.s32().Value();  seq_pu32.force_catch(seq_shuf_lk);
                seq_dens_lk  = k.s33().Value();  seq_pu33.force_catch(seq_dens_lk);
                seq_punch_lk = k.s34().Value();  seq_pu34.force_catch(seq_punch_lk);
                seq_pu35.arm_to(seq_var_lk, k.s35().Value());  // variant stays 0 until S35 crosses it
                seq_vol_lk   = k.s36().Value();  seq_pu36.force_catch(seq_vol_lk);
                seq_tight_lk = k.s37().Value();  seq_pu37.force_catch(seq_tight_lk);
                seq.Start();
                if (!midi_ext_clock) midi.SendStart();
            } else {
                rearm_seq_pickups();
            }
            seq_mode_on = true;
            if (!is_boot) { led_event = LedEvent::CONFIRM; }
        } else {
            // SW2 center or down → pads/knobs switch to the pitched mode, but a
            // running seq keeps playing in the background with its settings
            // locked at their last SW2-Up values. P2+P11 pauses/resumes it.
            if (seq_mode_on) {
                pool.AllNotesOff();
                seq_mode_on  = false;
                is_drum_mode = false;
            }
            current_mode = sw2_to_mode(sw2);
            // The faders served the seq while SW2 was Up — pickup so pitched
            // volume/blend/width don't jump on the flick back.
            pitch_pu_vol.arm_to(pitched_vol_lk, k.s36().Value());
            pitch_pu_blend.arm_to(pitched_blend_lk, k.s37().Value());
            pitch_pu_w.arm(k.s37().Value());
            if (current_mode == PlayMode::RANDOM && !slots_ready)
                generate_full_random(false);
            if (current_mode == PlayMode::BASIC_PITCH) {
                if (bp_slots_active) {
                    // Re-arm escape refs: knobs may have moved while in other
                    // modes — require a fresh grab to drop the snapshots.
                    bp_ref_h = last_h; bp_ref_t = last_t; bp_ref_m = last_m; bp_ref_d = last_d;
                } else {
                    pool.SetEngine(current_engine);
                }
            }
            if (!is_boot) {
                led_event      = LedEvent::NUMBERED;
                led_event_data = sw2_blink_count(sw2);
            }
        }
    }

    // P0 is the S37 width modifier in every mode: held = S37 edits stereo
    // width, released = S37 is back to its normal role (blend / tightness).
    // Re-arm the incoming role's pickup on each edge so neither value jumps.
    {
        static bool last_p0_held = false;
        bool p0_held = touch.pads().IsTouched(0);
        if (p0_held != last_p0_held) {
            last_p0_held = p0_held;
            float v37 = k.s37().Value();
            if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
                if (p0_held) rec_k37w.arm(v37);
                else         rec_k37.arm_to(live_slots()[rec_slot].blend, v37);
            } else if (seq_mode_on) {
                if (p0_held) seq_puw.arm(v37);
                else         seq_pu37.arm_to(seq_tight_lk, v37);
            } else {
                if (p0_held) pitch_pu_w.arm(v37);
                else         pitch_pu_blend.arm_to(pitched_blend_lk, v37);
            }
        }
    }

    // P1 FX modifier: on the press edge arm the movement-catches (nudge ~3%
    // and the pot position IS the value, like the width controls — a
    // crossing pickup against a center-mirror encoding felt dead); on the
    // release edge re-arm the bare roles' pickups so drive / pattern select
    // don't jump to wherever the FX edit left the pot.
    {
        static bool last_p1_fx = false;
        if (p1_fx != last_p1_fx) {
            last_p1_fx = p1_fx;
            if (p1_fx) {
                fx_mc_rev.arm(k.s30().Value());
                fx_mc_dly.arm(k.s35().Value());
            } else {
                if (seq_mode_on) {
                    seq_pu30.arm_to(seq_drive_lk, k.s30().Value());
                    seq_pu35.arm_to(seq_var_lk,   k.s35().Value());
                } else {
                    cc_pu_drive.arm_to(eff_drive, k.s30().Value());
                }
            }
        }
        if (p1_fx) {
            float v30 = k.s30().Value();
            if (fx_mc_rev.update(v30)) {
                if (seq_mode_on) fx_rev_seq_lk = v30; else fx_rev_pitched_lk = v30;
                fx_rev_char_lk = v30;
            }
            float v35 = k.s35().Value();
            if (fx_mc_dly.update(v35)) {
                if (seq_mode_on) fx_dly_seq_lk = v35; else fx_dly_pitched_lk = v35;
                fx_dly_char_lk = v35;
            }
        }
    }

    // Sequencer knob/switch updates — only while SW2 is Up and not recording
    // (rec mode borrows S30/S32/S33/S34/S37 for slot editing). Every pot goes
    // through pickup, so a pot used by another mode doesn't jump the setting
    // until it crosses the stored value.
    // Seq layout: S30=drive, S31=tempo, S32=shuffle, S33=density, S34=punch,
    // S35=pattern variant (within the SW1 genre), S37=tightness.
    if (seq_mode_on && rec_mode == RecMode::IDLE) {
        if (!p1_fx && seq_pu30.update(drive))            seq_drive_lk = drive;
        if (seq_pu31.update(k.s31().Value()))  seq_tempo_lk = k.s31().Value();
        if (seq_pu32.update(k.s32().Value()))  seq_shuf_lk  = k.s32().Value();
        if (seq_pu33.update(k.s33().Value()))  seq_dens_lk  = k.s33().Value();
        if (seq_pu34.update(k.s34().Value()))  seq_punch_lk = k.s34().Value();
        if (!p1_fx && seq_pu35.update(k.s35().Value()))  seq_var_lk   = k.s35().Value();
        if (seq_pu36.update(k.s36().Value()))  seq_vol_lk   = k.s36().Value();
        if (touch.pads().IsTouched(0)) {
            if (seq_puw.update(k.s37().Value())) seq_width_lk = snap_width(k.s37().Value());
        } else {
            if (seq_pu37.update(k.s37().Value())) seq_tight_lk = k.s37().Value();
        }
    }
    if (seq_mode_on) {
        seq.SetGenre(seq_genre_lk);  // change-latched from SW1 (see SW1 handler)
        seq.SetTempo(seq_tempo_lk);
        seq.SetShuffle(seq_shuf_lk);
        seq.SetDensity(seq_dens_lk);
        seq.SetVariant(seq_var_lk);
    }
    // Fader updates outside Seq/recording: S36 = volume, S37 = OUT↔AUX blend,
    // P0+S37 = stereo width (0 = mono blend, 1 = raw OUT/AUX split).
    if (!seq_mode_on && rec_mode == RecMode::IDLE) {
        if (pitch_pu_vol.update(k.s36().Value())) pitched_vol_lk = k.s36().Value();
        float v37 = k.s37().Value();
        if (touch.pads().IsTouched(0)) {
            if (pitch_pu_w.update(v37)) pitched_width_lk = snap_width(v37);
        } else {
            if (pitch_pu_blend.update(v37)) pitched_blend_lk = v37;
        }
    }
    pool.SetSeqVolume(seq_vol_lk);
    pool.SetPitchedVolume(pitched_vol_lk);
    pool.SetSeqWidth(seq_width_lk);
    pool.SetPitchedWidth(pitched_width_lk);

    // Decode the FX mirror knobs into per-group sends + shared character.
    // Squared send = audio taper: finer control in the useful low-wet range.
    {
        float w;
        fx_decode(fx_rev_seq_lk, &w);      pool.SetSeqReverbSend(w * w);
        fx_decode(fx_rev_pitched_lk, &w);  pool.SetPitchedReverbSend(w * w);
        int side = fx_decode(fx_rev_char_lk, &w);
        fx.SetReverbCharacter(side, w);

        fx_decode(fx_dly_seq_lk, &w);      pool.SetSeqDelaySend(w * w);
        fx_decode(fx_dly_pitched_lk, &w);  pool.SetPitchedDelaySend(w * w);
        side = fx_decode(fx_dly_char_lk, &w);
        // Dotted 1/8 = 3 sixteenth steps of the seq clock (internal tempo;
        // under external MIDI clock this is the knob fallback tempo).
        fx.SetDelayCharacter(side, w,
                             3.f * static_cast<float>(seq.StepBlocks())
                                 * static_cast<float>(kBlockSize));
    }

    // P0+P2 hold counter — active in every playmode:
    //   Basic Pitch: 1s soft tight (±0.25) → 2s soft wide (±0.45), same engine
    //                → 3s restore clean live-knob sound (drops the snapshots)
    //   Random:      1s full random, decay locked to S37 → 2s decay spread
    //   Seq:         1s soft param variance on current kit → 2s full new kit
    {
        bool p0 = touch.pads().IsTouched(0);
        bool p2 = touch.pads().IsTouched(2);
        bool both = p0 && p2 && rec_mode == RecMode::IDLE;
        bool in_bp = !seq_mode_on && current_mode == PlayMode::BASIC_PITCH;
        if (!both) {
            p0p2_hold_count  = 0;
            p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
            p0p2_all_done    = false;
            p0p2_stage_fired = 0;
        } else if (!p0p2_all_done) {
            p0p2_hold_count++;
            if (!p0p2_fired1 && p0p2_hold_count >= 250) {
                p0p2_fired1 = true;
                if (seq_mode_on) {
                    mutate_drum_soft();
                } else if (current_mode == PlayMode::RANDOM) {
                    generate_full_random(false);
                } else {
                    generate_soft_random(bp_slots, current_engine,
                                         eff_h, eff_t, eff_m, eff_d, 0.25f);
                    bp_slots_active = true;
                    bp_ref_h = last_h; bp_ref_t = last_t; bp_ref_m = last_m; bp_ref_d = last_d;
                }
                fire_hold_stage(1);
            }
            if (!p0p2_fired2 && p0p2_hold_count >= 500) {
                p0p2_fired2   = true;
                p0p2_all_done = !in_bp;   // Basic Pitch has a 3rd stage
                if (seq_mode_on) {
                    generate_drum_random();
                    pool.AllNotesOff();
                    seq.Start();
                    if (!midi_ext_clock) midi.SendStart();
                } else if (current_mode == PlayMode::RANDOM) {
                    generate_full_random(true);
                } else {
                    generate_soft_random(bp_slots, current_engine,
                                         eff_h, eff_t, eff_m, eff_d, 0.45f);
                    bp_slots_active = true;
                    bp_ref_h = last_h; bp_ref_t = last_t; bp_ref_m = last_m; bp_ref_d = last_d;
                }
                fire_hold_stage(2);
            }
            if (!p0p2_fired3 && p0p2_hold_count >= 750 && in_bp) {
                p0p2_fired3   = true;
                p0p2_all_done = true;
                // Stage 3: back to the clean live sound.
                bp_slots_active = false;
                pool.SetEngine(current_engine);
                VoiceParams vp;
                vp.engine    = current_engine;
                vp.harmonics = eff_h;
                vp.timbre    = eff_t;
                vp.morph     = morph_is_decay(current_engine) ? eff_d : eff_m;
                vp.decay     = eff_d;
                vp.blend     = pitched_blend_lk;
                pool.AuditionWithParams(root_note_f(), vp);
                p0p2_stage_fired = 3;
            }
        }
    }

    // Basic Pitch snapshot escape: grabbing any timbral knob returns to live mode.
    // Not while in Seq — the same knobs mean shuffle/density/tightness there and
    // current_mode still holds the last pitched mode.
    if (bp_slots_active && !seq_mode_on && current_mode == PlayMode::BASIC_PITCH
            && rec_mode == RecMode::IDLE) {
        if (fabsf(last_h - bp_ref_h) > 0.05f || fabsf(last_t - bp_ref_t) > 0.05f ||
            fabsf(last_m - bp_ref_m) > 0.05f || fabsf(last_d - bp_ref_d) > 0.05f) {
            bp_slots_active = false;
            pool.SetEngine(current_engine);
        }
    }

    // Recording entry — unified across Seq and Random: hold a pad (P3–P9) for
    // kRecEntryHoldBlocks (2 s). Same gesture, same threshold, in both modes;
    // the main loop animates the countdown from entry_hold_count.
    bool rec_entry_allowed = seq_mode_on || (current_mode == PlayMode::RANDOM);
    if (rec_entry_allowed && rec_mode == RecMode::IDLE) {
        int held_pad = -1;
        for (int i = 3; i <= 9; i++) {
            if (touch.pads().IsTouched(i)) { held_pad = i; break; }
        }
        if (entry_wait_clear) {
            // Post-exit latch: the hold that confirmed/copied must fully end
            // before a pad can start a new entry countdown.
            if (held_pad < 0) entry_wait_clear = false;
            entry_hold_count = 0;
            entry_hold_pad   = -1;
        } else if (held_pad < 0 || held_pad != entry_hold_pad) {
            entry_hold_count = 0;
            entry_hold_pad   = held_pad;
        } else if (++entry_hold_count >= kRecEntryHoldBlocks) {
            entry_hold_count = 0;
            entry_hold_pad   = -1;
            enter_rec_mode(held_pad - 3);
        }
    } else {
        entry_hold_count = 0;
        entry_hold_pad   = -1;
    }

    // rec_tick (block-accurate retrigger clock) + rec hold counter.
    if (rec_mode != RecMode::IDLE) rec_tick++;
    // Confirm: hold rec pad alone ≥ 800ms.
    // rec_entry_released ensures the initial entry tap can't bleed into an immediate confirm.
    if (rec_mode != RecMode::IDLE && rec_slot >= 0) {
        bool rec_pad_held = touch.pads().IsTouched(static_cast<uint16_t>(rec_slot + 3));
        if (!rec_pad_held) {
            rec_hold_count     = 0;
            rec_entry_released = true;
        } else if (rec_entry_released) {
            if (++rec_hold_count >= kLongHoldBlocks) confirm_rec_mode();
        }
    }

    // Cancel / copy: secondary pad tracking while in recording.
    if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
        bool src_held  = touch.pads().IsTouched(static_cast<uint16_t>(rec_slot + 3));
        int  other_slot = -1;
        for (int i = 3; i <= 9; i++) {
            if (i - 3 != rec_slot && touch.pads().IsTouched(i)) { other_slot = i - 3; break; }
        }
        if (other_slot < 0) {
            // Secondary released: fire cancel if held 50ms–1199ms without source held.
            if (cancel_pad >= 0 && cancel_count >= 12 && cancel_count < kLongHoldBlocks && !src_held)
                cancel_rec_mode();
            cancel_pad     = -1;
            cancel_count   = 0;
            copy_hold_anim = 0;
        } else if (other_slot != cancel_pad) {
            cancel_pad     = other_slot;
            cancel_count   = 0;
            copy_hold_anim = 0;
        } else {
            // Secondary held: fire copy when the long-hold elapses with source
            // also held. The main-loop countdown animation follows the hold
            // only while both pads are down (a lone secondary is a cancel tap).
            ++cancel_count;
            copy_hold_anim = src_held ? cancel_count : 0;
            if (cancel_count >= kLongHoldBlocks && src_held) {
                live_slots()[cancel_pad] = live_slots()[rec_slot];
                // Audible confirmation: play the copied sound on the target.
                if (is_drum_mode) {
                    trigger_drum(cancel_pad);
                } else {
                    const auto& cs = live_slots()[cancel_pad];
                    pool.AuditionWithParams(root_note_f(), slot_params(cs));
                }
                led_event      = LedEvent::CONFIRM;
                cancel_pad     = -1;  // reset so next secondary starts fresh
                cancel_count   = 0;
                copy_hold_anim = 0;
            }
        }
    } else {
        cancel_pad     = -1;
        cancel_count   = 0;
        copy_hold_anim = 0;
    }

    // Recording mode knob pickup + retrigger.
    if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
        bool changed = false;
        auto& slot   = live_slots()[rec_slot];
        float v30 = k.s30().Value(), v31 = k.s31().Value(), v32 = k.s32().Value();
        float v33 = k.s33().Value(), v34 = k.s34().Value(), v36 = k.s36().Value();
        float v37 = k.s37().Value(), v35 = k.s35().Value();
        // P1 in drum recording = this slot's FX send trims: S30 reverb, S35
        // delay — the same combo as the global FX layer, scoped to the slot.
        // Each role catches its own stored value, so releasing P1 can't jump
        // drive or model select to wherever the send edit left the pot.
        bool p1_snd = is_drum_mode && touch.pads().IsTouched(1)
                      && !touch.pads().IsTouched(0) && !touch.pads().IsTouched(2);
        if (p1_snd != rec_p1_last) {
            rec_p1_last = p1_snd;
            if (p1_snd) {
                rec_k30fx.arm_to(slot.rev_send, v30);
                rec_k35fx.arm_to(slot.dly_send, v35);
            } else {
                rec_k30.arm_to(slot.drive, v30);
                rec_bank_caught[0] = rec_bank_caught[1] = false;
                rec_bank_thresh[0] = rec_bank_thresh[1] = v35;
            }
        }
        if (p1_snd) {
            bool snd_changed = false;
            if (rec_k30fx.update(v30)) { slot.rev_send = v30; snd_changed = true; }
            if (rec_k35fx.update(v35)) { slot.dly_send = v35; snd_changed = true; }
            // Squared like slot_params. Audible per seq trigger and on the
            // paused-seq audition alike — drum auditions ride the drum
            // group's sends with the per-voice trim applied.
            if (snd_changed)
                pool.UpdateAuditionSends(slot.rev_send * slot.rev_send,
                                         slot.dly_send * slot.dly_send);
        }
        if (!p1_snd && rec_k30.update(v30)) { slot.drive  = v30; if (!seq_mode_on) pool.SetDrive(v30); }
        if (rec_k36.update(v36)) { slot.volume = v36; }
        if (touch.pads().IsTouched(0)) {
            if (rec_k37w.update(v37)) {
                slot.width = snap_width(v37);
                pool.UpdateAuditionWidth(slot.width);
            }
        } else {
            if (rec_k37.update(v37)) {
                slot.blend = v37;
                pool.UpdateAuditionBlend(slot.blend);
            }
        }
        if (rec_k31.update(v31)) { slot.decay     = v31; changed = true; }
        if (rec_k32.update(v32)) { slot.harmonics = v32; changed = true; }
        if (rec_k33.update(v33)) { slot.timbre    = v33; changed = true; }
        if (rec_k34.update(v34)) { slot.morph     = v34; changed = true; }
        if (changed) {
            // Drum audition updates carry the same shaping as drum_params
            // (tightness on the tail, punch on the kick) — otherwise the
            // sound jumps on the next retrigger.
            float upd_morph = slot.morph;
            if (morph_is_decay(slot.engine))
                upd_morph = is_drum_mode ? slot.decay * (0.2f + seq_tight_lk * 0.8f)
                                         : slot.decay;
            float upd_timbre = slot.timbre;
            if (is_drum_mode && rec_slot == 0)
                upd_timbre = slot.timbre + seq_punch_lk * (1.0f - slot.timbre);
            pool.UpdateAuditionParams(slot.harmonics, upd_timbre, upd_morph, slot.decay);
        }
        // Fixed-rate pulse — NOT keyed on `changed`: that flag is level (pickup
        // caught), not edge, so a changed-gated scheme ran at two speeds
        // depending on whether any knob had picked up yet (3s until the first
        // catch, then 500ms forever). One steady rate from entry instead.
        bool seq_running = seq_mode_on && seq.IsActive();
        if (!seq_running && (rec_tick - rec_retrig_tick) >= kRetrigBlocks) {
            rec_retrig_tick = rec_tick;
            // Drum slots re-audition with the full seq-trigger shaping and on
            // the drum group's volume/sends — an unlocked audition rode the
            // *pitched* group, which is silent whenever that fader is down,
            // so editing against a paused seq gave no sound at all.
            if (is_drum_mode) {
                pool.AuditionWithParams(slot.note, drum_params(rec_slot), true);
            } else {
                pool.AuditionWithParams(root_note_f(), slot_params(slot));
            }
            rec_hit_flash = true;
        }
        if (!p1_snd) process_rec_model_select(v35);
    }

    // Global model select.
    if (rec_mode == RecMode::IDLE) process_model_select(k.s35().Value());

    // Apply voice params.
    // Seq mode: drive applied per-trigger below (overall S30 × slot ratio).
    // Pitched modes: S30→drive (global). LPG colour is fixed at its neutral
    // midpoint — S31 is the unified Decay now. FM dead without CV input.
    if (seq_mode_on) {
        pool.SetFMAmount(0.0f);
        pool.SetLPGColour(lpg_colour);
    } else {
        if (rec_mode == RecMode::IDLE) pool.SetDrive(eff_drive);
        pool.SetFMAmount(0.0f);
        pool.SetLPGColour(lpg_colour);
    }
    // Live global knobs only when Basic Pitch is not playing randomize snapshots.
    if (current_mode == PlayMode::BASIC_PITCH && !bp_slots_active) {
        pool.SetHarmonics(eff_h);
        pool.SetTimbre(eff_t);
        // Unified decay: engines 19–23 keep their real decay on MORPH — the
        // Decay knob drives it there and S34 has no effect on those engines.
        pool.SetMorph(morph_is_decay(current_engine) ? eff_d : eff_m);
        pool.SetDecay(eff_d);
        pool.SetBlend(pitched_blend_lk);
    }

    // Sequencer tick — runs in every playmode while the seq is playing.
    if (seq.IsActive()) {
        uint8_t triggers = seq.Tick();
        // Beat pulse — lowest-priority LED signal: never overwrites a pending
        // event, suppressed while recording and during the hold-off that rec
        // exits and dispatched blinks arm.
        if (beat_led_hold) beat_led_hold = beat_led_hold - 1;
        if (seq.BeatFired() && rec_mode == RecMode::IDLE
                && beat_led_hold == 0 && led_event == LedEvent::NONE)
            led_event = LedEvent::BEAT;
        // Force-fire the recording slot on every *other* step (8th notes —
        // 16ths were overwhelming while editing), and only for drum recording
        // (Seq mode); a Random-slot rec index must not fire the drum slot of
        // the same number.
        if (seq_mode_on && rec_slot >= 0 && seq.StepFired() && (seq.Step() % 2 == 0))
            triggers |= static_cast<uint8_t>(1u << rec_slot);
        for (int i = 0; i < 7; i++) {
            if (triggers & (1u << i)) {
                trigger_drum(i);
                // Heartbeat: the rec slot sounding drives the LED double blink.
                if (seq_mode_on && rec_mode == RecMode::RECORDING && i == rec_slot)
                    rec_hit_flash = true;
                // Mirror to MIDI out as one-shot GM hits (queued; the main
                // loop drains — never TX from this ISR).
                midi.SendNoteOn(kMidiDrumCh, kDrumSlotGm[i], 100);
                midi.SendNoteOff(kMidiDrumCh, kDrumSlotGm[i]);
            }
        }
    }

    // MIDI clock: drop back to the internal clock (knob tempo) when the
    // external one goes silent; as master, emit 24 ppqn from the seq's own
    // timebase so external gear can't drift against the drums.
    if (midi_ext_clock
            && System::GetNow() - midi_clock_last_ms > kExtClockTimeoutMs) {
        midi_ext_clock = false;
        seq.SetExternalClock(false);
    }
    if (!midi_ext_clock) {
        for (int n = seq.MidiClockTick(); n > 0; n--) midi.SendClock();
    }

    // Render. Static to keep the buffers (main + FX send buses) off the ISR
    // stack.
    static float left[kBlockSize];
    static float right[kBlockSize];
    static float rev_bus_l[kBlockSize], rev_bus_r[kBlockSize];
    static float dly_bus_l[kBlockSize], dly_bus_r[kBlockSize];
    __builtin_memset(left,  0, size * sizeof(float));
    __builtin_memset(right, 0, size * sizeof(float));
    __builtin_memset(rev_bus_l, 0, size * sizeof(float));
    __builtin_memset(rev_bus_r, 0, size * sizeof(float));
    __builtin_memset(dly_bus_l, 0, size * sizeof(float));
    __builtin_memset(dly_bus_r, 0, size * sizeof(float));
    for (size_t offset = 0; offset < size; offset += kChunkSize)
        pool.Render(left + offset, right + offset,
                    rev_bus_l + offset, rev_bus_r + offset,
                    dly_bus_l + offset, dly_bus_r + offset, kChunkSize);

    // FX returns sum into the mix before the soft-clip. Each sleeps when its
    // send bus and tail are silent, so unused FX cost nothing per block.
    fx.ProcessDelay(dly_bus_l, dly_bus_r, left, right, size);
    fx.ProcessReverb(rev_bus_l, rev_bus_r, left, right, size);

    // Output: soft-clip via x/(1+|x|). Levels are per-group in VoicePool
    // (SetSeqVolume / SetPitchedVolume) — no master scale here.
    for (size_t i = 0; i < size; i++) {
        float L = left[i];
        float R = right[i];
        out[0][i] = L / (1.0f + fabsf(L));
        out[1][i] = R / (1.0f + fabsf(R));
    }
    last_block_load = static_cast<float>(System::GetTick() - blk_start) * blk_ticks_inv;
    cpu_meter.OnBlockEnd();
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    hw.Init();
    // DaisySeed::Init skips led.Init() when the Daisy bootloader reports
    // < v6.0 (no version stamp in backup SRAM) and the app runs from QSPI —
    // SetLed then writes through a null GPIO port and the user LED stays
    // dark in every mode. The LED config is populated unconditionally before
    // that skip, so re-running the init here is safe (and idempotent when
    // DaisySeed::Init already did it).
    hw.led.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    HW::hw().setHW(&hw);
#ifndef USB_MIDI
    HW::hw().startLog();
#endif

    rng = System::GetNow() ^ 0xDEADBEEF;

    touch.Init(hw);
    pool.Init();
    pool.SetEngine(current_engine);
    fx.Init(hw.AudioSampleRate());   // clears the SDRAM FX buffers (NOLOAD section)
    // TRS MIDI always (idle UART is free); USB MIDI only with -DUSB_MIDI,
    // which is also what suppresses StartLog above — one USB port, one owner.
    midi.Init();
    cpu_meter.Init(hw.AudioSampleRate(), kBlockSize);
    blk_ticks_inv = 1.0f / (static_cast<float>(System::GetTickFreq())
                            * (static_cast<float>(kBlockSize) / hw.AudioSampleRate()));

    touch.pads().SetOnTouch([](uint16_t pad) {
        if (pad >= 3 && pad <= 9) {
            int slot = pad - 3;

            if (rec_mode == RecMode::RECORDING) {
                // Pad presses ignored while recording. Cancel/copy handled in AudioCallback.

            } else if (seq_mode_on) {
                // Seq mode: pads play the drum kit (works while paused too).
                // Hold-to-record is detected in AudioCallback.
                trigger_drum(slot);
                midi.SendNoteOn(kMidiDrumCh, kDrumSlotGm[slot], 100);
                midi.SendNoteOff(kMidiDrumCh, kDrumSlotGm[slot]);

            } else {
                // Normal note play.
                switch (current_mode) {
                    case PlayMode::BASIC_PITCH:
                        if (bp_slots_active) {
                            pool.NoteOnWithParams(slot, compute_note(pad),
                                                  slot_params(bp_slots[slot]));
                        } else {
                            pool.NoteOn(slot, compute_note(pad));
                        }
                        break;
                    case PlayMode::RANDOM:
                        pool.NoteOnWithParams(slot, compute_note(pad),
                                              slot_params(pad_slots[slot]));
                        break;
                }
                // Mirror to MIDI out; remember the note so the NoteOff still
                // matches if octave/root move while the pad is held.
                int n = static_cast<int>(compute_note(pad));
                midi_pad_note_out[slot] = n;
                midi.SendNoteOn(kMidiPitchCh, static_cast<uint8_t>(n), 100);
            }

        } else if (pad == 0) {
            // Seq re-randomize is now the staged P0+P2 hold (AudioCallback) — no instant combo.
            if (rec_mode == RecMode::IDLE) {
                bank_caught[0] = false;
                bank_thresh[0] = touch.knobs().s35().Value();
            }

        } else if (pad == 1) {
            // FX modifier (hold + S30 = reverb, + S35 = delay) — handled in
            // AudioCallback like the P0 width modifier; nothing to do on tap.

        } else if (pad == 2) {
            if (rec_mode == RecMode::IDLE) {
                bank_caught[1] = false;
                bank_thresh[1] = touch.knobs().s35().Value();
            }

        } else if (pad == 10) {
            if (rec_mode == RecMode::RECORDING && is_drum_mode
                    && !touch.pads().IsTouched(0) && rec_slot >= 0) {
                auto& s = live_slots()[rec_slot];
                s.note = std::max(0.f, s.note - 1.f);
                pool.AuditionWithParams(s.note, drum_params(rec_slot), true);
            } else if (touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                if (root_semitone > 0) { root_semitone--; pool.Audition(root_note_f()); }
                else                   { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0)) {
                octave_offset = std::max(-3, octave_offset - 1);
            }

        } else if (pad == 11) {
            if (rec_mode == RecMode::RECORDING && is_drum_mode
                    && !touch.pads().IsTouched(0) && rec_slot >= 0) {
                auto& s = live_slots()[rec_slot];
                s.note = std::min(127.f, s.note + 1.f);
                pool.AuditionWithParams(s.note, drum_params(rec_slot), true);
            } else if (rec_mode == RecMode::IDLE && touch.pads().IsTouched(2)) {
                // P2 (held first) + P11 → drum seq play/pause, in any playmode.
                // P2 being down disables P11's octave function until release.
                if (seq.IsActive()) {
                    seq.Stop();
                    if (!midi_ext_clock) midi.SendStop();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;   // 2 blinks: paused
                } else {
                    if (!drum_kit_ready) generate_drum_random();
                    seq_entered_once = true;   // manual start counts as first entry
                    seq.Resume();
                    if (!midi_ext_clock) midi.SendContinue();
                    led_event = LedEvent::CONFIRM;  // 3 blinks: playing
                }
            } else if (touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                if (root_semitone < 11) { root_semitone++; pool.Audition(root_note_f()); }
                else                    { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0) && !touch.pads().IsTouched(2)) {
                octave_offset = std::min(3, octave_offset + 1);
            }
        }
    });

    touch.pads().SetOnRelease([](uint16_t pad) {
        if (pad >= 3 && pad <= 9) {
            // Drum voices use slot ids 16+i, so this never cuts them — drums
            // ring out as one-shots; pitched notes gate off normally.
            if (rec_mode == RecMode::IDLE) pool.NoteOff(pad - 3);
            // MIDI NoteOff even if rec mode swallowed the release — a hanging
            // note on external gear is worse than a stray off.
            int slot = pad - 3;
            if (midi_pad_note_out[slot] >= 0) {
                midi.SendNoteOff(kMidiPitchCh,
                                 static_cast<uint8_t>(midi_pad_note_out[slot]));
                midi_pad_note_out[slot] = -1;
            }
        } else if (pad == 0 || pad == 2) {
            if (rec_mode == RecMode::IDLE) bank_caught[(pad == 0) ? 0 : 1] = false;
        }
        // P1 (FX modifier) release is edge-detected in AudioCallback.
        // P11 combo needs no release action.
    });

    hw.StartAudio(AudioCallback);

    uint32_t last_cpu_print = 0;
    while (true) {
        // MIDI in/out — every iteration; the LED paths below service it too.
        service_midi();

        // CPU load report every 2s (percent, integer). Cheap; serial only.
        // Max is windowed: reset after each print, so it reflects the worst
        // block within the last 2s, not since boot.
        uint32_t now_ms = System::GetNow();
        if (now_ms - last_cpu_print >= 2000) {
            last_cpu_print = now_ms;
#ifndef USB_MIDI
            HW::hw().print("CPU avg %d%% max %d%% shed %d",
                           static_cast<int>(cpu_meter.GetAvgCpuLoad() * 100.f),
                           static_cast<int>(cpu_meter.GetMaxCpuLoad() * 100.f),
                           static_cast<int>(shed_count));
#endif
            cpu_meter.Reset();
            shed_count = 0;
        }

        uint32_t hold  = p0p2_hold_count;
        bool     done  = p0p2_all_done;
        uint32_t stage = p0p2_stage_fired;

        // P0+P2 hold animation — runs in every playmode (highest priority —
        // swallows the loop iteration).
        if ((hold > 0 || done) && rec_mode == RecMode::IDLE) {
            if (stage > 0) {
                hw.SetLed(true);
                uint32_t t0 = System::GetNow();
                while (System::GetNow() - t0 < 500) {
                    delay_serviced(5);
                    if (p0p2_hold_count == 0 && !p0p2_all_done) break;
                }
                hw.SetLed(false);
                p0p2_stage_fired = 0;
            } else if (done) {
                delay_serviced(10);
            } else {
                uint32_t t        = (hold < 500) ? hold : 500;
                uint32_t interval = 150u - t * 110u / 500u;
                hw.SetLed(true);
                delay_serviced(interval);
                hw.SetLed(false);
                delay_serviced(interval);
            }
            continue;
        }

        // Recording-entry countdown: from ~0.2 s into the pad hold the LED
        // blinks with gradually shrinking intervals (~140 ms down to 30 ms at
        // the 2 s threshold) — release any time to abort.
        uint32_t ehold = entry_hold_count;
        if (rec_mode == RecMode::IDLE && ehold >= kRecEntryAnimStart) {
            uint32_t t        = (ehold < kRecEntryHoldBlocks) ? ehold : kRecEntryHoldBlocks;
            uint32_t interval = 150u - t * 120u / kRecEntryHoldBlocks;
            hw.SetLed(true);  delay_serviced(interval);
            hw.SetLed(false); delay_serviced(interval);
            continue;
        }

        // Recording mode LED (this branch owns the loop while recording).
        if (rec_mode == RecMode::RECORDING) {
            // Copy fires while still in recording — its confirm event would
            // otherwise sit undispatched until rec exits. Show it here.
            __disable_irq();
            bool copy_confirm = (led_event == LedEvent::CONFIRM);
            if (copy_confirm) led_event = LedEvent::NONE;
            __enable_irq();
            if (copy_confirm) { blink_confirm(); continue; }

            // Copy hold (source + second pad down): the entry countdown
            // animation restarts, accelerating toward the affirmation.
            uint32_t chold = copy_hold_anim;
            if (chold > 0) {
                uint32_t t        = (chold < kLongHoldBlocks) ? chold : kLongHoldBlocks;
                uint32_t interval = 150u - t * 120u / kLongHoldBlocks;
                hw.SetLed(true);  delay_serviced(interval);
                hw.SetLed(false); delay_serviced(interval);
                continue;
            }

            // Entry burst: one rapid pattern right after the 2 s hold lands...
            if (rec_entry_flash) {
                rec_entry_flash = false;
                for (int i = 0; i < 5; i++) {
                    hw.SetLed(true);  delay_serviced(35);
                    hw.SetLed(false); delay_serviced(35);
                }
                delay_serviced(250);
                continue;
            }

            // ...then the recording heartbeat: one fast double blink per
            // audible hit of the slot (seq step or paused-seq audition), so
            // the LED is locked to the audio — and unmistakably different
            // from the sequencer's single on/off beat pulse.
            if (rec_hit_flash) {
                rec_hit_flash = false;
                hw.SetLed(true);  delay_serviced(45);
                hw.SetLed(false); delay_serviced(45);
                hw.SetLed(true);  delay_serviced(45);
                hw.SetLed(false);
            }
            delay_serviced(10);
            continue;
        }

        // Atomic read of led_event + data.
        __disable_irq();
        LedEvent ev   = led_event;
        int      data = led_event_data;
        led_event = LedEvent::NONE;
        __enable_irq();

        // A real blink arms the beat hold-off first, so no beat pulse gets
        // queued during it or blends into its trailing edge.
        if (ev != LedEvent::NONE && ev != LedEvent::BEAT)
            beat_led_hold = kBeatLedHoldBlocks;

        switch (ev) {
            case LedEvent::NUMBERED: blink_numbered(data); break;
            case LedEvent::LIMIT:    blink_limit();        break;
            case LedEvent::CONFIRM:  blink_confirm();      break;
            case LedEvent::MODEL:
                hw.SetLed(true); delay_serviced(50); hw.SetLed(false); break;
            case LedEvent::BEAT:
                hw.SetLed(true); delay_serviced(20); hw.SetLed(false); break;
            default: delay_serviced(10); break;
        }
    }
}
