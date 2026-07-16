#pragma once
#include <cstdint>

// Layered note recorder for the Arp/Mel playmode's Rec state (SW1 right).
//
// Pure logic like Arp: TouchPlaited stamps notes in and fires voices from the
// events returned by Tick(). Time is counted in 24 ppqn ticks (6 per 16th),
// advanced fractionally from the master step clock, or 1:1 from received F8s
// under external MIDI clock.
//
// v1 semantics (see ARP-MEL-PLAN.md — iterate on hardware):
//  - The clock starts at the FIRST recorded note; loop length is fixed at
//    2 bars (32 sixteenths). Every wrap of the playhead commits the notes
//    played during that pass as one layer and opens a fresh one.
//  - Committed layers replay every pass; the open layer is heard live only,
//    then joins the loop on the next wrap.
//  - Undo (P0+P10 tap) clears the open take first, then pops committed
//    layers, newest first; when nothing is left the clock resets and waits
//    for a fresh first note. Holding P0+P10 clears everything at once
//    (ClearAll); P0 + playing pad 1–4 clears that committed layer, oldest
//    first (ClearLayer).
//  - Max 4 layers × 48 events. A full recorder rejects notes (caller blinks
//    LIMIT).

class NoteRec {
public:
    static constexpr int kMaxLayers    = 4;
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

    // Returns false when the note could not be stored (layers/events full).
    bool RecordNote(uint8_t note, uint8_t decay) {
        if (!recording_) return false;
        if (!started_) {
            started_ = true;
            cur_tick_ = 0;
            tick_f_   = 0.f;
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
    // events are written to out; returns the count.
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
            for (int l = 0; l < n_committed_; l++) {
                for (int e = 0; e < n_ev_[l]; e++) {
                    if (ev_[l][e].tick == cur_tick_ && n < max) out[n++] = ev_[l][e];
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
            started_  = false;
            cur_tick_ = 0;
            tick_f_   = 0.f;
            return true;
        }
        return false;
    }

    // Wipe every layer, the open take and the clock — back to waiting for a
    // fresh first note. Recording state (SW1) is untouched.
    void ClearAll() {
        for (int i = 0; i < kMaxLayers; i++) n_ev_[i] = 0;
        n_committed_ = 0;
        started_     = false;
        cur_tick_    = 0;
        tick_f_      = 0.f;
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
        }
        n_ev_[top] = 0;   // vacated slot: fresh open take (or stale copy)
        n_committed_--;
        if (!HasContent()) {           // emptied out: reset like Undo does
            started_  = false;
            cur_tick_ = 0;
            tick_f_   = 0.f;
        }
        return true;
    }

    int Layers() const { return n_committed_; }

    bool HasContent() const {
        if (n_committed_ > 0) return true;
        return n_committed_ < kMaxLayers && n_ev_[n_committed_] > 0;
    }

private:
    Event   ev_[kMaxLayers][kMaxEvents];
    uint8_t n_ev_[kMaxLayers] = { 0, 0, 0, 0 };
    int     n_committed_ = 0;
    bool    recording_   = false;
    bool    started_     = false;
    bool    beat_        = false;
    int     cur_tick_    = 0;
    float   tick_f_      = 0.f;

    void commit_open() {
        if (n_committed_ >= kMaxLayers) return;
        if (n_ev_[n_committed_] == 0) return;
        n_committed_++;
        if (n_committed_ < kMaxLayers) n_ev_[n_committed_] = 0;
    }
};
