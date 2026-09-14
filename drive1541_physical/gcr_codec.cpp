#include "gcr_codec.hpp"

#include <array>

namespace jemu::drive1541 {

namespace {

static constexpr std::array<std::uint8_t, 16> kNibbleToGcr = {
    0x0A, 0x0B, 0x12, 0x13,
    0x0E, 0x0F, 0x16, 0x17,
    0x09, 0x19, 0x1A, 0x1B,
    0x0D, 0x1D, 0x1E, 0x15
};

static constexpr std::array<std::uint8_t, 32> kGcrToNibble = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0x08,0x00,0x01,0xFF,0x0C,0x04,0x05,
    0xFF,0xFF,0x02,0x03,0xFF,0x0F,0x06,0x07,
    0xFF,0x09,0x0A,0x0B,0xFF,0x0D,0x0E,0xFF
};

} // namespace

std::vector<std::uint8_t> GcrCodec::encode_4to5(const std::uint8_t *src, std::size_t n) const {
    std::vector<std::uint8_t> out;
    if (src == nullptr || n == 0) {
        return out;
    }

    out.reserve(n * 2u);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b = src[i];
        const std::uint8_t hi = static_cast<std::uint8_t>((b >> 4) & 0x0Fu);
        const std::uint8_t lo = static_cast<std::uint8_t>(b & 0x0Fu);
        out.push_back(kNibbleToGcr[hi]);
        out.push_back(kNibbleToGcr[lo]);
    }
    return out;
}

GcrDecodeResult GcrCodec::decode_5to4(const std::uint8_t *src, std::size_t n) const {
    GcrDecodeResult result;
    if (src == nullptr || n == 0) {
        return result;
    }

    result.sync_found = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (is_sync_mark(src[i])) {
            result.sync_found = true;
            continue;
        }

        if ((i + 1u) >= n) {
            result.ok = false;
            result.data.clear();
            return result;
        }

        if (is_sync_mark(src[i + 1u])) {
            result.ok = false;
            result.data.clear();
            return result;
        }

        const std::uint8_t symHi = static_cast<std::uint8_t>(src[i] & 0x1Fu);
        const std::uint8_t symLo = static_cast<std::uint8_t>(src[i + 1u] & 0x1Fu);
        const std::uint8_t hi = kGcrToNibble[symHi];
        const std::uint8_t lo = kGcrToNibble[symLo];
        if (hi == 0xFFu || lo == 0xFFu) {
            result.ok = false;
            result.data.clear();
            return result;
        }

        result.data.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        i += 1u;
    }

    result.ok = true;
    return result;
}

bool GcrCodec::is_sync_mark(std::uint8_t b) noexcept {
    return b == 0xFFu;
}

} // namespace jemu::drive1541
