#pragma once

#include <cstdint>

namespace jemu::drive1541 {

class MechanicsModel {
public:
    void reset() noexcept;
    void set_motor_on(bool on) noexcept;
    void tick(std::uint64_t drive_cycles) noexcept;

    void step_in() noexcept;
    void step_out() noexcept;

    std::uint16_t half_track() const noexcept;
    double spindle_angle_norm() const noexcept;

private:
    bool motor_on_{false};
    std::uint16_t half_track_{36};
    double angle_{0.0};
};

} // namespace jemu::drive1541

namespace drive1541_physical {

using MechanicsModel = jemu::drive1541::MechanicsModel;

} // namespace drive1541_physical
