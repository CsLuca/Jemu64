#include "bitcell_timing_model.hpp"

namespace jemu::drive1541 {

void BitcellTimingModel::reset(std::uint32_t seed) noexcept {
    rng_ = (seed == 0u) ? 0x1541u : seed;
    zone_ = 0;
    relockState_ = 0;
}

void BitcellTimingModel::set_zone(std::uint8_t zone) noexcept {
    zone_ = static_cast<std::uint8_t>(zone & 0x03u);
}

std::uint32_t BitcellTimingModel::next_cell_ticks() noexcept {
    const std::uint32_t base = base_ticks_for_zone_(zone_);
    const std::int32_t jitter = bounded_jitter_();
    std::int32_t ticks = static_cast<std::int32_t>(base) + jitter;

    if ((rng_ & 0x1Fu) == 0x04u) {
        if (relockState_ < 6u) {
            relockState_ = static_cast<std::uint8_t>(relockState_ + 1u);
        }
    } else if (relockState_ > 0u) {
        relockState_ = static_cast<std::uint8_t>(relockState_ - 1u);
    }

    if (relockState_ >= 3u) {
        ticks += 1;
    }

    if ((rng_ & 0xFFu) == 0x5Au) {
        ticks += 1;
    }

    if (ticks < 1) {
        ticks = 1;
    }
    return static_cast<std::uint32_t>(ticks);
}

std::uint32_t BitcellTimingModel::base_ticks_for_zone_(std::uint8_t z) const noexcept {
    switch (z & 0x03u) {
        case 0:
            return 13u;
        case 1:
            return 14u;
        case 2:
            return 15u;
        default:
            return 16u;
    }
}

std::int32_t BitcellTimingModel::bounded_jitter_() noexcept {
    // Xorshift32 deterministic PRNG
    std::uint32_t x = rng_;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    rng_ = x;

    // bounded jitter with deterministic long-tail buckets in [-2, +2]
    const std::uint32_t bucket = (x ^ (x >> 11)) & 0x0Fu;
    if (bucket <= 1u) {
        return -2;
    }
    if (bucket <= 5u) {
        return -1;
    }
    if (bucket <= 10u) {
        return 0;
    }
    if (bucket <= 14u) {
        return 1;
    }
    return 2;
}

} // namespace jemu::drive1541
