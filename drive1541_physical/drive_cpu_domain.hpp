#pragma once

#include <cstdint>
#include <functional>

namespace drive1541_physical {

class DriveCpuDomain {
public:
    using ReadFn = std::function<std::uint8_t(std::uint16_t)>;
    using WriteFn = std::function<void(std::uint16_t, std::uint8_t)>;

    void reset() noexcept {
        stepCount_ = 0;
        lastOpcode_ = 0;
        if (read_) {
            const std::uint8_t lo = read_(0xFFFCu);
            const std::uint8_t hi = read_(0xFFFDu);
            pc_ = static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(hi) << 8));
        } else {
            pc_ = 0;
        }
    }

    void bind_bus(ReadFn rd, WriteFn wr) {
        read_ = std::move(rd);
        write_ = std::move(wr);
    }

    void set_irq(bool level) noexcept {
        irq_ = level;
    }

    void set_nmi(bool level) noexcept {
        nmi_ = level;
    }

    void step_one_cycle() noexcept {
        if (!read_) {
            return;
        }

        lastOpcode_ = read_(pc_);
        if (write_) {
            write_(static_cast<std::uint16_t>(0x0002u + (stepCount_ & 0x0001u)), lastOpcode_);
        }

        if (nmi_) {
            pc_ = static_cast<std::uint16_t>(pc_ + 2u);
        } else {
            pc_ = static_cast<std::uint16_t>(pc_ + 1u);
        }

        stepCount_++;
    }

    std::uint16_t pc() const noexcept { return pc_; }
    std::uint8_t last_opcode() const noexcept { return lastOpcode_; }
    std::uint64_t steps() const noexcept { return stepCount_; }

    // Existing camelCase helpers kept for compatibility with current scaffold call-sites.
    void stepOneCycle() noexcept { step_one_cycle(); }
    void setIrq(bool level) noexcept { set_irq(level); }
    void setNmi(bool level) noexcept { set_nmi(level); }

private:
    ReadFn read_{};
    WriteFn write_{};
    bool irq_{false};
    bool nmi_{false};

    std::uint16_t pc_{0};
    std::uint8_t lastOpcode_{0};
    std::uint64_t stepCount_{0};
};

} // namespace drive1541_physical
