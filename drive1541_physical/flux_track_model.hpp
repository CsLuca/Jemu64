#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jemu::drive1541 {

struct FluxTransition {
    std::uint32_t delta_ticks;
};

struct WeakRegion {
    std::uint32_t start_tick;
    std::uint32_t end_tick;
};

class FluxTrackModel {
public:
    void clear() noexcept;
    void set_transitions(std::vector<FluxTransition> t);
    void set_weak_regions(std::vector<WeakRegion> w);

    bool advance(std::uint32_t ticks, std::uint32_t absolute_tick) noexcept;

private:
    std::vector<FluxTransition> transitions_{};
    std::vector<WeakRegion> weak_{};
    std::size_t cursor_{0};
    std::uint32_t acc_{0};
    std::uint32_t weakSeed_{0x1541u};
    std::uint8_t relockWindow_{0};
    std::uint8_t noEdgeStreak_{0};

    bool in_weak_region_(std::uint32_t absolute_tick) const noexcept;
    std::uint32_t next_weak_noise_(std::uint32_t absolute_tick) noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using FluxTransition = jemu::drive1541::FluxTransition;
using WeakRegion = jemu::drive1541::WeakRegion;
using FluxTrackModel = jemu::drive1541::FluxTrackModel;

} // namespace drive1541_physical
