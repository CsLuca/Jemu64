#pragma once

namespace drive1541_physical {

enum class PhysicalProfile : unsigned char {
    Level1Functional = 0,
    Level2Cycle = 1,
    Level3Physical = 2,
    Level4Accuracy = 3,
    Level5Coupling = 4
};

} // namespace drive1541_physical
