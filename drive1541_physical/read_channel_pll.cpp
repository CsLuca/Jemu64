#include "read_channel_pll.hpp"

namespace jemu::drive1541 {

void ReadChannelPllModel::reset(std::uint32_t seed) noexcept {
    rng_ = (seed == 0u) ? 0x15414A17u : seed;
    zone_ = 0;
    revision_ = 0;
    lockTime_ = 0;
    phaseErrPpm_ = 0;
    phaseNoiseFloorPpm_ = 22;
    jitterScalePpm_ = 30;
    lockGainPpm_ = 250;
}

void ReadChannelPllModel::set_zone(std::uint8_t zone) noexcept {
    zone_ = static_cast<std::uint8_t>(zone & 0x03u);
}

void ReadChannelPllModel::set_drive_revision(std::uint8_t rev) noexcept {
    revision_ = rev;
}

void ReadChannelPllModel::set_calibration(std::uint16_t phase_noise_floor_ppm,
                                          std::uint16_t jitter_scale_ppm,
                                          std::uint16_t lock_gain_ppm) noexcept {
    phaseNoiseFloorPpm_ = (phase_noise_floor_ppm == 0u) ? 1u : phase_noise_floor_ppm;
    jitterScalePpm_ = (jitter_scale_ppm == 0u) ? 1u : jitter_scale_ppm;
    lockGainPpm_ = (lock_gain_ppm == 0u) ? 1u : lock_gain_ppm;
}

ReadChannelPllSample ReadChannelPllModel::advance(std::uint32_t cell_ticks) noexcept {
    const std::int32_t noise = next_noise_ppm_();
    const std::int32_t zoneNoise = static_cast<std::int32_t>(zone_base_noise_ppm_());
    const std::int32_t revSlew = static_cast<std::int32_t>(revision_slew_ppm_());
    const std::int32_t jitter = static_cast<std::int32_t>((cell_ticks & 0x07u) * jitterScalePpm_ / 8u);

    const std::int32_t driveNoise = noise + zoneNoise + revSlew + jitter;
    phaseErrPpm_ += driveNoise;

    const std::int32_t correction = static_cast<std::int32_t>((phaseErrPpm_ * static_cast<std::int32_t>(lockGainPpm_)) / 1000);
    phaseErrPpm_ -= correction;

    if (lockTime_ < 0xFFFFFFFFu) {
        lockTime_ += 1u;
    }

    const std::uint32_t absErr = static_cast<std::uint32_t>(phaseErrPpm_ < 0 ? -phaseErrPpm_ : phaseErrPpm_);
    ReadChannelPllSample sample;
    sample.lock_time = lockTime_;
    sample.phase_noise_rms_ppm = static_cast<std::uint32_t>(phaseNoiseFloorPpm_ + ((absErr + 2u) / 5u));
    sample.jitter_window_ppm = static_cast<std::uint32_t>(jitterScalePpm_ + static_cast<std::uint32_t>((noise < 0 ? -noise : noise) / 4));
    sample.locked = (absErr <= 520u);
    return sample;
}

std::int32_t ReadChannelPllModel::next_noise_ppm_() noexcept {
    std::uint32_t x = rng_;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    rng_ = x;

    const std::uint32_t bucket = (x >> 4) & 0x3Fu;
    const std::int32_t centered = static_cast<std::int32_t>(bucket) - 31;
    return static_cast<std::int32_t>(centered * static_cast<std::int32_t>(phaseNoiseFloorPpm_) / 8);
}

std::uint16_t ReadChannelPllModel::zone_base_noise_ppm_() const noexcept {
    switch (zone_ & 0x03u) {
        case 0: return 6u;
        case 1: return 11u;
        case 2: return 17u;
        default: return 23u;
    }
}

std::uint16_t ReadChannelPllModel::revision_slew_ppm_() const noexcept {
    switch (revision_) {
        case 1: return 9u;  // 1541C
        case 2: return 7u;  // 1541-II
        default: return 12u; // 1541
    }
}

} // namespace jemu::drive1541
