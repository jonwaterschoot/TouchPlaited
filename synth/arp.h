#pragma once
#include <cstdint>

// Arpeggiator core for the Arp/Mel playmode (SW2 center).
//
// Pure logic, no audio calls: TouchPlaited feeds it the master timebase
// (the drum sequencer's 16th-step block count + external MIDI clock ticks)
// and fires voices from the returned events. Design notes: notes.md →
// "Playmode overhaul"; plan: notesarchive/arp-mel-plan-archive.md.
//
// Pool: up to kMaxNotes entries keyed by pad slot. Plain mode adds on touch /
// removes on release; hold mode latches and a re-touch removes (toggle).
// Notes are stored WITHOUT the octave offset — the caller adds the current
// octave at fire time, so P10/P11 transpose the running arp.
//
// Clock: one arp step = the S31 division expressed against the master 16th
// grid. Internally block-counted from step_blocks; under external MIDI clock
// it consumes 24 ppqn ticks (6 per 16th) — the divisions are exact tick
// counts there. Swing delays odd arp steps like the drum seq's shuffle.
//
// Density (S33) gates steps through a Euclidean mask over a fixed 16-step
// cycle. Upper half = steady fill 0–100%; lower half = the same fill sweep
// with a 75% chance roll on sounding steps. The note selector advances only
// on steps that actually sound (masked steps hold the sequence position).

class Arp {
public:
    static constexpr int kMaxNotes  = 12;
    static constexpr int kEuclidLen = 16;

    // S35 zones, in the notes' order; random is the boot default.
    enum Order : uint8_t { PLAYED = 0, UP, DOWN, PINGPONG, RANDOM };

    struct Out {
        bool     fire        = false;
        int      note        = 0;   // base note (no octave offset applied)
        uint32_t gate_blocks = 0;   // suggested gate length for the NoteOff
    };

    // ── Pool ─────────────────────────────────────────────────────────────
    void Touch(int pad, uint8_t note, bool hold) {
        int at = find(pad);
        if (at >= 0) {
            if (hold) remove_at(at);        // hold: re-touch toggles off
            else      notes_[at].note = note;
            return;
        }
        if (count_ >= kMaxNotes) return;
        notes_[count_].pad  = static_cast<int8_t>(pad);
        notes_[count_].note = note;
        count_++;
        sort_pool();
    }

    void Release(int pad) {                 // plain mode only (caller gates)
        int at = find(pad);
        if (at >= 0) remove_at(at);
    }

    // Leave Hold: keep only pads still physically touched (bit i = pad slot i).
    void KeepOnly(uint16_t pad_mask) {
        for (int i = count_ - 1; i >= 0; i--)
            if (!(pad_mask & (1u << notes_[i].pad))) remove_at(i);
    }

    void Clear()       { count_ = 0; }
    bool Empty() const { return count_ == 0; }

    // ── Controls (0..1 pot values) ───────────────────────────────────────
    // S31 division detents, slow → fast: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32.
    // Center (0.5) lands on 1/16 — the neutral default.
    void SetDivision(float v) {
        int i = static_cast<int>(v * 6.f);
        div_ = i > 5 ? 5 : (i < 0 ? 0 : i);
    }
    void SetSwing(float v)   { swing_ = v * 0.5f; }
    void SetDensity(float v) {
        if (v >= 0.5f) {
            fill_   = static_cast<int>((v - 0.5f) * 2.f * 16.f + 0.5f);
            chance_ = false;
        } else {
            fill_   = static_cast<int>(v * 2.f * 16.f + 0.5f);
            chance_ = true;
        }
        if (fill_ > 16) fill_ = 16;
    }
    void SetOrder(float v) {
        int i = static_cast<int>(v * 5.f);
        order_ = static_cast<Order>(i > 4 ? 4 : (i < 0 ? 0 : i));
    }
    void SetOctaveRange(int r) { oct_range_ = r < 0 ? 0 : (r > 3 ? 3 : r); }
    int  OctaveRange() const   { return oct_range_; }

    // ── Clock ────────────────────────────────────────────────────────────
    // Call once per audio block. step_blocks = blocks per master 16th (the
    // seq's internal tempo); ext_ticks = MIDI F8s received since the last
    // block (0 when internally clocked); run = melodic transport (P2+P10).
    Out Tick(uint32_t step_blocks, bool ext, int ext_ticks, bool run) {
        Out o;
        blocks_since_fire_++;
        if (!run || count_ == 0) {
            // Idle: re-phase so the first note fires the moment we resume.
            phase_        = 0.f;
            tick_in_step_ = 0;
            odd_step_     = false;
            pending_      = true;
            fire_at_      = 0.f;
            return o;
        }

        if (ext) {
            const int ticks_per_step = kDivTicks[div_];
            const int fire_tick = static_cast<int>(swing_ * static_cast<float>(ticks_per_step))
                                      * (odd_step_ ? 1 : 0);
            while (ext_ticks-- > 0) {
                if (pending_ && tick_in_step_ >= fire_tick) {
                    pending_ = false;
                    fire(o, step_blocks);
                }
                if (++tick_in_step_ >= ticks_per_step) {
                    tick_in_step_ = 0;
                    advance();
                }
            }
            return o;
        }

        const float interval = static_cast<float>(step_blocks) * kDivMult[div_];
        if (pending_ && phase_ >= fire_at_) {
            pending_ = false;
            fire(o, step_blocks);
        }
        phase_ += 1.f;
        if (phase_ >= interval) {
            phase_ -= interval;
            advance();
            fire_at_ = odd_step_ ? swing_ * interval : 0.f;
        }
        return o;
    }

private:
    struct Entry { int8_t pad; uint8_t note; };

    static constexpr float kDivMult[6]  = { 4.f, 2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f };
    static constexpr int   kDivTicks[6] = { 24, 12, 8, 6, 4, 3 };

    Entry   notes_[kMaxNotes];
    uint8_t sorted_[kMaxNotes];    // indices into notes_, ascending pitch
    int     count_     = 0;
    int     div_       = 3;        // 1/16
    float   swing_     = 0.f;
    int     fill_      = 16;       // steady full fill by default
    bool    chance_    = false;
    Order   order_     = RANDOM;   // notes.md: random is the default
    int     oct_range_ = 0;

    float    phase_    = 0.f;      // blocks into the current arp step
    float    fire_at_  = 0.f;      // swing-delayed fire point (blocks)
    bool     pending_  = true;
    bool     odd_step_ = false;
    int      tick_in_step_ = 0;    // external clock position within the step
    int      epos_     = 0;        // euclid cycle position 0..15
    uint32_t pos_      = 0;        // note-sequence position
    uint32_t blocks_since_fire_ = 0;
    uint32_t rng_      = 0x91E10DA5;

    int find(int pad) const {
        for (int i = 0; i < count_; i++)
            if (notes_[i].pad == pad) return i;
        return -1;
    }
    void remove_at(int at) {
        for (int i = at; i < count_ - 1; i++) notes_[i] = notes_[i + 1];
        count_--;
        sort_pool();
    }
    void sort_pool() {
        for (int i = 0; i < count_; i++) sorted_[i] = static_cast<uint8_t>(i);
        for (int i = 1; i < count_; i++) {            // insertion sort, N ≤ 12
            uint8_t v = sorted_[i];
            int j = i - 1;
            while (j >= 0 && notes_[sorted_[j]].note > notes_[v].note) {
                sorted_[j + 1] = sorted_[j];
                j--;
            }
            sorted_[j + 1] = v;
        }
    }

    uint32_t next_rand() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return rng_;
    }

    // Bresenham-style Euclidean mask: step i of 16 sounds when the fill
    // accumulator crosses an integer there. fill 0 = silence, 16 = all.
    bool euclid_step(int i) const {
        return ((i + 1) * fill_ / kEuclidLen) - (i * fill_ / kEuclidLen) > 0;
    }

    void advance() {
        epos_     = (epos_ + 1) % kEuclidLen;
        odd_step_ = !odd_step_;
        pending_  = true;
    }

    void fire(Out& o, uint32_t step_blocks) {
        if (!euclid_step(epos_)) return;                       // masked step
        if (chance_ && (next_rand() & 0xFF) >= 192) return;    // 75% roll

        const int span  = count_ * (oct_range_ + 1);
        int idx;
        switch (order_) {
            case RANDOM: idx = static_cast<int>(next_rand() % static_cast<uint32_t>(span)); break;
            case PINGPONG: {
                // Up then down over the expanded span, endpoints not repeated.
                if (span <= 1) { idx = 0; break; }
                const int period = 2 * span - 2;
                int p = static_cast<int>(pos_ % static_cast<uint32_t>(period));
                idx = (p < span) ? p : period - p;
                break;
            }
            default: idx = static_cast<int>(pos_ % static_cast<uint32_t>(span)); break;
        }
        pos_++;   // advances only on sounding steps (masked steps hold position)

        const int oct  = idx / count_;
        const int slot = idx % count_;
        int entry;
        if (order_ == UP || order_ == PINGPONG) entry = sorted_[slot];
        else if (order_ == DOWN)                entry = sorted_[count_ - 1 - slot];
        else                                    entry = slot;   // played / random: insertion order

        o.fire = true;
        o.note = static_cast<int>(notes_[entry].note) + 12 * oct;

        // Gate ≈ 55% of the space between fires. Internally that's the
        // division interval; under external clock use the measured distance
        // (the knob tempo may be wrong there).
        uint32_t basis = blocks_since_fire_;
        const uint32_t nominal = static_cast<uint32_t>(static_cast<float>(step_blocks) * kDivMult[div_]);
        if (basis < 2 || basis > nominal * 4) basis = nominal;
        o.gate_blocks = basis * 55u / 100u;
        if (o.gate_blocks < 3) o.gate_blocks = 3;
        blocks_since_fire_ = 0;
    }
};
