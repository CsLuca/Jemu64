#pragma once

#include <algorithm>
#include <cstdint>

#include "drive_power_controller.hpp"

namespace drive1541_physical {

struct DriveSignalState {
    bool motor_on = false;
    bool activity = false;
    bool error = false;
    bool iec_load = false;
};

class DriveSignalModel {
public:
    void reset() noexcept {
        state_ = DriveSignalState{};
        activityTicks_ = 0;
        errorHoldTicks_ = 0;
        iecWindowTick_ = 0;
        iecEdgeCountWindow_ = 0;
    }

    void begin_tick(PowerState powerState) noexcept {
        const bool powered = (powerState == PowerState::On || powerState == PowerState::SpinningUp || powerState == PowerState::Resetting);
        state_.motor_on = powered;

        if (activityTicks_ > 0) {
            activityTicks_--;
        }
        state_.activity = powered && activityTicks_ > 0;

        if (errorHoldTicks_ > 0) {
            errorHoldTicks_--;
            state_.error = powered;
        } else {
            state_.error = false;
        }

        iecWindowTick_++;
        if (iecWindowTick_ >= kIecWindowTicks) {
            const std::uint32_t edgeThreshold = 8;
            state_.iec_load = powered && iecEdgeCountWindow_ >= edgeThreshold;
            iecEdgeCountWindow_ = 0;
            iecWindowTick_ = 0;
        } else if (!powered) {
            state_.iec_load = false;
            iecEdgeCountWindow_ = 0;
            iecWindowTick_ = 0;
        }
    }

    void note_flux_read() noexcept {
        activityTicks_ = std::max<std::uint32_t>(activityTicks_, kActivityPulseTicks);
    }

    void note_flux_write() noexcept {
        activityTicks_ = std::max<std::uint32_t>(activityTicks_, kActivityPulseTicks + 8);
    }

    void note_dos_busy() noexcept {
        activityTicks_ = std::max<std::uint32_t>(activityTicks_, kActivityPulseTicks / 2);
    }

    void note_status_line(const char *status) noexcept {
        if (status == nullptr || status[0] == '\0') {
            return;
        }
        const bool hasError = !(status[0] == '0' && status[1] == '0');
        if (hasError) {
            errorHoldTicks_ = kErrorHoldTicks;
            state_.error = true;
        }
    }

    void note_iec_edges(std::size_t edgeCount) noexcept {
        iecEdgeCountWindow_ = static_cast<std::uint32_t>(iecEdgeCountWindow_ + static_cast<std::uint32_t>(edgeCount));
    }

    DriveSignalState state() const noexcept {
        return state_;
    }

private:
    static constexpr std::uint32_t kActivityPulseTicks = 24;
    static constexpr std::uint32_t kErrorHoldTicks = 96;
    static constexpr std::uint32_t kIecWindowTicks = 32;

    DriveSignalState state_{};
    std::uint32_t activityTicks_ = 0;
    std::uint32_t errorHoldTicks_ = 0;
    std::uint32_t iecWindowTick_ = 0;
    std::uint32_t iecEdgeCountWindow_ = 0;
};

} // namespace drive1541_physical
