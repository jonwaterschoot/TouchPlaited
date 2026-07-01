#include "daisy_seed.h"
#include "touch/touch.h"
#include "synth/voice_pool.h"
#include "synth/sequencer.h"
#include "log.h"
#include <algorithm>

using namespace daisy;
using namespace synthux;

DaisySeed hw;
Touch touch;
VoicePool pool;
Sequencer seq;

static bool              seq_mode_on   = false;   // inside seq mode (running or paused)
static volatile uint32_t p1_hold_count = 0;
static volatile bool     p1_hold_fired = false;
static constexpr uint32_t kP1ExitBlocks = 200;    // 800 ms at 4 ms/block

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
};
static PadSlot pad_slots[kPadSlots];
static bool    slots_ready_m2 = false;
static bool    slots_ready_m3 = false;
static bool    is_drum_mode   = false;

static float last_h = 0.5f, last_t = 0.5f, last_m = 0.5f, last_d = 0.5f;

static int current_engine = 0;

static int octave_offset        = 0;
static int root_semitone        = 0;
static constexpr int kPitchBase = 60;

static void generate_soft_random(int engine, float h, float t, float m, float d, float spread) {
    for (int i = 0; i < kPadSlots; i++) {
        pad_slots[i].engine    = engine;
        pad_slots[i].harmonics = clampf(h + rand_range(-spread, spread));
        pad_slots[i].timbre    = clampf(t + rand_range(-spread, spread));
        pad_slots[i].morph     = clampf(m + rand_range(-spread, spread));
        pad_slots[i].decay     = clampf(d + rand_range(-spread, spread));
    }
    slots_ready_m2 = true;
    is_drum_mode   = false;
}

// Model groups: engine2 0–7 | original 8–23 | Drums 21–23
// Chiptune (7) excluded from Full Random — autonomous arpeggiator plays without gate.
static int engine_group(int e) { return (e >= 12) ? 1 : 0; }

static const int kFRGroup0[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11 };        // 11 — no Chiptune
static const int kFRGroup1[] = { 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 }; // 12
static const int kFRAll[]    = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 }; // 23
static int rand_engine_in_group(int g) {
    if (g == 0) return kFRGroup0[static_cast<int>(rand_f() * 11.f) % 11];
    return kFRGroup1[static_cast<int>(rand_f() * 12.f) % 12];
}
static int rand_engine_any() { return kFRAll[static_cast<int>(rand_f() * 23.f) % 23]; }

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
    s.morph     = rand_range(o.mlo, o.mhi);
    s.timbre    = rand_range(o.tlo, o.thi);
    s.harmonics = rand_range(o.hlo, o.hhi);
    s.decay     = rand_range(o.dlo, o.dhi);
    s.note      = rand_range(o.nlo, o.nhi);
}

static void generate_drum_random() {
    fill_drum_slot(pad_slots[0], kDrumKick,  2);
    fill_drum_slot(pad_slots[1], kDrumSnare, 3);
    fill_drum_slot(pad_slots[2], kDrumCHH,   2);
    fill_drum_slot(pad_slots[3], kDrumOHH,   1);
    fill_drum_slot(pad_slots[4], kDrumClap,  3);
    fill_drum_slot(pad_slots[5], kDrumTom,   2);
    fill_drum_slot(pad_slots[6], kDrumPerc,  4);
    slots_ready_m3 = true;
    is_drum_mode   = true;
}

// stage: 1=within current_engine group, 2=all 23 (no Chiptune)
static void generate_full_random(int stage) {
    int grp = engine_group(current_engine);
    float root = static_cast<float>(kPitchBase + root_semitone + octave_offset * 12);
    for (int i = 0; i < kPadSlots; i++) {
        pad_slots[i].engine    = (stage == 1) ? rand_engine_in_group(grp) : rand_engine_any();
        pad_slots[i].harmonics = rand_range(0.2f, 0.8f);
        pad_slots[i].timbre    = rand_range(0.2f, 0.8f);
        pad_slots[i].morph     = rand_range(0.2f, 0.8f);
        pad_slots[i].decay     = rand_range(0.3f, 0.9f);
        pad_slots[i].note      = root;
    }
    slots_ready_m3 = true;
    is_drum_mode   = false;
}

// ─── Playmode ─────────────────────────────────────────────────────────────────
// Switch3: 0=CENTER, 1=UP, 2=DOWN
// A() = SW2 (right, _switch_7_8): Init(D7, D6) → D7=Down(pos1), D6=Up(pos2)
// B() = SW1 (left, _switch_9_10): Init(D9, D8) → D9=left-flick(pos1), D8=right-flick(pos2)
enum class PlayMode { BASIC_PITCH, SOFT_RANDOM, FULL_RANDOM };
static volatile PlayMode current_mode = PlayMode::BASIC_PITCH;

// A() polarity verified on hardware: sw=2=Down→Basic, sw=0=Center→Soft, sw=1=Up→Full
static PlayMode sw2_to_mode(int sw) {
    switch (sw) {
        case 1:  return PlayMode::FULL_RANDOM;
        case 0:  return PlayMode::SOFT_RANDOM;
        default: return PlayMode::BASIC_PITCH;
    }
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
enum class RecMode { IDLE, RECORDING, COPY };
static volatile RecMode rec_mode    = RecMode::IDLE;
static int              rec_slot    = -1;   // 0–6: slot being edited
static int              rec_copy_dst = -1;  // COPY mode: destination slot
static PadSlot          rec_backup;          // saved state, restored on cancel

// Knob pickup for per-slot editing in recording mode.
struct KnobPickup {
    float thresh = 0.f;
    float prev   = 0.f;
    bool  caught = false;
    void arm(float current_val)  { thresh = current_val; prev = current_val; caught = false; }
    bool update(float curr) {
        if (!caught && ((prev < thresh) != (curr < thresh))) caught = true;
        prev = curr;
        return caught;
    }
};
static KnobPickup rec_k32, rec_k33, rec_k34, rec_k37;

// Model-select pickup for recording mode (separate from global bank pickup).
static bool  rec_bank_caught[2]  = { false, false };
static float rec_bank_thresh[2]  = { 0.f,   0.f   };

// Hold timer: P0 + rec_pad held ≥ kRecConfirmBlocks → confirm.
// Block size 96 at 48kHz = 2ms/block → 250 blocks = 500ms.
static uint32_t rec_hold_count   = 0;
static constexpr uint32_t kRecConfirmBlocks = 250;

// Re-audition cooldown: don't retrigger more than once per 250ms while editing.
static uint32_t rec_tick         = 0;   // increments each audio block in rec mode
static uint32_t rec_retrig_tick  = 0;
static constexpr uint32_t kRetrigDebounce = 125;  // 250ms

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
// Six-Op A/B/C (engines 2-4) are nearly silent at the default h=t=m=0.5.
// These presets land on audible FM algorithm positions.
struct AudPreset { float h, t, m; };
static const AudPreset kSixOpAud[3] = {
    { 0.15f, 0.75f, 0.40f },  // 2 Six-Op A
    { 0.20f, 0.70f, 0.45f },  // 3 Six-Op B
    { 0.10f, 0.80f, 0.30f },  // 4 Six-Op C
};
static int   bank_engine[2] = { 0, 12 };
static float bank_thresh[2] = { 0.0f, 0.0f };
static bool  bank_caught[2] = { false, false };
static constexpr float kBankDeadZone = 0.03f;

static void process_model_select(float s35_val) {
    if (current_mode == PlayMode::FULL_RANDOM) return;
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
        if (current_mode == PlayMode::BASIC_PITCH)
            pool.SetEngine(new_engine);
        else
            generate_soft_random(new_engine, last_h, last_t, last_m, last_d, 0.25f);
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
    if (new_engine != pad_slots[rec_slot].engine) {
        pad_slots[rec_slot].engine = new_engine;
        rec_bank_thresh[bank]      = s35_val;
        pool.UpdateAuditionEngine(new_engine);
    }
}

// ─── Output ───────────────────────────────────────────────────────────────────
static float out_level = 1.0f;

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
// Called from touch callback (inside audio ISR context) — no blocking calls.
static void enter_rec_mode(int slot) {
    rec_mode     = RecMode::RECORDING;
    rec_slot     = slot;
    rec_backup   = pad_slots[slot];
    rec_copy_dst = -1;
    rec_hold_count   = 0;
    rec_tick         = 0;
    rec_retrig_tick  = 0;

    // Arm knob pickups from current values so they must move before taking effect.
    rec_k32.arm(touch.knobs().s32().Value());
    rec_k33.arm(touch.knobs().s33().Value());
    rec_k34.arm(touch.knobs().s34().Value());
    rec_k37.arm(touch.knobs().s37().Value());

    // Arm model-select pickup.
    float s35 = touch.knobs().s35().Value();
    rec_bank_caught[0]  = rec_bank_caught[1] = false;
    rec_bank_thresh[0]  = rec_bank_thresh[1] = s35;

    pool.AllNotesOff();
    const auto& s = pad_slots[slot];
    float aud_note = (current_mode == PlayMode::FULL_RANDOM) ? s.note : root_note_f();
    pool.AuditionWithParams(aud_note, s.engine, s.harmonics, s.timbre, s.morph, s.decay);
}

static void cancel_rec_mode() {
    if (rec_slot >= 0) pad_slots[rec_slot] = rec_backup;
    rec_mode     = RecMode::IDLE;
    rec_slot     = -1;
    rec_copy_dst = -1;
    rec_hold_count = 0;
    pool.AllNotesOff();
}

static void confirm_rec_mode() {
    // In COPY mode, copy the edited slot into the destination.
    if (rec_mode == RecMode::COPY && rec_copy_dst >= 0)
        pad_slots[rec_copy_dst] = pad_slots[rec_slot];
    rec_mode     = RecMode::IDLE;
    rec_slot     = -1;
    rec_copy_dst = -1;
    rec_hold_count = 0;
    pool.AllNotesOff();
    led_event = LedEvent::CONFIRM;
}

// ─── P0+P2 hold state (ISR-writable, main-loop-readable) ──────────────────────
// Block size 192 at 48kHz = 4ms/block.  500 blocks = 2000ms.
static volatile uint32_t p0p2_hold_count  = 0;
static volatile uint32_t p0p2_stage_fired = 0;
static volatile bool     p0p2_all_done    = false;
static bool p0p2_fired1 = false, p0p2_fired2 = false, p0p2_fired3 = false;

static void fire_hold_stage(int stage) {
    const auto& s = pad_slots[0];
    float note = (current_mode == PlayMode::FULL_RANDOM) ? s.note : root_note_f();
    pool.AuditionWithParams(note, s.engine, s.harmonics, s.timbre, s.morph, s.decay);
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
    // Touch callbacks (NoteOn, NoteOff, enter_rec_mode) fire here at ISR priority.
    touch.Process();

    auto& k = touch.knobs();
    k.s30().Process(); k.s31().Process(); k.s32().Process(); k.s33().Process();
    k.s34().Process(); k.s35().Process(); k.s36().Process(); k.s37().Process();

    float s31 = k.s31().Value();
    float fm  = k.s30().Value();
    out_level = k.s36().Value();
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

    // SW2 / mode — last_sw2 always tracks current switch position (for seq exit restore),
    // but the mode change itself is skipped while seq_mode_on.
    int sw2 = touch.switches().A();
    if (sw2 != last_sw2) {
        if (last_sw2 < 0) {
            last_sw2 = sw2;
            if (!seq_mode_on) {
                current_mode = sw2_to_mode(sw2);
                if (current_mode == PlayMode::SOFT_RANDOM && !slots_ready_m2)
                    generate_soft_random(current_engine, last_h, last_t, last_m, last_d, 0.25f);
                else if (current_mode == PlayMode::FULL_RANDOM && !slots_ready_m3)
                    generate_full_random(2);
            }
        } else {
            last_sw2 = sw2;
            if (!seq_mode_on) {
                if (rec_mode != RecMode::IDLE) cancel_rec_mode();
                PlayMode new_mode = sw2_to_mode(sw2);
                current_mode = new_mode;
                pool.AllNotesOff();
                if (new_mode == PlayMode::BASIC_PITCH) {
                    pool.SetEngine(current_engine);
                } else {
                    if (new_mode == PlayMode::SOFT_RANDOM && !slots_ready_m2)
                        generate_soft_random(current_engine, last_h, last_t, last_m, last_d, 0.25f);
                    else if (new_mode == PlayMode::FULL_RANDOM && !slots_ready_m3)
                        generate_full_random(2);
                }
                led_event      = LedEvent::NUMBERED;
                led_event_data = sw2_blink_count(sw2);
            }
        }
    }

    // Sequencer knob/switch updates — active even when paused so changes take effect on resume.
    if (seq_mode_on) {
        seq.SetTempo(k.s30().Value());
        seq.SetShuffle(k.s31().Value());
        seq.SetDensity(k.s32().Value());
        seq.SetGenre(touch.switches().B());  // SW1: Center=Techno / Left=Electro / Right=Ambient
    }

    // P1 long-hold exit from seq mode (800 ms = 200 blocks at 4 ms/block).
    if (seq_mode_on) {
        if (touch.pads().IsTouched(1) && !p1_hold_fired) {
            if (++p1_hold_count >= kP1ExitBlocks) {
                p1_hold_fired  = true;
                seq.Stop();
                pool.AllNotesOff();
                seq_mode_on    = false;
                is_drum_mode   = false;
                current_mode   = sw2_to_mode(last_sw2);
                if (current_mode == PlayMode::BASIC_PITCH) pool.SetEngine(current_engine);
                led_event      = LedEvent::NUMBERED;
                led_event_data = 1;
            }
        } else if (!touch.pads().IsTouched(1)) {
            p1_hold_count = 0;
        }
    }

    // P0+P2 hold counter (block-accurate); generate_*() runs here in the ISR.
    {
        bool in_rand = (current_mode == PlayMode::SOFT_RANDOM ||
                        current_mode == PlayMode::FULL_RANDOM);
        bool p0 = touch.pads().IsTouched(0);
        bool p2 = touch.pads().IsTouched(2);
        bool both = p0 && p2 && rec_mode == RecMode::IDLE;
        if (!both || !in_rand) {
            p0p2_hold_count  = 0;
            p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
            p0p2_all_done    = false;
            p0p2_stage_fired = 0;
        } else if (!p0p2_all_done) {
            p0p2_hold_count++;
            if (!p0p2_fired1 && p0p2_hold_count >= 500) {
                p0p2_fired1 = true;
                if (current_mode == PlayMode::SOFT_RANDOM)
                    generate_soft_random(current_engine, last_h, last_t, last_m, last_d, 0.25f);
                else
                    generate_full_random(1);
                fire_hold_stage(1);
            }
            if (!p0p2_fired2 && p0p2_hold_count >= 1000) {
                p0p2_fired2 = true;
                if (current_mode == PlayMode::SOFT_RANDOM) {
                    generate_soft_random(current_engine, last_h, last_t, last_m, last_d, 0.45f);
                    p0p2_all_done = true;
                } else {
                    generate_full_random(2);
                }
                fire_hold_stage(2);
            }
            if (!p0p2_fired3 && current_mode == PlayMode::FULL_RANDOM
                             && p0p2_hold_count >= 1500) {
                p0p2_fired3   = true;
                p0p2_all_done = true;
                generate_drum_random();
                fire_hold_stage(3);
            }
        }
    }

    // rec_tick (block-accurate retrigger clock) + rec hold counter.
    if (rec_mode != RecMode::IDLE) rec_tick++;
    if (rec_mode != RecMode::IDLE && rec_slot >= 0) {
        if (touch.pads().IsTouched(2) &&
            touch.pads().IsTouched(static_cast<uint16_t>(rec_slot + 3))) {
            if (++rec_hold_count >= kRecConfirmBlocks) confirm_rec_mode();
        } else {
            rec_hold_count = 0;
        }
    }

    // Recording mode knob pickup + retrigger.
    if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
        bool changed = false;
        auto& slot   = pad_slots[rec_slot];
        float v32 = k.s32().Value(), v33 = k.s33().Value();
        float v34 = k.s34().Value(), v37 = k.s37().Value();
        if (rec_k32.update(v32)) { slot.harmonics = v32; changed = true; }
        if (rec_k33.update(v33)) { slot.timbre    = v33; changed = true; }
        if (rec_k34.update(v34)) { slot.morph     = v34; changed = true; }
        if (rec_k37.update(v37)) { slot.decay     = v37; changed = true; }
        if (changed)
            pool.UpdateAuditionParams(slot.harmonics, slot.timbre, slot.morph, slot.decay);
        bool periodic      = (rec_tick - rec_retrig_tick) >= 750;
        bool debounced_chg = changed && (rec_tick - rec_retrig_tick) >= kRetrigDebounce;
        if (periodic || debounced_chg) {
            rec_retrig_tick = rec_tick;
            float aud_note  = (current_mode == PlayMode::FULL_RANDOM)
                              ? slot.note : root_note_f();
            pool.AuditionWithParams(aud_note, slot.engine,
                                    slot.harmonics, slot.timbre, slot.morph, slot.decay);
        }
        process_rec_model_select(k.s35().Value());
    }

    // Global model select.
    if (rec_mode == RecMode::IDLE) process_model_select(k.s35().Value());

    // Apply voice params.
    // Seq mode:        S34→drive  S35→FM  (S30/S31/S32 are tempo/shuffle/density)
    // Manual drum mode: S31→drive  S30→FM
    // Pitched modes:  no drive    S30→FM   S31→LPG colour
    if (seq_mode_on) {
        pool.SetDrive(k.s34().Value());
        pool.SetFMAmount(k.s35().Value());
        pool.SetLPGColour(0.5f);
    } else if (is_drum_mode) {
        pool.SetDrive(s31);
        pool.SetFMAmount(fm);
        pool.SetLPGColour(0.5f);
    } else {
        pool.SetDrive(0.0f);
        pool.SetFMAmount(fm);
        pool.SetLPGColour(s31);
    }
    if (current_mode == PlayMode::BASIC_PITCH) {
        pool.SetHarmonics(last_h);
        pool.SetTimbre(last_t);
        pool.SetMorph(last_m);
        pool.SetDecay(last_d);
    }

    // Sequencer tick — Tick() returns 0 when paused, so no triggers fire while paused.
    if (seq_mode_on) {
        float   punch     = k.s33().Value();  // S33: kick timbre boost → more FM attack punch
        float   tightness = k.s37().Value();  // S37: hat morph compression → shorter/tighter hats
        uint8_t triggers  = seq.Tick();
        if (seq.BeatFired()) led_event = LedEvent::BEAT;
        for (int i = 0; i < 7; i++) {
            if (triggers & (1u << i)) {
                PadSlot s = pad_slots[i];  // copy — mutate per-trigger without touching the stored slot
                if (i == 0)           s.timbre = s.timbre + punch * (1.0f - s.timbre);  // kick punch
                if (i == 2 || i == 3) s.morph  = s.morph  * (1.0f - tightness * 0.8f); // hat tight
                pool.NoteOnWithParams(i, s.note, s.engine,
                    s.harmonics, s.timbre, s.morph, s.decay);
            }
        }
    }

    // Render. Static to keep 1.5 KB off the ISR stack.
    static float left[kBlockSize];
    static float right[kBlockSize];
    __builtin_memset(left,  0, size * sizeof(float));
    __builtin_memset(right, 0, size * sizeof(float));
    for (size_t offset = 0; offset < size; offset += kChunkSize)
        pool.Render(left + offset, right + offset, kChunkSize);

    // Output.
    const float scale = out_level / static_cast<float>(VoicePool::kVoices);
    for (size_t i = 0; i < size; i++) {
        out[0][i] = left[i]  * scale;
        out[1][i] = right[i] * scale;
    }
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

    touch.pads().SetOnTouch([](uint16_t pad) {
        if (pad >= 3 && pad <= 9) {
            int slot = pad - 3;
            bool in_random = (current_mode == PlayMode::SOFT_RANDOM ||
                              current_mode == PlayMode::FULL_RANDOM);

            if (rec_mode == RecMode::RECORDING) {
                // P0 + another pad → enter copy mode (copy rec_slot INTO this pad's slot).
                if (touch.pads().IsTouched(0) && slot != rec_slot) {
                    rec_mode     = RecMode::COPY;
                    rec_copy_dst = slot;
                    rec_hold_count = 0;
                    const auto& s = pad_slots[rec_slot];
                    pool.AuditionWithParams(root_note_f(), s.engine,
                                            s.harmonics, s.timbre, s.morph, s.decay);
                }
                // Other presses while recording: ignored (pad is silent).

            } else if (rec_mode == RecMode::COPY) {
                // Presses ignored in copy mode.

            } else if (in_random && touch.pads().IsTouched(0)) {
                // P0 + pad tap in modes 2 or 3 → enter recording mode for this slot.
                enter_rec_mode(slot);

            } else {
                // Normal note play.
                switch (current_mode) {
                    case PlayMode::BASIC_PITCH:
                        pool.NoteOn(slot, compute_note(pad));
                        break;
                    case PlayMode::SOFT_RANDOM: {
                        const auto& s = pad_slots[slot];
                        pool.NoteOnWithParams(slot, compute_note(pad),
                            s.engine, s.harmonics, s.timbre, s.morph, s.decay);
                        break;
                    }
                    case PlayMode::FULL_RANDOM: {
                        const auto& s = pad_slots[slot];
                        pool.NoteOnWithParams(slot, s.note,
                            s.engine, s.harmonics, s.timbre, s.morph, s.decay);
                        break;
                    }
                }
            }

        } else if (pad == 0) {
            if (rec_mode == RecMode::IDLE) {
                bank_caught[0] = false;
                bank_thresh[0] = touch.knobs().s35().Value();
            }

        } else if (pad == 1) {
            if (!seq_mode_on) {
                // Enter seq mode — keep existing drum sounds if available.
                if (rec_mode != RecMode::IDLE) cancel_rec_mode();
                if (!is_drum_mode) { generate_drum_random(); is_drum_mode = true; }
                current_mode     = PlayMode::FULL_RANDOM;
                p0p2_hold_count  = 0;
                p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
                p0p2_stage_fired = 0;
                p0p2_all_done    = false;
                pool.AllNotesOff();
                seq.Start();
                seq_mode_on   = true;
                p1_hold_count = 0;
                p1_hold_fired = true;   // suppress hold-exit on the same press that entered
                led_event     = LedEvent::CONFIRM;
            } else if (touch.pads().IsTouched(2)) {
                // P1 + P2 in seq mode → randomize drum sounds, restart from bar 0.
                generate_drum_random();
                is_drum_mode  = true;
                pool.AllNotesOff();
                seq.Start();
                p1_hold_fired = true;   // suppress release pause/resume action
                led_event     = LedEvent::CONFIRM;
            } else {
                // Arm hold timer; pause/resume fires on P1 release (short tap).
                p1_hold_count = 0;
                // p1_hold_fired remains false so release action can fire.
            }

        } else if (pad == 2) {
            if (seq_mode_on && touch.pads().IsTouched(1)) {
                // P2 while P1 held in seq mode → randomize drum sounds, restart.
                generate_drum_random();
                is_drum_mode  = true;
                pool.AllNotesOff();
                seq.Start();
                p1_hold_fired = true;   // suppress P1 release action
                led_event     = LedEvent::CONFIRM;
            } else if (rec_mode == RecMode::IDLE) {
                bank_caught[1] = false;
                bank_thresh[1] = touch.knobs().s35().Value();
            }

        } else if (pad == 10) {
            if (rec_mode == RecMode::RECORDING && current_mode == PlayMode::FULL_RANDOM
                    && !touch.pads().IsTouched(0) && rec_slot >= 0) {
                auto& s = pad_slots[rec_slot];
                s.note = std::max(0.f, s.note - 1.f);
                pool.AuditionWithParams(s.note, s.engine, s.harmonics, s.timbre, s.morph, s.decay);
            } else if (touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                if (root_semitone > 0) { root_semitone--; pool.Audition(root_note_f()); }
                else                   { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0)) {
                octave_offset = std::max(-3, octave_offset - 1);
            }

        } else if (pad == 11) {
            if (rec_mode == RecMode::RECORDING && current_mode == PlayMode::FULL_RANDOM
                    && !touch.pads().IsTouched(0) && rec_slot >= 0) {
                auto& s = pad_slots[rec_slot];
                s.note = std::min(127.f, s.note + 1.f);
                pool.AuditionWithParams(s.note, s.engine, s.harmonics, s.timbre, s.morph, s.decay);
            } else if (touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                if (root_semitone < 11) { root_semitone++; pool.Audition(root_note_f()); }
                else                    { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0)) {
                octave_offset = std::min(3, octave_offset + 1);
            }
        }
    });

    touch.pads().SetOnRelease([](uint16_t pad) {
        if (pad >= 3 && pad <= 9) {
            if (rec_mode == RecMode::IDLE) pool.NoteOff(pad - 3);
        } else if (pad == 0 || pad == 2) {
            if (rec_mode == RecMode::IDLE) bank_caught[(pad == 0) ? 0 : 1] = false;
        } else if (pad == 1) {
            // Short tap in seq mode (no hold-exit, no combo) → pause / resume.
            if (seq_mode_on && !p1_hold_fired) {
                if (seq.IsActive()) {
                    seq.Stop();
                    pool.AllNotesOff();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;   // 2 blinks: paused
                } else {
                    seq.Resume();
                    led_event = LedEvent::CONFIRM;  // 3 blinks: resumed
                }
            }
            p1_hold_count = 0;
            p1_hold_fired = false;
        }
    });

    hw.StartAudio(AudioCallback);

    while (true) {
        bool     in_random = (current_mode == PlayMode::SOFT_RANDOM ||
                              current_mode == PlayMode::FULL_RANDOM);
        uint32_t hold  = p0p2_hold_count;
        bool     done  = p0p2_all_done;
        uint32_t stage = p0p2_stage_fired;

        // P0+P2 hold animation (highest priority — swallows the loop iteration).
        if (in_random && (hold > 0 || done) && rec_mode == RecMode::IDLE) {
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

        // Copy mode: double blink pattern.
        if (rec_mode == RecMode::COPY) {
            hw.SetLed(true);  System::Delay(100);
            hw.SetLed(false); System::Delay(100);
            hw.SetLed(true);  System::Delay(100);
            hw.SetLed(false); System::Delay(500);
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
