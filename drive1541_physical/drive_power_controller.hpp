#pragma once

#include <cstdint>

namespace drive1541_physical {

enum class PowerState : std::uint8_t {
    Off = 0,
    SpinningUp = 1,
    On = 2,
    Resetting = 3,
    SpinningDown = 4
};

class DrivePowerController {
public:
    void powerOn(bool cold) noexcept {
        coldBoot_ = cold;
        state_ = PowerState::SpinningUp;
        timer_ = spinupCycles_;
    }

    void powerOff() noexcept {
        state_ = PowerState::SpinningDown;
        timer_ = spindownCycles_;
    }

    void reset() noexcept {
        state_ = PowerState::Resetting;
        timer_ = resetCycles_;
    }

    void tick(std::uint64_t cycles) noexcept {
        if (timer_ > cycles) {
            timer_ -= cycles;
            return;
        }
        if (timer_ != 0) {
            timer_ = 0;
        }
        advanceState();
    }

    PowerState state() const noexcept {
        return state_;
    }

    bool isOn() const noexcept {
        return state_ == PowerState::On;
    }

    bool coldBootRequested() const noexcept {
        return coldBoot_;
    }

    bool isTransitioning() const noexcept {
        return state_ == PowerState::SpinningUp || state_ == PowerState::SpinningDown || state_ == PowerState::Resetting;
    }

    bool isDriveOutputAllowed() const noexcept {
        return state_ == PowerState::On;
    }

private:
    void advanceState() noexcept {
        if (state_ == PowerState::SpinningUp || state_ == PowerState::Resetting) {
            state_ = PowerState::On;
        } else if (state_ == PowerState::SpinningDown) {
            state_ = PowerState::Off;
        }
    }

    PowerState state_{PowerState::On};
    std::uint64_t timer_{0};
    bool coldBoot_{true};

    static constexpr std::uint64_t spinupCycles_ = 20000;
    static constexpr std::uint64_t spindownCycles_ = 5000;
    static constexpr std::uint64_t resetCycles_ = 3000;
};

} // namespace drive1541_physical
