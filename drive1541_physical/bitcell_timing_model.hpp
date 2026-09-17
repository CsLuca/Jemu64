#pragma once

#include <cstdint>

#include "read_channel_pll.hpp"

namespace jemu::drive1541 {

class BitcellTimingModel {
public:
    void reset(std::uint32_t seed = 0x1541u) noexcept;
    void set_zone(std::uint8_t zone) noexcept;
    void set_drive_revision(std::uint8_t rev) noexcept;
    void set_level4_enabled(bool enabled) noexcept;
    void set_level4_calibration(std::uint16_t phaseNoiseFloorPpm,
                                std::uint16_t jitterScalePpm,
                                std::uint16_t lockGainPpm) noexcept;
    std::uint32_t next_cell_ticks() noexcept;
    ReadChannelPllSample last_pll_sample() const noexcept;

private:
    std::uint8_t zone_{0};
    std::uint32_t rng_{0x1541u};
    std::uint8_t relockState_{0};
    bool level4Enabled_{false};
    ReadChannelPllModel pll_{};
    ReadChannelPllSample lastPll_{};

    std::uint32_t base_ticks_for_zone_(std::uint8_t z) const noexcept;
    std::int32_t bounded_jitter_() noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using BitcellTimingModel = jemu::drive1541::BitcellTimingModel;

} // namespace drive1541_physical
