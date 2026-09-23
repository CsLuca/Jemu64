#pragma once

#include <cassert>
#include <iostream>
#include <vector>

#include "../cia6526.hpp"
#include "../iec_bridge.hpp"

namespace iec_temporal_contract_tests_detail {

struct TestIecDevice : public IIecDevice {
    bool pullClk = false;
    bool pullData = false;
    bool atnHigh = true;
    bool clkHigh = true;
    bool dataHigh = true;
    std::uint64_t tickCount = 0;

    void tickIecHalfCycle() override {
        tickCount++;
        // Deterministic periodic pattern producing repeated edge updates.
        pullClk = ((tickCount % 4u) == 1u) || ((tickCount % 4u) == 2u);
        pullData = ((tickCount % 6u) == 3u);
    }

    void setIecLines(bool atn, bool clk, bool data) override {
        atnHigh = atn;
        clkHigh = clk;
        dataHigh = data;
    }

    bool getIecDrivePullCLK() const override {
        return pullClk;
    }

    bool getIecDrivePullDATA() const override {
        return pullData;
    }
};

static std::vector<IecTemporalTraceEvent> runTrace(std::uint64_t &doubleCommitCount) {
    CIA6526 cia2;
    TestIecDevice drive;

    IecBridgePolarity polarity = makeRuntimeDefaultIecPolarity();
    IecBusDomain domain(cia2, drive, polarity);
    domain.setC64DomainEnabled(false);
    domain.setDriveDomainEnabled(true);
    domain.setTemporalDebugEnabled(true);
    domain.clearTemporalTrace();
    domain.configureDomainRatesForTest(985248u, 985248u, 0, 0u, 0);

    for (int i = 0; i < 80; ++i) {
        domain.tickHalfCycle();
    }

    doubleCommitCount = domain.getTemporalDoubleCommitSameTimestampCount();
    return domain.getTemporalTrace();
}

} // namespace iec_temporal_contract_tests_detail

static void runIecTemporalContractTests() {
    using namespace iec_temporal_contract_tests_detail;

    std::uint64_t referenceDoubleCommit = 0;
    const std::vector<IecTemporalTraceEvent> referenceTrace = runTrace(referenceDoubleCommit);
    if (referenceTrace.empty()) {
        std::cerr << "[IEC TEMPORAL] FAIL: empty temporal trace" << std::endl;
        assert(false);
    }
    if (referenceDoubleCommit != 0) {
        std::cerr << "[IEC TEMPORAL] FAIL: double commit detected in reference trace" << std::endl;
        assert(false);
    }

    for (int run = 0; run < 20; ++run) {
        std::uint64_t runDoubleCommit = 0;
        const std::vector<IecTemporalTraceEvent> trace = runTrace(runDoubleCommit);
        if (runDoubleCommit != 0) {
            std::cerr << "[IEC TEMPORAL] FAIL: double commit detected run=" << run << std::endl;
            assert(false);
        }
        if (trace.size() != referenceTrace.size()) {
            std::cerr << "[IEC TEMPORAL] FAIL: trace size mismatch run=" << run << std::endl;
            assert(false);
        }
        for (std::size_t i = 0; i < trace.size(); ++i) {
            const IecTemporalTraceEvent &a = trace[i];
            const IecTemporalTraceEvent &b = referenceTrace[i];
            if (a.timestamp != b.timestamp ||
                a.sequence != b.sequence ||
                a.phase != b.phase ||
                a.atnHigh != b.atnHigh ||
                a.clkHigh != b.clkHigh ||
                a.dataHigh != b.dataHigh) {
                std::cerr << "[IEC TEMPORAL] FAIL: non-deterministic temporal trace run=" << run << std::endl;
                assert(false);
            }
        }
    }

    {
        bool sawCommitFromDrive = false;
        bool sawCommitFromC64 = false;
        bool sawDelayedCommit = false;
        bool sawModeledCommit = false;
        bool sawSampleBeforeCommit = false;

        for (const IecTemporalTraceEvent &ev : referenceTrace) {
            if (ev.phase == IecTemporalPhase::Sample) {
                sawSampleBeforeCommit = true;
            }
            if (ev.phase == IecTemporalPhase::CommitEdge) {
                if (!sawSampleBeforeCommit) {
                    std::cerr << "[IEC TEMPORAL] FAIL: commit edge observed before any sample phase" << std::endl;
                    assert(false);
                }
                if (ev.edgeOwner == IecEdgeOwner::Drive) {
                    sawCommitFromDrive = true;
                }
                if (ev.edgeOwner == IecEdgeOwner::C64) {
                    sawCommitFromC64 = true;
                }
                if (ev.edgeOwner == IecEdgeOwner::LineModel) {
                    sawModeledCommit = true;
                }
                if (ev.effectiveDelayTicks > 0) {
                    sawDelayedCommit = true;
                }
                if (ev.edgeOwner == IecEdgeOwner::None || ev.edgeCause == IecEdgeCause::None) {
                    std::cerr << "[IEC TEMPORAL] FAIL: commit edge missing owner/cause metadata" << std::endl;
                    assert(false);
                }
            }
        }

        if (!sawCommitFromDrive) {
            std::cerr << "[IEC TEMPORAL] FAIL: missing drive-owned commit edges in trace" << std::endl;
            assert(false);
        }
        if (!sawDelayedCommit) {
            std::cerr << "[IEC TEMPORAL] FAIL: missing delayed commit edges in trace" << std::endl;
            assert(false);
        }

        (void)sawCommitFromC64;
        (void)sawModeledCommit;
    }

    std::cerr << "[IEC TEMPORAL] PASS: deterministic phase ordering (N=20) + no double-commit" << std::endl;

    {
        CIA6526 cia2;
        TestIecDevice drive;
        IecBridgePolarity polarity = makeRuntimeDefaultIecPolarity();
        IecBusDomain domain(cia2, drive, polarity);
        domain.setC64DomainEnabled(false);
        domain.setDriveDomainEnabled(false);
        domain.configureLineModelForTest(true, 0, 1, 1, 0, 2, 2);
        domain.setTemporalDebugEnabled(true);
        domain.clearTemporalTrace();

        // ATN low pulse shorter than min-low must not reach high immediately.
        domain.linkC64PullATN = true;
        domain.linkC64PullCLK = false;
        domain.linkC64PullDATA = false;
        domain.linkDrivePullCLK = false;
        domain.linkDrivePullDATA = false;
        domain.settleBusAndPropagateSamples();
        if (domain.linkLineATNHigh) {
            std::cerr << "[IEC TEMPORAL] FAIL: ATN expected low after pull assert" << std::endl;
            assert(false);
        }

        domain.linkC64PullATN = false;
        domain.settleBusAndPropagateSamples();
        if (domain.linkLineATNHigh) {
            std::cerr << "[IEC TEMPORAL] FAIL: ATN rose before min pulse/release delay" << std::endl;
            assert(false);
        }
        domain.nowUnits += 1;
        domain.executeTimedEventsAtNow();
        if (domain.linkLineATNHigh) {
            std::cerr << "[IEC TEMPORAL] FAIL: ATN rose too early with line model" << std::endl;
            assert(false);
        }
        domain.nowUnits += 1;
        domain.executeTimedEventsAtNow();
        if (!domain.linkLineATNHigh) {
            std::cerr << "[IEC TEMPORAL] FAIL: ATN did not rise after min pulse/release delay" << std::endl;
            assert(false);
        }

        bool sawLineModelCause = false;
        for (const IecTemporalTraceEvent &ev : domain.getTemporalTrace()) {
            if (ev.phase == IecTemporalPhase::CommitEdge &&
                ev.edgeOwner == IecEdgeOwner::LineModel &&
                (ev.edgeCause == IecEdgeCause::ReleaseDelay || ev.edgeCause == IecEdgeCause::MinPulse) &&
                ev.effectiveDelayTicks > 0) {
                sawLineModelCause = true;
                break;
            }
        }
        if (!sawLineModelCause) {
            std::cerr << "[IEC TEMPORAL] FAIL: missing line-model commit metadata in temporal trace" << std::endl;
            assert(false);
        }
    }
}
