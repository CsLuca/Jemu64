#pragma once

#include <cstdint>

namespace drive1541_physical {

class DriveViaDomain {
public:
    void reset() noexcept {}
    void tick() noexcept {}
    std::uint8_t readReg(std::uint8_t) const noexcept { return 0; }
    void writeReg(std::uint8_t, std::uint8_t) noexcept {}
    bool irqAsserted() const noexcept { return false; }
};

} // namespace drive1541_physical
