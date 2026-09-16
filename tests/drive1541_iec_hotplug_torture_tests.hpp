#pragma once

#include <array>
#include <cstdint>
#include <iostream>

static void runDrive1541IecHotplugTortureTests() {
    std::array<Drive1541, 4> drives;
    for (std::size_t i = 0; i < drives.size(); ++i) {
        if (!drives[i].loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 HOTPLUG] FAIL: cannot load rom" << std::endl;
            assert(false);
        }
        drives[i].iecDeviceAddress = static_cast<uint8_t>(8u + static_cast<uint8_t>(i));
        drives[i].powerOn(true);
        drives[i].powerController.tick(30000);
    }

    std::uint64_t progressGuard = 0;
    for (int t = 0; t < 260; ++t) {
        const bool hostAtn = ((t % 9) != 0);
        const bool hostClk = ((t % 3) != 0);
        const bool hostData = ((t % 5) != 0);

        for (std::size_t i = 0; i < drives.size(); ++i) {
            Drive1541 &d = drives[i];
            if ((t % 31) == static_cast<int>(i)) {
                d.powerOff();
            }
            if ((t % 37) == static_cast<int>(i + 1)) {
                d.powerOn(false);
            }
            if ((t % 43) == static_cast<int>(i + 2)) {
                d.powerReset();
            }

            d.setIecLines(hostAtn, hostClk, hostData);
            d.tickIecHalfCycle();

            if (!d.powerController.isDriveOutputAllowed()) {
                if (d.getIecDrivePullCLK() || d.getIecDrivePullDATA()) {
                    std::cerr << "[1541 HOTPLUG] FAIL: bus contention risk, disabled drive still pulling lines" << std::endl;
                    assert(false);
                }
            }

            progressGuard += d.cycles;
            progressGuard += d.physicalScheduler.now();
        }
    }

    if (progressGuard == 0) {
        std::cerr << "[1541 HOTPLUG] FAIL: no runtime progress observed in torture window" << std::endl;
        assert(false);
    }

    for (std::size_t i = 0; i < drives.size(); ++i) {
        if (drives[i].pendingIecRx() > 64 || drives[i].pendingIecTx() > 512) {
            std::cerr << "[1541 HOTPLUG] FAIL: queue growth suggests deadlock/leak idx=" << i << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 HOTPLUG] PASS: IEC hotplug torture avoids contention/stuck-lines/deadlock" << std::endl;
}
