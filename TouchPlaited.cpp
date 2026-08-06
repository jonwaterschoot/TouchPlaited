#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"
#include "touch/touch.h"
#include "display/oled_screen.h"
#include "display/oled_ui.h"
#include "display/oled_boot.h"
#include "synth/voice_pool.h"
#include "synth/sequencer.h"
#include "synth/arp.h"
#include "synth/note_rec.h"
#include "synth/fx.h"
#include "synth/settings_journal.h"
#include "midi/midi_io.h"
#include "midi/telemetry.h"
#include "log.h"
#include <algorithm>
#include <cmath>

using namespace daisy;
using namespace synthux;

// libDaisy's USBD descriptor now expects the application to supply these
// (Synthux fork, "Use external names" / "One more external name") instead
// of the old hardcoded "Electrosmith" / "Daisy Seed ..." defines.
extern "C"
{
const char* USBD_MANUFACTURER_STRING = "TouchPlaited";
const char* USBD_PRODUCT_STRING_HS   = "TouchPlaited";
const char* USBD_PRODUCT_STRING_FS   = "TouchPlaited";
}

DaisySeed hw;
Touch touch;
OledScreen oled;
VoicePool pool;
Sequencer seq;
Arp arp;
NoteRec note_rec;
MidiIO midi;

// LED shadow: every blink goes through set_led so telemetry can stream the
// exact pattern to the visualizer instead of the webapp re-implementing it.
static volatile bool led_lit = false;
static inline void set_led(bool on) {
    led_lit = on;
    hw.SetLed(on);
}

// ISR load measurement — groundwork for expanding the voice pool. Printed over
// serial every 2s from the main loop (visible via `make debug` / USB serial).
static CpuLoadMeter cpu_meter;

// Settings persistence (QSPI journal — synth/settings_journal.h, README):
// restored gates the boot-time pot arming below so the saved state holds
// until each pot picks it up, instead of the pots stomping it on block one.
static SettingsJournal settings_journal;
static bool            settings_restored = false;

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
// the background while SW2 sits on Basic Pitch or Arp/Mel. P2+P11 toggles it.
static bool seq_mode_on      = false;
static bool seq_entered_once = false;   // first SW2-Up entry auto-starts the seq

// Seq settings, stored separately from the pots. They only follow a pot after
// it picks up (crosses the stored value) inside Seq mode — so using the same
// pot in another mode, or re-entering Seq, never jumps a setting.
// Defaults match the Sequencer class defaults (120 BPM, no shuffle, density
// 2, chance 0.5 = pattern's own chance nibbles untouched).
static float seq_tempo_lk  = 0.5f;
static float seq_shuf_lk   = 0.0f;
static float seq_dens_lk   = 0.5f;
static float seq_chance_lk = 0.5f;  // S34 in Seq — see Sequencer::SetChance
static float seq_tight_lk  = 0.5f;
static float seq_drive_lk  = 0.0f;
static float seq_var_lk    = 0.0f;   // S35 in Seq = pattern variant within genre
static int   seq_genre_lk  = 1;      // SW1 in Seq = genre (0 IDM 1 Techno 2 Electro);
                                      // starts at Techno; change-latched (see SW1 handler)
// SW1's other change-latched role, declared up here beside the genre because
// service_telemetry() publishes both together (t.sw1_latch) long before the
// scale system itself is defined. Raw switch order, not panel order — it
// indexes kScales directly; sw1_panel_pos() normalises it on the wire.
static int   scale_lk      = 0;
static float seq_vol_lk    = 1.0f;   // S36 in Seq = drum group volume
static float seq_width_lk  = 0.0f;   // P0+S37 in Seq = drum-group stereo width (0 = mono)

// Pitched-mode fader state. Volume (S36) and blend (S37) go through pickup:
// S37 doubles as the width control while P0 is held, and recording borrows
// both faders — a raw live read would jump the setting on every hand-off.
// Width defaults to 0: the mix is mono until the width control opens it.
// Volume and blend are per-mode (20/07/26 notes + 21/07/26 follow-up: "each
// playmode has its own volume... REC is considered separate", extended to
// drive and blend too) — pitched_vol_lk/pitched_blend_lk are Basic Pitch's;
// the arp and Rec get their own (arp_vol_lk/rec_vol_lk, arp_blend_lk/
// rec_blend_lk, declared with the rest of the Arp/Mel state below). Width
// stays shared across all three — never asked to split, and it's a fader
// position (0=mono), not a per-voice trigger param like drive/blend.
static float pitched_vol_lk   = 1.0f;
static float pitched_blend_lk = 0.5f;
static float pitched_width_lk = 0.0f;

// FX mirror knobs (P1+S30 = reverb, P1+S35 = delay), stored per group like
// the volume/width pairs so each group keeps an independent wet level AND
// character (21/07/26 follow-up: 4 fully independent reverb/delay instances
// now — see fx.h — so there's no more shared "last edit wins" character
// lock; each group's own knob position drives its own instance's character
// directly, decoded fresh every block where it's used).
// Encoding: 0.5 = off (center dead zone); left of center = character A, wet
// grows toward 0.0; right of center = character B, wet grows toward 1.0.
// Reverb: room | hall. Delay: slapback | synced dotted 1/8.
// Drum recording adds a per-slot trim on top of the drum-group send (PadSlot
// rev_send/dly_send, edited with the same P1 combo): group send × slot trim,
// so the mirror knob stays the master wet and the trims fine-balance the kit.
// fx_*_pitched_lk is Basic Pitch's; the arp and Rec get their own
// (fx_*_arp_lk/fx_*_rec_lk, declared with the rest of the Arp/Mel state).
static float fx_rev_seq_lk = 0.5f, fx_rev_pitched_lk = 0.5f;
static float fx_dly_seq_lk = 0.5f, fx_dly_pitched_lk = 0.5f;

// Mirror-knob decode: side -1/0/+1 and wet 0..1 (distance from the dead zone).
static constexpr float kFxDeadZone = 0.06f;
static int fx_decode(float v, float* wet) {
    const float half = 0.5f - kFxDeadZone;
    if (v < half)        { *wet = (half - v) / half;         return -1; }
    if (v > 1.f - half)  { *wet = (v - (1.f - half)) / half; return  1; }
    *wet = 0.f;
    return 0;
}

// Volume fader taper: knob is stored/persisted linear (UI, snapshots, dq8
// all expect 0..1), but a linear pot makes a quiet mix-under-drums level
// (a few % gain) sit in an unusably tiny sliver of travel. This maps knob
// linearly to dB instead — 0 = -kVolTaperRangeDb, 1 = unity — the way a real
// fader tapers, so the quiet end gets proportionally far more of the travel:
// knob 0.25 ≈ -37.5 dB (1.3%), 0.5 ≈ -25 dB (5.6%), 0.75 ≈ -12.5 dB (23.7%).
// Only applied where the value is handed to the DSP (pool.Set*Volume) — the
// stored *_vol_lk stays linear so LEDs/snapshots are unaffected.
static constexpr float kVolTaperRangeDb = 50.f;
static float vol_taper(float knob) {
    if (knob <= 0.f) return 0.f;
    float db = kVolTaperRangeDb * (knob - 1.f);
    return powf(10.f, db * (1.f / 20.f));
}

// Unified long-hold threshold for recording confirm / copy in every mode.
// 1200 ms: long enough that a deliberate drum-pad hold reads as intent, not
// an accident.
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

// A random engine 0–23, skipping Chiptune (7) — self-running arpeggiator,
// excluded from every random pool. Used for boot randomization (Basic Pitch,
// the arp's and Rec's independent sounds all start on an unrelated model).
static int random_engine() {
    int idx = static_cast<int>(rand_f() * 23.f);   // 0..22
    return (idx >= 7) ? idx + 1 : idx;
}

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

// Engines whose internal decay lives on MORPH: Six-Op FM (2–4) route it to
// the DX7 envelope-time control (their LPG is bypassed — already_enveloped —
// so the real Decay param does nothing there), String and Modal to the
// physical model's damping, the drum engines (21–23) to their tail. The
// Decay knob owns morph for these; S34 has no effect on them.
static bool decay_via_morph(int e) {
    return (e >= 2 && e <= 4) || (e >= 19 && e <= 23);
}

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
    if (decay_via_morph(s.engine))
        p.morph = (tight < 0.f) ? s.decay : s.decay * (0.2f + tight * 0.8f);
    return p;
}
// Per-mode slot arrays — each playmode keeps its own state so flicking SW2
// back and forth restores the last sound (mode memory). Only P0+P2 re-randomizes.
static PadSlot drum_slots[kPadSlots];       // Seq mode drum kit
static bool    drum_kit_ready = false;      // generated once; refreshed only by P0+P2 stage 2
static bool    is_drum_mode = false;

// Basic Pitch P0+P2 randomize snapshots. While active, pads play these instead
// of the live global knobs; grabbing any timbral knob returns to live mode.
static PadSlot bp_slots[kPadSlots];
static bool    bp_slots_active = false;
static float   bp_ref_h = 0.f, bp_ref_t = 0.f, bp_ref_m = 0.f, bp_ref_d = 0.f;

// Slot array being edited in recording mode. Recording is Seq-only since the
// Random mode's pitched slot editing retired with the Arp/Mel overhaul.
static PadSlot* live_slots() { return drum_slots; }

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

// Per-mode octave: Basic Pitch, the arp (background-latched, independent of
// whatever mode is currently in view), and Rec each keep their own — bare
// P10/P11 taps write whichever store matches current_mode/arp_state at the
// time (see active_octave()); nothing else changes hands.
static int bp_octave            = 0;
static int arp_octave           = 0;
static int rec_octave           = 0;
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

// Six-Op A/B/C (engines 2–4) are nearly silent over most of their parameter
// space: harmonics is a quantized DX7 patch selector and timbre is the FM
// modulator level (quiet near zero). These presets land on audible spots —
// used for the model-change audition AND as the anchor for random generation.
// m is the DX7 envelope-time anchor (morph): auditions apply it directly via
// VoiceParams; live play ignores stored morph on these engines and routes the
// Decay knob there instead (decay_via_morph).
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
// Particle (18) is excluded from every drum table: its intentionally sporadic
// crackle reads as a hardware fault when it lands in a random kit. Noise (17)
// covers the same ground and stays.
static const DrumOpt kDrumSnare[] = {
    { 22, 0.10f,0.60f, 0.30f,0.80f, 0.30f,0.70f, 0.4f,0.8f, 48.f,60.f },
    { 17, 0.05f,0.20f, 0.55f,0.90f, 0.30f,0.70f, 0.3f,0.6f, 48.f,60.f },
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
    { 22, 0.05f,0.20f, 0.05f,0.35f, 0.30f,0.60f, 0.3f,0.5f, 66.f,80.f },
};

// Which pool each kit slot draws from, and the role volume that comes with
// it — the single source of truth for both whole-kit generation and the
// "is this slot still playing its own role" test below. Splitting it out of
// generate_drum_random() is what made per-slot randomizing (Rec P0+P2) and
// the off-pool snap-back possible without a second copy of the table.
struct DrumPool { const DrumOpt* opts; int n; float volume; };
static const DrumPool kDrumPools[kPadSlots] = {
    { kDrumKick,  2, 0.90f }, { kDrumSnare, 2, 0.80f }, { kDrumCHH,  2, 0.55f },
    { kDrumOHH,   1, 0.65f }, { kDrumClap,  2, 0.75f }, { kDrumTom,  2, 0.70f },
    { kDrumPerc,  3, 0.50f },
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

// True while slot i is still on one of the engines its role is curated for.
// Rec mode's S35 reaches all 24 engines, so a slot can be pointed at anything
// by hand — this is how the randomizers tell "a kick that needs varying" from
// "a Speech engine somebody parked on the kick pad".
static bool slot_engine_in_pool(int i, int engine) {
    const DrumPool& p = kDrumPools[i];
    for (int j = 0; j < p.n; j++)
        if (p.opts[j].e == engine) return true;
    return false;
}

// Re-pick slot i from its own curated pool: new engine, new params, role
// volume. The "stick to kick models" primitive — every randomizer that
// replaces a sound goes through here, so none of them can wander off-role.
static void fill_drum_slot_from_pool(int i) {
    fill_drum_slot(drum_slots[i], kDrumPools[i].opts, kDrumPools[i].n);
    drum_slots[i].volume = kDrumPools[i].volume;
}

static void generate_drum_random() {
    for (int i = 0; i < kPadSlots; i++) fill_drum_slot_from_pool(i);
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
// morph via tightness, kick punch riding drive, overall drive × slot ratio).
// Shared by triggers and rec-mode auditions, so tweaking a drum against the
// paused seq sounds the same as it will when the pattern runs.
//
// Kick punch used to be its own S34 knob; that knob is now Chance (a
// sequencer-wide control, see Sequencer::SetChance) and punch instead rides
// S30 Drive — turning the kick up pushes both together, so there's no
// dedicated knob to go dead on models that don't need it (2026-08-04).
static VoiceParams drum_params(int i) {
    const PadSlot& s = drum_slots[i];
    VoiceParams p = slot_params(s, seq_tight_lk);
    if (i == 0) p.timbre = p.timbre + seq_drive_lk * (1.0f - p.timbre);
    p.drive = clampf(seq_drive_lk * s.drive);
    return p;
}

// Fire drum slot i — used by both seq steps and manual pad hits in Seq mode.
// Slot id 16+i keeps drum voices out of reach of pad NoteOffs (slots 0–6), and
// lock_params=true shields them from the active playmode's global knob writes.
static void trigger_drum(int i, float vel = 1.0f) {
    VoiceParams p = drum_params(i);
    p.volume *= vel;   // MIDI velocity; pads and seq steps pass 1.0
    pool.NoteOnWithParams(16 + i, drum_slots[i].note, p, VoiceGroup::kDrum);
}

// Vary one slot in place: same engine, same note, params nudged. The unit
// both the whole-kit stage 1 and Rec's per-pad stage 1 are built from.
static void mutate_drum_slot_soft(int i) {
    auto& s = drum_slots[i];
    s.harmonics = clampf(s.harmonics + rand_range(-0.10f, 0.10f));
    s.timbre    = clampf(s.timbre    + rand_range(-0.10f, 0.10f));
    s.decay     = clampf(s.decay     + rand_range(-0.08f, 0.08f));
}

// Seq P0+P2 stage 1: nudge params of the current kit — same engines, same
// notes, with one exception. A slot sitting on an engine outside its own
// curated pool (Rec mode's S35 reaches all 24) is re-picked from that pool
// instead of jittered: jittering it left the kick pad on whatever non-kick
// model had been chosen by hand, so "vary kit" could never bring the kit
// back to kit-shaped sounds. In-pool slots are untouched by the snap, so
// this is still "same kit, new variation" for a kit that was randomized.
static void mutate_drum_soft() {
    for (int i = 0; i < kPadSlots; i++) {
        if (!slot_engine_in_pool(i, drum_slots[i].engine)) fill_drum_slot_from_pool(i);
        else                                               mutate_drum_slot_soft(i);
    }
}

// ─── Playmode ─────────────────────────────────────────────────────────────────
// Switch3: 0=CENTER, 1=UP, 2=DOWN
// A() = SW2 (right, _switch_7_8): Init(D7, D6) → D7=Down(pos1), D6=Up(pos2)
// B() = SW1 (left, _switch_9_10): Init(D9, D8) → D9=right-flick(pos1), D8=left-flick(pos2)
enum class PlayMode { BASIC_PITCH, ARP_MEL };
static volatile PlayMode current_mode = PlayMode::BASIC_PITCH;

// A() polarity verified on hardware: sw=2=Down→Basic, sw=0=Center→Arp/Mel, sw=1=Up→Seq
static PlayMode sw2_to_mode(int sw) {
    if (sw == 0) return PlayMode::ARP_MEL;
    return PlayMode::BASIC_PITCH;
}

// ─── Arp/Mel mode state ───────────────────────────────────────────────────────
// SW1 sub-state inside Arp/Mel, change-latched like scale/genre: the physical
// position only takes effect when the switch MOVES while Arp/Mel is active —
// entering the mode never applies it, and the state persists across SW2
// flicks (mode memory; a latched arp keeps playing in the background).
// Positions: center = Arp (momentary pool), left = Hold (latch, re-touch
// removes, leaving Hold clears), right = Rec (record note layers).
enum class ArpState { ARP, HOLD, REC };
static ArpState arp_state = ArpState::ARP;

// Which octave store a bare P10/P11 tap or compute_note() should use right
// now. Seq mode has no pitched octave of its own — falls back to bp_octave,
// same "current_mode still holds the last pitched mode" doctrine used by the
// bp_slots_active escape check elsewhere. The running arp reads arp_octave
// directly instead (see fire_arp_note) — it keeps its own octave even when
// this resolves to bp_octave/rec_octave because Basic Pitch or Rec is what's
// currently in view.
static int& active_octave() {
    if (!seq_mode_on && current_mode == PlayMode::ARP_MEL)
        return (arp_state == ArpState::REC) ? rec_octave : arp_octave;
    return bp_octave;
}

// The arp's own sound model. Seeded from the live Basic Pitch sound on the
// FIRST Arp/Mel entry ever, then fully independent — BP knob moves and mode
// round-trips never touch it again (hardware feedback 16/07/26: the earlier
// re-latch-on-every-BP-entry made the two modes impossible to tell apart).
// Editing it: P0+P1 hold toggles the sound-edit knob layer (arp_snd_edit),
// P0/P2+S35 swaps the engine, P0+P2 staged hold varies h/t/m around it.
// Decay (S31), drive (S30) and blend stay live via their own arp knobs and
// are written into this slot at every trigger.
static PadSlot arp_snd;
static bool    arp_snd_ready = false;

// Rec's own sound model — fully independent of arp_snd and Basic Pitch
// (20/07/26 notes: "fully detach sound model from playmodes... each starts
// with a random model at boot"). Unlike arp_snd's lazy first-entry seed,
// rec_snd is randomized once at boot (main()) and ready is never false again.
// Editing it: P0+P1 hold toggles rec_snd_edit while SW1=Rec, P0/P2+S35 swaps
// the engine, P0+P2 staged hold varies h/t/m — same gestures as arp_snd, just
// scoped to the Rec sub-state. Decay/drive stay the shared arp_decay_lk /
// arp_drive_lk knobs (same physical knobs, same meaning, in Arp and Rec
// alike) and are written into this slot at every trigger.
static PadSlot rec_snd;
static bool    rec_snd_ready = false;

// Sound-edit sub-mode (P0+P1 held ~1s, toggle): the knobs leave their arp/Rec
// roles and edit arp_snd or rec_snd (whichever SW1 sub-state is active) with
// the Basic Pitch layout — S30 drive, S31 decay, S32 harmonics, S33 timbre,
// S34 morph. Exits on re-toggle, any SW1 state change, or leaving/re-entering
// the mode. Only one of the two can be active at a time (SW1 selects which).
static bool arp_snd_edit = false;
static bool rec_snd_edit = false;

// Arp knob settings — same locked-value + pickup scheme as the seq knobs.
// Defaults: division center (1/16), steady full density, order random.
static float arp_drive_lk = 0.0f, arp_div_lk  = 0.5f, arp_swing_lk = 0.0f,
             arp_dens_lk  = 1.0f, arp_decay_lk = 0.5f, arp_order_lk = 1.0f;
static int   arp_oct_range = 0;   // P0+P10/P11, 0–3 extra octaves

// Rec-only knob layer (S32-S35 while SW1=Rec — 20/07/26 notes,
// notesarchive/arp-mel-plan-archive.md §7/C10). S31 decay stays the shared
// arp_decay_lk above (a hold gesture on a note's tail feels the same either
// way); only S32-S35 take on Rec-specific meanings. Defaults: speed 1x
// (center), no shift (center), always fires, original order (left).
static float rec_speed_lk = 0.0f, rec_shift_lk = 0.5f,
             rec_chance_lk = 1.0f, rec_order_lk = 0.0f;

// Rec's own drive (21/07/26 follow-up: was sharing arp_drive_lk — the notes
// asked for every fader to be independent between Rec and Arp, not just
// volume/FX-send).
static float rec_drive_lk = 0.0f;

// Per-mode volume (S36) and FX send (P1+S30/S35) locks for the arp and Rec —
// see pitched_vol_lk/fx_*_pitched_lk above (Basic Pitch's). 20/07/26 notes:
// each playmode's volume and FX send stay independent, Rec separate from Arp.
static float arp_vol_lk = 1.0f, rec_vol_lk = 1.0f;
static float arp_blend_lk = 0.5f, rec_blend_lk = 0.5f;
static float fx_rev_arp_lk = 0.5f, fx_rev_rec_lk = 0.5f;
static float fx_dly_arp_lk = 0.5f, fx_dly_rec_lk = 0.5f;

// Which volume/blend lock a bare S36/S37 edit should target right now — same
// "current_mode + arp_state" dispatch as active_octave() above. Only ever
// called from !seq_mode_on contexts (Seq has its own seq_vol_lk, untouched
// by this), so no seq_mode_on branch is needed.
static float& active_pitched_vol_lk() {
    if (current_mode == PlayMode::ARP_MEL)
        return (arp_state == ArpState::REC) ? rec_vol_lk : arp_vol_lk;
    return pitched_vol_lk;
}
static float& active_blend_lk() {
    if (current_mode == PlayMode::ARP_MEL)
        return (arp_state == ArpState::REC) ? rec_blend_lk : arp_blend_lk;
    return pitched_blend_lk;
}
// Which FX-send lock a P1+S30/P1+S35 edit should target — unlike the volume
// pickup above, the FX combo is live in every mode including Seq (drum
// sends), so this needs the same seq_mode_on branch active_octave() has.
static float& active_fx_rev_lk() {
    if (seq_mode_on) return fx_rev_seq_lk;
    if (current_mode == PlayMode::ARP_MEL)
        return (arp_state == ArpState::REC) ? fx_rev_rec_lk : fx_rev_arp_lk;
    return fx_rev_pitched_lk;
}
static float& active_fx_dly_lk() {
    if (seq_mode_on) return fx_dly_seq_lk;
    if (current_mode == PlayMode::ARP_MEL)
        return (arp_state == ArpState::REC) ? fx_dly_rec_lk : fx_dly_arp_lk;
    return fx_dly_pitched_lk;
}

// Melodic transport (P2+P10) — gates the arp AND the Rec loop together; the
// drum seq keeps its own transport on P2+P11. Defaults to running so the
// plain arp sounds as soon as notes are held.
static bool arp_run_on = true;

// Rec capture arm (2026-07-21 follow-up): SW1=Rec used to start recording
// the instant you touched a pad, which made auditioning Rec's own (now
// independent, possibly unfamiliar) sound impossible without also
// committing a note. Pads always sound regardless of this flag; only
// RecordNote() is gated on it. Always lands false on a fresh Rec entry;
// P2+P10, only while SW1=Rec, toggles it instead of its usual transport
// meaning (disarming does NOT stop playback — committed layers keep
// looping while you're punched out, only new capture stops).
static bool rec_armed = false;

// Arp/loop voice gates: the arp fires real gates (not one-shots), so each
// trigger takes a round-robin pool slot and a block countdown to its NoteOff.
// Slots 24–27 = arp, 28–31 = Rec loop — clear of pads (0–6), drums (16–22)
// and MIDI (32+).
static constexpr int kArpSlotBase = 24;
static constexpr int kRecSlotBase = 28;
static constexpr int kArpGates    = 4;
static uint32_t arp_gate_blocks[kArpGates] = { 0, 0, 0, 0 };
static uint8_t  arp_gate_note[kArpGates]   = { 0, 0, 0, 0 };
static int      arp_gate_rr = 0;
static uint32_t rec_gate_blocks[kArpGates] = { 0, 0, 0, 0 };
static uint8_t  rec_gate_note[kArpGates]   = { 0, 0, 0, 0 };
static int      rec_gate_rr = 0;

// P2 + pad P3-P7 layer gestures in Rec (5 pads, one per NoteRec layer):
// per-pad hold counter tracked in AudioCallback; a pad that reaches
// kLongHoldBlocks fires ClearLayer (or ClearAll if >=2 pads are down at that
// moment) and is marked "fired" so releasing it doesn't also toggle mute.
// SetOnRelease reads both arrays to fire the tap (ToggleMute) action; the
// main loop also reads them (volatile) for the hold-countdown LED animation.
static volatile uint32_t p2layer_hold[5]  = { 0, 0, 0, 0, 0 };
static volatile bool     p2layer_fired[5] = { false, false, false, false, false };
// Set alongside p2layer_fired[i], read by compute_hold_telemetry() for the
// OLED/visualizer confirm flash: 0 none yet, 1 a layer (or all) actually
// cleared, 2 the pad had nothing recorded (LED's LIMIT case) — the LED
// itself already tells these apart (NUMBERED vs LIMIT blink); this is that
// same distinction, carried to the screen instead of a blink rhythm.
static volatile uint8_t  p2layer_outcome[5] = { 0, 0, 0, 0, 0 };

// External MIDI clock ticks for the arp + loop clocks, counted here because
// Sequencer consumes its own. Written from the MIDI handler (main loop, IRQs
// off), consumed once per block in AudioCallback.
static volatile uint32_t arp_ext_ticks = 0;

// Switch3 raw (0 center, 1 up, 2 down) → panel position (0 left, 1 center,
// 2 right). Anything that has to agree with what the panel/OLED prints must
// go through this — the Seq genre indexes the pattern registry, whose order
// is panel order. Scale and the Arp sub-state deliberately keep consuming the
// raw value: their tables are written against up/down/center directly.
static int sw1_panel_pos(int sw_raw) {
    static const uint8_t kMap[3] = { 1, 2, 0 };
    return kMap[(sw_raw >= 0 && sw_raw <= 2) ? sw_raw : 0];
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
// Rec entry's confirm is published through fire_confirm() (below) at the
// exact instant entry fires: entry_hold_count resets to 0 in that same ISR
// call, so compute_hold_telemetry() can't catch the threshold-crossing frame
// by polling the counter.

// Secondary pad tracking for cancel / copy while in recording. copy_hold_anim
// mirrors the copy hold progress (only while the source pad is also down) for
// the main-loop LED: the entry countdown animation restarts during a copy.
static int               cancel_pad     = -1;
static uint32_t          cancel_count   = 0;
static volatile uint32_t copy_hold_anim = 0;
// Same idea as rec entry above: copy_hold_anim/cancel_count both reset to 0
// in the same block that fires the copy, so fire_confirm() is the only trace
// of the completion compute_hold_telemetry() can still see.

// ─── Confirm latch ────────────────────────────────────────────────────────────
// A threshold firing is a one-shot. The counters behind it reset in the same
// ISR block, so "it just fired" used to live for exactly one main-loop pass —
// and both consumers sample slower than that: OledUi::Service sits behind an
// 80 ms redraw throttle and Telemetry::SendState behind a 33 ms rate limit,
// so a confirm landing inside either window was lost outright. That's why
// "RECORDING" only appeared sometimes and the entry bar looked stuck at ~98%.
// The ISR now posts the confirm here; the main loop turns it into a latch
// that keeps reporting for kConfirmLatchMs, comfortably longer than either
// window, so every consumer sees at least one frame carrying it.
// Kept shorter than OledUi's own kConfirmFlashMs (220 ms) on purpose: by the
// time the screen releases the flash the latch is long expired, so the bar
// can't flicker back in behind it.
static constexpr uint32_t kConfirmLatchMs = 120;
static volatile uint8_t   pending_confirm_kind    = 0;  // 0 = nothing posted
static volatile uint8_t   pending_confirm_stage   = 0;
static volatile uint8_t   pending_confirm_outcome = 0;

// ISR-side: post a confirm. kind/stage/outcome match TelemetryState's
// hold_kind/hold_stage/hold_outcome (midi/telemetry.h).
static inline void fire_confirm(uint8_t kind, uint8_t stage, uint8_t outcome = 0) {
    pending_confirm_stage   = stage;
    pending_confirm_outcome = outcome;
    pending_confirm_kind    = kind;   // written last — it's the "ready" flag
}

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
// which is how S30/S34 went dead after seq re-entry with drive/chance at
// their rails.
// Rail targets additionally catch on deliberate movement (~3%, like the width
// MoveCatch): a pot can sit fractionally under its rail forever, which left
// knobs armed to 1.0 feeling dead — the arp Density/Order defaults were
// practically unreachable (hardware feedback 18/07/26).
struct KnobPickup {
    static constexpr float kNear     = 0.01f;
    static constexpr float kMoveZone = 0.03f;
    float thresh = 0.f;
    float prev   = 0.f;
    float ref    = 0.f;
    bool  caught = false;
    bool  rail   = false;
    // Arm against a stored value: the pot takes over when it reaches it.
    void arm_to(float target, float current_val) {
        thresh = target;
        prev   = current_val;
        ref    = current_val;
        caught = false;
        rail   = target <= 0.015f || target >= 0.985f;
    }
    void force_catch(float current_val) {
        thresh = current_val; prev = current_val; ref = current_val;
        caught = true; rail = false;
    }
    bool update(float curr) {
        if (!caught) {
            bool crossed = (prev <= thresh && curr >= thresh) ||
                           (prev >= thresh && curr <= thresh);
            bool near    = (curr - thresh <= kNear) && (thresh - curr <= kNear);
            bool moved   = rail && ((curr - ref >= kMoveZone) ||
                                    (ref - curr >= kMoveZone));
            if (crossed || near || moved) caught = true;
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
// Pickups for the Arp/Mel knob layer (S30 drive, S31 decay, S32 division,
// S33 swing, S34 density, S35 order — decay sits on S31 like every other
// mode, the arp functions shift one knob right) — armed on every entry.
static KnobPickup arp_pu30, arp_pu31, arp_pu32, arp_pu33, arp_pu34, arp_pu35;
// Pickups for the Rec-only S32-S35 layer (speed/shift/chance/order) — armed
// on every SW1 transition into Rec. S31 keeps using arp_pu31 (shared decay);
// S30 gets its own (rec_pu30, drive is independent per 21/07/26 follow-up).
static KnobPickup rec_pu30, rec_pu32, rec_pu33, rec_pu34, rec_pu35;
// Pickups for the Arp/Mel sound-edit layer (BP layout on arp_snd) — armed on
// every P0+P1 toggle into sound edit.
static KnobPickup arp_se30, arp_se31, arp_se32, arp_se33, arp_se34;
// Same, for Rec's independent sound (BP layout on rec_snd) — armed on every
// P0+P1 toggle into sound edit while SW1=Rec.
static KnobPickup rec_se30, rec_se31, rec_se32, rec_se33, rec_se34;
// Pitched-mode faders — armed at boot and on every return to a pitched mode.
// Volume and blend are both per-mode (21/07/26 follow-up).
static KnobPickup pitch_pu_vol;     // S36 volume (Basic Pitch)
static KnobPickup pitch_pu_blend;   // S37 blend (Basic Pitch)
static KnobPickup arp_pu_vol;       // S36 volume (arp)
static KnobPickup arp_pu_blend;     // S37 blend (arp)
static KnobPickup rec_pu_vol;       // S36 volume (Rec)
static KnobPickup rec_pu_blend;     // S37 blend (Rec)

// Arm whichever of pitch_pu_vol/arp_pu_vol/rec_pu_vol and pitch_pu_blend/
// arp_pu_blend/rec_pu_blend match the current mode/arp_state (see
// active_pitched_vol_lk()/active_blend_lk()) — called on every hand-off into
// a pitched context: SW2 flick back from Seq, rec-mode exit (S36/S37 are
// borrowed there), and SW1 sub-state changes within Arp/Mel (arp_state
// flipping between Arp/Hold and Rec changes which lock S36/S37 target).
static void rearm_pitched_faders() {
    auto& kn = touch.knobs();
    float v36 = kn.s36().Value();
    float v37 = kn.s37().Value();
    if (current_mode == PlayMode::ARP_MEL) {
        if (arp_state == ArpState::REC) {
            rec_pu_vol.arm_to(active_pitched_vol_lk(), v36);
            rec_pu_blend.arm_to(active_blend_lk(), v37);
        } else {
            arp_pu_vol.arm_to(active_pitched_vol_lk(), v36);
            arp_pu_blend.arm_to(active_blend_lk(), v37);
        }
    } else {
        pitch_pu_vol.arm_to(active_pitched_vol_lk(), v36);
        pitch_pu_blend.arm_to(active_blend_lk(), v37);
    }
}

static void rearm_seq_pickups() {
    auto& kn = touch.knobs();
    seq_pu30.arm_to(seq_drive_lk, kn.s30().Value());
    seq_pu31.arm_to(seq_tempo_lk, kn.s31().Value());
    seq_pu32.arm_to(seq_shuf_lk,  kn.s32().Value());
    seq_pu33.arm_to(seq_dens_lk,  kn.s33().Value());
    seq_pu34.arm_to(seq_chance_lk, kn.s34().Value());
    seq_pu35.arm_to(seq_var_lk,   kn.s35().Value());
    seq_pu36.arm_to(seq_vol_lk,   kn.s36().Value());
    seq_pu37.arm_to(seq_tight_lk, kn.s37().Value());
    seq_puw.arm(kn.s37().Value());
    // Recording borrows S36/S37 — re-arm the pitched set too.
    rearm_pitched_faders();
    pitch_pu_w.arm(kn.s37().Value());
}

static void rearm_arp_pickups() {
    auto& kn = touch.knobs();
    arp_pu30.arm_to(arp_drive_lk, kn.s30().Value());
    arp_pu31.arm_to(arp_decay_lk, kn.s31().Value());
    arp_pu32.arm_to(arp_div_lk,   kn.s32().Value());
    arp_pu33.arm_to(arp_swing_lk, kn.s33().Value());
    arp_pu34.arm_to(arp_dens_lk,  kn.s34().Value());
    arp_pu35.arm_to(arp_order_lk, kn.s35().Value());
}

static void rearm_rec_pickups() {
    auto& kn = touch.knobs();
    rec_pu30.arm_to(rec_drive_lk,  kn.s30().Value());
    // S31 decay is the one knob genuinely shared with Arp/Hold (arp_pu31/
    // arp_decay_lk) — arm it here too so entering/re-entering Rec never
    // skips it: the only other pickup that ever arms it is
    // rearm_arp_pickups(), which isn't called on a Rec entry.
    arp_pu31.arm_to(arp_decay_lk,  kn.s31().Value());
    rec_pu32.arm_to(rec_speed_lk,  kn.s32().Value());
    rec_pu33.arm_to(rec_shift_lk,  kn.s33().Value());
    rec_pu34.arm_to(rec_chance_lk, kn.s34().Value());
    rec_pu35.arm_to(rec_order_lk,  kn.s35().Value());
}

static void arm_arp_se_pickups() {
    auto& kn = touch.knobs();
    arp_se30.arm_to(arp_drive_lk,      kn.s30().Value());
    arp_se31.arm_to(arp_decay_lk,      kn.s31().Value());
    arp_se32.arm_to(arp_snd.harmonics, kn.s32().Value());
    arp_se33.arm_to(arp_snd.timbre,    kn.s33().Value());
    arp_se34.arm_to(arp_snd.morph,     kn.s34().Value());
}

// Leave sound edit (toggle, SW1 move, mode change): the pots go back to
// their arp roles — re-arm so nothing jumps to wherever editing left them.
static void exit_arp_snd_edit() {
    if (!arp_snd_edit) return;
    arp_snd_edit = false;
    rearm_arp_pickups();
}

static void arm_rec_se_pickups() {
    auto& kn = touch.knobs();
    rec_se30.arm_to(rec_drive_lk,      kn.s30().Value());
    rec_se31.arm_to(arp_decay_lk,      kn.s31().Value());
    rec_se32.arm_to(rec_snd.harmonics, kn.s32().Value());
    rec_se33.arm_to(rec_snd.timbre,    kn.s33().Value());
    rec_se34.arm_to(rec_snd.morph,     kn.s34().Value());
}

// Leave Rec's sound edit (toggle, SW1 move, mode change): the pots go back to
// whatever Rec's S30-S34 currently mean (still the shared Arp knob layer
// until Phase 9 gives Rec its own S32-S35 — arp pickups cover both today).
static void exit_rec_snd_edit() {
    if (!rec_snd_edit) return;
    rec_snd_edit = false;
    // rearm_rec_pickups() covers S30/S31/S32-S35 — the whole Rec knob layer
    // (was a pre-existing gap: this used to call rearm_arp_pickups(), which
    // armed the wrong S30/S32-S35 set for Rec's own knob layer).
    rearm_rec_pickups();
}

// Forward-declared: defined with the rest of the Arp/Mel helpers below, but
// on_midi_note_on (right after the MIDI section comment) needs them earlier.
static VoiceParams arp_params(float decay);
static VoiceParams rec_params(float decay);

// ─── MIDI (mapping design: notesarchive/notes_archive_2026-07.md →
//     "MIDI mapping sketch") ─────────────────────────────────────────────────
// Channel split: ch1 = pitched — the note number IS the pitch, bypassing the
// pad scale/root/octave logic — ch10 = drums via the GM map. CC20–31 and
// 85–88 map to *functions*, not pots; a CC write re-arms that pot's pickup so
// the pot must cross the value to take over (same rule as mode hand-offs).
// Handlers run from MidiIO::Service (main loop) inside an IRQ-off section.
static constexpr uint8_t kMidiPitchCh  = 0;    // ch1
static constexpr uint8_t kMidiDrumCh   = 9;    // ch10
static constexpr int     kMidiSlotBase = 32;   // pool slot ids: pads 0–6, drums 16–22, MIDI 32+note

// GM percussion → drum slot (kick, snare, CHH, OHH, clap, tom, perc). The
// note picks the slot and the slot's stored pitch plays — same as a pad hit.
//
// "Phase 2" was originally going to add transposition: a note within ±6 of a
// slot's anchor would play the slot pitched. Dropped deliberately (2026-08-05)
// — the seven slots are drums, each already carries its own tuned pitch as
// part of the sound, and a transposing kick pad is a different instrument, not
// a played one. **Velocity is the expressive axis instead** — honoured on the
// way in (below) and now generated on the way out (drum_velocity()).
//
// The anchors match the standard 4×4 grid controllers land on, so a pad
// controller's bottom two rows drive the kit with no remapping:
//     48  49  50  51
//     44  45 [46 OHH] 47
//     40 [41 TOM][42 CHH][43 PERC]
//    [36 KICK] 37 [38 SNARE][39 CLAP]
// The wider GM aliases stay accepted so a general drum track still lands
// somewhere sensible; only 43 moved (it was a tom alias, it is Perc's anchor).
static int gm_to_drum_slot(uint8_t note) {
    switch (note) {
        case 35: case 36:                               return 0;  // kicks
        case 38: case 40:                               return 1;  // snares
        case 42: case 44:                               return 2;  // closed + pedal hat
        case 46:                                        return 3;  // open hat
        case 39:                                        return 4;  // clap
        case 41: case 45: case 47: case 48: case 50:    return 5;  // toms
        case 37: case 43: case 54: case 56:
        case 75: case 76:                               return 6;  // perc/rim/tamb/cowbell/clave/woodblock
        default:                                        return -1;
    }
}
// GM note sent out per drum slot (pad hits in Seq mode + seq steps) — the
// anchors of the grid above, so what we send is what we listen for.
static const uint8_t kDrumSlotGm[kPadSlots] = { 36, 38, 42, 46, 39, 41, 43 };

// A step's authored weight (Sequencer's 1–4 accent tier) → GM velocity. With
// pitch fixed per slot, velocity is the only expressive axis ch10 has, and
// every hit used to leave as a flat 100 — a ghost note and a downbeat kick
// were indistinguishable to whatever was listening. Weight 0 is the fallback
// for hits with no step behind them (pad taps, the forced rec-slot trigger).
static uint8_t drum_velocity(uint8_t weight) {
    switch (weight) {
        case 1:  return 45;    // ghost
        case 2:  return 70;    // light accent
        case 3:  return 95;    // medium
        case 4:  return 120;   // strong
        default: return 100;
    }
}

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

    // Pitched: play the current mode's sound — Rec's own sound while SW1=Rec,
    // else the latched arp sound in Arp/Hold, snapshot slots in BP-snapshot
    // mode, else the live knobs (the per-block global setters keep updating
    // those voices, so knobs stay live on held MIDI notes too).
    bool in_rec_snd = current_mode == PlayMode::ARP_MEL
                      && arp_state == ArpState::REC && rec_snd_ready;
    bool in_arp_snd = current_mode == PlayMode::ARP_MEL
                      && arp_state != ArpState::REC && arp_snd_ready;
    VoiceParams p;
    if (in_rec_snd) {
        p = rec_params(arp_decay_lk);
    } else if (in_arp_snd) {
        p = arp_params(arp_decay_lk);
    } else if (current_mode == PlayMode::BASIC_PITCH && bp_slots_active) {
        p = slot_params(bp_slots[note % kPadSlots]);
    } else {
        p.engine    = current_engine;
        p.harmonics = eff_h;
        p.timbre    = eff_t;
        p.morph     = decay_via_morph(current_engine) ? eff_d : eff_m;
        p.decay     = eff_d;
        p.drive     = eff_drive;
        p.blend     = pitched_blend_lk;
    }
    p.volume *= gain;   // scale, don't replace — Random slots carry recorded volumes
    // Notes carrying the arp's or Rec's sound are arp-owned: the BP knobs
    // must not morph them if SW2 flicks to Basic Pitch while they're
    // held/decaying.
    pool.NoteOnWithParams(kMidiSlotBase + note, static_cast<float>(note), p,
                          in_rec_snd ? VoiceGroup::kRec
                          : in_arp_snd ? VoiceGroup::kArp : VoiceGroup::kBP);
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
        case 30: seq_chance_lk = v; seq_pu34.arm_to(v, kn.s34().Value());
                 seq.SetChance(v);                                          break;
        case 31: seq_tight_lk = v; seq_pu37.arm_to(v, kn.s37().Value());     break;
        // FX mirror values, same center-off encoding as the P1 knob layer
        // (64 ≈ off; below = character A, above = character B, wet grows
        // outward). Basic Pitch and drums only — the arp's and Rec's own FX
        // aren't reachable over MIDI (same as their sound). Each group's
        // send+character are both this one value now (21/07/26 follow-up:
        // 4 independent instances, no more shared character lock). Re-arming
        // the movement-catch means a caught pot must be nudged again to take
        // back over.
        case 85: fx_rev_pitched_lk = v; fx_mc_rev.arm(kn.s30().Value()); break;
        case 86: fx_rev_seq_lk = v;     fx_mc_rev.arm(kn.s30().Value()); break;
        case 87: fx_dly_pitched_lk = v; fx_mc_dly.arm(kn.s35().Value()); break;
        case 88: fx_dly_seq_lk = v;     fx_mc_dly.arm(kn.s35().Value()); break;
        default: break;
    }
}

// DAW stop sends CC123 — release MIDI voices only; pad-held notes stay.
static void on_midi_all_notes_off() {
    for (int n = 0; n < 128; n++) pool.NoteOff(kMidiSlotBase + n);
}

// ── External clock (MIDI + CV) ────────────────────────────────────────────────
// Two external clock sources, one master at a time:
//   MIDI — F8 on either port, 24 ppqn. Hard-syncs the seq (6 ticks per 16th)
//          and mutes the tempo knob/CC27 (the block counter isn't consulted).
//   CV   — pulses on the S43 jack (A11), 1 pulse per 16th step. Edge-detected
//          once per block (~4ms) from the raw ADC with a software Schmitt
//          trigger; each pulse is multiplied into 6 synthetic 24 ppqn ticks
//          spread over the measured pulse period and fed down the exact same
//          path as MIDI F8s (see the CV block in AudioCallback).
// Hierarchy: MIDI outranks CV — higher resolution, and it carries transport.
// CV edges keep being measured while MIDI drives, so a MIDI dropout (500ms
// silent) hands over to CV within one pulse; a CV dropout (~2.5 missed
// pulses) falls back to the knob tempo. Whichever clock is master (internal
// included) is forwarded to BOTH outputs: F8s on MIDI out and a ~12ms pulse
// per 16th on the S40 jack (D25) — the unit always acts as a clock hub.
// Local start/stop send FA/FB/FC unless MIDI is master (then the DAW owns
// transport and its FA/FB/FC pass through instead).
enum class ClockSrc : uint8_t { NONE, MIDI, CV };
static volatile ClockSrc  ext_clock_src       = ClockSrc::NONE;
static volatile uint32_t  midi_clock_last_ms  = 0;
static constexpr uint32_t kMidiClockTimeoutMs = 500;
static bool ext_clocked()       { return ext_clock_src != ClockSrc::NONE; }
static bool midi_clock_master() { return ext_clock_src == ClockSrc::MIDI; }

// CV-in edge detector state. Schmitt thresholds in raw 16-bit ADC counts:
// rise above ~1.0V, re-arm below ~0.6V at the 3.3V pin — catches any
// conditioned pulse >= ~1.5V while sitting far above the floor noise.
// Everything except the last-edge stamp is touched only from the audio ISR.
static constexpr uint16_t kCvInHiRaw          = 19661;
static constexpr uint16_t kCvInLoRaw          = 11796;
static constexpr uint32_t kCvPeriodMinBlocks  = 3;     // ~12ms, ~1250 BPM guard
static constexpr uint32_t kCvPeriodMaxBlocks  = 750;   // 3s — slower is "no clock"
static bool               cv_in_high          = false;
static uint32_t           cv_blocks_since_edge = 0;
static float              cv_period_blocks    = 125.f; // last measured period
static int                cv_ticks_in_window  = 6;     // 6 = closed, wait for edge
static float              cv_tick_acc         = 0.f;
static volatile uint32_t  cv_edge_last_ms     = 0;

// Clock out (S40/D25): every 6th tick of the master clock — whatever its
// source — starts a 3-block (~12ms) high pulse. Producers bump the pending
// counter (MIDI handler with IRQs off, or the ISR itself); the ISR consumes.
// Phase resets on transport Start so the first pulse lands on the downbeat.
static daisy::GPIO        cv_clock_out;
static volatile uint32_t  clkout_pending = 0;
static volatile bool      clkout_resync  = true;
static uint32_t           clkout_phase   = 0;
static uint32_t           clkout_pulse   = 0;
static constexpr uint32_t kClkOutPulseBlocks = 3;

static void on_midi_clock() {
    midi_clock_last_ms = System::GetNow();
    if (ext_clock_src != ClockSrc::MIDI) {
        ext_clock_src = ClockSrc::MIDI;   // instant promotion — MIDI outranks CV
        seq.SetExternalClock(true);       // no-op (no re-phase) if CV had it
    }
    seq.OnMidiClock();
    arp_ext_ticks = arp_ext_ticks + 1;   // arp + Rec-loop clocks (see AudioCallback)
    midi.SendClock();   // pass-through
    clkout_pending = clkout_pending + 1;   // MIDI in → CV out
}

static void on_midi_start() {
    if (!drum_kit_ready) generate_drum_random();
    seq_entered_once = true;   // like P2+P11: a started seq counts as entered
    seq.Start();
    midi.SendStart();
    clkout_resync = true;   // next tick = downbeat pulse on the S40 jack
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

// ─── Visualizer telemetry ─────────────────────────────────────────────────────
// Full panel-state snapshot streamed as SysEx over USB MIDI (rate-limited in
// Telemetry). Runs from service_midi so it keeps flowing during the blocking
// LED blink loops — that's exactly what lets the visualizer mirror the blinks.
static Telemetry telemetry;
static OledUi oled_ui;

// Defined below the P0+P2 hold state it reads (~line 1560) — forward
// declared here so service_telemetry() can call it despite coming first in
// the file. Mirrors the LED loop's own accelerating-blink precedence exactly
// (highest first): P0+P2 hold, then rec entry, then layer clear, then layer
// copy — so hold_kind/hold_progress never disagree with what the LED (and
// the physical OLED, fed the same TelemetryState) are already showing.
// `stage` counts confirms fired so far for the current hold (0 while still
// building; increments the instant a threshold fires and stays there for as
// long as the gesture stays held) — OledUi/the visualizer edge-detect it
// against the previous frame to catch the confirm flash. `outcome` is only
// meaningful for hold_kind 3 at the instant stage becomes 1: 1 success,
// 2 empty (nothing was there to clear).
static void compute_hold_telemetry(uint32_t now_ms, uint8_t& kind, uint8_t& progress,
                                   uint8_t& stage, uint8_t& outcome);

// ─── Which pickup is live on each pot right now (telemetry B1) ────────────────
// `KnobPickup::caught` has always known whether a pot is doing anything; what
// was missing was the map from "current mode + layer + held modifier" to the
// eight pickups actually in play, so nothing downstream could say it. Without
// that the screen printed the raw pot position whether or not it meant
// anything — you could sweep a knob through its whole travel, watch the number
// move, and hear nothing.
//
// The layer selection below mirrors the knob-application block in
// AudioCallback exactly (same order, same guards); if a role moves there, it
// moves here. Only value-crossing pickups are reported: the movement-catches
// (P0+S37 width, the P1 FX mirror knobs, Rec's S35 bank select) engage on any
// deliberate ~3% nudge, so there is no target to aim at and no dead travel to
// warn about — reporting them would put a marker on screen that the very next
// turn of the pot satisfies.
static void capture_pickups(TelemetryState& t) {
    const KnobPickup* pu[8] = { nullptr, nullptr, nullptr, nullptr,
                                nullptr, nullptr, nullptr, nullptr };
    auto& kn = touch.knobs();
    const bool p0    = touch.pads().IsTouched(0);
    const bool p2    = touch.pads().IsTouched(2);
    const bool p1_fx = touch.pads().IsTouched(1) && !p0 && !p2
                       && rec_mode == RecMode::IDLE;

    if (rec_mode == RecMode::RECORDING && rec_slot >= 0) {
        // Slot editing borrows the whole panel. P1 swaps S30/S35 to the
        // slot's own send trims; S35's bare role is the bank select, which
        // is a dead-zone catch and so stays unreported either way.
        const bool p1_snd = is_drum_mode && touch.pads().IsTouched(1) && !p0 && !p2;
        pu[0] = p1_snd ? &rec_k30fx : &rec_k30;
        pu[1] = &rec_k31;
        pu[2] = &rec_k32;
        pu[3] = &rec_k33;
        pu[4] = &rec_k34;
        if (p1_snd) pu[5] = &rec_k35fx;
        pu[6] = &rec_k36;
        if (!p0) pu[7] = &rec_k37;      // P0 held = width, a MoveCatch
    } else if (seq_mode_on) {
        if (!p1_fx) pu[0] = &seq_pu30;
        pu[1] = &seq_pu31;
        pu[2] = &seq_pu32;
        pu[3] = &seq_pu33;
        pu[4] = &seq_pu34;
        if (!p1_fx) pu[5] = &seq_pu35;
        pu[6] = &seq_pu36;
        if (!p0) pu[7] = &seq_pu37;
    } else if (current_mode == PlayMode::ARP_MEL) {
        if (arp_snd_edit) {
            if (!p1_fx) pu[0] = &arp_se30;
            pu[1] = &arp_se31; pu[2] = &arp_se32; pu[3] = &arp_se33; pu[4] = &arp_se34;
        } else if (rec_snd_edit) {
            if (!p1_fx) pu[0] = &rec_se30;
            pu[1] = &rec_se31; pu[2] = &rec_se32; pu[3] = &rec_se33; pu[4] = &rec_se34;
        } else if (arp_state == ArpState::REC) {
            if (!p1_fx) pu[0] = &rec_pu30;
            pu[1] = &arp_pu31;   // decay is the one knob genuinely shared with Arp/Hold
            pu[2] = &rec_pu32; pu[3] = &rec_pu33; pu[4] = &rec_pu34;
            if (!p1_fx && !p0 && !p2) pu[5] = &rec_pu35;
        } else {
            if (!p1_fx) pu[0] = &arp_pu30;
            pu[1] = &arp_pu31; pu[2] = &arp_pu32; pu[3] = &arp_pu33; pu[4] = &arp_pu34;
            if (!p1_fx && !p0 && !p2) pu[5] = &arp_pu35;
        }
        pu[6] = arp_state == ArpState::REC ? &rec_pu_vol   : &arp_pu_vol;
        if (!p0) pu[7] = arp_state == ArpState::REC ? &rec_pu_blend : &arp_pu_blend;
    } else {
        // Basic Pitch: the pots feed eff_* through the CC pickups, which are
        // force-caught at boot and only re-arm after a MIDI CC write — so
        // these are normally all live, and the one time they are not is
        // exactly the case with no other feedback at all.
        if (!p1_fx) pu[0] = &cc_pu_drive;
        pu[1] = &cc_pu_d;
        pu[2] = &cc_pu_h;
        pu[3] = &cc_pu_t;
        pu[4] = &cc_pu_m;
        pu[6] = &pitch_pu_vol;
        if (!p0) pu[7] = &pitch_pu_blend;
    }

    t.pickup_armed = 0;
    for (int i = 0; i < 8; i++) {
        t.pickup_target[i] = 0;
        if (pu[i] == nullptr || pu[i]->caught) continue;
        t.pickup_armed |= static_cast<uint8_t>(1u << i);
        int x = static_cast<int>(pu[i]->thresh * 127.f + 0.5f);
        t.pickup_target[i] = static_cast<uint8_t>(x < 0 ? 0 : (x > 127 ? 127 : x));
    }
    (void)kn;
}

static void service_telemetry() {
    TelemetryState t;

    uint16_t pad_bits = 0;
    for (int i = 0; i < 12; i++)
        if (touch.pads().IsTouched(i)) pad_bits |= static_cast<uint16_t>(1u << i);
    t.pads = pad_bits;

    auto to7 = [](float v) {
        int x = static_cast<int>(v * 127.f + 0.5f);
        return static_cast<uint8_t>(x < 0 ? 0 : (x > 127 ? 127 : x));
    };
    auto& kn = touch.knobs();
    const uint8_t raw[8] = {
        to7(kn.s30().Value()), to7(kn.s31().Value()),
        to7(kn.s32().Value()), to7(kn.s33().Value()),
        to7(kn.s34().Value()), to7(kn.s35().Value()),
        to7(kn.s36().Value()), to7(kn.s37().Value()),
    };
    // Pot noise sits at ±1 LSB even after AnalogControl's one-pole, which
    // kept STATE frames (and the app's callouts/log) churning forever.
    // Report a knob only when it moves ≥2 LSB from its last reported value;
    // the rails pass through so 0/127 stay reachable. Telemetry-only — the
    // audio paths keep their own smoothing and move-catch.
    static uint8_t reported[8] = { 0xFF, 0xFF, 0xFF, 0xFF,
                                   0xFF, 0xFF, 0xFF, 0xFF };
    for (int i = 0; i < 8; i++) {
        const int d = static_cast<int>(raw[i]) - static_cast<int>(reported[i]);
        if (reported[i] > 127 || raw[i] == 0 || raw[i] == 127 || d >= 2 || d <= -2)
            reported[i] = raw[i];
        t.controls[i] = reported[i];
    }

    // Switch3 raw (0 center, 1 up, 2 down) → panel positions. SW1 (switches
    // B): down=Minor is panel-left; up=Major panel-right — shared with the
    // Seq genre latch via sw1_panel_pos. SW2 (switches A): up=Seq is panel-top.
    static const uint8_t kSw2Map[3] = { 1, 0, 2 };
    int sw2_raw = touch.switches().A();
    t.sw1 = static_cast<uint8_t>(sw1_panel_pos(touch.switches().B()));
    t.sw2 = kSw2Map[(sw2_raw >= 0 && sw2_raw <= 2) ? sw2_raw : 0];
    // What SW1's two latched roles actually hold, as opposed to where the
    // lever is sitting — the screens report the loaded genre/scale and flag
    // the difference, so flicking SW1 in one mode can't make another mode
    // claim a genre that isn't playing. Both normalised to panel order:
    // seq_genre_lk already is (see the SW1 handler), scale_lk is raw.
    t.sw1_latch = static_cast<uint8_t>((seq_genre_lk & 0x03)
                                       | ((sw1_panel_pos(scale_lk) & 0x03) << 2));

    t.led      = led_lit ? 127 : 0;
    // Per-playmode sound independence (21/07/26): the model in view is the
    // active group's engine, not Basic Pitch's. Same ready-flag fallback as
    // the audio dispatch in on_midi_note_on — an unseeded arp/rec sound still
    // shows BP's engine, which is what a first-ever entry will latch anyway.
    if (!seq_mode_on && current_mode == PlayMode::ARP_MEL) {
        const bool rec_view = arp_state == ArpState::REC;
        t.model = static_cast<uint8_t>(
            rec_view ? (rec_snd_ready ? rec_snd.engine : current_engine)
                     : (arp_snd_ready ? arp_snd.engine : current_engine));
    } else {
        t.model = static_cast<uint8_t>(current_engine);
    }
    t.mode     = seq_mode_on ? 0
               : (current_mode == PlayMode::ARP_MEL ? 1 : 2);
    t.playing  = seq.IsActive();
    // Arp/Mel sound-edit layer (P0+P1 toggle) — the app swaps its knob labels
    // to the Basic Pitch layout on whichever sound is in view while this is
    // on (the arp's own model in Arp/Hold, Rec's independent model in Rec).
    t.snd_edit = !seq_mode_on && current_mode == PlayMode::ARP_MEL
                 && rec_mode == RecMode::IDLE && (arp_snd_edit || rec_snd_edit);
    t.seq_step = t.playing ? static_cast<uint8_t>(seq.Step() & 0x7F) : 0x7F;
    t.octave   = static_cast<uint8_t>(active_octave() + 3);
    t.root     = static_cast<uint8_t>(root_semitone);

    // Active mode's drive and FX sends (shown contextually by the app).
    t.fx_drive  = to7(seq_mode_on ? seq_drive_lk : eff_drive);
    t.fx_reverb = to7(active_fx_rev_lk());
    t.fx_delay  = to7(active_fx_dly_lk());

    // Recording state + drum kit snapshot (KIT frame) — the app labels the
    // rec knob layer with the edited slot's engine and shows the kit in its
    // info screen.
    t.rec_slot = (rec_mode == RecMode::RECORDING && rec_slot >= 0)
                     ? static_cast<uint8_t>(rec_slot) : 0x7F;
    // NoteRec layer state (Arp/Mel Rec) — info screen + gesture log source
    // for mute/clear (21/07/26 LED/telemetry follow-up).
    t.rec_layers = static_cast<uint8_t>(note_rec.Layers());
    t.rec_mute   = note_rec.MuteMask();
    // Master clock source + the change-latched Arp/Mel sub-state (the live
    // SW1 lever can disagree with it — mode memory keeps arp_state across
    // SW2 round-trips) and Rec's capture-armed flag (P2+P10).
    t.clock_src = static_cast<uint8_t>(ext_clock_src == ClockSrc::MIDI ? 1
                                       : ext_clock_src == ClockSrc::CV ? 2 : 0);
    t.arp_flags = static_cast<uint8_t>(
        (arp_state == ArpState::HOLD ? 1 : arp_state == ArpState::REC ? 2 : 0)
        | (rec_armed ? 0x04 : 0x00)
        | (arp_run_on ? 0x08 : 0x00)
        | ((arp_oct_range & 0x03) << 4));
    // The arp's note pool. Always published, not just in Arp/Mel: the pool
    // survives leaving the mode (Hold keeps playing in the background), so a
    // screen that only saw it while SW2 sat in the middle would go blank on
    // exactly the state that outlives the visit.
    t.arp_pool  = arp.PoolMask();
    t.seq_pattern = static_cast<uint8_t>(seq.VariantSlot());
    capture_pickups(t);
    const uint32_t now_ms = System::GetNow();
    compute_hold_telemetry(now_ms, t.hold_kind, t.hold_progress, t.hold_stage, t.hold_outcome);
    for (int i = 0; i < kPadSlots; i++) {
        const PadSlot& s = drum_slots[i];
        t.kit[i][0] = static_cast<uint8_t>(s.engine) & 0x7F;
        t.kit[i][1] = to7(s.harmonics);
        t.kit[i][2] = to7(s.timbre);
        t.kit[i][3] = to7(s.morph);
        t.kit[i][4] = to7(s.decay);
        const int n = static_cast<int>(s.note + 0.5f);
        t.kit[i][5] = static_cast<uint8_t>(n < 0 ? 0 : (n > 127 ? 127 : n));
    }

    telemetry.Service(t, now_ms, midi);
    // Same snapshot the visualizer decodes, straight to the physical
    // screen — see display/oled_ui.h.
    oled_ui.Service(t, now_ms, oled);
}

static void service_midi() {
    midi.Service(kMidiHandlers);
    service_telemetry();
}

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

// Hold timer: rec_pad alone held ≥ kLongHoldBlocks → confirm. Volatile
// because compute_hold_telemetry() reads it from the main loop to draw the
// save bar, same as every other hold counter.
static volatile uint32_t rec_hold_count = 0;
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

// scale_lk (declared with seq_genre_lk near the top) is change-latched per
// role: a switch has no value to "cross", so the pickup equivalent is
// ignoring the position it acquired while serving the other role until it
// moves again (see SW1 handler).

static float compute_note(int pad) {
    int degree = pad - 3;
    int note   = kPitchBase + root_semitone + kScales[scale_lk][degree] + active_octave() * 12;
    return static_cast<float>(std::max(0, std::min(127, note)));
}

static float root_note_f() {
    return static_cast<float>(kPitchBase + root_semitone + active_octave() * 12);
}

// ─── Arp/Mel helpers (ISR context — no blocking calls) ────────────────────────

// Render params for an arp/loop trigger: the latched sound with the arp's
// own live drive/blend written in. decay is a parameter so the Rec loop can
// replay each note with the decay it was recorded with.
static VoiceParams arp_params(float decay) {
    arp_snd.decay = decay;
    arp_snd.drive = arp_drive_lk;
    arp_snd.blend = arp_blend_lk;
    return slot_params(arp_snd);
}

// Same, for Rec's independent sound — decay stays the shared arp_decay_lk,
// but drive/blend are Rec's own (21/07/26 follow-up).
static VoiceParams rec_params(float decay) {
    rec_snd.decay = decay;
    rec_snd.drive = rec_drive_lk;
    rec_snd.blend = rec_blend_lk;
    return slot_params(rec_snd);
}

// Fire one arp note: base note + current octave, round-robin gate slot, MIDI
// mirror on ch1. A still-open gate on the reused slot is closed first so its
// external NoteOff always matches.
static void fire_arp_note(int base_note, uint32_t gate_blocks) {
    // arp_octave, not active_octave(): the arp keeps its own octave even
    // while it plays in the background of another mode (mode memory, same
    // doctrine as arp_snd).
    int n = base_note + arp_octave * 12;
    n = n < 0 ? 0 : (n > 127 ? 127 : n);
    if (arp_gate_blocks[arp_gate_rr]) {
        pool.NoteOff(kArpSlotBase + arp_gate_rr);
        midi.SendNoteOff(kMidiPitchCh, arp_gate_note[arp_gate_rr]);
    }
    pool.NoteOnWithParams(kArpSlotBase + arp_gate_rr, static_cast<float>(n),
                          arp_params(arp_decay_lk), VoiceGroup::kArp);
    midi.SendNoteOn(kMidiPitchCh, static_cast<uint8_t>(n), 100);
    arp_gate_note[arp_gate_rr]   = static_cast<uint8_t>(n);
    arp_gate_blocks[arp_gate_rr] = gate_blocks;
    arp_gate_rr = (arp_gate_rr + 1) % kArpGates;
}

// Fire one Rec-loop event: absolute note (octave was applied at record time),
// recorded decay, gate = half a master 16th.
static void fire_rec_note(uint8_t note, uint8_t decay255, uint32_t step_blocks) {
    if (rec_gate_blocks[rec_gate_rr]) {
        pool.NoteOff(kRecSlotBase + rec_gate_rr);
        midi.SendNoteOff(kMidiPitchCh, rec_gate_note[rec_gate_rr]);
    }
    pool.NoteOnWithParams(kRecSlotBase + rec_gate_rr, static_cast<float>(note),
                          rec_params(static_cast<float>(decay255) * (1.0f / 255.0f)),
                          VoiceGroup::kRec);
    midi.SendNoteOn(kMidiPitchCh, note, 100);
    rec_gate_note[rec_gate_rr]   = note;
    uint32_t gate = step_blocks / 2;
    rec_gate_blocks[rec_gate_rr] = gate < 3 ? 3 : gate;
    rec_gate_rr = (rec_gate_rr + 1) % kArpGates;
}

// Close every open arp/loop gate — transport stop and mode teardown.
static void arp_gates_off() {
    for (int i = 0; i < kArpGates; i++) {
        if (arp_gate_blocks[i]) {
            pool.NoteOff(kArpSlotBase + i);
            midi.SendNoteOff(kMidiPitchCh, arp_gate_note[i]);
            arp_gate_blocks[i] = 0;
        }
        if (rec_gate_blocks[i]) {
            pool.NoteOff(kRecSlotBase + i);
            midi.SendNoteOff(kMidiPitchCh, rec_gate_note[i]);
            rec_gate_blocks[i] = 0;
        }
    }
}

// SW1 moved while Arp/Mel is active. Raw positions: 0=center=Arp,
// 2=left=Hold, 1=right=Rec (panel orientation per the telemetry maps).
static void apply_arp_sw1(int sw1) {
    ArpState ns = (sw1 == 0) ? ArpState::ARP
                : (sw1 == 2) ? ArpState::HOLD
                             : ArpState::REC;
    if (ns == arp_state) return;
    exit_arp_snd_edit();   // sub-state change ends a sound edit in progress
    exit_rec_snd_edit();
    bool was_rec = arp_state == ArpState::REC;
    if (arp_state == ArpState::HOLD) {
        // Leaving Hold clears the latch; pads still physically down survive.
        uint16_t mask = 0;
        for (int i = 3; i <= 9; i++)
            if (touch.pads().IsTouched(i)) mask |= static_cast<uint16_t>(1u << (i - 3));
        arp.KeepOnly(mask);
    }
    // Always lands disarmed — entering Rec no longer auto-starts capture
    // (see rec_armed); leaving Rec while armed still commits the open take.
    rec_armed = false;
    note_rec.SetRecording(false);
    arp_state = ns;
    // arp_state selects which volume/blend S36/S37 now target (arp_* vs
    // rec_*) — re-arm so neither jumps to wherever the other one sat.
    rearm_pitched_faders();
    // S32-S35 mean Division/Swing/Density/Order in Arp/Hold, Speed/Shift/
    // Chance/Order in Rec — re-arm whichever layer is now live so neither
    // jumps to wherever the pot sat while serving the other one.
    if (ns == ArpState::REC) rearm_rec_pickups();
    else if (was_rec)        rearm_arp_pickups();
}

// P0+P2 staged hold in Arp/Mel: vary the latched sound around where it is —
// stage 1 tight, stage 2 wide. The engine stays; P0/P2+S35 changes it.
// Shared by arp_snd and rec_snd — which one mutates depends on arp_state.
static void mutate_snd(PadSlot& snd, float spread) {
    snd.harmonics = clampf(snd.harmonics + rand_range(-spread, spread));
    snd.timbre    = clampf(snd.timbre    + rand_range(-spread, spread));
    snd.morph     = clampf(snd.morph     + rand_range(-spread, spread));
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

// Six-Op patch-index blink: last quantized S32 patch zone (32 per bank),
// -1 = untracked (non-FM engine, or re-arm without blinking on entry).
static int fm_patch_blink_idx = -1;

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

    // Which engine field this selection edits: Basic Pitch's current_engine,
    // or — in Arp/Mel — whichever independent sound is currently in view
    // (arp_snd or rec_snd, by SW1 sub-state). Comparing against and writing
    // only the right one is what keeps the three sounds actually detached —
    // writing current_engine unconditionally here used to re-link Basic
    // Pitch's engine to whatever was last picked for the arp.
    bool in_bp  = current_mode == PlayMode::BASIC_PITCH;
    bool in_rec = current_mode == PlayMode::ARP_MEL && arp_state == ArpState::REC;
    int& target_engine = in_bp ? current_engine
                                : (in_rec ? rec_snd.engine : arp_snd.engine);

    if (new_engine != target_engine) {
        target_engine     = new_engine;
        bank_engine[bank] = new_engine;
        bank_thresh[bank] = s35_val;
        if (in_bp) {
            pool.SetEngine(new_engine);
            bp_slots_active = false;   // explicit model choice returns to live knobs
        } else if (in_rec) {
            // Six-Op lands on its known-audible preset — the latched values
            // were tuned for another engine and usually sit silent there.
            if (new_engine >= 2 && new_engine <= 4) {
                const auto& a = kSixOpAud[new_engine - 2];
                rec_snd.harmonics = a.h;
                rec_snd.timbre    = a.t;
                // No morph write: decay_via_morph routes the shared Decay
                // knob (arp_decay_lk) to the DX7 envelope at every trigger.
            }
            rec_snd_ready = true;
        } else {
            if (new_engine >= 2 && new_engine <= 4) {
                const auto& a = kSixOpAud[new_engine - 2];
                arp_snd.harmonics = a.h;
                arp_snd.timbre    = a.t;
            }
            arp_snd_ready = true;
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
            pool.AuditionWithParams(root_note_f(), vp,
                in_bp ? VoiceGroup::kBP : (in_rec ? VoiceGroup::kRec : VoiceGroup::kArp));
        } else {
            pool.Audition(root_note_f(), new_engine,
                in_bp ? VoiceGroup::kBP : (in_rec ? VoiceGroup::kRec : VoiceGroup::kArp));
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
    set_led(false); delay_serviced(200);
    for (int i = 0; i < n; i++) {
        set_led(true);  delay_serviced(200);
        set_led(false); delay_serviced(200);
    }
}

static void blink_limit() {
    for (int i = 0; i < 3; i++) {
        set_led(true);  delay_serviced(60);
        set_led(false); delay_serviced(80);
    }
}

// 3 rapid blinks — recording mode confirm/copy stored.
static void blink_confirm() {
    for (int i = 0; i < 3; i++) {
        set_led(true);  delay_serviced(80);
        set_led(false); delay_serviced(60);
    }
}

// ─── Recording mode helpers ───────────────────────────────────────────────────
// Called from AudioCallback (ISR context) — no blocking calls.
// Arm the recording knob layer against a slot's actual stored values: each
// pot takes effect only when it reaches the value it is editing — no jumps,
// works from either direction (arming at the pot's own position made upward
// moves dead). Called on entry, and again whenever a randomize replaces the
// slot under the pots, which would otherwise leave every pickup armed to a
// value the slot no longer has.
static void arm_rec_slot_pickups(int slot) {
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
}

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
    fire_confirm(2, 1);           // service_telemetry: OLED/visualizer confirm flash
    rec_hit_flash      = false;

    arm_rec_slot_pickups(slot);

    pool.AllNotesOff();
    // While the seq is running it force-fires this slot every step, so a
    // sustained audition voice would just double the sound — skip it. Drum
    // auditions use the seq-trigger param shaping and ride the drum group's
    // volume/sends (kDrum) — the pitched fader may be at zero.
    if (!(seq_mode_on && seq.IsActive())) {
        if (is_drum_mode) {
            pool.AuditionWithParams(drum_slots[slot].note, drum_params(slot), VoiceGroup::kDrum);
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
    fire_confirm(6, 1);    // "Cancelled" — the edits went back to rec_backup
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
    fire_confirm(5, 1);    // "Saved"
}

// ─── Hold pacing ──────────────────────────────────────────────────────────────
// Block size 192 at 48kHz = 4ms/block.
//
// Every build-up gesture spends its first `announce` blocks holding an empty
// bar while the screen names what it's building toward (display/oled_ui.cpp's
// hold_note()) and the LED pulses slowly, then fills over the rest with the
// LED accelerating. Two hardware findings (2026-08-04) drove this:
//   - a stage's confirm flash owns the screen for kConfirmFlashMs, during
//     which the *next* stage's counter keeps running — with no announce
//     window that time came straight out of the bar, so it reappeared
//     already ~a fifth full and looked like it started halfway. The announce
//     window is sized well above the flash so the flash lands inside it and
//     the bar always visibly starts from empty.
//   - one second per stage left no time to read what the stage does and
//     stop there, so the stages are longer now.
// Tune these two first if the pacing still feels off on hardware.
static constexpr uint32_t kStageAnnounceBlocks = 150;  // 600 ms
static constexpr uint32_t kStageFillBlocks     = 350;  // 1400 ms
static constexpr uint32_t kStageBlocks = kStageAnnounceBlocks + kStageFillBlocks;
// Rec entry keeps its 2 s total (kRecEntryHoldBlocks) and just reserves the
// head of it the same way; the 1.2 s holds (kLongHoldBlocks — layer copy,
// rec save) get a proportionally shorter one.
static constexpr uint32_t kRecEntryAnnounceBlocks = 150;  // 600 ms
static constexpr uint32_t kShortAnnounceBlocks    = 75;   // 300 ms

// Fraction of the way through a build-up, as the bar should draw it: 0 for
// the whole announce window, then 0..127 across the fill.
static uint8_t hold_progress_of(uint32_t count, uint32_t announce, uint32_t total) {
    if (count <= announce) return 0;
    const uint32_t span = total > announce ? total - announce : 1;
    const uint32_t done = count - announce;
    return static_cast<uint8_t>(done >= span ? 127 : done * 127u / span);
}

// ─── P0+P2 hold state (ISR-writable, main-loop-readable) ──────────────────────
// Stages every kStageBlocks in every playmode; Basic Pitch adds a 3rd that
// drops the randomize snapshots and restores the clean live-knob sound.
static volatile uint32_t p0p2_hold_count  = 0;
static volatile uint32_t p0p2_stage_fired = 0;
static volatile bool     p0p2_all_done    = false;
static bool p0p2_fired1 = false, p0p2_fired2 = false, p0p2_fired3 = false;

// ─── P0+P2 inside Rec: the same gesture, scoped to one pad ────────────────────
// The whole-kit randomize is all-or-nothing: it replaces seven sounds when
// usually only one is wrong. In Rec you are already pointed at a single slot,
// so the same combo varies just that one — stage 1 nudges the sound it has,
// stage 2 re-picks it from the slot's own curated pool (kDrumPools), which is
// the per-instrument version of "stick to kick models". Two stages, same
// kStageBlocks pacing as everywhere else.
static volatile uint32_t rec_p0p2_hold_count  = 0;
static volatile uint32_t rec_p0p2_stage_fired = 0;
static volatile bool     rec_p0p2_all_done    = false;
static bool rec_p0p2_fired1 = false, rec_p0p2_fired2 = false;

// ─── P0+P1 sound-edit hold (ISR-writable, main-loop-readable) ─────────────────
// The one build-up gesture that had no screen feedback of any kind: it
// silently changes what every knob in the mode means, and until it fired
// there was nothing to say it was coming. Same shape as the others now —
// counter here, bar and confirm through compute_hold_telemetry().
static constexpr uint32_t kSndEditHoldBlocks = 250;  // 1000 ms
static volatile uint32_t  se_hold_count = 0;
static volatile bool      se_fired      = false;

static void fire_hold_stage(int stage) {
    // A running sequencer plays the new sounds itself, so Seq needed no
    // audition — but now that stage 2 no longer force-starts the transport,
    // randomizing against a stopped seq is a real state, and it used to fire
    // in total silence. Audition the kick: it's slot 0, it's the sound the
    // curation is about, and one hit is enough to say "that landed".
    if (seq_mode_on) {
        if (!seq.IsActive())
            pool.AuditionWithParams(drum_slots[0].note, drum_params(0), VoiceGroup::kDrum);
    } else {
        if (current_mode == PlayMode::ARP_MEL) {
            pool.AuditionWithParams(root_note_f(),
                arp_state == ArpState::REC ? rec_params(arp_decay_lk)
                                            : arp_params(arp_decay_lk),
                arp_state == ArpState::REC ? VoiceGroup::kRec : VoiceGroup::kArp);
        } else {
            pool.AuditionWithParams(root_note_f(), slot_params(bp_slots[0]));
        }
    }
    p0p2_stage_fired = static_cast<uint32_t>(stage);
    // Release before the latch expires and p0p2_fired1/2/3 are cleared by the
    // next block — without this the confirm would vanish with them.
    fire_confirm(1, static_cast<uint8_t>(stage));
}

// Rec's per-pad randomize stage: land the change on the audition voice so it
// is audible immediately, and re-arm the knob layer against the slot's new
// values (see arm_rec_slot_pickups). With the seq running the slot is
// force-fired every other step, so it re-sounds on its own.
static void fire_rec_slot_stage(int stage) {
    if (rec_slot < 0) return;
    arm_rec_slot_pickups(rec_slot);
    if (!(seq_mode_on && seq.IsActive()))
        pool.AuditionWithParams(drum_slots[rec_slot].note, drum_params(rec_slot),
                                VoiceGroup::kDrum);
    rec_hit_flash        = true;
    rec_p0p2_stage_fired = static_cast<uint32_t>(stage);
    fire_confirm(8, static_cast<uint8_t>(stage));
}

// Same signal the LED loop (below) blinks from, reduced to a 0..127 fraction
// plus a confirm edge for the OLED/visualizer progress bar instead of a
// blink rate/rhythm — see the forward declaration above for why this lives
// here and not there, and for what `stage`/`outcome` mean.
static void compute_hold_telemetry(uint32_t now_ms, uint8_t& kind, uint8_t& progress,
                                   uint8_t& stage, uint8_t& outcome) {
    kind     = 0;
    progress = 0;
    stage    = 0;
    outcome  = 0;

    // 0) A confirm posted by the ISR (see the confirm latch above) outranks
    // everything, including a hold that's already building toward its next
    // threshold — the gesture that just completed is the news. Held for
    // kConfirmLatchMs so neither consumer's throttle can miss it; once it
    // expires the chain below resumes from whatever's still live.
    static uint8_t  latch_kind = 0, latch_stage = 0, latch_outcome = 0;
    static uint32_t latch_until = 0;
    if (pending_confirm_kind != 0) {
        latch_kind    = pending_confirm_kind;
        latch_stage   = pending_confirm_stage;
        latch_outcome = pending_confirm_outcome;
        latch_until   = now_ms + kConfirmLatchMs;
        pending_confirm_kind = 0;
    }
    if (latch_kind != 0) {
        if (static_cast<int32_t>(now_ms - latch_until) < 0) {
            kind     = latch_kind;
            stage    = latch_stage;
            outcome  = latch_outcome;
            progress = 127;
            return;
        }
        latch_kind = 0;
    }

    // 1) P0+P2 hold (re-randomize in Seq, vary sound in Arp/Mel) — highest
    // priority, matches the LED loop's own comment. p0p2_hold_count runs
    // continuously across stages (fired1/2/3 latch which have already
    // crossed; p0p2_all_done means the last applicable one has), so progress
    // is rescaled to *each stage's own window* — every segment fills a full
    // 0..127 and stage counts up once per threshold, instead of one bar
    // stuck at a fraction of a denominator that isn't always reachable (a
    // normal 2-stage hold never sees the 3rd-stage-only 750 ceiling).
    if ((p0p2_hold_count > 0 || p0p2_all_done) && rec_mode == RecMode::IDLE) {
        kind  = 1;
        stage = static_cast<uint8_t>(
            (p0p2_fired1 ? 1 : 0) + (p0p2_fired2 ? 1 : 0) + (p0p2_fired3 ? 1 : 0));
        if (p0p2_all_done) {
            progress = 127;
            return;
        }
        // Stages are equal-length, so the current one's elapsed time is just
        // the remainder — each fills its own 0..127 after its announce
        // window (see kStageAnnounceBlocks).
        const uint32_t lo    = static_cast<uint32_t>(stage) * kStageBlocks;
        const uint32_t count = p0p2_hold_count > lo ? p0p2_hold_count - lo : 0u;
        progress = hold_progress_of(count, kStageAnnounceBlocks, kStageBlocks);
        return;
    }

    // 1b) The same combo inside Rec, scoped to the slot being edited. Its own
    // kind (8) rather than a mode-dependent reading of kind 1: the label, the
    // note row and the confirm all say "this pad", not "the kit". Two stages,
    // so no all-done third window to rescale around.
    if ((rec_p0p2_hold_count > 0 || rec_p0p2_all_done)
            && rec_mode == RecMode::RECORDING) {
        kind  = 8;
        stage = static_cast<uint8_t>((rec_p0p2_fired1 ? 1 : 0) + (rec_p0p2_fired2 ? 1 : 0));
        if (rec_p0p2_all_done) {
            progress = 127;
            return;
        }
        const uint32_t lo    = static_cast<uint32_t>(stage) * kStageBlocks;
        const uint32_t count = rec_p0p2_hold_count > lo ? rec_p0p2_hold_count - lo : 0u;
        progress = hold_progress_of(count, kStageAnnounceBlocks, kStageBlocks);
        return;
    }

    // 2) Recording entry (hold a drum pad kRecEntryHoldBlocks = 2s). Its
    // confirm comes from the latch above — entry_hold_count resets to 0 in
    // the same ISR call that fires entry, so there's nothing to read here.
    if (rec_mode == RecMode::IDLE && entry_hold_count >= kRecEntryAnimStart) {
        kind     = 2;
        progress = hold_progress_of(entry_hold_count, kRecEntryAnnounceBlocks,
                                    kRecEntryHoldBlocks);
        return;
    }

    // 3) P2+pad layer-clear hold (kLongHoldBlocks = 1.2s) — same max-across-
    // pads rule the LED loop uses when more than one pad is mid-hold.
    // p2layer_fired[i] (unlike entry/copy above) stays latched for as long
    // as the pad stays held after firing, so no extra flag is needed here —
    // p2layer_outcome[i] carries which of the LED's NUMBERED/LIMIT it was.
    {
        int fired_idx = -1, max_hold = -1;
        for (int i = 0; i < 5; i++) {
            if (p2layer_fired[i] && static_cast<int>(p2layer_hold[i]) > max_hold) {
                fired_idx = i;
                max_hold  = static_cast<int>(p2layer_hold[i]);
            }
        }
        if (rec_mode == RecMode::IDLE && fired_idx >= 0) {
            kind = 3; stage = 1; progress = 127; outcome = p2layer_outcome[fired_idx];
            return;
        }
        uint32_t building = 0;
        for (int i = 0; i < 5; i++) {
            if (p2layer_hold[i] > 0 && !p2layer_fired[i] && p2layer_hold[i] > building)
                building = p2layer_hold[i];
        }
        if (rec_mode == RecMode::IDLE && building >= kRecEntryAnimStart) {
            kind     = 3;
            progress = static_cast<uint8_t>(std::min<uint32_t>(127, building * 127u / kLongHoldBlocks));
            return;
        }
    }

    // 4) Layer-copy confirm hold (kLongHoldBlocks = 1.2s), only while a
    // recording is actually in progress — matches the LED loop's own guard.
    // copy_hold_anim resets to 0 in the same block that fires the copy, so
    // the completion itself arrives via the latch above, same as kind 2.
    if (rec_mode == RecMode::RECORDING && copy_hold_anim > 0) {
        kind     = 4;
        progress = hold_progress_of(copy_hold_anim, kShortAnnounceBlocks, kLongHoldBlocks);
        return;
    }

    // 4b) P0+P1 sound-edit hold. Sits above the Rec holds because its combo
    // is Arp/Mel-only and can't overlap them (it requires rec_mode IDLE),
    // and below P0+P2 so the mutate combo still wins if P2 joins mid-hold.
    if (se_hold_count > 0 && !se_fired) {
        kind     = 7;
        progress = hold_progress_of(se_hold_count, kShortAnnounceBlocks, kSndEditHoldBlocks);
        return;
    }

    // 5) Rec save-confirm hold (kLongHoldBlocks, the rec pad alone). The one
    // hold in the set that had no bar and no flash at all — you held a pad
    // and recording simply ended, with nothing saying it had saved rather
    // than cancelled. Last in the chain so a copy (both pads down, counted
    // separately) still wins, matching the LED.
    if (rec_mode == RecMode::RECORDING && rec_hold_count >= kRecEntryAnimStart) {
        kind     = 5;
        progress = hold_progress_of(rec_hold_count, kShortAnnounceBlocks, kLongHoldBlocks);
    }
}

// ─── Switch tracking ──────────────────────────────────────────────────────────
// Initialised to -1 so the first AudioCallback block sets mode silently.
static int last_sw1 = -1;
static int last_sw2 = -1;

// ─── Audio callback ───────────────────────────────────────────────────────────
static constexpr size_t kBlockSize = 192;
static constexpr size_t kChunkSize = 24;

// Boot fade-in: ramps the master output up from silence over the first
// kBootFadeMs after hw.StartAudio() starts, so whatever fader position /
// engine state block 0 happens to land on (pots at boot are read live —
// see the fader-arm block below) doesn't slam straight into the DAC as a
// hard-edged onset, which is what a "click right after boot" usually is in
// the digital domain. (A pop from the codec's own power-up sequencing, if
// any remains once this is in, is a separate hardware concern this can't
// reach.) Reaches 1.0 and stays there for the rest of the unit's life —
// the branch below costs nothing once boot_fade_gain has saturated.
static constexpr float kBootFadeMs           = 60.f;
static constexpr float kSampleRateHz         = 48000.f; // fixed via SAI_48KHZ, see main()
static constexpr float kBootFadeIncPerSample = 1.f / (kBootFadeMs * 0.001f * kSampleRateHz);
static float boot_fade_gain = 0.f;

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

    // One-time fader arm: volume and blend follow their pots live from boot —
    // the pickups only start gating after a mode hand-off or P0 width edit.
    // Must run BEFORE the cc_pu_* updates below: a default-constructed pickup
    // catches on its first update (thresh 0), which on a persisted boot would
    // let the pots stomp the restored eff_* before the arm ever happened.
    static bool faders_armed = false;
    if (!faders_armed) {
        faders_armed = true;
        if (settings_restored) {
            // Persisted boot: every restored value holds until its pot
            // crosses it — the same pickup doctrine as a mode hand-off,
            // applied to the pots' arbitrary power-on positions.
            pitch_pu_vol.arm_to(pitched_vol_lk, k.s36().Value());
            pitch_pu_blend.arm_to(pitched_blend_lk, k.s37().Value());
            pitch_pu_w.arm(k.s37().Value());
            cc_pu_h.arm_to(eff_h, last_h);
            cc_pu_t.arm_to(eff_t, last_t);
            cc_pu_m.arm_to(eff_m, last_m);
            cc_pu_d.arm_to(eff_d, last_d);
            cc_pu_drive.arm_to(eff_drive, drive);
            // Restored BP snapshots: anchor the knob-grab escape refs to the
            // pots' current rest positions, not their pre-reboot values —
            // otherwise block one reads "knob moved" and escapes to live.
            if (bp_slots_active) {
                bp_ref_h = last_h; bp_ref_t = last_t;
                bp_ref_m = last_m; bp_ref_d = last_d;
            }
        } else {
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
    }

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

    // SW1 / scale or genre — change-latched: the position only takes effect
    // when the switch moves while its role is active, so a position it
    // acquired while serving the other role never jumps the setting on a
    // playmode flick (the switch equivalent of the knob pickups).
    int sw1 = touch.switches().B();
    if (sw1 != last_sw1) {
        if (last_sw1 >= 0) {
            // Genre is panel-ordered (see sw1_panel_pos): left=IDM,
            // center=Techno, right=Electro, matching kSw1Seq and the
            // pattern registry. Scale/Arp below stay on the raw value.
            if (seq_mode_on)                              seq_genre_lk = sw1_panel_pos(sw1);
            else if (current_mode == PlayMode::ARP_MEL)   apply_arp_sw1(sw1);
            else                                          scale_lk     = sw1;
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
            // Plain-Arp pool notes were held pads; their releases now belong
            // to the drums, so drop them (Hold/Rec keep playing — mode memory).
            if (current_mode == PlayMode::ARP_MEL && arp_state == ArpState::ARP)
                arp.Clear();
            p0p2_hold_count  = 0;
            p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
            p0p2_stage_fired = 0;
            p0p2_all_done    = false;
            // Arm the seq-knob pickups: on the very first entry the pots are
            // live immediately; on re-entry each pot must cross its stored
            // setting first (it served another mode in the meantime).
            if (!seq_entered_once) {
                seq_entered_once = true;
                if (settings_restored) {
                    // Persisted boot: the first entry behaves like a re-entry
                    // — restored settings hold, pots must pick them up.
                    rearm_seq_pickups();
                } else {
                    seq_drive_lk = k.s30().Value();  seq_pu30.force_catch(seq_drive_lk);
                    seq_tempo_lk = k.s31().Value();  seq_pu31.force_catch(seq_tempo_lk);
                    seq_shuf_lk  = k.s32().Value();  seq_pu32.force_catch(seq_shuf_lk);
                    seq_dens_lk  = k.s33().Value();  seq_pu33.force_catch(seq_dens_lk);
                    seq_chance_lk = k.s34().Value(); seq_pu34.force_catch(seq_chance_lk);
                    seq_pu35.arm_to(seq_var_lk, k.s35().Value());  // variant stays 0 until S35 crosses it
                    seq_vol_lk   = k.s36().Value();  seq_pu36.force_catch(seq_vol_lk);
                    seq_tight_lk = k.s37().Value();  seq_pu37.force_catch(seq_tight_lk);
                }
                seq.Start();
                if (!midi_clock_master()) midi.SendStart();
                clkout_resync = true;
            } else {
                rearm_seq_pickups();
            }
            seq_mode_on = true;
            if (!is_boot) { led_event = LedEvent::CONFIRM; }
        } else {
            // SW2 center or down → pads/knobs switch to the pitched mode, but a
            // running seq keeps playing in the background with its settings
            // locked at their last SW2-Up values. P2+P11 pauses/resumes it.
            bool was_seq = seq_mode_on;
            PlayMode prev_pitched = current_mode;
            if (seq_mode_on) {
                pool.AllNotesOff();
                seq_mode_on  = false;
                is_drum_mode = false;
            }
            current_mode = sw2_to_mode(sw2);
            // The faders served the seq while SW2 was Up — pickup so pitched
            // volume/blend/width don't jump on the flick back.
            rearm_pitched_faders();
            pitch_pu_w.arm(k.s37().Value());
            if (current_mode == PlayMode::ARP_MEL) {
                // Seed the arp's sound from the live BP sound on the FIRST
                // entry ever; after that the models are independent — edit
                // the arp's via P0+P1 sound edit, never by BP round-trips.
                if (!arp_snd_ready) {
                    arp_snd.engine    = current_engine;
                    arp_snd.harmonics = eff_h;
                    arp_snd.timbre    = eff_t;
                    arp_snd.morph     = eff_m;
                    arp_snd_ready     = true;
                }
                arp_snd_edit = false;   // always enter in play, not sound edit
                rec_snd_edit = false;
                // Re-arm whichever knob layer matches the CURRENT sub-state
                // — mode memory keeps arp_state across SW2 round-trips, so
                // blindly arming the Arp layer here left Rec's own pickups
                // (rec_pu30/32-35) stale whenever you re-entered Arp/Mel
                // with SW1 already resting on Rec. If those were already
                // caught from an earlier Rec session, S32-35 would just
                // track the live knob from then on and could snap Speed/
                // Shift/Chance/Order (or drive) to wherever the pot was
                // sitting the instant this block ran (21/07/26 follow-up:
                // Rec's knobs must never move until the knob itself does).
                if (arp_state == ArpState::REC) rearm_rec_pickups();
                else                            rearm_arp_pickups();
            } else if (prev_pitched == PlayMode::ARP_MEL && !was_seq
                       && arp_state == ArpState::ARP) {
                // Leaving plain Arp for BP: the pool was only held pads —
                // clear it so releases in the other mode can't strand notes.
                // Hold/Rec keep playing in the background (mode memory).
                arp.Clear();
            }
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
            } else if (p0_held) {
                pitch_pu_w.arm(v37);
            } else {
                KnobPickup& bpu = current_mode == PlayMode::ARP_MEL
                    ? (arp_state == ArpState::REC ? rec_pu_blend : arp_pu_blend)
                    : pitch_pu_blend;
                bpu.arm_to(active_blend_lk(), v37);
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
            if (fx_mc_rev.update(v30)) active_fx_rev_lk() = v30;
            float v35 = k.s35().Value();
            if (fx_mc_dly.update(v35)) active_fx_dly_lk() = v35;
        }
    }

    // Sequencer knob/switch updates — only while SW2 is Up and not recording
    // (rec mode borrows S30/S32/S33/S34/S37 for slot editing). Every pot goes
    // through pickup, so a pot used by another mode doesn't jump the setting
    // until it crosses the stored value.
    // Seq layout: S30=drive (also rides the kick's punch — drum_params()),
    // S31=tempo, S32=shuffle, S33=density, S34=chance,
    // S35=pattern variant (within the SW1 genre), S37=tightness.
    if (seq_mode_on && rec_mode == RecMode::IDLE) {
        if (!p1_fx && seq_pu30.update(drive))            seq_drive_lk = drive;
        if (seq_pu31.update(k.s31().Value()))  seq_tempo_lk = k.s31().Value();
        if (seq_pu32.update(k.s32().Value()))  seq_shuf_lk  = k.s32().Value();
        if (seq_pu33.update(k.s33().Value()))  seq_dens_lk  = k.s33().Value();
        if (seq_pu34.update(k.s34().Value()))  seq_chance_lk = k.s34().Value();
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
        seq.SetChance(seq_chance_lk);
        seq.SetVariant(seq_var_lk);
    }
    // Arp/Mel knob layer — S30 drive, S31 decay, S32 division, S33 swing,
    // S34 density, S35 order, all through pickups (decay lives on S31 in
    // every mode; the arp functions shift one knob right). S30/S35 freeze
    // under P1 (FX layer); S35 also under P0/P2 (model select owns it there).
    // Sound edit (P0+P1 toggle) swaps the whole layer for the Basic Pitch
    // layout on the arp's own model: S30 drive, S31 decay, S32 harmonics,
    // S33 timbre, S34 morph — the arp functions freeze until you toggle back.
    if (!seq_mode_on && current_mode == PlayMode::ARP_MEL
            && rec_mode == RecMode::IDLE) {
        if (arp_snd_edit) {
            if (!p1_fx && arp_se30.update(drive))       arp_drive_lk = drive;
            if (arp_se31.update(k.s31().Value()))       arp_decay_lk = k.s31().Value();
            if (arp_se32.update(k.s32().Value()))       arp_snd.harmonics = k.s32().Value();
            if (arp_se33.update(k.s33().Value()))       arp_snd.timbre    = k.s33().Value();
            if (arp_se34.update(k.s34().Value()))       arp_snd.morph     = k.s34().Value();
        } else if (rec_snd_edit) {
            if (!p1_fx && rec_se30.update(drive))       rec_drive_lk = drive;
            if (rec_se31.update(k.s31().Value()))       arp_decay_lk = k.s31().Value();
            if (rec_se32.update(k.s32().Value()))       rec_snd.harmonics = k.s32().Value();
            if (rec_se33.update(k.s33().Value()))       rec_snd.timbre    = k.s33().Value();
            if (rec_se34.update(k.s34().Value()))       rec_snd.morph     = k.s34().Value();
        } else if (arp_state == ArpState::REC) {
            // Rec-only layer (20/07/26 notes): S31 stays the shared arp
            // decay; S30 is Rec's own drive (21/07/26 follow-up); S32-S35
            // become Speed/Shift/Chance/Order instead of Division/Swing/
            // Density/Order.
            bool p0p2 = touch.pads().IsTouched(0) || touch.pads().IsTouched(2);
            if (!p1_fx && rec_pu30.update(drive))       rec_drive_lk = drive;
            if (arp_pu31.update(k.s31().Value()))       arp_decay_lk = k.s31().Value();
            if (rec_pu32.update(k.s32().Value()))       rec_speed_lk  = k.s32().Value();
            if (rec_pu33.update(k.s33().Value()))       rec_shift_lk  = k.s33().Value();
            if (rec_pu34.update(k.s34().Value()))       rec_chance_lk = k.s34().Value();
            if (!p1_fx && !p0p2 && rec_pu35.update(k.s35().Value()))
                                                        rec_order_lk = k.s35().Value();
        } else {
            bool p0p2 = touch.pads().IsTouched(0) || touch.pads().IsTouched(2);
            if (!p1_fx && arp_pu30.update(drive))       arp_drive_lk = drive;
            if (arp_pu31.update(k.s31().Value()))       arp_decay_lk = k.s31().Value();
            if (arp_pu32.update(k.s32().Value()))       arp_div_lk   = k.s32().Value();
            if (arp_pu33.update(k.s33().Value()))       arp_swing_lk = k.s33().Value();
            if (arp_pu34.update(k.s34().Value()))       arp_dens_lk  = k.s34().Value();
            if (!p1_fx && !p0p2 && arp_pu35.update(k.s35().Value()))
                                                        arp_order_lk = k.s35().Value();
        }
    }
    // Applied unconditionally — the arp/loop may be running in the
    // background of any playmode, like the drum seq's setters above.
    arp.SetDivision(arp_div_lk);
    arp.SetSwing(arp_swing_lk);
    arp.SetDensity(arp_dens_lk);
    arp.SetOrder(arp_order_lk);
    arp.SetOctaveRange(arp_oct_range);
    note_rec.SetSpeed(rec_speed_lk);
    note_rec.SetShift(rec_shift_lk);
    note_rec.SetChance(rec_chance_lk);
    note_rec.SetOrderRandom(rec_order_lk > 0.5f);
    // Fader updates outside Seq/recording: S36 = volume, S37 = OUT↔AUX blend,
    // P0+S37 = stereo width (0 = mono blend, 1 = raw OUT/AUX split).
    if (!seq_mode_on && rec_mode == RecMode::IDLE) {
        float v36 = k.s36().Value();
        KnobPickup& vol_pu = current_mode == PlayMode::ARP_MEL
            ? (arp_state == ArpState::REC ? rec_pu_vol : arp_pu_vol)
            : pitch_pu_vol;
        if (vol_pu.update(v36)) active_pitched_vol_lk() = v36;
        float v37 = k.s37().Value();
        if (touch.pads().IsTouched(0)) {
            if (pitch_pu_w.update(v37)) pitched_width_lk = snap_width(v37);
        } else {
            KnobPickup& blend_pu = current_mode == PlayMode::ARP_MEL
                ? (arp_state == ArpState::REC ? rec_pu_blend : arp_pu_blend)
                : pitch_pu_blend;
            if (blend_pu.update(v37)) active_blend_lk() = v37;
        }
    }
    pool.SetSeqVolume(vol_taper(seq_vol_lk));
    pool.SetPitchedVolume(vol_taper(pitched_vol_lk));
    pool.SetArpVolume(vol_taper(arp_vol_lk));
    pool.SetRecVolume(vol_taper(rec_vol_lk));
    pool.SetSeqWidth(seq_width_lk);
    pool.SetPitchedWidth(pitched_width_lk);

    // Decode each group's FX mirror knob into its own send amount AND its
    // own character (21/07/26 follow-up: 4 fully independent reverbs/delays
    // now, so each group's knob position drives its own instance directly —
    // no more shared "last edit wins" character lock). Squared send = audio
    // taper: finer control in the useful low-wet range.
    {
        float w;
        int side;
        // Dotted 1/8 = 3 sixteenth steps of the seq clock (internal tempo;
        // under external MIDI clock this is the knob fallback tempo) —
        // shared by every group's synced-delay character; the master tempo
        // is one thing regardless of which group's delay is synced to it.
        float synced = 3.f * static_cast<float>(seq.StepBlocks())
                           * static_cast<float>(kBlockSize);

        side = fx_decode(fx_rev_pitched_lk, &w); pool.SetPitchedReverbSend(w * w);
        fx_bp.SetReverbCharacter(side, w);
        side = fx_decode(fx_rev_arp_lk, &w);     pool.SetArpReverbSend(w * w);
        fx_arp.SetReverbCharacter(side, w);
        side = fx_decode(fx_rev_rec_lk, &w);     pool.SetRecReverbSend(w * w);
        fx_rec.SetReverbCharacter(side, w);
        side = fx_decode(fx_rev_seq_lk, &w);     pool.SetSeqReverbSend(w * w);
        fx_drum.SetReverbCharacter(side, w);

        side = fx_decode(fx_dly_pitched_lk, &w); pool.SetPitchedDelaySend(w * w);
        fx_bp.SetDelayCharacter(side, w, synced);
        side = fx_decode(fx_dly_arp_lk, &w);     pool.SetArpDelaySend(w * w);
        fx_arp.SetDelayCharacter(side, w, synced);
        side = fx_decode(fx_dly_rec_lk, &w);     pool.SetRecDelaySend(w * w);
        fx_rec.SetDelayCharacter(side, w, synced);
        side = fx_decode(fx_dly_seq_lk, &w);     pool.SetSeqDelaySend(w * w);
        fx_drum.SetDelayCharacter(side, w, synced);
    }

    // P0+P2 hold counter — active in every playmode, one stage per
    // kStageBlocks (2 s; see the hold-pacing block above):
    //   Basic Pitch: soft tight (±0.25) → soft wide (±0.45), same engine
    //                → restore clean live-knob sound (drops the snapshots)
    //   Arp/Mel:     tight variance (±0.10) on the latched sound → wide (±0.25)
    //   Seq:         soft param variance on current kit → full new kit
    {
        bool p0 = touch.pads().IsTouched(0);
        bool p2 = touch.pads().IsTouched(2);
        bool both = p0 && p2 && rec_mode == RecMode::IDLE;
        bool in_bp = !seq_mode_on && current_mode == PlayMode::BASIC_PITCH;
        // In Rec the combo is live too, scoped to the edited slot (below).
        // Neither of P0/P2's own Rec roles conflicts: both sit behind a
        // catch (S37 width on P0 is a MoveCatch, S35 model select needs a
        // dead-zone-sized turn), so holding the pair without touching a
        // fader changes nothing else.
        bool rec_both = p0 && p2 && rec_mode == RecMode::RECORDING
                        && rec_slot >= 0 && is_drum_mode;
        if (!both) {
            p0p2_hold_count  = 0;
            p0p2_fired1 = p0p2_fired2 = p0p2_fired3 = false;
            p0p2_all_done    = false;
            p0p2_stage_fired = 0;
        } else if (!p0p2_all_done) {
            p0p2_hold_count++;
            if (!p0p2_fired1 && p0p2_hold_count >= kStageBlocks) {
                p0p2_fired1 = true;
                if (seq_mode_on) {
                    mutate_drum_soft();
                } else if (current_mode == PlayMode::ARP_MEL) {
                    mutate_snd(arp_state == ArpState::REC ? rec_snd : arp_snd, 0.10f);
                } else {
                    generate_soft_random(bp_slots, current_engine,
                                         eff_h, eff_t, eff_m, eff_d, 0.25f);
                    bp_slots_active = true;
                    bp_ref_h = last_h; bp_ref_t = last_t; bp_ref_m = last_m; bp_ref_d = last_d;
                }
                fire_hold_stage(1);
            }
            if (!p0p2_fired2 && p0p2_hold_count >= 2u * kStageBlocks) {
                p0p2_fired2   = true;
                p0p2_all_done = !in_bp;   // Basic Pitch has a 3rd stage
                if (seq_mode_on) {
                    generate_drum_random();
                    pool.AllNotesOff();
                    // Transport is not part of the kit. This used to call
                    // Start() unconditionally, so a new kit force-started the
                    // sequencer — you could not audition a fresh kit pad by
                    // pad against a stopped seq, the randomize itself began
                    // playing. A *running* seq still restarts from bar 0 so
                    // the new kit lands on a downbeat, which is what the
                    // original behaviour was actually for.
                    if (seq.IsActive()) {
                        seq.Start();
                        if (!midi_clock_master()) midi.SendStart();
                        clkout_resync = true;
                    }
                } else if (current_mode == PlayMode::ARP_MEL) {
                    mutate_snd(arp_state == ArpState::REC ? rec_snd : arp_snd, 0.25f);
                } else {
                    generate_soft_random(bp_slots, current_engine,
                                         eff_h, eff_t, eff_m, eff_d, 0.45f);
                    bp_slots_active = true;
                    bp_ref_h = last_h; bp_ref_t = last_t; bp_ref_m = last_m; bp_ref_d = last_d;
                }
                fire_hold_stage(2);
            }
            if (!p0p2_fired3 && p0p2_hold_count >= 3u * kStageBlocks && in_bp) {
                p0p2_fired3   = true;
                p0p2_all_done = true;
                // Stage 3: back to the clean live sound.
                bp_slots_active = false;
                pool.SetEngine(current_engine);
                VoiceParams vp;
                vp.engine    = current_engine;
                vp.harmonics = eff_h;
                vp.timbre    = eff_t;
                vp.morph     = decay_via_morph(current_engine) ? eff_d : eff_m;
                vp.decay     = eff_d;
                vp.blend     = pitched_blend_lk;
                pool.AuditionWithParams(root_note_f(), vp);
                p0p2_stage_fired = 3;
                fire_confirm(1, 3);   // stage 3 doesn't go through fire_hold_stage()
            }
        }

        // Rec's per-pad version. Same pacing, two stages:
        //   1 — vary the sound this pad already has (engine + note kept)
        //   2 — new sound for this pad from its own curated pool
        // After either, the whole rec knob layer is re-armed: the pots were
        // armed to the values the slot had at entry, and those values have
        // just been replaced underneath them.
        if (!rec_both) {
            rec_p0p2_hold_count  = 0;
            rec_p0p2_fired1 = rec_p0p2_fired2 = false;
            rec_p0p2_all_done    = false;
            rec_p0p2_stage_fired = 0;
        } else if (!rec_p0p2_all_done) {
            rec_p0p2_hold_count++;
            const int slot = rec_slot;
            if (!rec_p0p2_fired1 && rec_p0p2_hold_count >= kStageBlocks) {
                rec_p0p2_fired1 = true;
                mutate_drum_slot_soft(slot);
                fire_rec_slot_stage(1);
            }
            if (!rec_p0p2_fired2 && rec_p0p2_hold_count >= 2u * kStageBlocks) {
                rec_p0p2_fired2   = true;
                rec_p0p2_all_done = true;
                fill_drum_slot_from_pool(slot);
                fire_rec_slot_stage(2);
            }
        }
    }

    // P0+P1 held ~1s in Arp/Mel (without P2 — that's the mutate combo) toggles
    // the sound-edit knob layer on whichever sound is in view — the arp's own
    // model in Arp/Hold, Rec's independent model while SW1=Rec. Entry
    // auditions the current sound; with the arp/loop running, every trigger
    // is live feedback.
    {
        bool combo = !seq_mode_on && current_mode == PlayMode::ARP_MEL
                     && rec_mode == RecMode::IDLE
                     && touch.pads().IsTouched(0) && touch.pads().IsTouched(1)
                     && !touch.pads().IsTouched(2);
        if (!combo) {
            se_hold_count = 0;
            se_fired      = false;
        } else if (!se_fired && ++se_hold_count >= kSndEditHoldBlocks) {
            se_fired = true;
            // outcome tells the screen which way it went — the two are
            // opposite gestures on the same combo (1 entered, 2 left).
            const bool leaving = (arp_state == ArpState::REC) ? rec_snd_edit : arp_snd_edit;
            fire_confirm(7, 1, leaving ? 2 : 1);
            if (arp_state == ArpState::REC) {
                if (!rec_snd_edit) {
                    rec_snd_edit = true;
                    arm_rec_se_pickups();
                    pool.AuditionWithParams(root_note_f(), rec_params(arp_decay_lk), VoiceGroup::kRec);
                    led_event = LedEvent::CONFIRM;
                } else {
                    exit_rec_snd_edit();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;
                }
            } else {
                if (!arp_snd_edit) {
                    arp_snd_edit = true;
                    arm_arp_se_pickups();
                    pool.AuditionWithParams(root_note_f(), arp_params(arp_decay_lk), VoiceGroup::kArp);
                    led_event = LedEvent::CONFIRM;
                } else {
                    exit_arp_snd_edit();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;
                }
            }
        }
    }

    // P2 + pad P3-P7 hold in Rec: reaching kLongHoldBlocks fires ClearLayer
    // for that pad, or ClearAll if >=2 of P3-P7 are down at that moment
    // (checked fresh each time a pad's own counter reaches threshold, so
    // pads that joined the hold at different times still resolve correctly).
    // The tap case (released before threshold) is handled in SetOnRelease.
    {
        bool in_rec = !seq_mode_on && current_mode == PlayMode::ARP_MEL
                      && arp_state == ArpState::REC && rec_mode == RecMode::IDLE;
        bool p2 = in_rec && touch.pads().IsTouched(2);
        for (int i = 0; i < 5; i++) {
            bool held = p2 && touch.pads().IsTouched(3 + i);
            if (!held) {
                p2layer_hold[i]    = 0;
                p2layer_fired[i]   = false;
                p2layer_outcome[i] = 0;
                continue;
            }
            if (p2layer_fired[i] || ++p2layer_hold[i] < kLongHoldBlocks) continue;
            int n_held = 0;
            for (int j = 0; j < 5; j++)
                if (p2 && touch.pads().IsTouched(3 + j)) n_held++;
            if (n_held >= 2) {
                note_rec.ClearAll();
                for (int j = 0; j < 5; j++)
                    if (p2 && touch.pads().IsTouched(3 + j)) {
                        p2layer_fired[j]   = true;
                        p2layer_outcome[j] = 1;
                    }
                led_event = LedEvent::CONFIRM;
                fire_confirm(3, 1, 1);
            } else {
                p2layer_fired[i] = true;
                if (note_rec.ClearLayer(i)) {
                    p2layer_outcome[i] = 1;
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = i + 1;
                } else {
                    p2layer_outcome[i] = 2;
                    led_event = LedEvent::LIMIT;
                }
                fire_confirm(3, 1, p2layer_outcome[i]);
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

    // Recording entry — Seq only now: hold a pad (P3–P9) for kRecEntryHoldBlocks
    // (2 s). In Arp/Mel holding pads is the playing gesture, so slot editing is
    // gated off there (pitched slot editing retired with Random mode).
    bool rec_entry_allowed = seq_mode_on;
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
                fire_confirm(4, 1);   // service_telemetry: OLED/visualizer confirm flash
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
            // (tightness on the tail, punch on the kick riding drive) —
            // otherwise the sound jumps on the next retrigger.
            float upd_morph = slot.morph;
            if (decay_via_morph(slot.engine))
                upd_morph = is_drum_mode ? slot.decay * (0.2f + seq_tight_lk * 0.8f)
                                         : slot.decay;
            float upd_timbre = slot.timbre;
            if (is_drum_mode && rec_slot == 0)
                upd_timbre = slot.timbre + seq_drive_lk * (1.0f - slot.timbre);
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
                pool.AuditionWithParams(slot.note, drum_params(rec_slot), VoiceGroup::kDrum);
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
        // Global drive only in Basic Pitch: arp/loop voices carry their own
        // per-trigger drive (arp_drive_lk/rec_drive_lk) that a global write
        // would stomp.
        if (rec_mode == RecMode::IDLE && current_mode == PlayMode::BASIC_PITCH)
            pool.SetDrive(eff_drive);
        pool.SetFMAmount(0.0f);
        pool.SetLPGColour(lpg_colour);
    }
    // Live global knobs only when Basic Pitch is not playing randomize snapshots.
    if (current_mode == PlayMode::BASIC_PITCH && !bp_slots_active) {
        pool.SetHarmonics(eff_h);
        pool.SetTimbre(eff_t);
        // Six-Op preset browsing feedback: S32 is a quantized 32-patch
        // selector on engines 2-4. Mirror the engine's quantizer
        // (harmonics * 1.02 * 32) and fire the short MODEL blink when the
        // zone changes; the 15%-into-zone guard is a coarse stand-in for
        // the engine's hysteresis so a knob resting on a boundary can't
        // chatter. Armed silently on entry (no blink for the current zone).
        if (current_engine >= 2 && current_engine <= 4
                && rec_mode == RecMode::IDLE) {
            float q   = eff_h * 1.02f * 32.0f;
            int   idx = static_cast<int>(q);
            if (idx > 31) idx = 31;
            if (fm_patch_blink_idx < 0) {
                fm_patch_blink_idx = idx;
            } else if (idx != fm_patch_blink_idx) {
                float zone_pos = q - static_cast<float>(idx);
                if (zone_pos > 0.15f && zone_pos < 0.85f) {
                    fm_patch_blink_idx = idx;
                    if (led_event == LedEvent::NONE)
                        led_event = LedEvent::MODEL;
                }
            }
        } else {
            fm_patch_blink_idx = -1;
        }
        // Unified decay: the decay_via_morph engines (Six-Op 2–4, 19–23) keep
        // their real decay on MORPH — the Decay knob drives it there and S34
        // has no effect on those engines.
        pool.SetMorph(decay_via_morph(current_engine) ? eff_d : eff_m);
        pool.SetDecay(eff_d);
        pool.SetBlend(pitched_blend_lk);
    }

    // Beat-pulse hold-off countdown — shared by the seq and Rec-loop pulses.
    if (beat_led_hold) beat_led_hold = beat_led_hold - 1;

    // ── CV clock in (S43) ─────────────────────────────────────────────────────
    // Always-on listener: the jack rides the knob ADC DMA scan, so this is one
    // raw compare per block. Rising edges through the Schmitt window measure
    // the pulse period; while CV is master, each edge opens a 6-tick window
    // (24 ppqn over a 1/16th pulse) that the fractional accumulator spreads
    // across the measured period. The window is capped at 6 and any shortfall
    // is emitted at the next edge, so step boundaries land exactly on incoming
    // pulses regardless of tempo drift. Runs before seq.Tick so ticks count
    // this same block.
    int cv_ticks_this_block = 0;
    {
        const uint16_t cv_raw = k.ClockInRaw();
        bool edge = false;
        if (!cv_in_high && cv_raw > kCvInHiRaw)     { cv_in_high = true; edge = true; }
        else if (cv_in_high && cv_raw < kCvInLoRaw) { cv_in_high = false; }
        if (cv_blocks_since_edge < kCvPeriodMaxBlocks)
            cv_blocks_since_edge = cv_blocks_since_edge + 1;

        // One synthetic F8: seq + arp/Rec tick, MIDI-out forward, CV-out tick.
        auto emit_ticks = [&](int n) {
            for (int i = 0; i < n; i++) { seq.OnMidiClock(); midi.SendClock(); }
            cv_ticks_this_block += n;
            clkout_pending = clkout_pending + static_cast<uint32_t>(n);
        };

        if (edge) {
            cv_edge_last_ms = System::GetNow();
            bool sane_period = cv_blocks_since_edge >= kCvPeriodMinBlocks
                               && cv_blocks_since_edge < kCvPeriodMaxBlocks;
            if (ext_clock_src == ClockSrc::NONE) {
                // Acquire: become master. First period is unknown — seed it
                // from the knob tempo, corrected at the next edge.
                ext_clock_src = ClockSrc::CV;
                seq.SetExternalClock(true);
                cv_period_blocks   = static_cast<float>(seq.StepBlocks());
                cv_tick_acc        = 0.f;
                emit_ticks(1);   // tick 0: the step fires on the edge itself
                cv_ticks_in_window = 1;
            } else if (ext_clock_src == ClockSrc::CV) {
                if (sane_period)
                    cv_period_blocks = static_cast<float>(cv_blocks_since_edge);
                // Close the old window (catch-up burst if the estimate ran
                // slow) + tick 0 of the new one.
                emit_ticks((6 - cv_ticks_in_window) + 1);
                cv_ticks_in_window = 1;
                cv_tick_acc        = 0.f;
            } else {
                // MIDI is master: just keep the period fresh so a MIDI
                // dropout hands over to CV without a tempo glitch.
                if (sane_period)
                    cv_period_blocks = static_cast<float>(cv_blocks_since_edge);
                cv_ticks_in_window = 6;
            }
            cv_blocks_since_edge = 0;
        } else if (ext_clock_src == ClockSrc::CV && cv_ticks_in_window < 6) {
            cv_tick_acc += 6.f / cv_period_blocks;
            int n = 0;
            while (cv_tick_acc >= 1.f && cv_ticks_in_window + n < 6) {
                cv_tick_acc -= 1.f;
                n++;
            }
            if (n) { emit_ticks(n); cv_ticks_in_window += n; }
        }
    }

    // Sequencer tick — runs in every playmode while the seq is playing.
    if (seq.IsActive()) {
        uint8_t triggers = seq.Tick();
        // Beat pulse — lowest-priority LED signal: never overwrites a pending
        // event, suppressed while recording and during the hold-off that rec
        // exits and dispatched blinks arm.
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
                // loop drains — never TX from this ISR), carrying the step's
                // authored accent as velocity. The forced rec-slot trigger
                // has no step behind it, so StepWeight() reads 0 there and
                // drum_velocity() falls back to a plain 100.
                midi.SendNoteOn(kMidiDrumCh, kDrumSlotGm[i],
                                drum_velocity(seq.StepWeight(i)));
                midi.SendNoteOff(kMidiDrumCh, kDrumSlotGm[i]);
            }
        }
    }

    // Arp + Rec-loop clocks — run in every playmode (background play, like
    // the drum seq). Both follow the master tempo (the seq's step clock) or
    // external clock ticks (MIDI F8s, or the CV multiplier's synthetic ones);
    // P2+P10 gates them together.
    {
        int  ext        = 0;
        bool ext_active = ext_clocked();
        if (ext_clock_src == ClockSrc::MIDI) {
            ext = static_cast<int>(arp_ext_ticks);
            arp_ext_ticks = 0;
        } else {
            arp_ext_ticks = 0;   // never let F8s pile up while not MIDI-clocked
            ext = cv_ticks_this_block;   // 0 when internal
        }

        Arp::Out ao = arp.Tick(seq.StepBlocks(), ext_active, ext, arp_run_on);
        if (ao.fire) fire_arp_note(ao.note, ao.gate_blocks);

        // Rec loop: ticks are 24 ppqn (6 per 16th) — fractional internally,
        // 1:1 with external ticks under external clock.
        NoteRec::Event evs[8];
        float adv = 0.f;
        if (arp_run_on)
            adv = ext_active ? static_cast<float>(ext)
                             : 6.f / static_cast<float>(seq.StepBlocks());
        int n_ev = note_rec.Tick(adv, evs, 8);
        for (int i = 0; i < n_ev; i++)
            fire_rec_note(evs[i].note, evs[i].decay, seq.StepBlocks());

        // Loop quarter-note pulse when the drums aren't providing one — the
        // only tempo cue while recording against a stopped seq.
        if (!seq.IsActive() && note_rec.BeatFired() && rec_mode == RecMode::IDLE
                && beat_led_hold == 0 && led_event == LedEvent::NONE)
            led_event = LedEvent::BEAT;

        // Gate countdowns → NoteOffs (audio + the matching MIDI out).
        for (int i = 0; i < kArpGates; i++) {
            if (arp_gate_blocks[i] && --arp_gate_blocks[i] == 0) {
                pool.NoteOff(kArpSlotBase + i);
                midi.SendNoteOff(kMidiPitchCh, arp_gate_note[i]);
            }
            if (rec_gate_blocks[i] && --rec_gate_blocks[i] == 0) {
                pool.NoteOff(kRecSlotBase + i);
                midi.SendNoteOff(kMidiPitchCh, rec_gate_note[i]);
            }
        }
    }

    // Clock-master upkeep: MIDI silent 500ms → hand over to CV if its pulses
    // are fresh (seq stays external, no re-phase), else back to the internal
    // clock; CV silent ~2.5 pulse periods → internal. As internal master,
    // emit 24 ppqn from the seq's own timebase so external gear can't drift
    // against the drums.
    {
        uint32_t now = System::GetNow();
        // Block → ms: kBlockSize samples at 48kHz.
        constexpr float kMsPerBlock = static_cast<float>(kBlockSize) * 1000.f / 48000.f;
        uint32_t cv_timeout = static_cast<uint32_t>(cv_period_blocks * kMsPerBlock * 2.5f);
        if (cv_timeout < 500) cv_timeout = 500;
        bool cv_fresh = cv_edge_last_ms != 0
                        && now - cv_edge_last_ms <= cv_timeout;
        if (ext_clock_src == ClockSrc::MIDI
                && now - midi_clock_last_ms > kMidiClockTimeoutMs) {
            if (cv_fresh) {
                ext_clock_src      = ClockSrc::CV;
                cv_ticks_in_window = 6;   // window closed — next edge takes over
            } else {
                ext_clock_src = ClockSrc::NONE;
                seq.SetExternalClock(false);
            }
        } else if (ext_clock_src == ClockSrc::CV && !cv_fresh) {
            ext_clock_src = ClockSrc::NONE;
            seq.SetExternalClock(false);
        }
    }
    if (!ext_clocked()) {
        for (int n = seq.MidiClockTick(); n > 0; n--) {
            midi.SendClock();
            clkout_pending = clkout_pending + 1;   // internal → CV out too
        }
    }

    // ── Clock out (S40) ───────────────────────────────────────────────────────
    // Consume the pending master-clock ticks (F8s, CV-synthetic, or internal —
    // exactly one of the three produces at a time) and raise a ~12ms pulse on
    // every 6th, i.e. one pulse per 16th step, phase-anchored to Start.
    {
        uint32_t n = clkout_pending;
        if (n) clkout_pending = clkout_pending - n;
        if (clkout_resync) { clkout_phase = 0; clkout_resync = false; }
        bool fire = false;
        for (uint32_t i = 0; i < n; i++) {
            if (clkout_phase == 0) fire = true;
            clkout_phase = (clkout_phase + 1) % 6;
        }
        if (fire) {
            cv_clock_out.Write(true);
            clkout_pulse = kClkOutPulseBlocks;
        } else if (clkout_pulse && --clkout_pulse == 0) {
            cv_clock_out.Write(false);
        }
    }

    // Render. Static to keep the buffers (main + the 4 independent per-group
    // FX send buses — Basic Pitch/arp/Rec/drum, 21/07/26 follow-up) off the
    // ISR stack. Array index matches VoiceGroup's/FxGroup's underlying int
    // (kBP=0, kArp=1, kRec=2, kDrum=3).
    static float left[kBlockSize];
    static float right[kBlockSize];
    static float rev_l[4][kBlockSize], rev_r[4][kBlockSize];
    static float dly_l[4][kBlockSize], dly_r[4][kBlockSize];
    __builtin_memset(left,  0, size * sizeof(float));
    __builtin_memset(right, 0, size * sizeof(float));
    for (int g = 0; g < 4; g++) {
        __builtin_memset(rev_l[g], 0, size * sizeof(float));
        __builtin_memset(rev_r[g], 0, size * sizeof(float));
        __builtin_memset(dly_l[g], 0, size * sizeof(float));
        __builtin_memset(dly_r[g], 0, size * sizeof(float));
    }
    for (size_t offset = 0; offset < size; offset += kChunkSize) {
        FxBuses buses = {
            { rev_l[0] + offset, rev_l[1] + offset, rev_l[2] + offset, rev_l[3] + offset },
            { rev_r[0] + offset, rev_r[1] + offset, rev_r[2] + offset, rev_r[3] + offset },
            { dly_l[0] + offset, dly_l[1] + offset, dly_l[2] + offset, dly_l[3] + offset },
            { dly_r[0] + offset, dly_r[1] + offset, dly_r[2] + offset, dly_r[3] + offset },
        };
        pool.Render(left + offset, right + offset, buses, kChunkSize);
    }

    // FX returns sum into the mix before the soft-clip. Each of the 4 groups
    // sleeps independently when its own send bus and tail are silent, so an
    // unused group's FX costs ~0 regardless of the others.
    fx_bp.ProcessDelay(dly_l[0], dly_r[0], left, right, size);
    fx_bp.ProcessReverb(rev_l[0], rev_r[0], left, right, size);
    fx_arp.ProcessDelay(dly_l[1], dly_r[1], left, right, size);
    fx_arp.ProcessReverb(rev_l[1], rev_r[1], left, right, size);
    fx_rec.ProcessDelay(dly_l[2], dly_r[2], left, right, size);
    fx_rec.ProcessReverb(rev_l[2], rev_r[2], left, right, size);
    fx_drum.ProcessDelay(dly_l[3], dly_r[3], left, right, size);
    fx_drum.ProcessReverb(rev_l[3], rev_r[3], left, right, size);

    // Output: soft-clip via x/(1+|x|), then the boot fade-in gain declared
    // above — 0 for the first block, linearly up to 1 over kBootFadeMs, a
    // no-op multiply for the rest of the unit's life after that. Levels are
    // otherwise per-group in VoicePool (SetSeqVolume / SetPitchedVolume) —
    // no other master scale here.
    for (size_t i = 0; i < size; i++) {
        float L = left[i];
        float R = right[i];
        if (boot_fade_gain < 1.f) {
            boot_fade_gain += kBootFadeIncPerSample;
            if (boot_fade_gain > 1.f) boot_fade_gain = 1.f;
        }
        out[0][i] = (L / (1.0f + fabsf(L))) * boot_fade_gain;
        out[1][i] = (R / (1.0f + fabsf(R))) * boot_fade_gain;
    }
    last_block_load = static_cast<float>(System::GetTick() - blk_start) * blk_ticks_inv;
    cpu_meter.OnBlockEnd();
}

// ─── Settings persistence ─────────────────────────────────────────────────────
// What survives a power cycle: every per-mode lock (*_lk), the effective
// Basic Pitch params, all four slot groups (BP snapshots, drum kit, arp/Rec
// sounds), octaves/root and the change-latched switch states — quantized to
// 8 bits, which both keeps the record small and stops ADC noise from reading
// as a settings change. Deliberately NOT saved: anything a physical switch
// dictates at boot (SW1 scale, SW2 playmode), transport states, and the note
// recorder's loop content (that's a take, not a setting). The snapshot
// doubles as the change detector inside SettingsJournal::Tick, so no gesture
// handler needs a dirty flag.
static constexpr uint16_t kPersistVersion = 1;

struct __attribute__((packed)) SlotPersist {
    uint8_t  engine;
    uint8_t  pad;
    uint16_t note_q;   // note * 256
    uint8_t  harmonics, timbre, morph, decay, volume, drive, blend, width,
             rev_send, dly_send;
};

struct __attribute__((packed)) PersistState {
    uint8_t seq_tempo, seq_shuf, seq_dens, seq_chance, seq_tight, seq_drive,
            seq_var, seq_vol, seq_width, seq_genre;
    uint8_t pitched_vol, pitched_blend, pitched_width;
    uint8_t fx_rev_seq, fx_rev_pitched, fx_rev_arp, fx_rev_rec;
    uint8_t fx_dly_seq, fx_dly_pitched, fx_dly_arp, fx_dly_rec;
    uint8_t eff_h, eff_t, eff_m, eff_d, eff_drive, lpg_colour;
    uint8_t arp_drive, arp_div, arp_swing, arp_dens, arp_decay, arp_order;
    uint8_t rec_speed, rec_shift, rec_chance, rec_order, rec_drive;
    uint8_t arp_vol, rec_vol, arp_blend, rec_blend;
    int8_t  engine, bp_oct, arp_oct, rec_oct, root, arp_range;
    uint8_t arp_state8;
    uint8_t flags;   // bit0 bp_slots_active, 1 drum_kit_ready,
                     // 2 arp_snd_ready, 3 rec_snd_ready, 4 arp_run_on
    SlotPersist bp[kPadSlots], drum[kPadSlots], arp_s, rec_s;
};
static_assert(sizeof(PersistState) <= SettingsJournal::kMaxPayload,
              "PersistState must fit one journal slot");

static uint8_t  q8(float v) { v = clampf(v); return static_cast<uint8_t>(v * 255.f + 0.5f); }
static float    dq8(uint8_t b) { return static_cast<float>(b) / 255.f; }
static uint16_t q_note(float n) {
    if (n < 0.f)   n = 0.f;
    if (n > 127.f) n = 127.f;
    return static_cast<uint16_t>(n * 256.f + 0.5f);
}
static float dq_note(uint16_t q) { return static_cast<float>(q) / 256.f; }

static void persist_slot(SlotPersist& d, const PadSlot& s) {
    d.engine    = static_cast<uint8_t>(s.engine);
    d.pad       = 0;
    d.note_q    = q_note(s.note);
    d.harmonics = q8(s.harmonics); d.timbre   = q8(s.timbre);
    d.morph     = q8(s.morph);     d.decay    = q8(s.decay);
    d.volume    = q8(s.volume);    d.drive    = q8(s.drive);
    d.blend     = q8(s.blend);     d.width    = q8(s.width);
    d.rev_send  = q8(s.rev_send);  d.dly_send = q8(s.dly_send);
}

static void restore_slot(PadSlot& d, const SlotPersist& s) {
    d.engine    = (s.engine <= 23) ? s.engine : 0;
    d.note      = dq_note(s.note_q);
    d.harmonics = dq8(s.harmonics); d.timbre   = dq8(s.timbre);
    d.morph     = dq8(s.morph);     d.decay    = dq8(s.decay);
    d.volume    = dq8(s.volume);    d.drive    = dq8(s.drive);
    d.blend     = dq8(s.blend);     d.width    = dq8(s.width);
    d.rev_send  = dq8(s.rev_send);  d.dly_send = dq8(s.dly_send);
}

static void capture_state(PersistState& st) {
    st.seq_tempo = q8(seq_tempo_lk); st.seq_shuf  = q8(seq_shuf_lk);
    st.seq_dens  = q8(seq_dens_lk);  st.seq_chance = q8(seq_chance_lk);
    st.seq_tight = q8(seq_tight_lk); st.seq_drive = q8(seq_drive_lk);
    st.seq_var   = q8(seq_var_lk);   st.seq_vol   = q8(seq_vol_lk);
    st.seq_width = q8(seq_width_lk);
    st.seq_genre = static_cast<uint8_t>(seq_genre_lk);
    st.pitched_vol   = q8(pitched_vol_lk);
    st.pitched_blend = q8(pitched_blend_lk);
    st.pitched_width = q8(pitched_width_lk);
    st.fx_rev_seq = q8(fx_rev_seq_lk); st.fx_rev_pitched = q8(fx_rev_pitched_lk);
    st.fx_rev_arp = q8(fx_rev_arp_lk); st.fx_rev_rec     = q8(fx_rev_rec_lk);
    st.fx_dly_seq = q8(fx_dly_seq_lk); st.fx_dly_pitched = q8(fx_dly_pitched_lk);
    st.fx_dly_arp = q8(fx_dly_arp_lk); st.fx_dly_rec     = q8(fx_dly_rec_lk);
    st.eff_h = q8(eff_h); st.eff_t = q8(eff_t); st.eff_m = q8(eff_m);
    st.eff_d = q8(eff_d); st.eff_drive = q8(eff_drive);
    st.lpg_colour = q8(lpg_colour);
    st.arp_drive = q8(arp_drive_lk); st.arp_div   = q8(arp_div_lk);
    st.arp_swing = q8(arp_swing_lk); st.arp_dens  = q8(arp_dens_lk);
    st.arp_decay = q8(arp_decay_lk); st.arp_order = q8(arp_order_lk);
    st.rec_speed = q8(rec_speed_lk); st.rec_shift  = q8(rec_shift_lk);
    st.rec_chance = q8(rec_chance_lk); st.rec_order = q8(rec_order_lk);
    st.rec_drive = q8(rec_drive_lk);
    st.arp_vol   = q8(arp_vol_lk);   st.rec_vol   = q8(rec_vol_lk);
    st.arp_blend = q8(arp_blend_lk); st.rec_blend = q8(rec_blend_lk);
    st.engine    = static_cast<int8_t>(current_engine);
    st.bp_oct    = static_cast<int8_t>(bp_octave);
    st.arp_oct   = static_cast<int8_t>(arp_octave);
    st.rec_oct   = static_cast<int8_t>(rec_octave);
    st.root      = static_cast<int8_t>(root_semitone);
    st.arp_range = static_cast<int8_t>(arp_oct_range);
    st.arp_state8 = static_cast<uint8_t>(arp_state);
    st.flags = (bp_slots_active ? 1u : 0u) | (drum_kit_ready ? 2u : 0u)
             | (arp_snd_ready ? 4u : 0u)  | (rec_snd_ready ? 8u : 0u)
             | (arp_run_on ? 16u : 0u);
    for (int i = 0; i < kPadSlots; i++) {
        persist_slot(st.bp[i],   bp_slots[i]);
        persist_slot(st.drum[i], drum_slots[i]);
    }
    persist_slot(st.arp_s, arp_snd);
    persist_slot(st.rec_s, rec_snd);
}

static void apply_state(const PersistState& st) {
    seq_tempo_lk = dq8(st.seq_tempo); seq_shuf_lk  = dq8(st.seq_shuf);
    seq_dens_lk  = dq8(st.seq_dens);  seq_chance_lk = dq8(st.seq_chance);
    seq_tight_lk = dq8(st.seq_tight); seq_drive_lk = dq8(st.seq_drive);
    seq_var_lk   = dq8(st.seq_var);   seq_vol_lk   = dq8(st.seq_vol);
    seq_width_lk = dq8(st.seq_width);
    seq_genre_lk = (st.seq_genre <= 2) ? st.seq_genre : 1;  // fallback: Techno
    pitched_vol_lk   = dq8(st.pitched_vol);
    pitched_blend_lk = dq8(st.pitched_blend);
    pitched_width_lk = dq8(st.pitched_width);
    fx_rev_seq_lk = dq8(st.fx_rev_seq); fx_rev_pitched_lk = dq8(st.fx_rev_pitched);
    fx_rev_arp_lk = dq8(st.fx_rev_arp); fx_rev_rec_lk     = dq8(st.fx_rev_rec);
    fx_dly_seq_lk = dq8(st.fx_dly_seq); fx_dly_pitched_lk = dq8(st.fx_dly_pitched);
    fx_dly_arp_lk = dq8(st.fx_dly_arp); fx_dly_rec_lk     = dq8(st.fx_dly_rec);
    eff_h = dq8(st.eff_h); eff_t = dq8(st.eff_t); eff_m = dq8(st.eff_m);
    eff_d = dq8(st.eff_d); eff_drive = dq8(st.eff_drive);
    lpg_colour = dq8(st.lpg_colour);
    arp_drive_lk = dq8(st.arp_drive); arp_div_lk   = dq8(st.arp_div);
    arp_swing_lk = dq8(st.arp_swing); arp_dens_lk  = dq8(st.arp_dens);
    arp_decay_lk = dq8(st.arp_decay); arp_order_lk = dq8(st.arp_order);
    rec_speed_lk = dq8(st.rec_speed); rec_shift_lk  = dq8(st.rec_shift);
    rec_chance_lk = dq8(st.rec_chance); rec_order_lk = dq8(st.rec_order);
    rec_drive_lk = dq8(st.rec_drive);
    arp_vol_lk   = dq8(st.arp_vol);   rec_vol_lk   = dq8(st.rec_vol);
    arp_blend_lk = dq8(st.arp_blend); rec_blend_lk = dq8(st.rec_blend);
    current_engine = (st.engine >= 0 && st.engine <= 23) ? st.engine : 0;
    bp_octave  = std::max(-3, std::min(3, static_cast<int>(st.bp_oct)));
    arp_octave = std::max(-3, std::min(3, static_cast<int>(st.arp_oct)));
    rec_octave = std::max(-3, std::min(3, static_cast<int>(st.rec_oct)));
    root_semitone = std::max(0, std::min(11, static_cast<int>(st.root)));
    arp_oct_range = std::max(0, std::min(3, static_cast<int>(st.arp_range)));
    arp_state = static_cast<ArpState>(st.arp_state8 <= 2 ? st.arp_state8 : 0);
    bp_slots_active = st.flags & 1u;
    drum_kit_ready  = st.flags & 2u;
    arp_snd_ready   = st.flags & 4u;
    rec_snd_ready   = st.flags & 8u;
    arp_run_on      = st.flags & 16u;
    for (int i = 0; i < kPadSlots; i++) {
        restore_slot(bp_slots[i],   st.bp[i]);
        restore_slot(drum_slots[i], st.drum[i]);
    }
    restore_slot(arp_snd, st.arp_s);
    restore_slot(rec_snd, st.rec_s);
}

// Main-loop hook: capture → journal. The journal rate-limits internally
// (250 ms compare cadence, 3 s settle, one flash page per pass, skipped
// while the audio ISR is near its block budget).
static void persist_tick(uint32_t now_ms) {
    static PersistState snap;
    capture_state(snap);
    settings_journal.Tick(&snap, now_ms, last_block_load);
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

    // Restore persisted settings from the QSPI journal (see README). This is
    // also where stale journal sectors get erased — blocking, but audio has
    // not started yet, so the 45–300 ms per-sector cost is invisible.
    {
        static PersistState boot_state;
        if (settings_journal.Init(&boot_state, sizeof(boot_state),
                                  kPersistVersion)) {
            apply_state(boot_state);
            settings_restored = true;
        }
    }
    if (!settings_restored) {
        // First boot (or settings layout changed): original randomized start.
        current_engine = random_engine();   // Basic Pitch starts on a random model
        // Rec's sound is independent from boot (unlike arp_snd, which still lazily
        // seeds from Basic Pitch on its first-ever entry — a hardware-tested fix,
        // kept as shipped; only Rec gets a fully unrelated model at power-on).
        rec_snd.engine = random_engine();
        if (rec_snd.engine >= 2 && rec_snd.engine <= 4) {
            const auto& a = kSixOpAud[rec_snd.engine - 2];
            rec_snd.harmonics = a.h;
            rec_snd.timbre    = a.t;
        }
        rec_snd_ready = true;
    }

    touch.Init(hw);
    oled.Init(hw);
    // Boot animation + settings-restored status line, blocking — safe here
    // since neither the audio ISR nor OledUi's redraw throttle are running
    // yet. service_telemetry() takes over the screen once the loop starts.
    OledBoot::Run(oled, System::GetNow(), settings_restored, set_led);
    // S40 clock-out jack (D25): plain push-pull GPIO, pulsed from the audio ISR.
    cv_clock_out.Init(daisy::seed::D25, GPIO::Mode::OUTPUT);
    cv_clock_out.Write(false);
    pool.Init();
    pool.SetEngine(current_engine);
    // Clears the SDRAM FX buffers (NOLOAD section) for all 4 independent groups.
    fx_bp.Init(hw.AudioSampleRate());
    fx_arp.Init(hw.AudioSampleRate());
    fx_rec.Init(hw.AudioSampleRate());
    fx_drum.Init(hw.AudioSampleRate());
    // TRS MIDI unless -DOLED_I2C4 took its pins for the display (idle UART is
    // otherwise free); USB MIDI only with -DUSB_MIDI, which is also what
    // suppresses StartLog above — one USB port, one owner.
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

            } else if (current_mode == PlayMode::ARP_MEL
                       && arp_state != ArpState::REC) {
                // Arp / Hold: pads only feed the pool — the arp does the
                // sounding (a direct note would double every trigger). The
                // note is stored octave-normalized so P10/P11 transpose the
                // running arp; each trigger MIDI-mirrors itself.
                int base = static_cast<int>(compute_note(pad)) - arp_octave * 12;
                arp.Touch(slot, static_cast<uint8_t>(base),
                          arp_state == ArpState::HOLD);

            } else {
                // Normal note play: Basic Pitch, or live notes in Rec (which
                // also stamps them into the open layer).
                if (current_mode == PlayMode::ARP_MEL) {
                    if (touch.pads().IsTouched(2) && slot <= 4) {
                        // P2 + pad P3-P7 in Rec = layer gesture (tap =
                        // ToggleMute, hold = ClearLayer, multi-hold =
                        // ClearAll — tracked in AudioCallback, tap fires on
                        // release). The pad neither sounds nor records here.
                        return;
                    }
                    pool.NoteOnWithParams(slot, compute_note(pad),
                                          rec_params(arp_decay_lk),
                                          VoiceGroup::kRec);
                    // Unarmed = audition only: sound the pad, don't touch the
                    // take or the LED. RecordNote() already no-ops while
                    // unarmed (recording_ false) — but treating that as
                    // "limit reached" was wrong; only react to it while armed.
                    if (rec_armed) {
                        arp_run_on = true;   // first Rec note may start the clock
                        if (!note_rec.RecordNote(
                                static_cast<uint8_t>(compute_note(pad)),
                                static_cast<uint8_t>(arp_decay_lk * 255.f)))
                            led_event = LedEvent::LIMIT;   // layers/events full
                    }
                } else if (bp_slots_active) {
                    pool.NoteOnWithParams(slot, compute_note(pad),
                                          slot_params(bp_slots[slot]));
                } else {
                    pool.NoteOn(slot, compute_note(pad));
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
                pool.AuditionWithParams(s.note, drum_params(rec_slot), VoiceGroup::kDrum);
            } else if (rec_mode == RecMode::IDLE && touch.pads().IsTouched(2)
                       && !seq_mode_on && current_mode == PlayMode::ARP_MEL
                       && arp_state == ArpState::REC) {
                // P2+P10 while SW1=Rec cycles the three states this mode
                // actually has, rather than toggling arm alone (2026-08-05).
                // Arm-only left the transport unreachable from Rec: stopping
                // playback meant flicking SW1 out, pressing P2+P10 there and
                // flicking back, because arming force-starts the clock and
                // disarming deliberately does NOT stop it (2026-07-21 —
                // auto-start on entry made auditioning Rec's own sound
                // impossible without also recording a note).
                //   stopped -> capture live -> looping, punched out -> stopped
                // The open take commits on the punch-out step, so the stop
                // step can never lose one. LED counts down with the state:
                // 3 blinks capturing, 2 looping, 1 stopped.
                if (!arp_run_on) {
                    arp_run_on = true;
                    rec_armed  = true;
                    note_rec.SetRecording(true);
                    led_event = LedEvent::CONFIRM;   // 3 blinks: capturing
                } else if (rec_armed) {
                    rec_armed = false;
                    note_rec.SetRecording(false);
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;              // 2 blinks: looping only
                } else {
                    arp_run_on = false;
                    arp_gates_off();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 1;              // 1 blink: stopped
                }
            } else if (rec_mode == RecMode::IDLE && touch.pads().IsTouched(2)) {
                // P2 (held first) + P10 → melodic transport (arp + Rec loop),
                // in any playmode — the mirror of P2+P11 for the drums.
                arp_run_on = !arp_run_on;
                if (!arp_run_on) {
                    arp_gates_off();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;   // 2 blinks: paused
                } else {
                    led_event = LedEvent::CONFIRM;  // 3 blinks: running
                }
            } else if (rec_mode == RecMode::IDLE && !seq_mode_on
                       && current_mode == PlayMode::ARP_MEL
                       && arp_state == ArpState::REC
                       && touch.pads().IsTouched(1)) {
                // P1+P10 in Rec = undo: clears the open take first, then pops
                // committed layers newest-first. Moved off P0 2026-08-05 —
                // see the octave-range branch below for the whole argument.
                // Clearing a specific/all layers is still P2+pad (see the
                // P2-layer hold block in AudioCallback and SetOnRelease).
                if (note_rec.Undo()) { led_event = LedEvent::NUMBERED; led_event_data = 1; }
                else                 { led_event = LedEvent::LIMIT; }
            } else if (rec_mode == RecMode::IDLE && !seq_mode_on
                       && current_mode == PlayMode::ARP_MEL
                       && touch.pads().IsTouched(0)) {
                // P0+P10 = arp octave range down (0–3 extra octaves).
                // Swapped with undo 2026-08-05. The panel's modifier grammar
                // is P0 = sound & pitch, P1 = FX, P2 = transport, and octave
                // range was the one P1 combo that wasn't FX — a pitch control
                // sitting on the effects modifier. It was there because undo
                // owned P0+P10 in Rec, so binding range to P0 would have made
                // one combo mean two unrelated things depending on SW1.
                // Swapping fixes both at once: range is now P0 in all three
                // sub-states, and undo takes P1, which is otherwise unused on
                // these two pads. Range stays reachable from Rec on purpose —
                // a latched Hold arp keeps playing behind it.
                if (arp_oct_range > 0) {
                    arp_oct_range--;
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = arp_oct_range + 1;
                } else {
                    led_event = LedEvent::LIMIT;
                }
            } else if (!seq_mode_on && current_mode == PlayMode::BASIC_PITCH
                       && touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                // Root note is a Basic Pitch-only setting (20/07/26 notes:
                // scale and root can only be set there) — same restriction
                // scale_lk already has via the SW1 routing above.
                if (root_semitone > 0) { root_semitone--; pool.Audition(root_note_f()); }
                else                   { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0)) {
                // Base octave, −3 rail. Blinks like every other limit on the
                // panel (root, arp octave range, layers, undo) — the `+3 D#5`
                // readout says where you are, the blink says you asked for
                // more and got nothing.
                int& oct = active_octave();
                if (oct > -3) { oct--; }
                else          { led_event = LedEvent::LIMIT; }
            }

        } else if (pad == 11) {
            if (rec_mode == RecMode::RECORDING && is_drum_mode
                    && !touch.pads().IsTouched(0) && rec_slot >= 0) {
                auto& s = live_slots()[rec_slot];
                s.note = std::min(127.f, s.note + 1.f);
                pool.AuditionWithParams(s.note, drum_params(rec_slot), VoiceGroup::kDrum);
            } else if (rec_mode == RecMode::IDLE && touch.pads().IsTouched(2)) {
                // P2 (held first) + P11 → drum seq play/pause, in any playmode.
                // P2 being down disables P11's octave function until release.
                if (seq.IsActive()) {
                    seq.Stop();
                    if (!midi_clock_master()) midi.SendStop();
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = 2;   // 2 blinks: paused
                } else {
                    if (!drum_kit_ready) generate_drum_random();
                    seq_entered_once = true;   // manual start counts as first entry
                    seq.Resume();
                    if (!midi_clock_master()) midi.SendContinue();
                    led_event = LedEvent::CONFIRM;  // 3 blinks: playing
                }
            } else if (rec_mode == RecMode::IDLE && !seq_mode_on
                       && current_mode == PlayMode::ARP_MEL
                       && touch.pads().IsTouched(0)) {
                // P0+P11 = arp octave range up — see the P10 branch for why
                // this moved off P1. P0+P11 was entirely unbound in Arp/Mel,
                // so this half of the swap collided with nothing.
                if (arp_oct_range < 3) {
                    arp_oct_range++;
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = arp_oct_range + 1;
                } else {
                    led_event = LedEvent::LIMIT;
                }
            } else if (!seq_mode_on && current_mode == PlayMode::BASIC_PITCH
                       && touch.pads().IsTouched(0) && !any_musical_pad_held()) {
                // Root note is a Basic Pitch-only setting — see the P10 branch.
                if (root_semitone < 11) { root_semitone++; pool.Audition(root_note_f()); }
                else                    { led_event = LedEvent::LIMIT; }
            } else if (!touch.pads().IsTouched(0) && !touch.pads().IsTouched(2)) {
                // Base octave, +3 rail — see the P10 branch.
                int& oct = active_octave();
                if (oct < 3) { oct++; }
                else         { led_event = LedEvent::LIMIT; }
            }
        }
    });

    touch.pads().SetOnRelease([](uint16_t pad) {
        if (pad >= 3 && pad <= 9) {
            // Drum voices use slot ids 16+i, so this never cuts them — drums
            // ring out as one-shots; pitched notes gate off normally.
            if (rec_mode == RecMode::IDLE) pool.NoteOff(pad - 3);
            // Plain Arp: release drops the pool entry. Hold ignores releases
            // (latch); Rec's live notes are covered by the NoteOff above.
            if (!seq_mode_on && current_mode == PlayMode::ARP_MEL
                    && arp_state == ArpState::ARP)
                arp.Release(pad - 3);
            // P2 + pad P3-P7 tap in Rec (released before the hold threshold
            // fired a ClearLayer/ClearAll): toggle that layer's mute.
            int slot = pad - 3;
            if (slot <= 4 && p2layer_hold[slot] > 0 && !p2layer_fired[slot]) {
                if (note_rec.ToggleMute(slot)) {
                    led_event      = LedEvent::NUMBERED;
                    led_event_data = slot + 1;
                } else {
                    led_event = LedEvent::LIMIT;
                }
            }
            // MIDI NoteOff even if rec mode swallowed the release — a hanging
            // note on external gear is worse than a stray off.
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
            // oled: frames pushed in the window, and the worst single
            // transfer in it. That max is the number the I2C1-vs-I2C4 A/B
            // turns on — it is also, on the shared bus, exactly how long the
            // pads went unpolled (see i2c1_lock.h), so at 4ms/block it
            // converts straight to blocks of touch latency.
            HW::hw().print("CPU avg %d%% max %d%% shed %d sv %d%s%s | oled %d fr max %dus",
                           static_cast<int>(cpu_meter.GetAvgCpuLoad() * 100.f),
                           static_cast<int>(cpu_meter.GetMaxCpuLoad() * 100.f),
                           static_cast<int>(shed_count),
                           static_cast<int>(settings_journal.save_count()),
                           settings_journal.saving_disabled() ? " FULL" : "",
                           settings_journal.write_error() ? " WERR" : "",
                           static_cast<int>(OledScreen::FrameCount()),
                           static_cast<int>(OledScreen::MaxFrameUs()));
#endif
            cpu_meter.Reset();
            OledScreen::ResetFrameStats();
            shed_count = 0;
        }

        // Debounced settings auto-save (runs every pass — before the LED
        // branches below, which `continue` past the rest of the loop).
        persist_tick(now_ms);

        uint32_t hold  = p0p2_hold_count;
        bool     done  = p0p2_all_done;
        uint32_t stage = p0p2_stage_fired;

        // P0+P2 hold animation — runs in every playmode (highest priority —
        // swallows the loop iteration).
        if ((hold > 0 || done) && rec_mode == RecMode::IDLE) {
            if (stage > 0) {
                set_led(true);
                uint32_t t0 = System::GetNow();
                while (System::GetNow() - t0 < 500) {
                    delay_serviced(5);
                    if (p0p2_hold_count == 0 && !p0p2_all_done) break;
                }
                set_led(false);
                p0p2_stage_fired = 0;
            } else if (done) {
                delay_serviced(10);
            } else {
                // Same two-phase shape the screen draws (see the hold-pacing
                // block): slow pulses through the announce window — three of
                // them, the "a stage is coming" cue a unit with no screen
                // otherwise has no way to give — then the familiar
                // accelerating blink as the bar fills.
                const uint32_t off = hold % kStageBlocks;
                if (off < kStageAnnounceBlocks) {
                    set_led(true);  delay_serviced(130);
                    set_led(false); delay_serviced(70);
                } else {
                    const uint32_t t        = off - kStageAnnounceBlocks;
                    const uint32_t interval = 150u - t * 110u / kStageFillBlocks;
                    set_led(true);  delay_serviced(interval);
                    set_led(false); delay_serviced(interval);
                }
            }
            continue;
        }

        // The same countdown for Rec's per-pad randomize (P0+P2 while
        // editing a slot) — identical two-phase shape, since to the hand it
        // is the same gesture; only its scope differs.
        {
            uint32_t rhold  = rec_p0p2_hold_count;
            bool     rdone  = rec_p0p2_all_done;
            uint32_t rstage = rec_p0p2_stage_fired;
            if ((rhold > 0 || rdone) && rec_mode == RecMode::RECORDING) {
                if (rstage > 0) {
                    set_led(true);
                    uint32_t t0 = System::GetNow();
                    while (System::GetNow() - t0 < 500) {
                        delay_serviced(5);
                        if (rec_p0p2_hold_count == 0 && !rec_p0p2_all_done) break;
                    }
                    set_led(false);
                    rec_p0p2_stage_fired = 0;
                } else if (rdone) {
                    delay_serviced(10);
                } else {
                    const uint32_t off = rhold % kStageBlocks;
                    if (off < kStageAnnounceBlocks) {
                        set_led(true);  delay_serviced(130);
                        set_led(false); delay_serviced(70);
                    } else {
                        const uint32_t t        = off - kStageAnnounceBlocks;
                        const uint32_t interval = 150u - t * 110u / kStageFillBlocks;
                        set_led(true);  delay_serviced(interval);
                        set_led(false); delay_serviced(interval);
                    }
                }
                continue;
            }
        }

        // P0+P1 sound-edit countdown. Until now this 1 s hold gave nothing
        // at all until it fired — no build-up on the LED, none on the
        // screen — even though it silently reassigns every knob in the mode.
        // Short enough that the announce window gets a single slow pulse
        // rather than three (see kShortAnnounceBlocks).
        {
            uint32_t shold = se_hold_count;
            if (shold > 0 && !se_fired) {
                if (shold < kShortAnnounceBlocks) {
                    set_led(true);  delay_serviced(130);
                    set_led(false); delay_serviced(70);
                } else {
                    const uint32_t span = kSndEditHoldBlocks - kShortAnnounceBlocks;
                    const uint32_t t    = (shold < kSndEditHoldBlocks)
                                        ? shold - kShortAnnounceBlocks : span;
                    const uint32_t interval = 150u - t * 120u / span;
                    set_led(true);  delay_serviced(interval);
                    set_led(false); delay_serviced(interval);
                }
                continue;
            }
        }

        // Recording-entry countdown: from ~0.2 s into the pad hold, slow
        // pulses while the screen names what the hold does, then gradually
        // shrinking intervals (~150 ms down to 30 ms at the 2 s threshold) —
        // release any time to abort. Same two-phase shape as P0+P2 above.
        uint32_t ehold = entry_hold_count;
        if (rec_mode == RecMode::IDLE && ehold >= kRecEntryAnimStart) {
            if (ehold < kRecEntryAnnounceBlocks) {
                set_led(true);  delay_serviced(130);
                set_led(false); delay_serviced(70);
            } else {
                const uint32_t span = kRecEntryHoldBlocks - kRecEntryAnnounceBlocks;
                const uint32_t t    = (ehold < kRecEntryHoldBlocks)
                                    ? ehold - kRecEntryAnnounceBlocks : span;
                const uint32_t interval = 150u - t * 120u / span;
                set_led(true);  delay_serviced(interval);
                set_led(false); delay_serviced(interval);
            }
            continue;
        }

        // P2+pad layer-clear countdown (21/07/26 LED redesign): the same
        // accelerating-blink language as every other hold gesture, so
        // holding toward Clear/Clear-all reads as unmistakably different
        // from the instant tap that means mute/unmute — the tap gets its
        // own confirm blink (NUMBERED) with no preceding animation at all.
        // Stops claiming the loop the instant a pad's hold actually fires,
        // so the queued completion event (see AudioCallback) dispatches
        // through the normal path below instead of being swallowed here.
        {
            uint32_t max_hold   = 0;
            bool     any_count = false;
            for (int i = 0; i < 5; i++) {
                if (p2layer_hold[i] > 0 && !p2layer_fired[i]) {
                    any_count = true;
                    if (p2layer_hold[i] > max_hold) max_hold = p2layer_hold[i];
                }
            }
            if (rec_mode == RecMode::IDLE && any_count && max_hold >= kRecEntryAnimStart) {
                uint32_t t        = (max_hold < kLongHoldBlocks) ? max_hold : kLongHoldBlocks;
                uint32_t interval = 150u - t * 120u / kLongHoldBlocks;
                set_led(true);  delay_serviced(interval);
                set_led(false); delay_serviced(interval);
                continue;
            }
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
                set_led(true);  delay_serviced(interval);
                set_led(false); delay_serviced(interval);
                continue;
            }

            // Entry burst: one rapid pattern right after the 2 s hold lands...
            if (rec_entry_flash) {
                rec_entry_flash = false;
                for (int i = 0; i < 5; i++) {
                    set_led(true);  delay_serviced(35);
                    set_led(false); delay_serviced(35);
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
                set_led(true);  delay_serviced(45);
                set_led(false); delay_serviced(45);
                set_led(true);  delay_serviced(45);
                set_led(false);
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
                set_led(true); delay_serviced(50); set_led(false); break;
            case LedEvent::BEAT:
                set_led(true); delay_serviced(20); set_led(false); break;
            default: delay_serviced(10); break;
        }
    }
}
