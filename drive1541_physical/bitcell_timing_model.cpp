#include "bitcell_timing_model.hpp"

namespace jemu::drive1541 {

void BitcellTimingModel::reset(std::uint32_t seed) noexcept {
    rng_ = (seed == 0u) ? 0x1541u : seed;
    zone_ = 0;
    relockState_ = 0;
    pll_.reset(rng_ ^ 0x4C340E5Du);
    lastPll_ = ReadChannelPllSample{};
}

void BitcellTimingModel::set_zone(std::uint8_t zone) noexcept {
    zone_ = static_cast<std::uint8_t>(zone & 0x03u);
    pll_.set_zone(zone_);
}

void BitcellTimingModel::set_drive_revision(std::uint8_t rev) noexcept {
    pll_.set_drive_revision(rev);
}

void BitcellTimingModel::set_level4_enabled(bool enabled) noexcept {
    level4Enabled_ = enabled;
}

void BitcellTimingModel::set_level4_calibration(std::uint16_t phaseNoiseFloorPpm,
                                                std::uint16_t jitterScalePpm,
                                                std::uint16_t lockGainPpm) noexcept {
    pll_.set_calibration(phaseNoiseFloorPpm, jitterScalePpm, lockGainPpm);
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

    if (level4Enabled_) {
        lastPll_ = pll_.advance(static_cast<std::uint32_t>(ticks < 1 ? 1 : ticks));
        if (!lastPll_.locked) {
            ticks += 1;
        }
        const std::int32_t ppmAdj = static_cast<std::int32_t>(lastPll_.jitter_window_ppm / 40u);
        ticks += ppmAdj;
    }

    if (ticks < 1) {
        ticks = 1;
    }
    return static_cast<std::uint32_t>(ticks);
}

ReadChannelPllSample BitcellTimingModel::last_pll_sample() const noexcept {
    return lastPll_;
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
