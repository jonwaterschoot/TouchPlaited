#include "settings_journal.h"

#include <cstring>

#include "stm32h7xx.h"

namespace synthux {

// ─── RAM-resident QSPI program/erase ─────────────────────────────────────────
//
// The app executes from memory-mapped QSPI. Any write/erase requires taking
// the QUADSPI peripheral out of memory-mapped mode, and from that instant no
// instruction may be fetched from flash — so everything below runs from a
// function placed in .data (copied to SRAM by the startup code, and SRAM is
// executable), with all interrupts masked and zero calls to flash-resident
// code: no HAL, no libc, only inlined register pokes on the QUADSPI block.
//
// Sequence per operation:
//   1. Abort the memory-mapped transfer, snapshot CCR/ABR.
//   2. Kick the flash chip out of its continuous-read latch (the bootloader/
//      libDaisy map with mode bits 0xA0 = "skip opcode on subsequent reads";
//      a no-instruction quad read with mode bits 0x00 releases it, per the
//      IS25LP064A datasheet, so the 1-line commands below are understood).
//   3. WREN, wait for the write-enable latch.
//   4. Page-program (0x02) or 4 KB sector-erase (0xD7) — same opcodes and
//      line widths libDaisy uses for this chip.
//   5. Poll the status register until WIP clears.
//   6. Restore the snapshotted memory-mapped configuration and unmask
//      interrupts. The next instruction fetch re-enters continuous read.
//
// A page program keeps the window around ~1 ms; sector erase (45–300 ms) is
// only ever issued by SettingsJournal::Init before audio starts.

namespace {

struct QspiRegs {
    volatile uint32_t CR, DCR, SR, FCR, DLR, CCR, AR, ABR, DR;
};

// Cortex-M7 MPU registers (raw — the CMSIS accessors live in flash).
struct MpuRegs {
    volatile uint32_t TYPE, CTRL, RNR, RBAR, RASR;
};

constexpr uint32_t kQspiBase = 0x52005000u;
constexpr uint32_t kMpuBase  = 0xE000ED90u;

// Current libDaisy uses MPU regions 0–2, but the app can be running under an
// older bootloader's MPU layout (skip_clocks path never re-runs
// ConfigureMpu), so take the topmost region (H7 M7 has 16) and snapshot/
// restore it instead of assuming it is free.
constexpr uint32_t kMpuGuardRegion = 15u;

constexpr uint32_t kCrAbort = 1u << 1;
constexpr uint32_t kSrTcf   = 1u << 1;
constexpr uint32_t kSrBusy  = 1u << 5;
constexpr uint32_t kFcrAll  = 0x1Bu;  // CTEF | CTCF | CSMF | CTOF

// Poll bounds. A status-register read is a handful of QSPI clocks (~µs);
// the largest wait is a worst-case sector erase (300 ms), covered with a
// wide margin by kWipBound iterations of the status poll.
constexpr uint32_t kFlagBound = 2000000u;
constexpr uint32_t kWipBound  = 4000000u;

__attribute__((always_inline)) static inline int
wait_flag(QspiRegs* q, uint32_t mask, uint32_t bound)
{
    while(bound--)
        if(q->SR & mask)
            return 0;
    return -1;
}

__attribute__((always_inline)) static inline int
wait_not_busy(QspiRegs* q, uint32_t bound)
{
    while(bound--)
        if(!(q->SR & kSrBusy))
            return 0;
    return -1;
}

// One transaction that ends with TCF: wait for completion, clear flags,
// wait for the peripheral to go idle.
__attribute__((always_inline)) static inline int finish(QspiRegs* q)
{
    int rc = wait_flag(q, kSrTcf, kFlagBound);
    q->FCR = kFcrAll;
    if(wait_not_busy(q, kFlagBound) != 0)
        rc = -1;
    return rc;
}

// Read the flash status register (0x05), 1-line. Starts on the CCR write
// (no address, no explicit trigger needed for reads without address).
__attribute__((always_inline)) static inline int
read_status(QspiRegs* q, uint32_t* out)
{
    q->DLR = 0;  // 1 byte
    q->CCR = (1u << 26)   // FMODE  = indirect read
             | (1u << 24) // DMODE  = 1 line
             | (1u << 8)  // IMODE  = 1 line
             | 0x05u;     // READ_STATUS_REG
    int rc = wait_flag(q, kSrTcf, kFlagBound);
    *out   = *(volatile uint8_t*)&q->DR;
    q->FCR = kFcrAll;
    if(wait_not_busy(q, kFlagBound) != 0)
        rc = -1;
    return rc;
}

}  // namespace

// op: 0 = program page (data/len, len 1..256, must not cross a page
// boundary), 1 = erase the 4 KB sector containing flash_addr.
// flash_addr is chip-relative (0..0x7FFFFF). data must be in RAM.
// Returns 0 on success. Interrupts are masked for the whole operation.
//
// The .data.* section name is what gets this function into SRAM without a
// linker-script change: the stock script collects *(.data*) into the .data
// output section (SRAM, startup-copied, executable). The assembler emits a
// "setting incorrect section attributes" warning because code ("ax") in a
// .data-named section is unusual — expected and harmless; the map file
// shows the function at its 0x24xxxxxx home.
extern "C" __attribute__((section(".data.tp_qspi_ramfunc"), noinline, used))
int tp_qspi_ram_op(uint32_t op, uint32_t flash_addr, const uint8_t* data, uint32_t len)
{
    QspiRegs* q = (QspiRegs*)kQspiBase;
    MpuRegs*  m = (MpuRegs*)kMpuBase;

    uint32_t primask;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("cpsid i" ::: "memory");
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    // 0) Wall off the whole QSPI mapping from the core for the duration.
    // Masking interrupts is NOT enough: the M7 speculatively prefetches
    // instructions from 0x90xxxxxx (e.g. along the predicted return path
    // into flash-resident code), and a memory-mapped AXI access while the
    // QUADSPI sits in indirect mode stalls the AXI bus permanently — the
    // documented H7 XIP erratum; the symptom is a hard freeze, no fault.
    // With this no-access/XN region active, speculative fetches are dropped
    // silently by the core and never reach the bus. The guard region is
    // snapshotted and restored, and the MPU is force-enabled (with the
    // privileged default map as background) for the window in case the
    // bootloader's boot path left it off — a disabled MPU would make the
    // guard silently inert.
    m->RNR = kMpuGuardRegion;
    const uint32_t saved_rbar     = m->RBAR;
    const uint32_t saved_rasr     = m->RASR;
    const uint32_t saved_mpu_ctrl = m->CTRL;
    m->RASR = 0u;                       // disable while retargeting
    m->RBAR = 0x90000000u;              // VALID=0 -> region from RNR
    m->RASR = (1u << 28)                // XN
              | (0u << 24)              // AP = no access
              | (27u << 1)              // SIZE: 2^(27+1) = 256 MB
              | 1u;                     // enable
    m->CTRL = saved_mpu_ctrl | (1u << 2) | 1u;  // PRIVDEFENA | ENABLE
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    const uint32_t saved_ccr = q->CCR;
    const uint32_t saved_abr = q->ABR;

    int rc = 0;

    // 1) Abort memory-mapped mode (stops the prefetch engine).
    q->CR |= kCrAbort;
    {
        uint32_t bound = kFlagBound;
        while(bound-- && (q->CR & kCrAbort)) {}
        if(q->CR & kCrAbort)
            rc = -1;
    }
    if(wait_not_busy(q, kFlagBound) != 0)
    {
        // Abort can wedge with BUSY stuck after a memory-mapped prefetch
        // (H7 erratum); dropping EN forces the peripheral idle.
        q->CR &= ~1u;
        for(volatile uint32_t i = 0; i < 64; i++) {}
        q->CR |= 1u;
        if(wait_not_busy(q, kFlagBound) != 0)
            rc = -1;
    }
    q->FCR = kFcrAll;

    // 2) Release the flash's continuous-read latch: quad read, NO
    // instruction phase (the latch skips opcodes), mode bits 0x00.
    if(rc == 0)
    {
        q->DLR = 0;  // 1 dummy data byte
        q->ABR = 0;  // mode bits != Ax -> exit continuous read
        q->CCR = (1u << 26)    // FMODE  = indirect read
                 | (3u << 24)  // DMODE  = 4 lines
                 | (6u << 18)  // DCYC   = 6 (matches the mapped-read config)
                 | (3u << 14)  // ABMODE = 4 lines (8-bit mode byte)
                 | (2u << 12)  // ADSIZE = 24 bit
                 | (3u << 10); // ADMODE = 4 lines; IMODE = none
        q->AR = 0;             // any address; starts the transfer
        rc    = wait_flag(q, kSrTcf, kFlagBound);
        (void)*(volatile uint8_t*)&q->DR;
        q->FCR = kFcrAll;
        if(rc == 0 && wait_not_busy(q, kFlagBound) != 0)
            rc = -1;
    }

    // 3) Write enable + wait for the WEL latch.
    if(rc == 0)
    {
        q->CCR = (1u << 8) | 0x06u;  // indirect write, instruction only
        rc     = finish(q);
    }
    if(rc == 0)
    {
        uint32_t bound = kFlagBound;
        uint32_t sr    = 0;
        do
        {
            if(read_status(q, &sr) != 0)
            {
                rc = -1;
                break;
            }
        } while(!(sr & 0x02u) && bound--);
        if(rc == 0 && !(sr & 0x02u))
            rc = -1;
    }

    // 4) The operation itself.
    if(rc == 0)
    {
        if(op == 0)
        {
            q->DLR = len - 1;
            q->CCR = (1u << 24)    // DMODE  = 1 line
                     | (2u << 12)  // ADSIZE = 24 bit
                     | (1u << 10)  // ADMODE = 1 line
                     | (1u << 8)   // IMODE  = 1 line
                     | 0x02u;      // PAGE_PROG
            q->AR = flash_addr;
            for(uint32_t i = 0; i < len && rc == 0; i++)
            {
                uint32_t bound = kFlagBound;
                while(bound-- && ((q->SR >> 8) & 0x3Fu) >= 32u) {}
                if(((q->SR >> 8) & 0x3Fu) >= 32u)
                {
                    rc = -1;
                    break;
                }
                *(volatile uint8_t*)&q->DR = data[i];
            }
            if(rc == 0)
                rc = finish(q);
        }
        else
        {
            q->CCR = (2u << 12)   // ADSIZE = 24 bit
                     | (1u << 10) // ADMODE = 1 line
                     | (1u << 8)  // IMODE  = 1 line
                     | 0xD7u;     // SECTOR_ERASE (4 KB)
            q->AR = flash_addr;   // starts the transfer (no data phase)
            rc    = finish(q);
        }
    }

    // 5) Wait for WIP to clear (program ≤ ~1 ms, erase ≤ ~300 ms).
    if(rc == 0)
    {
        uint32_t bound = kWipBound;
        uint32_t sr    = 0x01u;
        while(bound--)
        {
            if(read_status(q, &sr) != 0)
            {
                rc = -1;
                break;
            }
            if(!(sr & 0x01u))
                break;
        }
        if(rc == 0 && (sr & 0x01u))
            rc = -1;
    }

    // 6) Restore memory-mapped mode — unconditionally: without it the next
    // instruction fetch from flash is fatal, so this is attempted even after
    // an error above.
    q->FCR = kFcrAll;
    q->ABR = saved_abr;
    q->DLR = 0;
    q->CCR = saved_ccr;
    __asm volatile("dsb 0xF" ::: "memory");

    // 7) Put the MPU back exactly as found — only now may the core touch
    // QSPI again.
    m->RNR  = kMpuGuardRegion;
    m->RASR = 0u;
    m->RBAR = saved_rbar;
    m->RASR = saved_rasr;
    m->CTRL = saved_mpu_ctrl;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    return rc;
}

// ─── Journal ─────────────────────────────────────────────────────────────────

namespace {

constexpr uint32_t kMagic  = 0x314A5054u;  // "TPJ1"
constexpr uint32_t kMmBase = 0x90000000u;

struct __attribute__((packed)) RecordHeader {
    uint32_t magic;
    uint32_t seqno;
    uint16_t len;
    uint16_t version;
    uint32_t crc;
};
static_assert(sizeof(RecordHeader) == SettingsJournal::kHeaderSize,
              "record header size");

uint32_t crc32_sw(const uint8_t* d, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for(uint32_t i = 0; i < n; i++)
    {
        c ^= d[i];
        for(int b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// The journal region is read through the data cache; after the peripheral
// writes flash behind the cache's back, drop the affected lines.
void invalidate_mapped(uint32_t flash_offset, uint32_t size)
{
    uint32_t addr    = kMmBase + flash_offset;
    uint32_t aligned = addr & ~31u;
    SCB_InvalidateDCache_by_Addr((uint32_t*)aligned,
                                 (int32_t)(size + (addr - aligned)));
}

}  // namespace

uint32_t SettingsJournal::BankOffset(uint32_t bank) const
{
    return bank0_offset_ + bank * kBankSize;
}

const uint8_t* SettingsJournal::Mapped(uint32_t flash_offset)
{
    return (const uint8_t*)(kMmBase + flash_offset);
}

bool SettingsJournal::RangeBlank(uint32_t flash_offset, uint32_t size)
{
    const uint32_t* p = (const uint32_t*)Mapped(flash_offset);
    for(uint32_t i = 0; i < size / 4; i++)
        if(p[i] != 0xFFFFFFFFu)
            return false;
    return true;
}

bool SettingsJournal::EraseDirtySectors(uint32_t bank)
{
    bool ok = true;
    for(uint32_t s = 0; s < kBankSize / kSectorSize; s++)
    {
        uint32_t off = BankOffset(bank) + s * kSectorSize;
        if(RangeBlank(off, kSectorSize))
            continue;
        if(tp_qspi_ram_op(1, off, nullptr, 0) != 0)
        {
            ok         = false;
            write_err_ = true;
        }
        invalidate_mapped(off, kSectorSize);
    }
    return ok;
}

bool SettingsJournal::Init(void* dst, uint16_t payload_len, uint16_t version)
{
    if(payload_len == 0 || payload_len > kMaxPayload)
        return false;
    payload_len_ = payload_len;
    version_     = version;

    // Place the banks just below a guard band at the top of whatever window
    // the bootloader actually mapped (DCR.FSIZE) — reading against the very
    // end of the mapping trips the H7 prefetch-past-FSIZE erratum (bus
    // error), and assuming 8 MB when the mapping is smaller faults on the
    // first read. If the window is unexpectedly small, disable persistence
    // instead of risking a collision with the app image.
    const uint32_t fsize  = (QUADSPI->DCR >> 16) & 0x1Fu;
    const uint32_t mapped = 1u << (fsize + 1u);
    if(mapped < 0x200000u)  // < 2 MB leaves no safe home above the app
    {
        disabled_ = true;
        return false;
    }
    bank0_offset_ = mapped - kTopGuardBytes - 2u * kBankSize;

    // Scan both banks for the newest CRC-valid record of this layout.
    uint32_t best_seq = 0, best_bank = 0, best_slot = 0;
    bool     found = false;
    for(uint32_t bank = 0; bank < 2; bank++)
    {
        for(uint32_t slot = 0; slot < kSlotsPerBank; slot++)
        {
            const uint8_t* p = Mapped(BankOffset(bank) + slot * kSlotStride);
            RecordHeader   h;
            memcpy(&h, p, sizeof(h));
            if(h.magic != kMagic || h.len != payload_len_
               || h.version != version_)
                continue;
            if(crc32_sw(p + kHeaderSize, h.len) != h.crc)
                continue;
            if(!found || (int32_t)(h.seqno - best_seq) > 0)
            {
                found     = true;
                best_seq  = h.seqno;
                best_bank = bank;
                best_slot = slot;
            }
        }
    }

    if(found)
    {
        const uint8_t* p = Mapped(BankOffset(best_bank) + best_slot * kSlotStride);
        memcpy(dst, p + kHeaderSize, payload_len_);
        // Prime the shadow so an unchanged boot never writes a record.
        memcpy(shadow_, p + kHeaderSize, payload_len_);
        have_shadow_ = true;
        next_seq_    = best_seq + 1;
        active_bank_ = best_bank;
        next_slot_   = best_slot + 1;
        // A power cut mid-write can leave a torn (non-blank, CRC-invalid)
        // slot after the newest record — skip past anything not blank.
        while(next_slot_ < kSlotsPerBank
              && !RangeBlank(BankOffset(active_bank_)
                                 + next_slot_ * kSlotStride,
                             kSlotStride))
            next_slot_++;
        other_clean_ = EraseDirtySectors(active_bank_ ^ 1u);
    }
    else
    {
        // Nothing valid (fresh flash, or a layout/version change): reclaim
        // everything so appends land on erased bytes.
        next_seq_    = 1;
        active_bank_ = 0;
        next_slot_   = 0;
        bool ok0     = EraseDirtySectors(0);
        other_clean_ = EraseDirtySectors(1);
        if(!ok0)
            disabled_ = true;
    }

    restored_ = found;
    return found;
}

void SettingsJournal::Tick(const void* snapshot, uint32_t now_ms, float cpu_load)
{
    if(payload_len_ == 0 || disabled_)
        return;

    // A record in flight: program the next page, one per pass, only while
    // the audio ISR has headroom for the ~1 ms interrupt-off window.
    if(writing_)
    {
        if(cpu_load >= kCpuGate)
            return;
        uint32_t off = rec_off_ + page_idx_ * kPageSize;
        uint32_t n   = rec_len_ - page_idx_ * kPageSize;
        if(n > kPageSize)
            n = kPageSize;
        if(tp_qspi_ram_op(0, off, rec_ + page_idx_ * kPageSize, n) != 0)
        {
            write_err_   = true;
            writing_     = false;
            have_shadow_ = false;  // re-detect and retry at the next slot
            next_slot_++;
            return;
        }
        page_idx_++;
        if(page_idx_ * kPageSize >= rec_len_)
        {
            writing_ = false;
            invalidate_mapped(rec_off_, rec_len_);
            if(memcmp(rec_, Mapped(rec_off_), rec_len_) == 0)
                saves_++;
            else
            {
                write_err_   = true;
                have_shadow_ = false;
            }
            next_slot_++;
        }
        return;
    }

    if(now_ms - last_poll_ < kPollMs)
        return;
    last_poll_ = now_ms;

    // Change detection on the quantized snapshot; any difference re-arms
    // the settle timer.
    if(!have_shadow_ || memcmp(snapshot, shadow_, payload_len_) != 0)
    {
        memcpy(shadow_, snapshot, payload_len_);
        have_shadow_ = true;
        dirty_       = true;
        last_change_ = now_ms;
        return;
    }

    if(!dirty_ || now_ms - last_change_ < kSettleMs)
        return;

    // Settled: stage a record. Bank rotation happens here; if the spare
    // bank was already used up this power cycle, stop saving rather than
    // pay a runtime erase (the stale bank is reclaimed next boot).
    if(next_slot_ >= kSlotsPerBank)
    {
        if(!other_clean_)
        {
            disabled_ = true;
            return;
        }
        active_bank_ ^= 1u;
        next_slot_   = 0;
        other_clean_ = false;
    }

    RecordHeader h;
    h.magic   = kMagic;
    h.seqno   = next_seq_++;
    h.len     = payload_len_;
    h.version = version_;
    h.crc     = crc32_sw(shadow_, payload_len_);
    memcpy(rec_, &h, sizeof(h));
    memcpy(rec_ + kHeaderSize, shadow_, payload_len_);
    rec_len_  = kHeaderSize + payload_len_;
    rec_off_  = BankOffset(active_bank_) + next_slot_ * kSlotStride;
    page_idx_ = 0;
    writing_  = true;
    dirty_    = false;
}

}  // namespace synthux
