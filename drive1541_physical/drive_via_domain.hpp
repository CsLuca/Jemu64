#pragma once

#include <array>
#include <cstdint>

namespace drive1541_physical {

class DriveViaDomain {
public:
    class Via6522 {
    public:
        std::array<std::uint8_t, 16> regs{};

        std::uint16_t timer1Counter{0xFFFFu};
        std::uint16_t timer1Latch{0xFFFFu};
        bool timer1Running{false};
        bool timer1Continuous{true};

        std::uint16_t timer2Counter{0xFFFFu};
        std::uint16_t timer2Latch{0xFFFFu};
        bool timer2Running{false};

        std::uint8_t serialShiftReg{0};
        std::uint8_t serialShiftBitsRemaining{0};
        bool serialShiftActive{false};
        std::uint64_t serialShiftEdgeCount{0};

        std::uint8_t ifr{0};
        std::uint8_t ier{0};

        void reset() noexcept;
        std::uint8_t read_reg(std::uint16_t addr) const noexcept;
        void write_reg(std::uint16_t addr, std::uint8_t value) noexcept;
        void tick(std::uint32_t cycles) noexcept;
        bool irq_asserted() const noexcept;

        std::uint8_t read(std::uint16_t addr) const noexcept { return read_reg(addr); }
        void write(std::uint16_t addr, std::uint8_t value) noexcept { write_reg(addr, value); }
        void tick() noexcept { tick(1); }

    private:
        void sync_regs_() noexcept;
    };

    DriveViaDomain() noexcept;
    DriveViaDomain(const DriveViaDomain &other) noexcept;
    DriveViaDomain &operator=(const DriveViaDomain &other) noexcept;
    DriveViaDomain(DriveViaDomain &&other) noexcept;
    DriveViaDomain &operator=(DriveViaDomain &&other) noexcept;

    void bind_external(Via6522 *via1, Via6522 *via2) noexcept;

    void reset() noexcept;
    std::uint8_t read_io(std::uint16_t addr) const noexcept;
    void write_io(std::uint16_t addr, std::uint8_t value) noexcept;
    void tick(std::uint32_t cycles) noexcept;
    bool irq_asserted() const noexcept;

    bool irqAsserted() const noexcept { return irq_asserted(); }

private:
    Via6522 &via1_ref_() noexcept;
    Via6522 &via2_ref_() noexcept;
    const Via6522 &via1_ref_() const noexcept;
    const Via6522 &via2_ref_() const noexcept;

    Via6522 via1_{};
    Via6522 via2_{};
    Via6522 *via1Active_{nullptr};
    Via6522 *via2Active_{nullptr};
};

} // namespace drive1541_physical
