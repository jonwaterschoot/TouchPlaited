#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace synthux {

// Copy of the screen for the web manual's live link (midi/telemetry.cpp).
// The driver stores each page it pushes. Frame() packs one changed half page
// as `F0 7D 53 4F 01 <page> <column> <packed data> F7`: groups of up to 7
// bytes, each after one byte with their top bits (bit n is byte n).
// Spec: simple-touch-web's README, "Live link".
//
// Static, like the driver's page counter: there is one display, and
// daisy::OledDisplay gives no access to its driver. Main loop only.
struct OledMirror {
    static constexpr int kWidth      = 128;
    static constexpr int kPages      = 4;
    static constexpr int kChunk      = 64;                      // half a page
    static constexpr int kChunks     = kPages * kWidth / kChunk;
    static constexpr int kFrameBytes = 7 + kChunk + (kChunk + 6) / 7 + 1;

    static void Store(size_t page, const uint8_t* src) {
        for (int half = 0; half < kWidth / kChunk; half++) {
            const int c = static_cast<int>(page) * 2 + half;
            uint8_t*  dst = &copy_[c * kChunk];
            if (std::memcmp(dst, src + half * kChunk, kChunk) == 0) continue;
            std::memcpy(dst, src + half * kChunk, kChunk);
            changed_ |= static_cast<uint8_t>(1u << c);
        }
    }

    // A page that just connected has an empty screen.
    static void All() { changed_ = 0xFF; }

    // Writes one frame into f (kFrameBytes) and returns its length, 0 when
    // nothing changed.
    static int Frame(uint8_t* f) {
        if (changed_ == 0) return 0;
        int c = next_;
        while (!(changed_ & (1u << c))) c = (c + 1) % kChunks;
        changed_ &= static_cast<uint8_t>(~(1u << c));
        next_ = static_cast<uint8_t>((c + 1) % kChunks);

        const uint8_t* src = &copy_[c * kChunk];
        const uint8_t  head[7] = { 0xF0, 0x7D, 0x53, 0x4F, 0x01,
                                   static_cast<uint8_t>(c / 2),
                                   static_cast<uint8_t>((c % 2) * kChunk) };
        std::memcpy(f, head, sizeof(head));
        int n = sizeof(head);
        for (int i = 0; i < kChunk; i += 7) {
            uint8_t& top = f[n++];
            top = 0;
            for (int j = 0; j < 7 && i + j < kChunk; j++) {
                top |= static_cast<uint8_t>((src[i + j] >> 7) << j);
                f[n++] = src[i + j] & 0x7F;
            }
        }
        f[n++] = 0xF7;
        return n;
    }

private:
    static inline uint8_t copy_[kPages * kWidth] = {};
    static inline uint8_t changed_ = 0;
    static inline uint8_t next_    = 0;
};

} // namespace synthux
