#pragma once

namespace drive1541_physical {

class DriveCpuDomain {
public:
    void reset() noexcept {}
    void stepOneCycle() noexcept {}
    void setIrq(bool) noexcept {}
    void setNmi(bool) noexcept {}
};

} // namespace drive1541_physical
