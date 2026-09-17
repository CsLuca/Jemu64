#pragma once

#include <array>
#include <cstdint>
#include <iostream>

#include "../drive1541_physical/bitcell_timing_model.hpp"

static void runDrive1541ReadChannelPllTests() {
    using drive1541_physical::BitcellTimingModel;
    using drive1541_physical::ReadChannelPllSample;

    {
        BitcellTimingModel m;
        m.reset(0x4A11u);
        m.set_zone(2);
        m.set_drive_revision(0);
        m.set_level4_enabled(true);
        m.set_level4_calibration(24, 32, 260);

        ReadChannelPllSample last{};
        for (int i = 0; i < 96; ++i) {
            (void)m.next_cell_ticks();
            last = m.last_pll_sample();
        }

        if (!last.locked || last.lock_time < 80u || last.phase_noise_rms_ppm == 0u || last.jitter_window_ppm == 0u) {
            std::cerr << "[1541 PLL] FAIL: lock/calibration envelope mismatch" << std::endl;
            assert(false);
        }
    }

    {
        auto runDigest = [](std::uint8_t rev) {
            BitcellTimingModel m;
            m.reset(0x991177u);
            m.set_zone(3);
            m.set_drive_revision(rev);
            m.set_level4_enabled(true);
            m.set_level4_calibration(23, 30, 255);

            std::uint64_t digest = 1469598103934665603ull;
            for (int i = 0; i < 128; ++i) {
                const std::uint32_t ticks = m.next_cell_ticks();
                const ReadChannelPllSample s = m.last_pll_sample();
                digest ^= ticks;
                digest *= 1099511628211ull;
                digest ^= s.phase_noise_rms_ppm;
                digest *= 1099511628211ull;
                digest ^= s.jitter_window_ppm;
                digest *= 1099511628211ull;
                digest ^= static_cast<std::uint64_t>(s.locked ? 1u : 0u);
                digest *= 1099511628211ull;
            }
            return digest;
        };

        const std::uint64_t dA = runDigest(0);
        const std::uint64_t dB = runDigest(0);
        const std::uint64_t dC = runDigest(1);
        if (dA != dB || dA == dC) {
            std::cerr << "[1541 PLL] FAIL: deterministic or revision-divergence digest mismatch" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 PLL] PASS: lock dynamics + phase noise + revision-aware deterministic replay" << std::endl;
}
