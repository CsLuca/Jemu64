#pragma once

#include <array>
#include <iostream>

#include "../drive1541_physical/drive_dos_memory_map.hpp"

static void runDrive1541MemoryMapTests() {
    using drive1541_physical::DriveDosMemoryMap;

    DriveDosMemoryMap map;
    map.reset();

    std::array<std::uint8_t, 0x4000> rom{};
    for (std::size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<std::uint8_t>(i & 0xFFu);
    }
    map.set_rom(rom.data(), rom.size());

    std::uint16_t lastIoReadAddr = 0;
    std::uint16_t lastIoWriteAddr = 0;
    std::uint8_t lastIoWriteValue = 0;
    bool ioReadSeen = false;
    bool ioWriteSeen = false;

    map.bind_io(
        [&](std::uint16_t a) -> std::uint8_t {
            ioReadSeen = true;
            lastIoReadAddr = a;
            return static_cast<std::uint8_t>(0x5A);
        },
        [&](std::uint16_t a, std::uint8_t v) {
            ioWriteSeen = true;
            lastIoWriteAddr = a;
            lastIoWriteValue = v;
        }
    );

    // RAM read/write
    map.write(0x0002u, 0x34u);
    if (map.read(0x0002u) != 0x34u) {
        std::cerr << "[1541 MMAP] FAIL: RAM read/write mismatch" << std::endl;
        assert(false);
    }

    // ROM read-only
    const std::uint8_t romBefore = map.read(0xC123u);
    map.write(0xC123u, static_cast<std::uint8_t>(romBefore ^ 0xFFu));
    const std::uint8_t romAfter = map.read(0xC123u);
    if (romAfter != romBefore) {
        std::cerr << "[1541 MMAP] FAIL: ROM write altered mapped ROM" << std::endl;
        assert(false);
    }

    // IO dispatch
    const std::uint8_t ioRead = map.read(0x1804u);
    if (!ioReadSeen || lastIoReadAddr != 0x1804u || ioRead != 0x5Au) {
        std::cerr << "[1541 MMAP] FAIL: IO read callback mismatch" << std::endl;
        assert(false);
    }
    map.write(0x1C02u, 0xA7u);
    if (!ioWriteSeen || lastIoWriteAddr != 0x1C02u || lastIoWriteValue != 0xA7u) {
        std::cerr << "[1541 MMAP] FAIL: IO write callback mismatch" << std::endl;
        assert(false);
    }

    // Out-of-map deterministic default (ROM decode with null ROM pointer)
    map.set_rom(nullptr, 0);
    const std::uint8_t unmapped = map.read(0xFF10u);
    if (unmapped != static_cast<std::uint8_t>(0xFF)) {
        std::cerr << "[1541 MMAP] FAIL: unmapped read default mismatch" << std::endl;
        std::cerr << "[1541 MMAP] detail: addr=$FF10 val=$" << std::hex << static_cast<int>(unmapped) << std::dec << std::endl;
        assert(false);
    }

    std::cerr << "[1541 MMAP] PASS: RAM/ROM/IO decode and deterministic defaults" << std::endl;
}
