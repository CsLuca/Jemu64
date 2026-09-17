#include "write_surface_model.hpp"

namespace jemu::drive1541 {

void WriteSurfaceModel::reset(std::uint32_t seed) noexcept {
    seed_ = (seed == 0u) ? 0x1541u : seed;
    trackTag_ = 0;
    lastSplice_ = 0;
    lastEraseBand_ = 0;
}

void WriteSurfaceModel::begin_track(std::uint8_t track, std::uint8_t sector) noexcept {
    trackTag_ = static_cast<std::uint16_t>((static_cast<std::uint16_t>(track) << 8) | sector);
}

void WriteSurfaceModel::apply_write_pass(std::array<std::uint8_t, 256> &buffer,
                                         std::uint8_t revision,
                                         std::uint8_t passIndex) noexcept {
    if (passIndex == 0u) {
        return;
    }

    const std::uint8_t revBias = static_cast<std::uint8_t>((revision + 1u) * 3u);
    const std::uint8_t splice = static_cast<std::uint8_t>((next_rand_() + trackTag_ + revBias + passIndex) & 0x1Fu);
    const std::uint8_t eraseBand = static_cast<std::uint8_t>((next_rand_() + (trackTag_ >> 1) + passIndex * 5u) & 0x0Fu);

    lastSplice_ = splice;
    lastEraseBand_ = eraseBand;

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const std::uint8_t pos = static_cast<std::uint8_t>(i & 0xFFu);
        if ((pos % 32u) == splice) {
            buffer[i] = static_cast<std::uint8_t>(buffer[i] ^ static_cast<std::uint8_t>(0x11u + revBias));
        }
        if ((pos % 17u) == eraseBand) {
            const std::uint8_t low = static_cast<std::uint8_t>((buffer[i] & 0x0Fu) + passIndex + (revBias & 0x03u));
            buffer[i] = static_cast<std::uint8_t>((buffer[i] & 0xF0u) | (low & 0x0Fu));
        }
        if ((pos % 41u) == static_cast<std::uint8_t>((trackTag_ + passIndex) & 0x1Fu)) {
            buffer[i] = static_cast<std::uint8_t>((buffer[i] << 1) | (buffer[i] >> 7));
        }
    }
}

std::uint32_t WriteSurfaceModel::next_rand_() noexcept {
    std::uint32_t x = seed_;
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    seed_ = x;
    return x;
}

} // namespace jemu::drive1541
