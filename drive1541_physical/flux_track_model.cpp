#include "flux_track_model.hpp"

#include <algorithm>

namespace jemu::drive1541 {

void FluxTrackModel::clear() noexcept {
    transitions_.clear();
    weak_.clear();
    cursor_ = 0;
    acc_ = 0;
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
}

void FluxTrackModel::set_weak_regions(std::vector<WeakRegion> w) {
    weak_ = std::move(w);
    for (auto &r : weak_) {
        if (r.end_tick < r.start_tick) {
            std::swap(r.start_tick, r.end_tick);
        }
    }
}

bool FluxTrackModel::advance(std::uint32_t ticks, std::uint32_t absolute_tick) noexcept {
    if (transitions_.empty() || ticks == 0u) {
        return false;
    }

    bool inWeak = false;
    for (const auto &r : weak_) {
        if (absolute_tick >= r.start_tick && absolute_tick <= r.end_tick) {
            inWeak = true;
            break;
        }
    }

    acc_ = static_cast<std::uint32_t>(acc_ + ticks);
    const std::uint32_t edgeDelta = transitions_[cursor_].delta_ticks;
    bool edge = false;

    if (acc_ >= edgeDelta) {
        acc_ = static_cast<std::uint32_t>(acc_ - edgeDelta);
        cursor_ = (cursor_ + 1u) % transitions_.size();
        edge = true;
    }

    if (!inWeak) {
        return edge;
    }

    // Deterministic weak-bit modulation: use absolute tick parity window.
    if ((absolute_tick & 0x03u) == 0u) {
        return !edge;
    }
    if ((absolute_tick & 0x07u) == 3u) {
        return true;
    }
    return edge;
}

} // namespace jemu::drive1541
