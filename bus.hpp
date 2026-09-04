#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "cia6526.hpp"
#include "ibus.hpp"
#include "sid.hpp"
#include "vicii.hpp"

class Bus : public IBus {
public:
    enum OpenBusRevision : uint8_t {
        OPENBUS_C64_NMOS = 0,
        OPENBUS_C64_HMOS = 1
    };

    struct OpenBusProfile {
        OpenBusRevision revision = OPENBUS_C64_NMOS;
        uint32_t decayReadThreshold = 64;
        uint8_t decayValue = 0xFF;
    };

    static constexpr OpenBusProfile makeOpenBusProfile(OpenBusRevision rev) {
        return (rev == OPENBUS_C64_HMOS)
            ? OpenBusProfile{OPENBUS_C64_HMOS, 24, 0x00}
            : OpenBusProfile{OPENBUS_C64_NMOS, 64, 0xFF};
    }

    OpenBusRevision openBusRevision = OPENBUS_C64_NMOS;
    OpenBusProfile openBusProfile = makeOpenBusProfile(OPENBUS_C64_NMOS);
    uint32_t openBusIdleReads = 0;
    bool openBusDecayEnabled = false;

    void setOpenBusRevision(OpenBusRevision rev) {
        openBusRevision = rev;
        openBusProfile = makeOpenBusProfile(rev);
        openBusIdleReads = 0;
        openBusDecayEnabled = true;
    }

    OpenBusRevision getOpenBusRevision() const {
        return openBusRevision;
    }

    const OpenBusProfile &getOpenBusProfile() const {
        return openBusProfile;
    }

    void onDrivenBusValue(uint8_t value) {
        openBusValue = value;
        openBusIdleReads = 0;
    }

    void applyOpenBusDecayIfNeeded() {
        if (!openBusDecayEnabled) {
            return;
        }
        openBusIdleReads++;
        if (openBusIdleReads >= openBusProfile.decayReadThreshold) {
            openBusValue = openBusProfile.decayValue;
        }
    }

    std::array<uint8_t, 0x10000> memory = {0};
    std::array<uint8_t, 0x2000> basicRom = {0};
    std::array<uint8_t, 0x2000> kernalRom = {0};
    std::array<uint8_t, 0x1000> charRom = {0};

    bool hasBasicRom = false;
    bool hasKernalRom = false;
    bool hasCharRom = false;

    uint8_t cpuPortDir = 0x2F;
    uint8_t cpuPortData = 0x37;

    VICII* vic = nullptr;
    CIA6526* cia1 = nullptr;
    CIA6526* cia2 = nullptr;
    SID* sid = nullptr;

    uint8_t openBusValue = 0xFF;
    std::function<void(uint16_t, uint8_t)> writeTap = nullptr;
    std::function<void(uint16_t, uint8_t)> readTap = nullptr;
    std::function<void(uint16_t)> preReadTap = nullptr;

    uint16_t addressBus = 0;
    bool lastIsWrite = false;
    uint8_t lastDataBusValue = 0xFF;
    bool enableTracing = false;
    bool flatMemoryMode = false;

    void printCIA1RegisterFriendly(uint16_t addr, uint8_t val) {
        uint8_t reg = addr & 0x0F;
        std::cout << "[CIA1] Write to $" << std::hex << addr
                  << " = $" << (int)val << std::endl;

        switch (reg) {
            case 0x00:
                std::cout << "  PRA (Port A):\n";
                std::cout << "    Keyboard columns: " << (int)(val & 0xFF) << "\n";
                std::cout << "    Joystick2 Fire: " << ((val & 0x10) ? "pressed" : "released") << "\n";
                std::cout << "    Paddle Fire: A=" << ((val >> 2) & 1) << ", B=" << ((val >> 3) & 1) << "\n";
                std::cout << "    Switch Port: " << (((val >> 6) & 1) ? "Port2" : "Port1") << "\n";
                break;

            case 0x01:
                std::cout << "  PRB (Port B):\n";
                std::cout << "    Keyboard rows: " << (int)(val & 0xFF) << "\n";
                std::cout << "    Joystick1 Fire: " << ((val & 0x10) ? "pressed" : "released") << "\n";
                std::cout << "    Timer A toggle on PB6: " << ((val & 0x40) ? "high" : "low") << "\n";
                std::cout << "    Timer B toggle on PB7: " << ((val & 0x80) ? "high" : "low") << "\n";
                break;

            case 0x02:
                std::cout << "  DDRA (Data Direction Port A): ";
                for (int i = 7; i >= 0; i--) std::cout << ((val >> i) & 1);
                std::cout << " (0=Input,1=Output)\n";
                break;

            case 0x03:
                std::cout << "  DDRB (Data Direction Port B): ";
                for (int i = 7; i >= 0; i--) std::cout << ((val >> i) & 1);
                std::cout << " (0=Input,1=Output)\n";
                break;

            case 0x04: std::cout << "  Timer A Low Byte: " << (int)val << "\n"; break;
            case 0x05: std::cout << "  Timer A High Byte: " << (int)val << "\n"; break;
            case 0x06: std::cout << "  Timer B Low Byte: " << (int)val << "\n"; break;
            case 0x07: std::cout << "  Timer B High Byte: " << (int)val << "\n"; break;

            case 0x08: std::cout << "  TOD 10THS: " << (val & 0x0F) << " tenth seconds\n"; break;
            case 0x09: std::cout << "  TOD Seconds: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F) << " units\n"; break;
            case 0x0A: std::cout << "  TOD Minutes: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F) << " units\n"; break;
            case 0x0B:
                std::cout << "  TOD Hours: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F)
                          << " units, " << (((val >> 7) & 1) ? "PM" : "AM") << "\n";
                break;

            case 0x0C: std::cout << "  Serial Shift Register (SDR) value: " << (int)val << "\n"; break;

            case 0x0D:
                std::cout << "  ICR (Interrupt Control):\n";
                std::cout << "    Timer A underflow: " << ((val & 0x01) ? "yes" : "no") << "\n";
                std::cout << "    Timer B underflow: " << ((val & 0x02) ? "yes" : "no") << "\n";
                std::cout << "    TOD alarm match: " << ((val & 0x04) ? "yes" : "no") << "\n";
                std::cout << "    SDR full/empty: " << ((val & 0x08) ? "yes" : "no") << "\n";
                std::cout << "    FLAG-pin IRQ: " << ((val & 0x10) ? "yes" : "no") << "\n";
                std::cout << "    IRQ occurred: " << ((val & 0x80) ? "yes" : "no") << "\n";
                break;

            case 0x0E: std::cout << "  CRA (Control Timer A): " << (int)val << "\n"; break;
            case 0x0F: std::cout << "  CRB (Control Timer B): " << (int)val << "\n"; break;

            default: break;
        }
    }

    void printCIA2RegisterFriendly(uint16_t addr, uint8_t val) {
        uint8_t reg = addr & 0x0F;
        std::cout << "[CIA2] Write to $" << std::hex << addr
                  << " = $" << (int)val << std::endl;

        switch (reg) {
            case 0x00: {
                std::cout << "  PRA (Port A):\n";
                std::cout << "    VIC memory bank select:\n";
                switch (val & 0x03) {
                    case 0x00: std::cout << "      -> Bank 3: $C000-$FFFF\n"; break;
                    case 0x01: std::cout << "      -> Bank 2: $8000-$BFFF\n"; break;
                    case 0x02: std::cout << "      -> Bank 1: $4000-$7FFF\n"; break;
                    case 0x03: std::cout << "      -> Bank 0: $0000-$3FFF (standard)\n"; break;
                }
                std::cout << "    RS-232 TXD (PA2): " << ((val & 0x04) ? "High" : "Low") << "\n";
                std::cout << "    Serial bus outputs:\n";
                std::cout << "      ATN OUT:   " << ((val & 0x08) ? "Active Low" : "Inactive High") << "\n";
                std::cout << "      CLOCK OUT: " << ((val & 0x10) ? "Active Low" : "Inactive High") << "\n";
                std::cout << "      DATA OUT:  " << ((val & 0x20) ? "Active Low" : "Inactive High") << "\n";
                std::cout << "    Serial bus inputs:\n";
                std::cout << "      CLOCK IN:  " << ((val & 0x40) ? "High (inactive)" : "Low (active)") << "\n";
                std::cout << "      DATA IN:   " << ((val & 0x80) ? "High (inactive)" : "Low (active)") << "\n";
                break;
            }

            case 0x01: {
                std::cout << "  PRB (Port B - User Port / RS-232):\n";
                std::cout << "    RS-232 signals:\n";
                std::cout << "      RXD (bit0): " << ((val & 0x01) ? "High" : "Low") << "\n";
                std::cout << "      RTS (bit1): " << ((val & 0x02) ? "High" : "Low") << "\n";
                std::cout << "      DTR (bit2): " << ((val & 0x04) ? "High" : "Low") << "\n";
                std::cout << "      RI  (bit3): " << ((val & 0x08) ? "High" : "Low") << "\n";
                std::cout << "      DCD (bit4): " << ((val & 0x10) ? "High" : "Low") << "\n";
                std::cout << "      CTS (bit6): " << ((val & 0x40) ? "High" : "Low") << "\n";
                std::cout << "      DSR (bit7): " << ((val & 0x80) ? "High" : "Low") << "\n";
                std::cout << "    User port bits: PB0..7 = 0x" << std::hex << (int)val << "\n";
                break;
            }

            case 0x02:
                std::cout << "  DDRA (Data Direction Port A): ";
                for (int i = 7; i >= 0; i--) std::cout << ((val >> i) & 1);
                std::cout << " (0=Input,1=Output)\n";
                break;

            case 0x03:
                std::cout << "  DDRB (Data Direction Port B): ";
                for (int i = 7; i >= 0; i--) std::cout << ((val >> i) & 1);
                std::cout << " (0=Input,1=Output)\n";
                break;

            case 0x04: std::cout << "  Timer A Low Byte: " << (int)val << "\n"; break;
            case 0x05: std::cout << "  Timer A High Byte: " << (int)val << "\n"; break;
            case 0x06: std::cout << "  Timer B Low Byte: " << (int)val << "\n"; break;
            case 0x07: std::cout << "  Timer B High Byte: " << (int)val << "\n"; break;

            case 0x08: std::cout << "  TOD 10THS: " << (val & 0x0F) << " tenth seconds\n"; break;
            case 0x09: std::cout << "  TOD Seconds: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F) << " units\n"; break;
            case 0x0A: std::cout << "  TOD Minutes: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F) << " units\n"; break;
            case 0x0B:
                std::cout << "  TOD Hours: " << ((val >> 4) & 0x07) << " tens, " << (val & 0x0F)
                          << " units, " << (((val >> 7) & 1) ? "PM" : "AM") << "\n";
                break;

            case 0x0C:
                std::cout << "  Serial Shift Register (SDR): " << (int)val << "\n";
                break;

            case 0x0D:
                std::cout << "  ICR (Interrupt Control / Status):\n";
                std::cout << "    Timer A underflow: " << ((val & 0x01) ? "yes" : "no") << "\n";
                std::cout << "    Timer B underflow: " << ((val & 0x02) ? "yes" : "no") << "\n";
                std::cout << "    TOD alarm match: " << ((val & 0x04) ? "yes" : "no") << "\n";
                std::cout << "    SDR full/empty: " << ((val & 0x08) ? "yes" : "no") << "\n";
                std::cout << "    FLAG-pin NMI (RS-232 RX): " << ((val & 0x10) ? "yes" : "no") << "\n";
                std::cout << "    NMI occurred: " << ((val & 0x80) ? "yes" : "no") << "\n";
                break;

            case 0x0E: std::cout << "  CRA (Control Timer A): " << (int)val << "\n"; break;
            case 0x0F: std::cout << "  CRB (Control Timer B): " << (int)val << "\n"; break;

            default: break;
        }
    }

    uint8_t read(uint16_t addr) {
        addressBus = addr;
        lastIsWrite = false;
        if (preReadTap) {
            preReadTap(addr);
        }

        if (flatMemoryMode) {
            uint8_t value = memory[addr];
            onDrivenBusValue(value);
            lastDataBusValue = value;
            if (readTap) {
                readTap(addr, value);
            }
            return value;
        }

        applyOpenBusDecayIfNeeded();
        uint8_t value = openBusValue;
        bool driven = false;

        if (addr == 0x0000) {
            value = cpuPortDir;
            onDrivenBusValue(value);
            lastDataBusValue = value;
            return value;
        }

        if (addr == 0x0001) {
            value = static_cast<uint8_t>((cpuPortData & cpuPortDir) | (0xFFu & ~cpuPortDir));
            onDrivenBusValue(value);
            lastDataBusValue = value;
            return value;
        }

        const bool loram = (cpuPortDir & 0x01) && (cpuPortData & 0x01);
        const bool hiram = (cpuPortDir & 0x02) && (cpuPortData & 0x02);
        const bool charen = (cpuPortDir & 0x04) && (cpuPortData & 0x04);

        const bool ioVisible = (loram || hiram) && charen;
        const bool charVisible = (loram || hiram) && !charen;

        if (addr >= 0xA000 && addr <= 0xBFFF && loram && hiram && hasBasicRom) {
            value = basicRom[addr - 0xA000];
            driven = true;
        } else if (addr >= 0xE000 && hasKernalRom && hiram) {
            value = kernalRom[addr - 0xE000];
            driven = true;
        } else if ((addr & 0xF000) == 0xD000) {
            if (!ioVisible) {
                if (charVisible && hasCharRom) {
                    value = charRom[addr - 0xD000];
                    driven = true;
                } else if (addr < memory.size()) {
                    value = memory[addr];
                    driven = true;
                }
            } else if (addr < 0xD400 && vic != nullptr) {
                value = vic->read(addr, openBusValue);
                driven = true;
            } else if (addr >= 0xDC00 && addr <= 0xDC0F && cia1 != nullptr) {
                value = cia1->read(addr, openBusValue);
                driven = true;
            } else if (addr >= 0xDD00 && addr <= 0xDD0F && cia2 != nullptr) {
                value = cia2->read(addr, openBusValue);
                driven = true;
            } else if (addr >= 0xD400 && addr <= 0xD41B && sid != nullptr) {
                value = sid->read(addr, openBusValue);
                driven = true;
            } else if (addr >= 0xDE00 && addr <= 0xDEFF) {
                value = openBusValue;
                driven = false;
            } else if (addr < memory.size()) {
                value = memory[addr];
                driven = true;
            }
        } else if (addr < memory.size()) {
            value = memory[addr];
            driven = true;
        }

        if (driven) {
            onDrivenBusValue(value);
        }
        lastDataBusValue = value;
        if (readTap) {
            readTap(addr, value);
        }
        return value;
    }

    uint8_t peek(uint16_t addr) const {
        if (flatMemoryMode) {
            return memory[addr];
        }

        if (addr == 0x0000) {
            return cpuPortDir;
        }

        if (addr == 0x0001) {
            return static_cast<uint8_t>((cpuPortData & cpuPortDir) | (0xFFu & ~cpuPortDir));
        }

        const bool loram = (cpuPortDir & 0x01) && (cpuPortData & 0x01);
        const bool hiram = (cpuPortDir & 0x02) && (cpuPortData & 0x02);
        const bool charen = (cpuPortDir & 0x04) && (cpuPortData & 0x04);
        const bool ioVisible = (loram || hiram) && charen;
        const bool charVisible = (loram || hiram) && !charen;

        if (addr >= 0xA000 && addr <= 0xBFFF && loram && hiram && hasBasicRom) {
            return basicRom[addr - 0xA000];
        }
        if (addr >= 0xE000 && hiram && hasKernalRom) {
            return kernalRom[addr - 0xE000];
        }

        if ((addr & 0xF000) == 0xD000) {
            if (!ioVisible) {
                if (charVisible && hasCharRom) {
                    return charRom[addr - 0xD000];
                }
                if (addr < memory.size()) {
                    return memory[addr];
                }
                return openBusValue;
            }

            if (addr < 0xD400 && vic != nullptr)
                return vic->peek(addr, openBusValue);
            else if (addr >= 0xDC00 && addr <= 0xDC0F && cia1 != nullptr)
                return cia1->peek(addr, openBusValue);
            else if (addr >= 0xDD00 && addr <= 0xDD0F && cia2 != nullptr)
                return cia2->peek(addr, openBusValue);
            else if (addr >= 0xD400 && addr <= 0xD41B && sid != nullptr)
                return sid->peek(addr, openBusValue);
        }

        if (addr < memory.size())
            return memory[addr];
        else
            return openBusValue;
    }

    void write(uint16_t addr, uint8_t val) {
        addressBus = addr;
        lastIsWrite = true;
        lastDataBusValue = val;

        if (flatMemoryMode) {
            memory[addr] = val;
            onDrivenBusValue(val);
            if (writeTap) {
                writeTap(addr, val);
            }
            return;
        }

        if (addr == 0x0000) {
            cpuPortDir = val;
            if (addr < memory.size()) {
                memory[addr] = val;
            }
            onDrivenBusValue(val);
            if (writeTap) {
                writeTap(addr, val);
            }
            return;
        }

        if (addr == 0x0001) {
            cpuPortData = val;
            if (addr < memory.size()) {
                memory[addr] = val;
            }
            onDrivenBusValue(val);
            if (writeTap) {
                writeTap(addr, val);
            }
            return;
        }

        const bool loram = (cpuPortDir & 0x01) && (cpuPortData & 0x01);
        const bool hiram = (cpuPortDir & 0x02) && (cpuPortData & 0x02);
        const bool charen = (cpuPortDir & 0x04) && (cpuPortData & 0x04);
        const bool ioVisible = (loram || hiram) && charen;

        std::cout << "WRITE global $" << std::hex << addr << std::endl;
        if ((addr & 0xF000) == 0xD000 && ioVisible) {
            if (addr < 0xD400 && vic != nullptr)
                vic->write(addr, val);
            else if (addr >= 0xDC00 && addr <= 0xDC0F && cia1 != nullptr) {
                std::cout << "WRITE CIA1 $" << std::hex << addr << " = $" << (int)val << std::endl;
                cia1->write(addr, val);
                printCIA1RegisterFriendly(addr, val);
            } else if (addr >= 0xDD00 && addr <= 0xDD0F && cia2 != nullptr) {
                std::cout << "WRITE CIA2 $" << std::hex << addr << " = $" << (int)val << std::endl;
                cia2->write(addr, val);
                printCIA2RegisterFriendly(addr, val);
            } else if (addr >= 0xD400 && addr <= 0xD41B && sid != nullptr)
                sid->write(addr, val);
            else if (addr < memory.size())
                memory[addr] = val;
        } else if (addr < memory.size()) {
            memory[addr] = val;
        }

        onDrivenBusValue(val);
        if (writeTap) {
            writeTap(addr, val);
        }
    }

    void laWrite(uint16_t addr, uint8_t val)
    {
        addressBus = addr;
        lastIsWrite = true;
        lastDataBusValue = val;

        onDrivenBusValue(val);

        if (enableTracing) {
            std::cout << "[DUMMY WRITE] $" << std::hex << std::setw(4) << std::setfill('0') << addr
                      << " <- $" << std::setw(2) << (int)val
                      << " (no memory change)" << std::endl;
        }
    }

    void reset() {
        std::fill(memory.begin(), memory.end(), 0);
        onDrivenBusValue(0xFF);
        flatMemoryMode = false;
        cpuPortDir = 0x2F;
        cpuPortData = 0x37;
        memory[0x0000] = cpuPortDir;
        memory[0x0001] = cpuPortData;
    }

    bool loadSystemRoms(const std::string &romDir = "roms") {
        hasBasicRom = false;
        hasKernalRom = false;
        hasCharRom = false;

        const std::filesystem::path base(romDir);
        auto loadRom = [&](const std::vector<std::string> &names, uint8_t *dst, size_t expected, bool &flag) {
            for (size_t i = 0; i < names.size(); ++i) {
                const std::filesystem::path p = base / names[i];
                std::ifstream in(p, std::ios::binary);
                if (!in.is_open()) {
                    continue;
                }
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (data.size() != expected) {
                    continue;
                }
                std::memcpy(dst, data.data(), expected);
                flag = true;
                return true;
            }
            return false;
        };

        loadRom({"basic.rom", "basic.901226-01.bin"}, basicRom.data(), basicRom.size(), hasBasicRom);
        loadRom({"kernal.rom", "kernal.901227-03.bin"}, kernalRom.data(), kernalRom.size(), hasKernalRom);
        loadRom({"chargen.rom", "char.rom", "characters.901225-01.bin"}, charRom.data(), charRom.size(), hasCharRom);

        return hasBasicRom && hasKernalRom && hasCharRom;
    }
};
