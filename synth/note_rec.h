#pragma once
#include <cstdint>

// Layered note recorder for the Arp/Mel playmode's Rec state (SW1 right).
//
// Pure logic like Arp: TouchPlaited stamps notes in and fires voices from the
// events returned by Tick(). Time is counted in 24 ppqn ticks (6 per 16th),
// advanced fractionally from the master step clock, or 1:1 from received F8s
// under external MIDI clock.
//
// v1 semantics (see notesarchive/arp-mel-plan-archive.md — iterate on hardware):
//  - The clock starts at the FIRST recorded note; loop length is fixed at
//    2 bars (32 sixteenths). Every wrap of the playhead commits the notes
//    played during that pass as one layer and opens a fresh one.
//  - Committed layers replay every pass; the open layer is heard live only,
//    then joins the loop on the next wrap.
//  - Undo (P0+P10 tap) clears the open take first, then pops committed
//    layers, newest first; when nothing is left the clock resets and waits
//    for a fresh first note.
//  - Max 5 layers × 48 events. A full recorder rejects notes (caller blinks
//    LIMIT).
//
// Layer gestures (20/07/26 notes; §7/C11 in notesarchive/arp-mel-plan-archive.md) — P2 + pad
// P3-P7 (one pad per layer): tap = ToggleMute, hold = ClearLayer, holding
// two or more at once = ClearAll. Replaces the earlier P0+pad / P0+P10-hold
// gestures (P0+P10 tap stays Undo).
//
// Rec-only knob layer (20/07/26 notes; §7/C10 in notesarchive/arp-mel-plan-archive.md) — Speed,
// Shift and Chance affect only the PLAYBACK of already-committed layers, not
// the live recording clock: new notes are still stamped at real time
// (cur_tick_), so overdubbing against a sped-up or shifted loop still lands
// where you actually played it. Order shuffles pitches across committed
// events (rhythm/timing untouched) — a non-destructive shadow copy, so
// flipping back to "original" always recovers exactly what was recorded.

class NoteRec {
public:
    static constexpr int kMaxLayers    = 5;
    static constexpr int kMaxEvents    = 48;
    static constexpr int kTicksPer16th = 6;
    static constexpr int kLoopTicks    = 32 * kTicksPer16th;   // 2 bars

    struct Event {
        uint8_t tick;    // 0..kLoopTicks-1
        uint8_t note;    // absolute MIDI note (octave applied at record time)
        uint8_t decay;   // S34 at record time, 0..255
    };

    // Rec state (SW1) — while true, RecordNote captures into the open layer.
    // Turning it off commits a non-empty open take immediately.
    void SetRecording(bool r) {
        if (recording_ && !r) commit_open();
        recording_ = r;
    }
    bool Recording() const { return recording_; }

    // S32 in Rec: 0 = 1x .. 1 = 8x playback rate of committed layers. Live
    // recording (RecordNote/cur_tick_) always stays real-time.
    void SetSpeed(float v) { speed_ = 1.0f + v * 7.0f; }

    // S33 in Rec: shifts every committed event's playback time by up to a
    // full loop in either direction (wraps) — center (0.5) = no shift.
    // Tick-level granularity (1/6 of a 16th) already covers "smaller than a
    // step" from the notes.
    void SetShift(float v) {
        shift_ticks_ = static_cast<int>((v - 0.5f) * 2.0f * static_cast<float>(kLoopTicks));
    }

    // S34 in Rec: per-hit playback probability, rolled fresh every pass —
    // 0 = notes never fire, 1 = always (matches recorded).
    void SetChance(float v) { chance_ = v; }

    // S35 in Rec (left of center): shuffle committed events' pitches among
    // themselves (rhythm/timing untouched) instead of playing as recorded.
    // Reshuffles on the off->on edge and whenever a fresh layer commits
    // while already on, so newly-added notes join the shuffle.
    void SetOrderRandom(bool r) {
        if (r && !order_random_) reshuffle();
        order_random_ = r;
    }

    // Returns false when the note could not be stored (layers/events full).
    bool RecordNote(uint8_t note, uint8_t decay) {
        if (!recording_) return false;
        if (!started_) {
            started_  = true;
            cur_tick_ = 0;
            tick_f_   = 0.f;
            play_tick_   = 0;
            play_tick_f_ = 0.f;
        }
        if (n_committed_ >= kMaxLayers) return false;
        uint8_t& n = n_ev_[n_committed_];
        if (n >= kMaxEvents) return false;
        ev_[n_committed_][n] = { static_cast<uint8_t>(cur_tick_), note, decay };
        n++;
        return true;
    }

    // Advance by adv ticks (fractional internally: 6/step_blocks per block;
    // integer F8 count under external clock; 0 = transport stopped). Fired
    // events are written to out; returns the count. Two independent clocks
    // share the same adv input: cur_tick_ (real-time, drives recording input
    // and layer commits) and play_tick_ (scaled by speed_, drives what
    // actually fires) — identical at the default speed (1x), diverge only
    // when Speed is in use.
    int Tick(float adv, Event* out, int max) {
        beat_ = false;
        int n = 0;
        if (!started_ || adv <= 0.f) return 0;

        tick_f_ += adv;
        while (tick_f_ >= 1.f) {
            tick_f_ -= 1.f;
            cur_tick_++;
            if (cur_tick_ >= kLoopTicks) {
                cur_tick_ = 0;
                if (recording_) commit_open();
            }
            if (cur_tick_ % 24 == 0) beat_ = true;   // quarter-note marker
        }

        play_tick_f_ += adv * speed_;
        while (play_tick_f_ >= 1.f) {
            play_tick_f_ -= 1.f;
            play_tick_++;
            if (play_tick_ >= kLoopTicks) play_tick_ = 0;
            for (int l = 0; l < n_committed_; l++) {
                if (mute_mask_ & (1u << l)) continue;
                for (int e = 0; e < n_ev_[l]; e++) {
                    int fire_at = static_cast<int>(ev_[l][e].tick) + shift_ticks_;
                    fire_at %= kLoopTicks;
                    if (fire_at < 0) fire_at += kLoopTicks;
                    if (fire_at != play_tick_) continue;
                    if (chance_ < 1.0f && rand01() >= chance_) continue;
                    if (n >= max) continue;
                    uint8_t note = order_random_ ? shuf_note_[l][e] : ev_[l][e].note;
                    out[n++] = { ev_[l][e].tick, note, ev_[l][e].decay };
                }
            }
        }
        return n;
    }

    bool BeatFired() const { return beat_; }

    bool Undo() {
        if (n_committed_ < kMaxLayers && n_ev_[n_committed_] > 0) {
            n_ev_[n_committed_] = 0;                 // clear the open take
            return true;
        }
        if (n_committed_ > 0) {
            n_committed_--;
            n_ev_[n_committed_] = 0;
            return true;
        }
        if (started_) {                              // empty: reset the clock
            reset_clock();
            return true;
        }
        return false;
    }

    // Wipe every layer, the open take and the clock — back to waiting for a
    // fresh first note. Recording state (SW1) is untouched.
    void ClearAll() {
        for (int i = 0; i < kMaxLayers; i++) n_ev_[i] = 0;
        n_committed_ = 0;
        mute_mask_   = 0;
        reset_clock();
    }

    // Clear committed layer i (0 = oldest), compacting the stack — the open
    // take, when a slot for it exists, shifts down with the rest. Returns
    // false when there is no such layer.
    bool ClearLayer(int i) {
        if (i < 0 || i >= n_committed_) return false;
        const int top = (n_committed_ < kMaxLayers) ? n_committed_
                                                    : kMaxLayers - 1;
        for (int l = i; l < top; l++) {
            n_ev_[l] = n_ev_[l + 1];
            for (int e = 0; e < n_ev_[l]; e++) ev_[l][e] = ev_[l + 1][e];
            if (mute_mask_ & (1u << (l + 1))) mute_mask_ |= static_cast<uint8_t>(1u << l);
            else                              mute_mask_ &= static_cast<uint8_t>(~(1u << l));
        }
        n_ev_[top] = 0;   // vacated slot: fresh open take (or stale copy)
        mute_mask_ &= static_cast<uint8_t>(~(1u << top));
        n_committed_--;
        if (order_random_) reshuffle();   // event set changed under it
        if (!HasContent()) reset_clock();  // emptied out: reset like Undo does
        return true;
    }

    // Tap gesture (P2 + pad, released before the hold threshold): mute or
    // unmute a committed layer without clearing it. Returns false when
    // there is no such layer.
    bool ToggleMute(int i) {
        if (i < 0 || i >= n_committed_) return false;
        mute_mask_ ^= static_cast<uint8_t>(1u << i);
        return true;
    }
    bool IsMuted(int i) const {
        return i >= 0 && i < n_committed_ && (mute_mask_ & (1u << i)) != 0;
    }
    // bit i = layer i muted, for telemetry (visualizer info screen).
    uint8_t MuteMask() const { return mute_mask_; }

    int Layers() const { return n_committed_; }

    bool HasContent() const {
        if (n_committed_ > 0) return true;
        return n_committed_ < kMaxLayers && n_ev_[n_committed_] > 0;
    }

private:
    Event   ev_[kMaxLayers][kMaxEvents];
    uint8_t shuf_note_[kMaxLayers][kMaxEvents];   // Order-random shadow pitches
    uint8_t n_ev_[kMaxLayers] = {};
    uint8_t mute_mask_ = 0;   // bit i = committed layer i is muted
    int     n_committed_ = 0;
    bool    recording_   = false;
    bool    started_     = false;
    bool    beat_        = false;
    int     cur_tick_    = 0;
    float   tick_f_      = 0.f;

    // Playback clock (Speed-scaled) — see Tick().
    int     play_tick_   = 0;
    float   play_tick_f_ = 0.f;

    // Rec-only knob state (see setters above). Defaults are all "off":
    // speed 1x, no shift, always fires, original order.
    float speed_        = 1.0f;
    int   shift_ticks_  = 0;
    float chance_       = 1.0f;
    bool  order_random_ = false;

    uint32_t rng_ = 0x9E3779B9u;
    uint32_t next_rand() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return rng_;
    }
    float rand01() { return static_cast<float>(next_rand() >> 8) * (1.0f / 16777216.0f); }

    // Fisher-Yates shuffle of every committed event's pitch, written into
    // shuf_note_ at the same (layer, index) slot — rhythm untouched, and
    // ev_ itself is never modified so "original" always recovers exactly.
    void reshuffle() {
        uint8_t pitches[kMaxLayers * kMaxEvents];
        int n = 0;
        for (int l = 0; l < n_committed_; l++)
            for (int e = 0; e < n_ev_[l]; e++)
                pitches[n++] = ev_[l][e].note;
        for (int i = n - 1; i > 0; i--) {
            int j = static_cast<int>(rand01() * static_cast<float>(i + 1));
            if (j > i) j = i;
            uint8_t t = pitches[i]; pitches[i] = pitches[j]; pitches[j] = t;
        }
        int k = 0;
        for (int l = 0; l < n_committed_; l++)
            for (int e = 0; e < n_ev_[l]; e++)
                shuf_note_[l][e] = pitches[k++];
    }

    void reset_clock() {
        started_     = false;
        cur_tick_    = 0;
        tick_f_      = 0.f;
        play_tick_   = 0;
        play_tick_f_ = 0.f;
    }

    void commit_open() {
        if (n_committed_ >= kMaxLayers) return;
        if (n_ev_[n_committed_] == 0) return;
        n_committed_++;
        if (n_committed_ < kMaxLayers) n_ev_[n_committed_] = 0;
        if (order_random_) reshuffle();
    }
};
