#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jemu::drive1541 {

struct GcrDecodeResult {
    bool ok{false};
    bool sync_found{false};
    bool relock_applied{false};
    std::uint32_t sync_loss_events{0};
    std::uint32_t invalid_symbol_events{0};
    std::vector<std::uint8_t> data{};
};

class GcrCodec {
public:
    std::vector<std::uint8_t> encode_4to5(const std::uint8_t *src, std::size_t n) const;
    GcrDecodeResult decode_5to4(const std::uint8_t *src, std::size_t n) const;
    static bool is_sync_mark(std::uint8_t b) noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using GcrDecodeResult = jemu::drive1541::GcrDecodeResult;
using GcrCodec = jemu::drive1541::GcrCodec;

} // namespace drive1541_physical
