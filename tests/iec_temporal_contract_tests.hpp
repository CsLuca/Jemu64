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

    std::cerr << "[IEC TEMPORAL] PASS: deterministic phase ordering (N=20) + no double-commit" << std::endl;
}
