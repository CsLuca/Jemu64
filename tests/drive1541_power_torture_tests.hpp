#pragma once

#include <array>
#include <cstdint>
#include <iostream>

static void runDrive1541PowerTortureTests() {
    std::array<Drive1541, 4> drives;
    for (std::size_t i = 0; i < drives.size(); ++i) {
        if (!drives[i].loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 POWER-TORTURE] FAIL: cannot load rom" << std::endl;
            assert(false);
        }
        drives[i].iecDeviceAddress = static_cast<uint8_t>(8u + static_cast<uint8_t>(i));
        drives[i].powerOn(true);
        drives[i].powerController.tick(30000);
    }

    // Mix states: 8 ON, 9 OFF, 10 RESETTING, 11 ON
    drives[1].powerOff();
    drives[2].powerReset();

    std::uint64_t digest = 1469598103934665603ull;
    for (int step = 0; step < 220; ++step) {
        for (std::size_t i = 0; i < drives.size(); ++i) {
            Drive1541 &d = drives[i];
            const bool atn = ((step + static_cast<int>(i)) % 4) != 0;
            const bool clk = ((step + static_cast<int>(i) * 2) % 5) != 0;
            const bool data = ((step + static_cast<int>(i) * 3) % 7) != 0;
            d.setIecLines(atn, clk, data);

            if ((step % 37) == 0 && i == 0) {
                d.powerReset();
            }
            if ((step % 41) == 0 && i == 1) {
                d.powerOn(false);
            }
            if ((step % 53) == 0 && i == 2) {
                d.powerOff();
            }
            if ((step % 47) == 0 && i == 3) {
                d.powerReset();
            }

            d.tickIecHalfCycle();

            if (!d.powerController.isDriveOutputAllowed()) {
                if (d.getIecDrivePullCLK() || d.getIecDrivePullDATA()) {
                    std::cerr << "[1541 POWER-TORTURE] FAIL: stuck pull lines while drive output must be disabled" << std::endl;
                    assert(false);
                }
            }

            digest ^= static_cast<std::uint64_t>(d.getPowerState());
            digest *= 1099511628211ull;
            digest ^= d.physicalScheduler.now();
            digest *= 1099511628211ull;
        }
    }

    if (digest == 1469598103934665603ull) {
        std::cerr << "[1541 POWER-TORTURE] FAIL: digest did not evolve" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 POWER-TORTURE] PASS: repeated on/off/reset during active IO without stuck lines" << std::endl;
}
