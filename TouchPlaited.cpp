#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "touch/touch.h"
#include "synth/voice_pool.h"
#include "synth/sequencer.h"
#include "log.h"
#include <algorithm>
#include <cmath>

using namespace daisy;
using namespace synthux;

DaisySeed hw;
Touch touch;
VoicePool pool;
Sequencer seq;

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
static float seq_vol_lk   = 1.0f;   // S36 in Seq = drum group volume

// Unified long-hold threshold for recording enter / confirm / copy in every
// mode. 1200 ms: long enough that holding a sustained note in Random doesn't
// trip recording by accident.
static constexpr uint32_t kLongHoldBlocks = 300;  // 1200 ms at 4 ms/block

// ─── Random ───────────────────────────────────────────────────────────────────
static uint32_t rng = 1;
static float rand_f() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng >> 8) / 16777216.0f;
}
static float rand_range(float lo, float hi) { return lo + rand_f() * (hi - lo); }
static float clampf(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

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
};
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
static const DrumOpt kDrumCHH[]   = {
    { 23, 0.00f,0.15f, 0.30f,0.80f, 0.30f,0.80f, 0.4f,0.8f, 60.f,84.f },
    { 17, 0.00f,0.12f, 0.65f,0.95f, 0.25f,0.65f, 0.3f,0.6f, 60.f,84.f },
};
static const DrumOpt kDrumOHH[]   = {
    { 23, 0.40f,0.85f, 0.30f,0.70f, 0.30f,0.80f, 0.4f,0.8f, 60.f,84.f },
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
static const DrumOpt kDrumPerc[]  = {
    { 19, 0.05f,0.25f, 0.30f,0.70f, 0.20f,0.60f, 0.3f,0.6f, 55.f,79.f },
    { 20, 0.40f,0.90f, 0.30f,0.70f, 0.20f,0.60f, 0.4f,0.8f, 60.f,84.f },
    { 23, 0.15f,0.40f, 0.50f,0.90f, 0.50f,1.00f, 0.4f,0.7f, 72.f,96.f },
    { 18, 0.20f,0.50f, 0.30f,0.70f, 0.30f,0.70f, 0.3f,0.6f, 60.f,80.f },
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
    fill_drum_slot(drum_slots[6], kDrumPerc,  4); drum_slots[6].volume = 0.60f;
    // slot.drive is a ratio of the overall S30 drive in seq mode; 1.0 = follow fully.
    for (int i = 0; i < kPadSlots; i++) drum_slots[i].drive = 1.0f;
    drum_kit_ready = true;
}

// Fire drum slot i — used by both seq steps and manual pad hits in Seq mode.
// Slot id 16+i keeps drum voices out of reach of pad NoteOffs (slots 0–6), and
// lock_params=true shields them from the active playmode's global knob writes.
static void trigger_drum(int i) {
    PadSlot s = drum_slots[i];  // copy — mutate per-trigger without touching stored slot
    if (i == 0) s.timbre = s.timbre + seq_punch_lk * (1.0f - s.timbre);
    if (s.engine >= 21 && s.engine <= 23)
        s.morph = s.decay * (0.2f + seq_tight_lk * 0.8f);  // route tail→morph; S37 down = shorter
    pool.NoteOnWithParams(16 + i, s.note, s.engine,
        s.harmonics, s.timbre, s.morph, s.decay,
        s.volume, clampf(seq_drive_lk * s.drive), true);
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
        pad_slots[i].decay = spread_decay ? clampf(last_d + rand_range(-0.25f, 0.25f))
                                          : last_d;
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

// Drum mode entry: hold pad 3–9 for 800ms (AudioCallback-driven, not touch callback).
static int      entry_hold_pad   = -1;
static uint32_t entry_hold_count = 0;

// Secondary pad tracking for cancel / copy while in recording.
static int      cancel_pad   = -1;
static uint32_t cancel_count = 0;

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
static KnobPickup rec_k30, rec_k32, rec_k33, rec_k34, rec_k36, rec_k37;

// Pickups for the seq settings — armed on every Seq entry (see SW2 handling)
// and re-armed after recording (rec mode borrows most of these pots).
static KnobPickup seq_pu30, seq_pu31, seq_pu32, seq_pu33, seq_pu34, seq_pu36, seq_pu37;

static void rearm_seq_pickups() {
    auto& kn = touch.knobs();
    seq_pu30.arm_to(seq_drive_lk, kn.s30().Value());
    seq_pu31.arm_to(seq_tempo_lk, kn.s31().Value());
    seq_pu32.arm_to(seq_shuf_lk,  kn.s32().Value());
    seq_pu33.arm_to(seq_dens_lk,  kn.s33().Value());
    seq_pu34.arm_to(seq_punch_lk, kn.s34().Value());
    seq_pu36.arm_to(seq_vol_lk,   kn.s36().Value());
    seq_pu37.arm_to(seq_tight_lk, kn.s37().Value());
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

static float compute_note(int pad) {
    int degree = pad - 3;
    int sw     = touch.switches().B();
    int note   = kPitchBase + root_semitone + kScales[sw][degree] + octave_offset * 12;
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
            generate_soft_random(pad_slots, new_engine, last_h, last_t, last_m, last_d, 0.25f);
            slots_ready = true;
        }
        if (new_engine >= 2 && new_engine <= 4) {
            const auto& p = kSixOpAud[new_engine - 2];
            pool.AuditionWithParams(root_note_f(), new_engine, p.h, p.t, p.m, 0.6f);
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

// N medium blinks — mode/scale confirmation.
static void blink_numbered(int n) {
    hw.SetLed(false); System::Delay(200);
    for (int i = 0; i < n; i++) {
        hw.SetLed(true);  System::Delay(200);
        hw.SetLed(false); System::Delay(200);
    }
}

static void blink_limit() {
    for (int i = 0; i < 3; i++) {
        hw.SetLed(true);  System::Delay(60);
        hw.SetLed(false); System::Delay(80);
    }
}

// 3 rapid blinks — recording mode confirm/copy stored.
static void blink_confirm() {
    for (int i = 0; i < 3; i++) {
        hw.SetLed(true);  System::Delay(80);
        hw.SetLed(false); System::Delay(60);
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

    // Arm knob pickups to the slot's actual values: each pot takes effect only
    // when it reaches the value it is editing — no jumps, works from either
    // direction (arming at the pot's own position made upward moves dead).
    const auto& sl = live_slots()[slot];
    rec_k30.arm_to(sl.drive,     touch.knobs().s30().Value());
    rec_k32.arm_to(sl.harmonics, touch.knobs().s32().Value());
    rec_k33.arm_to(sl.timbre,    touch.knobs().s33().Value());
    rec_k34.arm_to(sl.morph,     touch.knobs().s34().Value());
    rec_k36.arm_to(sl.volume,    touch.knobs().s36().Value());
    rec_k37.arm_to(sl.decay,     touch.knobs().s37().Value());

    // Arm model-select pickup.
    float s35 = touch.knobs().s35().Value();
    rec_bank_caught[0]  = rec_bank_caught[1] = false;
    rec_bank_thresh[0]  = rec_bank_thresh[1] = s35;

    pool.AllNotesOff();
    // While the seq is running it force-fires this slot every step, so a
    // sustained audition voice would just double the sound — skip it.
    if (!(seq_mode_on && seq.IsActive())) {
        const auto& s = live_slots()[slot];
        float aud_note  = is_drum_mode ? s.note : root_note_f();
        float aud_morph = (s.engine >= 21 && s.engine <= 23) ? s.decay : s.morph;
        pool.AuditionWithParams(aud_note, s.engine, s.harmonics, s.timbre, aud_morph, s.decay,
                                s.volume);
    }
}

static void cancel_rec_mode() {
    if (rec_slot >= 0) live_slots()[rec_slot] = rec_backup;
    rec_mode       = RecMode::IDLE;
    rec_slot       = -1;
    rec_hold_count = 0;
    cancel_pad     = -1;
    cancel_count   = 0;
    pool.AllNotesOff();
    rearm_seq_pickups();   // rec borrowed these pots; require fresh pickup
}

static void confirm_rec_mode() {
    rec_mode       = RecMode::IDLE;
    rec_slot       = -1;
    rec_hold_count = 0;
    cancel_pad     = -1;
    cancel_count   = 0;
    pool.AllNotesOff();
    rearm_seq_pickups();   // rec borrowed these pots; require fresh pickup
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
        pool.AuditionWithParams(root_note_f(), s.engine, s.harmonics, s.timbre, s.morph, s.decay,
                                s.volume);
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

    float s31   = k.s31().Value();
    float drive = k.s30().Value();
    last_h    = k.s32().Value();
    last_t    = k.s33().Value();
    last_m    = k.s34().Value();
    last_d    = k.s37().Value();

    // SW1 / scale
    int sw1 = touch.switches().B();
    if (sw1 != last_sw1) {
        if (last_sw1 >= 0) {
            led_event      = LedEvent::NUMBERED;
            led_event_data = sw1_blink_count(sw1);
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
                seq_vol_lk   = k.s36().Value();  seq_pu36.force_catch(seq_vol_lk);
                seq_tight_lk = k.s37().Value();  seq_pu37.force_catch(seq_tight_lk);
                seq.Start();
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

    // Sequencer knob/switch updates — only while SW2 is Up and not recording
    // (rec mode borrows S30/S32/S33/S34/S37 for slot editing). Every pot goes
    // through pickup, so a pot used by another mode doesn't jump the setting
    // until it crosses the stored value.
    // Seq layout: S30=drive, S31=tempo, S32=shuffle, S33=density, S34=punch, S37=tightness.
    if (seq_mode_on && rec_mode == RecMode::IDLE) {
        if (seq_pu30.update(drive))            seq_drive_lk = drive;
        if (seq_pu31.update(k.s31().Value()))  seq_tempo_lk = k.s31().Value();
        if (seq_pu32.update(k.s32().Value()))  seq_shuf_lk  = k.s32().Value();
        if (seq_pu33.update(k.s33().Value()))  seq_dens_lk  = k.s33().Value();
        if (seq_pu34.update(k.s34().Value()))  seq_punch_lk = k.s34().Value();
        if (seq_pu36.update(k.s36().Value()))  seq_vol_lk   = k.s36().Value();
        if (seq_pu37.update(k.s37().Value()))  seq_tight_lk = k.s37().Value();
        seq.SetGenre(touch.switches().B());  // SW1: Center=Techno / Left=Electro / Right=Ambient
    }
    if (seq_mode_on) {
        seq.SetTempo(seq_tempo_lk);
        seq.SetShuffle(seq_shuf_lk);
        seq.SetDensity(seq_dens_lk);
    }
    // Group volumes: drums always follow the stored seq volume; the pitched
    // level follows S36 live only outside Seq mode and recording.
    pool.SetSeqVolume(seq_vol_lk);
    if (!seq_mode_on && rec_mode == RecMode::IDLE)
        pool.SetPitchedVolume(k.s36().Value());

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
                                         last_h, last_t, last_m, last_d, 0.25f);
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
                } else if (current_mode == PlayMode::RANDOM) {
                    generate_full_random(true);
                } else {
                    generate_soft_random(bp_slots, current_engine,
                                         last_h, last_t, last_m, last_d, 0.45f);
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
                pool.AuditionWithParams(root_note_f(), current_engine,
                                        last_h, last_t, last_m, last_d);
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
    // kLongHoldBlocks. Same gesture, same threshold, in both modes.
    bool rec_entry_allowed = seq_mode_on || (current_mode == PlayMode::RANDOM);
    if (rec_entry_allowed && rec_mode == RecMode::IDLE) {
        int held_pad = -1;
        for (int i = 3; i <= 9; i++) {
            if (touch.pads().IsTouched(i)) { held_pad = i; break; }
        }
        if (held_pad < 0 || held_pad != entry_hold_pad) {
            entry_hold_count = 0;
            entry_hold_pad   = held_pad;
        } else if (++entry_hold_count >= kLongHoldBlocks) {
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
            // Secondary released: fire cancel if held 50ms–799ms without source held.
            if (cancel_pad >= 0 && cancel_count >= 12 && cancel_count < kLongHoldBlocks && !src_held)
                cancel_rec_mode();
            cancel_pad   = -1;
            cancel_count = 0;
        } else if (other_slot != cancel_pad) {
            cancel_pad   = other_slot;
            cancel_count = 0;
        } else {
            // Secondary held: fire copy when the long-hold elapses with source also held.
            if (++cancel_count >= kLongHoldBlocks && src_held) {
                live_slots()[cancel_pad] = live_slots()[rec_slot];
                // Audible confirmation: play the copied sound on the target.
                if (is_drum_mode) {
                    trigger_drum(cancel_pad);
                } else {
                    const auto& cs = live_slots()[cancel_pad];
                    float cm = (cs.engine >= 21 && cs.engine <= 23) ? cs.decay : cs.morph;
                    pool.AuditionWithParams(root_note_f(), cs.engine,
                                            cs.harmonics, cs.timbre, cm, cs.decay,
                                            cs.volume);
                }
                led_event    = LedEvent::CONFIRM;
                cancel_pad   = -1;  // reset so next secondary starts fresh
                cancel_count = 0;
            }
        }
    } else {
        cancel_pad   = -1;
        cancel_count = 0;
    }

    // Recording mode knob pickup + retrigger.
    if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
        bool changed = false;
        auto& slot   = live_slots()[rec_slot];
        float v30 = k.s30().Value(), v32 = k.s32().Value(), v33 = k.s33().Value();
        float v34 = k.s34().Value(), v36 = k.s36().Value(), v37 = k.s37().Value();
        if (rec_k30.update(v30)) { slot.drive  = v30; if (!seq_mode_on) pool.SetDrive(v30); }
        if (rec_k36.update(v36)) { slot.volume = v36; }
        if (rec_k32.update(v32)) { slot.harmonics = v32; changed = true; }
        if (rec_k33.update(v33)) { slot.timbre    = v33; changed = true; }
        if (rec_k34.update(v34)) { slot.morph     = v34; changed = true; }
        if (rec_k37.update(v37)) { slot.decay     = v37; changed = true; }
        if (changed) {
            float upd_morph = (slot.engine >= 21 && slot.engine <= 23) ? slot.decay : slot.morph;
            pool.UpdateAuditionParams(slot.harmonics, slot.timbre, upd_morph, slot.decay);
        }
        // Fixed-rate pulse — NOT keyed on `changed`: that flag is level (pickup
        // caught), not edge, so a changed-gated scheme ran at two speeds
        // depending on whether any knob had picked up yet (3s until the first
        // catch, then 500ms forever). One steady rate from entry instead.
        bool seq_running = seq_mode_on && seq.IsActive();
        if (!seq_running && (rec_tick - rec_retrig_tick) >= kRetrigBlocks) {
            rec_retrig_tick = rec_tick;
            float aud_note  = is_drum_mode ? slot.note : root_note_f();
            float aud_morph = (slot.engine >= 21 && slot.engine <= 23) ? slot.decay : slot.morph;
            pool.AuditionWithParams(aud_note, slot.engine,
                                    slot.harmonics, slot.timbre, aud_morph, slot.decay,
                                    slot.volume);
        }
        process_rec_model_select(k.s35().Value());
    }

    // Global model select.
    if (rec_mode == RecMode::IDLE) process_model_select(k.s35().Value());

    // Apply voice params.
    // Seq mode: drive applied per-trigger below (overall S30 × slot ratio).
    // Pitched modes: S30→drive (global), S31→LPG colour. FM dead without CV input.
    if (seq_mode_on) {
        pool.SetFMAmount(0.0f);
        pool.SetLPGColour(0.5f);
    } else {
        if (rec_mode == RecMode::IDLE) pool.SetDrive(drive);
        pool.SetFMAmount(0.0f);
        pool.SetLPGColour(s31);
    }
    // Live global knobs only when Basic Pitch is not playing randomize snapshots.
    if (current_mode == PlayMode::BASIC_PITCH && !bp_slots_active) {
        pool.SetHarmonics(last_h);
        pool.SetTimbre(last_t);
        pool.SetMorph(last_m);
        pool.SetDecay(last_d);
    }

    // Sequencer tick — runs in every playmode while the seq is playing.
    if (seq.IsActive()) {
        uint8_t triggers = seq.Tick();
        if (seq.BeatFired() && rec_mode == RecMode::IDLE) led_event = LedEvent::BEAT;
        // Force-fire the recording slot on every *other* step (8th notes —
        // 16ths were overwhelming while editing), and only for drum recording
        // (Seq mode); a Random-slot rec index must not fire the drum slot of
        // the same number.
        if (seq_mode_on && rec_slot >= 0 && seq.StepFired() && (seq.Step() % 2 == 0))
            triggers |= static_cast<uint8_t>(1u << rec_slot);
        for (int i = 0; i < 7; i++) {
            if (triggers & (1u << i)) trigger_drum(i);
        }
    }

    // Render. Static to keep 1.5 KB off the ISR stack.
    static float left[kBlockSize];
    static float right[kBlockSize];
    __builtin_memset(left,  0, size * sizeof(float));
    __builtin_memset(right, 0, size * sizeof(float));
    for (size_t offset = 0; offset < size; offset += kChunkSize)
        pool.Render(left + offset, right + offset, kChunkSize);

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

            } else {
                // Normal note play.
                switch (current_mode) {
                    case PlayMode::BASIC_PITCH:
                        if (bp_slots_active) {
                            const auto& s = bp_slots[slot];
                            pool.NoteOnWithParams(slot, compute_note(pad),
                                s.engine, s.harmonics, s.timbre, s.morph, s.decay,
                                s.volume, s.drive);
                        } else {
                            pool.NoteOn(slot, compute_note(pad));
                        }
                        break;
                    case PlayMode::RANDOM: {
                        const auto& s = pad_slots[slot];
                        pool.NoteOnWithParams(slot, compute_note(pad),
                            s.engine, s.harmonics, s.timbre, s.morph, s.decay,
                            s.volume, s.drive);
                        break;
                    }
                }
            }

        } else if (pad == 0) {
            // Seq re-randomize is now the staged P0+P2 hold (AudioCallback) — no instant combo.
            if (rec_mode == RecMode::IDLE) {
                bank_caught[0] = false;
                bank_thresh[0] = touch.knobs().s35().Value();
            }

        } else if (pad == 1) {
            // Unused — play/pause moved to the P2+P11 combo. Reserved for
            // future melodic seq trigger.

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
                pool.AuditionWithParams(s.note, s.engine, s.harmonics, s.timbre, s.morph, s.decay,
                                        s.volume);
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
                pool.AuditionWithParams(s.note, s.engine, s.harmonics, s.timbre, s.morph, s.decay,
                                        s.volume);
            } else if (rec_mode == RecMode::IDLE && touch.pads().IsTouched(2)) {
                // P2 (held first) + P11 → drum seq play/pause, in any playmode.
                // P2 being down disables P11's octave function until release.
                if (seq.IsActive()) {
                    seq.Stop();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;   // 2 blinks: paused
                } else {
                    if (!drum_kit_ready) generate_drum_random();
                    seq_entered_once = true;   // manual start counts as first entry
                    seq.Resume();
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
        } else if (pad == 0 || pad == 2) {
            if (rec_mode == RecMode::IDLE) bank_caught[(pad == 0) ? 0 : 1] = false;
        }
        // P1: unused. P11 combo needs no release action.
    });

    hw.StartAudio(AudioCallback);

    uint32_t last_cpu_print = 0;
    while (true) {
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
                    System::Delay(5);
                    if (p0p2_hold_count == 0 && !p0p2_all_done) break;
                }
                hw.SetLed(false);
                p0p2_stage_fired = 0;
            } else if (done) {
                System::Delay(10);
            } else {
                uint32_t t        = (hold < 500) ? hold : 500;
                uint32_t interval = 150u - t * 110u / 500u;
                hw.SetLed(true);
                System::Delay(interval);
                hw.SetLed(false);
                System::Delay(interval);
            }
            continue;
        }

        // Recording mode: steady 300ms blink.
        if (rec_mode == RecMode::RECORDING) {
            hw.SetLed(true);  System::Delay(150);
            hw.SetLed(false); System::Delay(150);
            continue;
        }

        // Atomic read of led_event + data.
        __disable_irq();
        LedEvent ev   = led_event;
        int      data = led_event_data;
        led_event = LedEvent::NONE;
        __enable_irq();

        switch (ev) {
            case LedEvent::NUMBERED: blink_numbered(data); break;
            case LedEvent::LIMIT:    blink_limit();        break;
            case LedEvent::CONFIRM:  blink_confirm();      break;
            case LedEvent::MODEL:
                hw.SetLed(true); System::Delay(50); hw.SetLed(false); break;
            case LedEvent::BEAT:
                hw.SetLed(true); System::Delay(20); hw.SetLed(false); break;
            default: System::Delay(10); break;
        }
    }
}
