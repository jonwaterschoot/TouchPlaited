#pragma once

#include <cstdint>

namespace synthux {

// Append-only settings journal in 64 KB near the top of the QSPI flash —
// placed just below a 64 KB guard band under the end of the memory-mapped
// window (see kTopGuardBytes), at a base derived from DCR.FSIZE at boot.
//
// The firmware executes from that same QSPI chip (BOOT_QSPI), so libDaisy's
// QSPIHandle/PersistentStorage refuse to write at runtime — taking the
// peripheral out of memory-mapped mode would cut off instruction fetch.
// This module owns a small register-level program/erase routine that runs
// from SRAM with interrupts disabled (see settings_journal.cpp), plus a
// journal layout that keeps every runtime flash touch down to a sub-ms
// page program:
//
//   - Two 32 KB banks (16 × 4 KB sectors total) near the top of flash, far
//     above the app image at 0x90040000, but never against the very end of
//     the mapping (H7 prefetch erratum — see kTopGuardBytes).
//   - A save appends one 512-byte record (header + CRC32 + payload) at the
//     next free slot — NOR flash programs 1→0 without erasing, so appending
//     never needs the slow (45–300 ms) sector erase.
//   - Records are written one 256-byte page per Tick() call, each inside
//     its own ~1 ms interrupt-off window, and only while the audio ISR has
//     headroom.
//   - Boot (Init): scan both banks for the newest CRC-valid record, restore
//     it, and erase whichever sectors the journal no longer needs — the
//     expensive erases happen before audio ever starts.
//   - If both banks fill within a single power cycle (128+ saves), saving
//     stops instead of glitching; the stale bank is reclaimed next boot.
//
// Wear math: one 4 KB sector holds 8 records, so a sector is erased once
// per 8·16 = 128 saves round-robin across the region. At 100k erase cycles
// per sector the journal outlives the hardware by orders of magnitude.
class SettingsJournal {
  public:
    static constexpr uint32_t kSlotStride = 512;
    static constexpr uint32_t kHeaderSize = 16;
    static constexpr uint32_t kMaxPayload = kSlotStride - kHeaderSize;

    // Boot-only (blocking, before StartAudio): scan, restore the newest
    // valid record into dst, reclaim stale sectors. Returns true when a
    // record matching len+version was found and copied into dst.
    bool Init(void* dst, uint16_t payload_len, uint16_t version);

    // Call every main-loop pass with the current settings snapshot (same
    // length as Init's payload_len). Internally rate-limited: snapshots are
    // compared every kPollMs, a save starts kSettleMs after the last
    // change, and each pass programs at most one flash page — skipped
    // entirely while cpu_load is above kCpuGate so the interrupt-off window
    // never lands on an audio block that is already near its budget.
    void Tick(const void* snapshot, uint32_t now_ms, float cpu_load);

    bool     restored() const { return restored_; }
    bool     saving_disabled() const { return disabled_; }
    bool     write_error() const { return write_err_; }
    uint32_t save_count() const { return saves_; }

  private:
    static constexpr uint32_t kPollMs   = 250;
    static constexpr uint32_t kSettleMs = 3000;
    static constexpr float    kCpuGate  = 0.85f;

    static constexpr uint32_t kPageSize     = 256;
    static constexpr uint32_t kSectorSize   = 0x1000;
    static constexpr uint32_t kBankSize     = 0x8000;   // 32 KB
    static constexpr uint32_t kSlotsPerBank = kBankSize / kSlotStride;
    // Never touch the last bytes of the QSPI mapping: memory-mapped reads
    // there make the controller's prefetcher run past the FSIZE boundary —
    // an STM32H7 erratum that raises a bus error (= boot hard fault).
    static constexpr uint32_t kTopGuardBytes = 0x10000;  // 64 KB

    uint32_t              BankOffset(uint32_t bank) const;
    static const uint8_t* Mapped(uint32_t flash_offset);
    static bool           RangeBlank(uint32_t flash_offset, uint32_t size);
    bool                  EraseDirtySectors(uint32_t bank);  // boot only

    // Set by Init from the QUADSPI's own DCR.FSIZE — the bootloader owns
    // that register, so the journal adapts to whatever window it mapped.
    uint32_t bank0_offset_ = 0;

    uint16_t payload_len_ = 0;
    uint16_t version_     = 0;

    uint32_t next_seq_    = 1;
    uint32_t active_bank_ = 0;
    uint32_t next_slot_   = 0;
    bool     other_clean_ = false;

    bool restored_  = false;
    bool disabled_  = false;
    bool write_err_ = false;

    // change detection / debounce
    bool     have_shadow_ = false;
    bool     dirty_       = false;
    uint32_t last_poll_   = 0;
    uint32_t last_change_ = 0;

    // in-flight record
    bool     writing_  = false;
    uint32_t page_idx_ = 0;
    uint32_t rec_len_  = 0;
    uint32_t rec_off_  = 0;

    uint32_t saves_ = 0;

    uint8_t shadow_[kMaxPayload];
    uint8_t rec_[kSlotStride];
};

}  // namespace synthux
