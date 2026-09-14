#pragma once

#include <iostream>
#include <vector>

#include "../drive1541_physical/flux_track_model.hpp"

static void runDrive1541FluxTrackTests() {
    using drive1541_physical::FluxTrackModel;
    using drive1541_physical::FluxTransition;
    using drive1541_physical::WeakRegion;

    {
        FluxTrackModel m;
        m.set_transitions({FluxTransition{3u}, FluxTransition{5u}});
        std::vector<int> edges;
        for (std::uint32_t t = 1; t <= 16; ++t) {
            if (m.advance(1u, t)) {
                edges.push_back(static_cast<int>(t));
            }
        }
        const std::vector<int> expected = {3, 8, 11, 16};
        if (edges != expected) {
            std::cerr << "[1541 FLUX] FAIL: transition sequence mismatch" << std::endl;
            assert(false);
        }
    }

    {
        FluxTrackModel mA;
        FluxTrackModel mB;
        mA.set_transitions({FluxTransition{4u}});
        mB.set_transitions({FluxTransition{4u}});
        mA.set_weak_regions({WeakRegion{8u, 16u}});
        mB.set_weak_regions({WeakRegion{8u, 16u}});

        for (std::uint32_t t = 1; t <= 32; ++t) {
            const bool a = mA.advance(1u, t);
            const bool b = mB.advance(1u, t);
            if (a != b) {
                std::cerr << "[1541 FLUX] FAIL: weak region determinism mismatch" << std::endl;
                assert(false);
            }
        }
    }

    {
        FluxTrackModel m;
        m.set_transitions({FluxTransition{2u}, FluxTransition{2u}, FluxTransition{2u}});
        int edgeCount = 0;
        for (std::uint32_t t = 1; t <= 30; ++t) {
            if (m.advance(1u, t)) {
                edgeCount++;
            }
        }
        if (edgeCount != 15) {
            std::cerr << "[1541 FLUX] FAIL: track wrap edge count mismatch" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 FLUX] PASS: transitions + weak region determinism + coherent wrap" << std::endl;
}
