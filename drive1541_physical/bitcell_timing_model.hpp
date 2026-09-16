#pragma once

#include <cstdint>

namespace jemu::drive1541 {

class BitcellTimingModel {
public:
    void reset(std::uint32_t seed = 0x1541u) noexcept;
    void set_zone(std::uint8_t zone) noexcept;
    std::uint32_t next_cell_ticks() noexcept;

private:
    std::uint8_t zone_{0};
    std::uint32_t rng_{0x1541u};
    std::uint8_t relockState_{0};

    std::uint32_t base_ticks_for_zone_(std::uint8_t z) const noexcept;
    std::int32_t bounded_jitter_() noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using BitcellTimingModel = jemu::drive1541::BitcellTimingModel;

} // namespace drive1541_physical
