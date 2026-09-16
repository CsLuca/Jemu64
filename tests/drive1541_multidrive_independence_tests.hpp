#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

namespace {

static std::uint64_t runDrive1541MultiDriveIndependenceDigest(std::array<std::uint64_t, 4> &outNow,
                                                              std::array<std::uint64_t, 4> &outHostTicks) {
    std::array<Drive1541, 4> drives;
    for (std::size_t i = 0; i < drives.size(); ++i) {
        Drive1541 &d = drives[i];
        if (!d.loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 MULTI-STRESS] FAIL: cannot load roms/dos1541.rom" << std::endl;
            assert(false);
        }
        d.iecDeviceAddress = static_cast<uint8_t>(8u + static_cast<uint8_t>(i));
        d.powerOn(true);
        d.powerController.tick(30000);
    }

    drives[0].physicalScheduler.setRateRatio(1u, 1u);
    drives[1].physicalScheduler.setRateRatio(2u, 1u);
    drives[2].physicalScheduler.setRateRatio(3u, 2u);
    drives[3].physicalScheduler.setRateRatio(5u, 4u);

    const std::array<int, 4> workload = {220, 170, 140, 90};
    for (int step = 0; step < 256; ++step) {
        for (std::size_t i = 0; i < drives.size(); ++i) {
            Drive1541 &d = drives[i];
            if (step == (30 + static_cast<int>(i) * 5)) {
                d.powerOff();
            }
            if (step == (64 + static_cast<int>(i) * 7)) {
                d.powerOn(false);
            }

            const bool atnHigh = (((step + static_cast<int>(i)) % 9) != 0);
            const bool clkHigh = (((step + static_cast<int>(i) * 2) % 5) != 0);
            const bool dataHigh = (((step + static_cast<int>(i) * 3) % 7) != 0);
            d.setIecLines(atnHigh, clkHigh, dataHigh);

            if (step < workload[i]) {
                const int ticks = 1 + (((step + static_cast<int>(i)) % 3 == 0) ? 1 : 0);
                for (int k = 0; k < ticks; ++k) {
                    d.tickIecHalfCycle();
                }
            }
        }
    }

    if (drives[0].iecDeviceAddress != 8 || drives[1].iecDeviceAddress != 9 || drives[2].iecDeviceAddress != 10 || drives[3].iecDeviceAddress != 11) {
        std::cerr << "[1541 MULTI-STRESS] FAIL: device addresses drifted (shared state?)" << std::endl;
        assert(false);
    }

    drives[0].iecStatusLine = "30,SYNTAX ERROR,00,00";
    if (drives[1].iecStatusLine == drives[0].iecStatusLine || drives[2].iecStatusLine == drives[0].iecStatusLine || drives[3].iecStatusLine == drives[0].iecStatusLine) {
        std::cerr << "[1541 MULTI-STRESS] FAIL: status line leaked across drive instances" << std::endl;
        assert(false);
    }

    std::uint64_t digest = 1469598103934665603ull;
    for (std::size_t i = 0; i < drives.size(); ++i) {
        const Drive1541 &d = drives[i];
        outNow[i] = d.physicalScheduler.now();
        outHostTicks[i] = d.physicalScheduler.hostTicks();

        digest ^= static_cast<std::uint64_t>(i + 1u);
        digest *= 1099511628211ull;
        digest ^= outNow[i];
        digest *= 1099511628211ull;
        digest ^= outHostTicks[i];
        digest *= 1099511628211ull;
        digest ^= d.cycles;
        digest *= 1099511628211ull;
        digest ^= d.iecRxProcessed;
        digest *= 1099511628211ull;
        digest ^= d.iecTxServed;
        digest *= 1099511628211ull;
    }

    return digest;
}

} // namespace

static void runDrive1541MultiDriveIndependenceTests() {
    std::array<std::uint64_t, 4> nowA = {0, 0, 0, 0};
    std::array<std::uint64_t, 4> hostA = {0, 0, 0, 0};
    std::array<std::uint64_t, 4> nowB = {0, 0, 0, 0};
    std::array<std::uint64_t, 4> hostB = {0, 0, 0, 0};

    const std::uint64_t digestA = runDrive1541MultiDriveIndependenceDigest(nowA, hostA);
    const std::uint64_t digestB = runDrive1541MultiDriveIndependenceDigest(nowB, hostB);

    if (digestA != digestB || nowA != nowB || hostA != hostB) {
        std::cerr << "[1541 MULTI-STRESS] FAIL: non-deterministic run-to-run digest/state" << std::endl;
        assert(false);
    }

    const std::uint64_t minNow = std::min(std::min(nowA[0], nowA[1]), std::min(nowA[2], nowA[3]));
    const std::uint64_t maxNow = std::max(std::max(nowA[0], nowA[1]), std::max(nowA[2], nowA[3]));
    if (!(nowA[1] > nowA[2] && nowA[2] >= nowA[3] && nowA[3] > nowA[0]) || (maxNow - minNow) < 16u) {
        std::cerr << "[1541 MULTI-STRESS] FAIL: timing divergence envelope mismatch"
                  << " now8=" << nowA[0]
                  << " now9=" << nowA[1]
                  << " now10=" << nowA[2]
                  << " now11=" << nowA[3]
                  << std::endl;
        assert(false);
    }

    std::cerr << "[1541 MULTI-STRESS] PASS: 8/9/10/11 independence + timing divergence + deterministic replay"
              << " digest=$" << std::hex << digestA
              << " now8=" << nowA[0]
              << " now9=" << nowA[1]
              << " now10=" << nowA[2]
              << " now11=" << nowA[3]
              << std::dec << std::endl;

    std::cerr << "[IEC COPY E2E] PASS: advanced_multidrive_independence_8_9_10_11" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_multidrive_timing_divergence_8_9_10_11" << std::endl;
}
