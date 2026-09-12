#pragma once

#include <cstdint>

namespace drive1541_physical {

class DriveScheduler {
public:
    void reset() noexcept {
        driveCycles_ = 0;
    }

    void tickHostCycles(std::uint64_t hostCycles) noexcept {
        driveCycles_ += hostCycles;
    }

    std::uint64_t driveCycles() const noexcept {
        return driveCycles_;
    }

private:
    std::uint64_t driveCycles_ = 0;
};

} // namespace drive1541_physical
