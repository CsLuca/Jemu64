#pragma once

#include <array>
#include <iostream>

#include "../drive1541_physical/bitcell_timing_model.hpp"

static void runDrive1541BitcellTimingTests() {
    using drive1541_physical::BitcellTimingModel;

    {
        BitcellTimingModel m;
        m.reset(0x1541u);
        std::array<std::uint32_t, 4> zoneBase{};
        for (std::uint8_t z = 0; z < 4; ++z) {
            m.reset(0x1541u);
            m.set_zone(z);
            zoneBase[z] = m.next_cell_ticks();
        }
        if (!(zoneBase[0] < zoneBase[1] && zoneBase[1] < zoneBase[2] && zoneBase[2] < zoneBase[3])) {
            std::cerr << "[1541 BITCELL] FAIL: zone base ordering mismatch" << std::endl;
            assert(false);
        }
    }

    {
        BitcellTimingModel m;
        m.reset(0x2244u);
        m.set_zone(2);
        constexpr std::uint32_t kMinZone2 = 13u;
        constexpr std::uint32_t kMaxZone2 = 17u;
        for (int i = 0; i < 512; ++i) {
            const std::uint32_t ticks = m.next_cell_ticks();
            if (ticks < kMinZone2 || ticks > kMaxZone2) {
                std::cerr << "[1541 BITCELL] FAIL: jitter out of expected bound" << std::endl;
                assert(false);
            }
        }
    }

    {
        BitcellTimingModel a;
        BitcellTimingModel b;
        a.reset(0x9911u);
        b.reset(0x9911u);
        a.set_zone(3);
        b.set_zone(3);
        for (int i = 0; i < 128; ++i) {
            const std::uint32_t va = a.next_cell_ticks();
            const std::uint32_t vb = b.next_cell_ticks();
            if (va != vb) {
                std::cerr << "[1541 BITCELL] FAIL: deterministic seed sequence mismatch" << std::endl;
                assert(false);
            }
        }
    }

    std::cerr << "[1541 BITCELL] PASS: zone speed + bounded jitter + deterministic sequence" << std::endl;
}
