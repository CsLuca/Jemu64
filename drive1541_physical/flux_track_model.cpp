#include "flux_track_model.hpp"

#include <algorithm>

namespace jemu::drive1541 {

void FluxTrackModel::clear() noexcept {
    transitions_.clear();
    weak_.clear();
    cursor_ = 0;
    acc_ = 0;
    weakSeed_ = 0x1541u;
    relockWindow_ = 0;
    noEdgeStreak_ = 0;
}

void FluxTrackModel::set_transitions(std::vector<FluxTransition> t) {
    transitions_ = std::move(t);
    if (transitions_.empty()) {
        cursor_ = 0;
        acc_ = 0;
        return;
    }
    for (auto &tr : transitions_) {
        if (tr.delta_ticks == 0u) {
            tr.delta_ticks = 1u;
        }
    }
    cursor_ = 0;
    acc_ = 0;
    weakSeed_ = static_cast<std::uint32_t>(0x1541u + static_cast<std::uint32_t>(transitions_.size() * 17u));
    relockWindow_ = 0;
    noEdgeStreak_ = 0;
}

void FluxTrackModel::set_weak_regions(std::vector<WeakRegion> w) {
    weak_ = std::move(w);
    for (auto &r : weak_) {
        if (r.end_tick < r.start_tick) {
            std::swap(r.start_tick, r.end_tick);
        }
    }
}

bool FluxTrackModel::in_weak_region_(std::uint32_t absolute_tick) const noexcept {
    for (const auto &r : weak_) {
        if (absolute_tick >= r.start_tick && absolute_tick <= r.end_tick) {
            return true;
        }
    }
    return false;
}

std::uint32_t FluxTrackModel::next_weak_noise_(std::uint32_t absolute_tick) noexcept {
    std::uint32_t x = weakSeed_ ^ (absolute_tick * 0x9E3779B9u);
    x ^= (x << 13);
    x ^= (x >> 17);
    x ^= (x << 5);
    weakSeed_ = x;
    return x;
}

bool FluxTrackModel::advance(std::uint32_t ticks, std::uint32_t absolute_tick) noexcept {
    if (transitions_.empty() || ticks == 0u) {
        return false;
    }

    const bool inWeak = in_weak_region_(absolute_tick);

    acc_ = static_cast<std::uint32_t>(acc_ + ticks);
    const std::uint32_t edgeDelta = transitions_[cursor_].delta_ticks;
    bool edge = false;

    if (acc_ >= edgeDelta) {
        acc_ = static_cast<std::uint32_t>(acc_ - edgeDelta);
        cursor_ = (cursor_ + 1u) % transitions_.size();
        edge = true;
        noEdgeStreak_ = 0;
    } else {
        if (noEdgeStreak_ < 0xFFu) {
            noEdgeStreak_ = static_cast<std::uint8_t>(noEdgeStreak_ + 1u);
        }
    }

    if (!inWeak) {
        if (relockWindow_ > 0u) {
            relockWindow_ = static_cast<std::uint8_t>(relockWindow_ - 1u);
        }
        return edge;
    }

    const std::uint32_t noise = next_weak_noise_(absolute_tick);
    const bool jitterFlip = ((noise & 0x07u) == 0u);
    const bool forcedPulse = ((noise & 0x1Fu) == 0x03u);

    if (!edge && noEdgeStreak_ >= 3u) {
        relockWindow_ = static_cast<std::uint8_t>(4u + (noise & 0x03u));
    }

    if (relockWindow_ > 0u) {
        relockWindow_ = static_cast<std::uint8_t>(relockWindow_ - 1u);
        if (forcedPulse || ((noise & 0x03u) == 1u)) {
            return true;
        }
        return edge;
    }

    if (jitterFlip) {
        return !edge;
    }
    if (forcedPulse) {
        return true;
    }
    return edge;
}

} // namespace jemu::drive1541
