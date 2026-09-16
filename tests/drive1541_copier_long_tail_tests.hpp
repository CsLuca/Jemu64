#pragma once

#include <iostream>
#include <vector>

#include "../drive1541_physical/bitcell_timing_model.hpp"
#include "../drive1541_physical/flux_track_model.hpp"
#include "../drive1541_physical/gcr_codec.hpp"

static void runDrive1541CopierLongTailTests() {
    using drive1541_physical::BitcellTimingModel;
    using drive1541_physical::FluxTrackModel;
    using drive1541_physical::FluxTransition;
    using drive1541_physical::GcrCodec;
    using drive1541_physical::WeakRegion;

    {
        FluxTrackModel m;
        m.set_transitions({FluxTransition{9u}, FluxTransition{13u}, FluxTransition{11u}});
        m.set_weak_regions({WeakRegion{32u, 80u}});
        int edges = 0;
        for (std::uint32_t t = 1; t <= 128; ++t) {
            if (m.advance(3u, t)) {
                edges++;
            }
        }
        if (edges < 10 || edges > 100) {
            std::cerr << "[1541 COPIER LT] FAIL: weak/relock edge envelope out of range" << std::endl;
            assert(false);
        }
    }

    {
        BitcellTimingModel a;
        BitcellTimingModel b;
        a.reset(0x7788u);
        b.reset(0x7788u);
        a.set_zone(3);
        b.set_zone(3);
        for (int i = 0; i < 256; ++i) {
            const std::uint32_t ta = a.next_cell_ticks();
            const std::uint32_t tb = b.next_cell_ticks();
            if (ta != tb) {
                std::cerr << "[1541 COPIER LT] FAIL: deterministic jitter/relock sequence mismatch" << std::endl;
                assert(false);
            }
            if (ta < 14u || ta > 20u) {
                std::cerr << "[1541 COPIER LT] FAIL: long-tail bitcell range exceeded" << std::endl;
                assert(false);
            }
        }
    }

    {
        const GcrCodec codec;
        std::vector<std::uint8_t> noisy = {
            0xFFu, 0x0Au, 0x0Bu, 0x00u, 0x19u, 0xFFu, 0x1Au, 0x1Bu, 0x15u
        };
        const auto decoded = codec.decode_5to4(noisy.data(), noisy.size());
        if (!decoded.ok || !decoded.sync_found || decoded.data.empty()) {
            std::cerr << "[1541 COPIER LT] FAIL: noisy sync/relock decode path not tolerant" << std::endl;
            assert(false);
        }
        if (!decoded.relock_applied) {
            std::cerr << "[1541 COPIER LT] FAIL: relock path did not activate on noisy track" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 COPIER LT] PASS: long-tail weak/sync/relock/jitter hardening" << std::endl;
}
