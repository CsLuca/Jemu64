#pragma once

#include <cstdint>

namespace drive1541_physical {

class DriveDosMemoryMap {
public:
    std::uint8_t read(std::uint16_t) const noexcept { return 0xFF; }
    void write(std::uint16_t, std::uint8_t) noexcept {}
};

} // namespace drive1541_physical
