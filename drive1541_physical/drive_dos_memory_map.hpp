#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace drive1541_physical {

class DriveDosMemoryMap {
public:
    using IoReadFn = std::function<std::uint8_t(std::uint16_t)>;
    using IoWriteFn = std::function<void(std::uint16_t, std::uint8_t)>;

    void reset() noexcept {
        ram_.fill(0);
    }

    void set_rom(const std::uint8_t *data, std::size_t size) noexcept {
        rom_ = data;
        rom_size_ = size;
    }

    void bind_io(IoReadFn rd, IoWriteFn wr) {
        io_read_ = std::move(rd);
        io_write_ = std::move(wr);
    }

    void seed_ram(const std::uint8_t *data, std::size_t size) noexcept {
        if (data == nullptr || size == 0) {
            return;
        }
        const std::size_t n = (size < ram_.size()) ? size : ram_.size();
        for (std::size_t i = 0; i < n; ++i) {
            ram_[i] = data[i];
        }
    }

    std::uint8_t read(std::uint16_t addr) const noexcept {
        if (is_io_(addr)) {
            return io_read_ ? io_read_(addr) : static_cast<std::uint8_t>(0xFF);
        }
        if (is_rom_(addr)) {
            if (rom_ == nullptr) {
                return static_cast<std::uint8_t>(0xFF);
            }
            const std::size_t offset = static_cast<std::size_t>(addr - 0xC000u);
            if (offset < rom_size_) {
                return rom_[offset];
            }
            return static_cast<std::uint8_t>(0xFF);
        }
        if (is_ram_(addr)) {
            return ram_[addr];
        }
        return static_cast<std::uint8_t>(0xFF);
    }

    void write(std::uint16_t addr, std::uint8_t value) noexcept {
        if (is_io_(addr)) {
            if (io_write_) {
                io_write_(addr, value);
            }
            return;
        }
        if (is_rom_(addr)) {
            return;
        }
        if (is_ram_(addr)) {
            ram_[addr] = value;
        }
    }

private:
    std::array<std::uint8_t, 0xC000> ram_{};
    const std::uint8_t *rom_{nullptr};
    std::size_t rom_size_{0};
    IoReadFn io_read_{};
    IoWriteFn io_write_{};

    bool is_ram_(std::uint16_t a) const noexcept {
        return a < 0xC000u;
    }

    bool is_io_(std::uint16_t a) const noexcept {
        return ((a & 0xFFF0u) == 0x1800u) || ((a & 0xFFF0u) == 0x1C00u);
    }

    bool is_rom_(std::uint16_t a) const noexcept {
        return a >= 0xC000u;
    }
};

} // namespace drive1541_physical
