#pragma once

#include <array>
#include <iostream>
#include <vector>

#include "../drive1541_physical/drive_iec_port.hpp"
#include "../drive1541_physical/iec_edge_queue.hpp"

static void runDrive1541IecEdgeQueueTests() {
    using drive1541_physical::IecEdgeLine;
    using drive1541_physical::IecEdgeQueue;
    using drive1541_physical::IecEdgeSource;
    using drive1541_physical::IecLines;
    using drive1541_physical::DriveIecPort;

    {
        IecEdgeQueue q;
        q.push(10, IecEdgeLine::Data, false, IecEdgeSource::Host);
        q.push(10, IecEdgeLine::Atn, false, IecEdgeSource::Host);
        q.push(10, IecEdgeLine::Clk, true, IecEdgeSource::Host);
        q.push(10, IecEdgeLine::Clk, false, IecEdgeSource::Drive);

        const std::vector<drive1541_physical::IecEdgeEvent> ev = q.pop_ready(10);
        if (ev.size() != 4) {
            std::cerr << "[1541 IEC EDGEQ] FAIL: expected 4 events in stable ordering test" << std::endl;
            assert(false);
        }

        const std::array<IecEdgeLine, 4> expectedLine = {
            IecEdgeLine::Atn,
            IecEdgeLine::Clk,
            IecEdgeLine::Clk,
            IecEdgeLine::Data
        };
        const std::array<IecEdgeSource, 4> expectedSource = {
            IecEdgeSource::Host,
            IecEdgeSource::Host,
            IecEdgeSource::Drive,
            IecEdgeSource::Host
        };
        for (size_t i = 0; i < ev.size(); ++i) {
            if (ev[i].line != expectedLine[i] || ev[i].source != expectedSource[i]) {
                std::cerr << "[1541 IEC EDGEQ] FAIL: deterministic tie-break ordering mismatch" << std::endl;
                assert(false);
            }
        }
    }

    {
        auto runTraceDigest = []() -> uint64_t {
            DriveIecPort port;
            port.reset();

            const std::array<IecLines, 6> hostTrace = {{
                {true, true, true},
                {false, true, true},
                {false, false, true},
                {false, true, false},
                {true, true, false},
                {true, true, true}
            }};

            const std::array<IecLines, 4> driveTrace = {{
                {true, true, true},
                {true, false, true},
                {true, false, false},
                {true, true, true}
            }};

            uint64_t digest = 1469598103934665603ull;
            for (size_t i = 0; i < hostTrace.size(); ++i) {
                const uint64_t ts = static_cast<uint64_t>(5 + i * 7);
                port.queueHostLines(ts, hostTrace[i]);
            }
            for (size_t i = 0; i < driveTrace.size(); ++i) {
                const uint64_t ts = static_cast<uint64_t>(8 + i * 9);
                port.queueDriveLines(ts, driveTrace[i]);
            }

            for (uint64_t now = 0; now <= 64; ++now) {
                port.applyReady(now, true);
                const IecLines in = port.busInput();
                const IecLines out = port.busDriveOutput();
                const uint8_t packed = static_cast<uint8_t>((in.atn ? 1u : 0u) |
                                                            ((in.clk ? 1u : 0u) << 1) |
                                                            ((in.data ? 1u : 0u) << 2) |
                                                            ((out.atn ? 1u : 0u) << 3) |
                                                            ((out.clk ? 1u : 0u) << 4) |
                                                            ((out.data ? 1u : 0u) << 5));
                digest ^= packed;
                digest *= 1099511628211ull;
            }
            return digest;
        };

        const uint64_t d1 = runTraceDigest();
        const uint64_t d2 = runTraceDigest();
        if (d1 == 1469598103934665603ull || d1 != d2) {
            std::cerr << "[1541 IEC EDGEQ] FAIL: deterministic replay digest mismatch" << std::endl;
            assert(false);
        }
    }

    {
        DriveIecPort port;
        port.reset();

        port.queueDriveLines(12, {true, false, false});
        port.applyReady(20, false);
        IecLines out = port.busDriveOutput();
        if (!out.clk || !out.data) {
            std::cerr << "[1541 IEC EDGEQ] FAIL: drive-off should block drive output events" << std::endl;
            assert(false);
        }

        port.applyReady(20, true);
        out = port.busDriveOutput();
        if (out.clk || out.data) {
            std::cerr << "[1541 IEC EDGEQ] FAIL: queued drive edges not applied when drive resumes" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 IEC EDGEQ] PASS: timestamped IEC edge queue determinism + gating" << std::endl;
}
