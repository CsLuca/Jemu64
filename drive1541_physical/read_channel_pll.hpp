#pragma once

#include <cstdint>

namespace jemu::drive1541 {

struct ReadChannelPllSample {
    std::uint32_t lock_time = 0;
    std::uint32_t phase_noise_rms_ppm = 0;
    std::uint32_t jitter_window_ppm = 0;
    bool locked = false;
};

class ReadChannelPllModel {
public:
    void reset(std::uint32_t seed = 0x15414A17u) noexcept;
    void set_zone(std::uint8_t zone) noexcept;
    void set_drive_revision(std::uint8_t rev) noexcept;
    void set_calibration(std::uint16_t phase_noise_floor_ppm,
                         std::uint16_t jitter_scale_ppm,
                         std::uint16_t lock_gain_ppm) noexcept;

    ReadChannelPllSample advance(std::uint32_t cell_ticks) noexcept;

private:
    std::uint8_t zone_{0};
    std::uint8_t revision_{0};
    std::uint32_t rng_{0x15414A17u};
    std::uint32_t lockTime_{0};
    std::int32_t phaseErrPpm_{0};
    std::uint16_t phaseNoiseFloorPpm_{22};
    std::uint16_t jitterScalePpm_{30};
    std::uint16_t lockGainPpm_{250};

    std::int32_t next_noise_ppm_() noexcept;
    std::uint16_t zone_base_noise_ppm_() const noexcept;
    std::uint16_t revision_slew_ppm_() const noexcept;
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using ReadChannelPllSample = jemu::drive1541::ReadChannelPllSample;
using ReadChannelPllModel = jemu::drive1541::ReadChannelPllModel;

} // namespace drive1541_physical
