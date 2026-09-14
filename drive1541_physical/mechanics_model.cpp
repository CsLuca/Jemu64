#include "mechanics_model.hpp"

namespace jemu::drive1541 {

void MechanicsModel::reset() noexcept {
    motor_on_ = false;
    half_track_ = 36;
    angle_ = 0.0;
}

void MechanicsModel::set_motor_on(bool on) noexcept {
    motor_on_ = on;
}

void MechanicsModel::tick(std::uint64_t drive_cycles) noexcept {
    if (!motor_on_ || drive_cycles == 0u) {
        return;
    }

    // 1541-like spindle approximation: ~300 RPM, mapped to drive cycles.
    // Keep deterministic and lightweight for level3 scaffold.
    constexpr double kCyclesPerRevolution = 200000.0;
    const double delta = static_cast<double>(drive_cycles) / kCyclesPerRevolution;
    angle_ += delta;
    while (angle_ >= 1.0) {
        angle_ -= 1.0;
    }
    while (angle_ < 0.0) {
        angle_ += 1.0;
    }
}

void MechanicsModel::step_in() noexcept {
    if (half_track_ < 84u) {
        half_track_ = static_cast<std::uint16_t>(half_track_ + 1u);
    }
}

void MechanicsModel::step_out() noexcept {
    if (half_track_ > 2u) {
        half_track_ = static_cast<std::uint16_t>(half_track_ - 1u);
    }
}

std::uint16_t MechanicsModel::half_track() const noexcept {
    return half_track_;
}

double MechanicsModel::spindle_angle_norm() const noexcept {
    return angle_;
}

} // namespace jemu::drive1541
