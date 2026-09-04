#pragma once

#include <array>
#include <cstdint>

class VIA6522 {
public:
    std::array<uint8_t, 16> regs = {0};

    uint16_t timer1Counter = 0xFFFF;
    uint16_t timer1Latch = 0xFFFF;
    bool timer1Running = false;
    bool timer1Continuous = true;

    uint16_t timer2Counter = 0xFFFF;
    uint16_t timer2Latch = 0xFFFF;
    bool timer2Running = false;

    uint8_t ifr = 0;
    uint8_t ier = 0;

    uint8_t read(uint16_t addr) const {
        const uint8_t reg = static_cast<uint8_t>(addr & 0x0F);
        switch (reg) {
            case 0x04: return static_cast<uint8_t>(timer1Counter & 0xFF);
            case 0x05: return static_cast<uint8_t>((timer1Counter >> 8) & 0xFF);
            case 0x08: return static_cast<uint8_t>(timer2Counter & 0xFF);
            case 0x09: return static_cast<uint8_t>((timer2Counter >> 8) & 0xFF);
            case 0x0D: {
                uint8_t v = static_cast<uint8_t>(ifr & 0x7F);
                if ((ifr & ier & 0x7F) != 0) {
                    v = static_cast<uint8_t>(v | 0x80);
                }
                return v;
            }
            case 0x0E: return ier;
            default: return regs[reg];
        }
    }

    void write(uint16_t addr, uint8_t val) {
        const uint8_t reg = static_cast<uint8_t>(addr & 0x0F);
        regs[reg] = val;
        switch (reg) {
            case 0x04:
                timer1Latch = static_cast<uint16_t>((timer1Latch & 0xFF00) | val);
                break;
            case 0x05:
                timer1Latch = static_cast<uint16_t>((timer1Latch & 0x00FF) | (uint16_t(val) << 8));
                timer1Counter = timer1Latch;
                timer1Running = true;
                ifr = static_cast<uint8_t>(ifr & static_cast<uint8_t>(~0x40));
                break;
            case 0x08:
                timer2Latch = static_cast<uint16_t>((timer2Latch & 0xFF00) | val);
                break;
            case 0x09:
                timer2Latch = static_cast<uint16_t>((timer2Latch & 0x00FF) | (uint16_t(val) << 8));
                timer2Counter = timer2Latch;
                timer2Running = true;
                ifr = static_cast<uint8_t>(ifr & static_cast<uint8_t>(~0x20));
                break;
            case 0x0B:
                timer1Continuous = (val & 0x40) == 0;
                break;
            case 0x0D:
                ifr = static_cast<uint8_t>(ifr & static_cast<uint8_t>(~(val & 0x7F)));
                break;
            case 0x0E:
                if ((val & 0x80) != 0) {
                    ier = static_cast<uint8_t>(ier | (val & 0x7F));
                } else {
                    ier = static_cast<uint8_t>(ier & static_cast<uint8_t>(~(val & 0x7F)));
                }
                break;
            default:
                break;
        }
    }

    void tick() {
        if (timer1Running) {
            if (timer1Counter == 0) {
                ifr = static_cast<uint8_t>(ifr | 0x40);
                if (timer1Continuous) {
                    timer1Counter = timer1Latch;
                } else {
                    timer1Running = false;
                }
            } else {
                timer1Counter = static_cast<uint16_t>(timer1Counter - 1);
            }
        }

        if (timer2Running) {
            if (timer2Counter == 0) {
                ifr = static_cast<uint8_t>(ifr | 0x20);
                timer2Running = false;
            } else {
                timer2Counter = static_cast<uint16_t>(timer2Counter - 1);
            }
        }

        regs[0x04] = static_cast<uint8_t>(timer1Counter & 0xFF);
        regs[0x05] = static_cast<uint8_t>((timer1Counter >> 8) & 0xFF);
        regs[0x08] = static_cast<uint8_t>(timer2Counter & 0xFF);
        regs[0x09] = static_cast<uint8_t>((timer2Counter >> 8) & 0xFF);
        regs[0x0D] = ifr;
        regs[0x0E] = ier;
    }
};
