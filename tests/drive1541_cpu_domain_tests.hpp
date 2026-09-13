#pragma once

#include <array>
#include <iostream>

#include "../drive1541_physical/drive_cpu_domain.hpp"

static void runDrive1541CpuDomainTests() {
    using drive1541_physical::DriveCpuDomain;

    std::array<std::uint8_t, 65536> mem{};
    mem.fill(0);

    mem[0xFFFCu] = 0x34u;
    mem[0xFFFDu] = 0x12u;
    mem[0x1234u] = 0xA9u;
    mem[0x1235u] = 0xEAu;

    std::uint16_t lastReadAddr = 0;
    std::uint16_t lastWriteAddr = 0;
    std::uint8_t lastWriteValue = 0;
    bool readSeen = false;
    bool writeSeen = false;

    DriveCpuDomain cpu;
    cpu.bind_bus(
        [&](std::uint16_t addr) -> std::uint8_t {
            readSeen = true;
            lastReadAddr = addr;
            return mem[addr];
        },
        [&](std::uint16_t addr, std::uint8_t v) {
            writeSeen = true;
            lastWriteAddr = addr;
            lastWriteValue = v;
            mem[addr] = v;
        }
    );

    cpu.reset();
    if (!readSeen || cpu.pc() != 0x1234u) {
        std::cerr << "[1541 CPU DOMAIN] FAIL: reset vector fetch mismatch" << std::endl;
        assert(false);
    }

    cpu.step_one_cycle();
    if (!writeSeen || lastWriteAddr != 0x0002u || lastWriteValue != 0xA9u) {
        std::cerr << "[1541 CPU DOMAIN] FAIL: write callback not triggered as expected" << std::endl;
        assert(false);
    }

    const std::uint16_t pcAfterFirst = cpu.pc();
    cpu.set_irq(true);
    cpu.set_nmi(false);
    cpu.step_one_cycle();
    const std::uint16_t pcAfterIrq = cpu.pc();

    cpu.set_irq(false);
    cpu.set_nmi(true);
    cpu.step_one_cycle();
    const std::uint16_t pcAfterNmi = cpu.pc();

    if (pcAfterIrq != static_cast<std::uint16_t>(pcAfterFirst + 1u)) {
        std::cerr << "[1541 CPU DOMAIN] FAIL: IRQ path broke deterministic PC cadence" << std::endl;
        assert(false);
    }
    if (pcAfterNmi != static_cast<std::uint16_t>(pcAfterIrq + 2u)) {
        std::cerr << "[1541 CPU DOMAIN] FAIL: NMI path broke deterministic PC cadence" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 CPU DOMAIN] PASS: reset fetch + bus callbacks + IRQ/NMI deterministic cadence" << std::endl;
}
