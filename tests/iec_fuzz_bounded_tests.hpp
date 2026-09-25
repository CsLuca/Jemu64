#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecFuzzBoundedTests() {
    // Procedure: bounded property fuzz over timing knobs must keep counters finite and deterministic.
    std::uint32_t seed = 0x1541A55Au;
    auto nextRand = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    std::uint64_t digest = 0;
    std::uint64_t activity = 0;
    for (int caseIdx = 0; caseIdx < 64; ++caseIdx) {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecEnableRxTimingWindow = true;

        drive.iecRxSetupTicks = static_cast<std::uint32_t>(nextRand() % 3u);
        drive.iecRxHoldTicks = static_cast<std::uint32_t>(nextRand() % 3u);
        drive.iecSerialTimeoutHysteresisTicks = 1u + static_cast<std::uint32_t>(nextRand() % 4u);
        drive.iecReceiverTimeoutTicks = 16u + static_cast<std::uint32_t>(nextRand() % 32u);
        drive.iecSenderTimeoutTicks = 16u + static_cast<std::uint32_t>(nextRand() % 32u);
        drive.iecDeviceBetweenBytesTicks = 1u + static_cast<std::uint32_t>(nextRand() % 8u);
        drive.iecAtnResponseTimeoutTicks = 16u + static_cast<std::uint32_t>(nextRand() % 32u);
        drive.iecDeviceNotPresentTimeoutTicks = 16u + static_cast<std::uint32_t>(nextRand() % 32u);
        drive.iecTxQueue.push_back(static_cast<std::uint8_t>(nextRand() & 0xFFu));
        drive.iecTxQueue.push_back(static_cast<std::uint8_t>(nextRand() & 0xFFu));

        bool atn = true;
        bool clk = true;
        bool data = true;
        for (int step = 0; step < 256; ++step) {
            const std::uint32_t r = nextRand();
            if ((r & 0x07u) == 0u) { atn = !atn; }
            if ((r & 0x03u) == 0u) { clk = !clk; }
            if ((r & 0x05u) == 0u) { data = !data; }
            drive.setIecLines(atn, clk, data);
            drive.stepIecSerial();

            activity += static_cast<std::uint64_t>(!atn) + static_cast<std::uint64_t>(!clk) + static_cast<std::uint64_t>(!data);
            activity += static_cast<std::uint64_t>(drive.iecRxBitCount);
            activity += static_cast<std::uint64_t>(drive.iecTxBitCount);

            digest ^= (static_cast<std::uint64_t>(drive.iecRxTimeoutCount) << 1);
            digest ^= (static_cast<std::uint64_t>(drive.iecTxTimeoutCount) << 9);
            digest ^= (static_cast<std::uint64_t>(drive.iecEoiTimeoutCount) << 17);
            digest ^= static_cast<std::uint64_t>(drive.pendingIecTx() & 0xFFFFu);
            digest ^= (static_cast<std::uint64_t>(step + 1) << 27);
            digest ^= (atn ? (1ULL << 40) : 0ULL) ^ (clk ? (1ULL << 41) : 0ULL) ^ (data ? (1ULL << 42) : 0ULL);
            digest = (digest << 7) | (digest >> 57);

            if (drive.iecRxTimeoutCount > 1024 ||
                drive.iecTxTimeoutCount > 1024 ||
                drive.iecEoiTimeoutCount > 1024 ||
                drive.iecAtnResponseTimeoutCount > 1024 ||
                drive.iecDeviceNotPresentTimeoutCount > 1024) {
                std::cerr << "[IEC FUZZ] FAIL: timeout counter runaway under bounded fuzz" << std::endl;
                assert(false);
            }
        }
    }

    if (activity == 0) {
        std::cerr << "[IEC FUZZ] FAIL: missing activity under bounded fuzz" << std::endl;
        assert(false);
    }

    std::cerr << "[IEC FUZZ] PASS: bounded timing-state fuzz is deterministic"
              << " seed=$" << std::hex << seed
              << " digest=$" << digest
              << " activity=" << std::dec << activity
              << std::dec
              << std::endl;
}
