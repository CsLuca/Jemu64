#include "drive_via_domain.hpp"

namespace drive1541_physical {

DriveViaDomain::DriveViaDomain() noexcept
    : via1Active_(&via1_), via2Active_(&via2_) {}

DriveViaDomain::DriveViaDomain(const DriveViaDomain &other) noexcept
    : via1_(other.via1_), via2_(other.via2_), via1Active_(&via1_), via2Active_(&via2_) {}

DriveViaDomain &DriveViaDomain::operator=(const DriveViaDomain &other) noexcept {
    if (this == &other) {
        return *this;
    }
    via1_ = other.via1_;
    via2_ = other.via2_;
    via1Active_ = &via1_;
    via2Active_ = &via2_;
    return *this;
}

DriveViaDomain::DriveViaDomain(DriveViaDomain &&other) noexcept
    : via1_(other.via1_), via2_(other.via2_), via1Active_(&via1_), via2Active_(&via2_) {
    other.via1Active_ = &other.via1_;
    other.via2Active_ = &other.via2_;
}

DriveViaDomain &DriveViaDomain::operator=(DriveViaDomain &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    via1_ = other.via1_;
    via2_ = other.via2_;
    via1Active_ = &via1_;
    via2Active_ = &via2_;
    other.via1Active_ = &other.via1_;
    other.via2Active_ = &other.via2_;
    return *this;
}

void DriveViaDomain::Via6522::reset() noexcept {
    regs.fill(0);
    timer1Counter = 0xFFFFu;
    timer1Latch = 0xFFFFu;
    timer1Running = false;
    timer1Continuous = true;
    timer2Counter = 0xFFFFu;
    timer2Latch = 0xFFFFu;
    timer2Running = false;
    serialShiftReg = 0;
    serialShiftBitsRemaining = 0;
    serialShiftActive = false;
    serialShiftEdgeCount = 0;
    ifr = 0;
    ier = 0;
    sync_regs_();
}

std::uint8_t DriveViaDomain::Via6522::read_reg(std::uint16_t addr) const noexcept {
    const std::uint8_t reg = static_cast<std::uint8_t>(addr & 0x0Fu);
    switch (reg) {
        case 0x04:
            return static_cast<std::uint8_t>(timer1Counter & 0x00FFu);
        case 0x05:
            return static_cast<std::uint8_t>((timer1Counter >> 8) & 0x00FFu);
        case 0x08:
            return static_cast<std::uint8_t>(timer2Counter & 0x00FFu);
        case 0x09:
            return static_cast<std::uint8_t>((timer2Counter >> 8) & 0x00FFu);
        case 0x0D: {
            std::uint8_t value = static_cast<std::uint8_t>(ifr & 0x7Fu);
            if ((ifr & ier & 0x7Fu) != 0) {
                value = static_cast<std::uint8_t>(value | 0x80u);
            }
            return value;
        }
        case 0x0E:
            return ier;
        default:
            return regs[reg];
    }
}

void DriveViaDomain::Via6522::write_reg(std::uint16_t addr, std::uint8_t value) noexcept {
    const std::uint8_t reg = static_cast<std::uint8_t>(addr & 0x0Fu);
    regs[reg] = value;
    switch (reg) {
        case 0x04:
            timer1Latch = static_cast<std::uint16_t>((timer1Latch & 0xFF00u) | value);
            break;
        case 0x05:
            timer1Latch = static_cast<std::uint16_t>((timer1Latch & 0x00FFu) | (static_cast<std::uint16_t>(value) << 8));
            timer1Counter = timer1Latch;
            timer1Running = true;
            ifr = static_cast<std::uint8_t>(ifr & static_cast<std::uint8_t>(~0x40u));
            break;
        case 0x08:
            timer2Latch = static_cast<std::uint16_t>((timer2Latch & 0xFF00u) | value);
            break;
        case 0x09:
            timer2Latch = static_cast<std::uint16_t>((timer2Latch & 0x00FFu) | (static_cast<std::uint16_t>(value) << 8));
            timer2Counter = timer2Latch;
            timer2Running = true;
            ifr = static_cast<std::uint8_t>(ifr & static_cast<std::uint8_t>(~0x20u));
            break;
        case 0x0B:
            timer1Continuous = (value & 0x40u) == 0;
            break;
        case 0x0A:
            serialShiftReg = value;
            serialShiftBitsRemaining = 8;
            serialShiftActive = true;
            ifr = static_cast<std::uint8_t>(ifr & static_cast<std::uint8_t>(~0x04u));
            break;
        case 0x0D:
            ifr = static_cast<std::uint8_t>(ifr & static_cast<std::uint8_t>(~(value & 0x7Fu)));
            break;
        case 0x0E:
            if ((value & 0x80u) != 0) {
                ier = static_cast<std::uint8_t>(ier | (value & 0x7Fu));
            } else {
                ier = static_cast<std::uint8_t>(ier & static_cast<std::uint8_t>(~(value & 0x7Fu)));
            }
            break;
        default:
            break;
    }
    sync_regs_();
}

void DriveViaDomain::Via6522::tick(std::uint32_t cycles) noexcept {
    for (std::uint32_t i = 0; i < cycles; ++i) {
        if (timer1Running) {
            if (timer1Counter == 0u) {
                ifr = static_cast<std::uint8_t>(ifr | 0x40u);
                if (timer1Continuous) {
                    timer1Counter = timer1Latch;
                } else {
                    timer1Running = false;
                }
            } else {
                timer1Counter = static_cast<std::uint16_t>(timer1Counter - 1u);
            }
        }

        if (timer2Running) {
            if (timer2Counter == 0u) {
                ifr = static_cast<std::uint8_t>(ifr | 0x20u);
                timer2Running = false;
            } else {
                timer2Counter = static_cast<std::uint16_t>(timer2Counter - 1u);
            }
        }

        const bool shiftEnabled = ((regs[0x0B] & 0x1Cu) != 0);
        if (shiftEnabled && serialShiftActive && serialShiftBitsRemaining > 0) {
            serialShiftReg = static_cast<std::uint8_t>((serialShiftReg << 1) & 0xFEu);
            serialShiftBitsRemaining = static_cast<std::uint8_t>(serialShiftBitsRemaining - 1u);
            serialShiftEdgeCount++;
            if (serialShiftBitsRemaining == 0) {
                serialShiftActive = false;
                ifr = static_cast<std::uint8_t>(ifr | 0x04u);
            }
        }
    }
    sync_regs_();
}

bool DriveViaDomain::Via6522::irq_asserted() const noexcept {
    return (ifr & ier & 0x7Fu) != 0;
}

void DriveViaDomain::Via6522::sync_regs_() noexcept {
    regs[0x04] = static_cast<std::uint8_t>(timer1Counter & 0x00FFu);
    regs[0x05] = static_cast<std::uint8_t>((timer1Counter >> 8) & 0x00FFu);
    regs[0x08] = static_cast<std::uint8_t>(timer2Counter & 0x00FFu);
    regs[0x09] = static_cast<std::uint8_t>((timer2Counter >> 8) & 0x00FFu);
    regs[0x0A] = serialShiftReg;
    regs[0x0D] = ifr;
    regs[0x0E] = ier;
}

void DriveViaDomain::bind_external(Via6522 *via1, Via6522 *via2) noexcept {
    via1Active_ = via1 ? via1 : &via1_;
    via2Active_ = via2 ? via2 : &via2_;
}

DriveViaDomain::Via6522 &DriveViaDomain::via1_ref_() noexcept {
    return *via1Active_;
}

DriveViaDomain::Via6522 &DriveViaDomain::via2_ref_() noexcept {
    return *via2Active_;
}

const DriveViaDomain::Via6522 &DriveViaDomain::via1_ref_() const noexcept {
    return *via1Active_;
}

const DriveViaDomain::Via6522 &DriveViaDomain::via2_ref_() const noexcept {
    return *via2Active_;
}

void DriveViaDomain::reset() noexcept {
    via1_ref_().reset();
    via2_ref_().reset();
}

std::uint8_t DriveViaDomain::read_io(std::uint16_t addr) const noexcept {
    const std::uint16_t window = static_cast<std::uint16_t>(addr & 0xFFF0u);
    if (window == 0x1800u) {
        return via1_ref_().read_reg(addr);
    }
    if (window == 0x1C00u) {
        return via2_ref_().read_reg(addr);
    }
    return static_cast<std::uint8_t>(0xFFu);
}

void DriveViaDomain::write_io(std::uint16_t addr, std::uint8_t value) noexcept {
    const std::uint16_t window = static_cast<std::uint16_t>(addr & 0xFFF0u);
    if (window == 0x1800u) {
        via1_ref_().write_reg(addr, value);
        return;
    }
    if (window == 0x1C00u) {
        via2_ref_().write_reg(addr, value);
    }
}

void DriveViaDomain::tick(std::uint32_t cycles) noexcept {
    via1_ref_().tick(cycles);
    via2_ref_().tick(cycles);
}

bool DriveViaDomain::irq_asserted() const noexcept {
    return via1_ref_().irq_asserted() || via2_ref_().irq_asserted();
}

} // namespace drive1541_physical
