#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace jemu::drive1541 {

class WriteSurfaceModel {
public:
    void reset(std::uint32_t seed = 0x1541u) noexcept;
    void begin_track(std::uint8_t track, std::uint8_t sector) noexcept;
    void apply_write_pass(std::array<std::uint8_t, 256> &buffer,
                          std::uint8_t revision,
                          std::uint8_t passIndex) noexcept;

private:
    std::uint32_t seed_{0x1541u};
    std::uint16_t trackTag_{0};
    std::uint8_t lastSplice_{0};
    std::uint8_t lastEraseBand_{0};

    std::uint32_t next_rand_() noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using WriteSurfaceModel = jemu::drive1541::WriteSurfaceModel;

} // namespace drive1541_physical
