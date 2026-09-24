#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../cia6526.hpp"
#include "../iec_bridge.hpp"

namespace iec_topology_tests_detail {

struct StaticHostEndpoint : public IIecHostEndpoint {
    bool pullAtn = false;
    bool pullClk = false;
    bool pullData = false;

    IecC64Signals deriveSignals(const IecBridgePolarity &) const override {
        IecC64Signals s;
        s.c64PullATN = pullAtn;
        s.c64PullCLK = pullClk;
        s.c64PullDATA = pullData;
        s.c64AtnDriven = true;
        s.c64ClkDriven = true;
        s.c64DataDriven = true;
        return s;
    }

    void applyInputs(const IecBridgePolarity &, const IecC64Signals &, const IecResolvedLines &) override {}
    void setSerialPins(bool, bool) override {}
    void tickHalfCycle() override {}
};

struct PullOnlyDeviceEndpoint : public IIecDeviceEndpoint {
    bool pullClk = false;
    bool pullData = false;

    void tickHalfCycle() override {}
    void setLines(bool, bool, bool) override {}
    bool getPullCLK() const override { return pullClk; }
    bool getPullDATA() const override { return pullData; }
};

static std::vector<IecTemporalTraceEvent> runTopologyTrace(const char *profileName) {
    StaticHostEndpoint host;
    PullOnlyDeviceEndpoint d8;
    PullOnlyDeviceEndpoint d9;

    IecCable cable;
    cable.connectHost(host);
    cable.connectDeviceEndpoint(d8);
    cable.connectDeviceEndpoint(d9);
    IecBusDomain *domain = cable.bus();
    if (domain == nullptr) {
        return {};
    }

    domain->setTemporalDebugEnabled(true);
    domain->clearTemporalTrace();
    domain->configureDomainRatesForTest(985248u, 985248u, 0, 0u, 0);

    if (profileName != nullptr) {
        std::string p = profileName;
        for (char &c : p) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        uint64_t release = (p == "PROFILE_LONG") ? 3u : ((p == "PROFILE_MEDIUM") ? 2u : 1u);
        uint64_t minPulse = release;
        domain->configureLineModelForTest(true, release, release, release, minPulse, minPulse, minPulse);
    }

    d8.pullClk = true;
    for (int i = 0; i < 48; ++i) {
        cable.tickHalfCycle();
    }

    d8.pullClk = false;
    for (int i = 0; i < 64; ++i) {
        cable.tickHalfCycle();
    }

    return domain->getTemporalTrace();
}

static uint64_t firstDelayedLineModelCommit(const std::vector<IecTemporalTraceEvent> &trace) {
    for (const IecTemporalTraceEvent &ev : trace) {
        if (ev.phase == IecTemporalPhase::CommitEdge &&
            ev.edgeOwner == IecEdgeOwner::LineModel &&
            ev.effectiveDelayTicks > 0) {
            return ev.effectiveDelayTicks;
        }
    }
    return 0;
}

static uint64_t firstDriveOwnedCommitDelay(const std::vector<IecTemporalTraceEvent> &trace) {
    for (const IecTemporalTraceEvent &ev : trace) {
        if (ev.phase == IecTemporalPhase::CommitEdge &&
            ev.edgeOwner == IecEdgeOwner::Drive &&
            ev.edgeCause == IecEdgeCause::PullChange) {
            return ev.effectiveDelayTicks;
        }
    }
    return 0;
}

static uint64_t runDriveSkewCommitDelay(uint64_t driveSkewUnits) {
    StaticHostEndpoint host;
    PullOnlyDeviceEndpoint d8;

    IecCable cable;
    cable.connectHost(host);
    cable.connectDeviceEndpoint(d8);
    IecBusDomain *domain = cable.bus();
    if (domain == nullptr) {
        return 0;
    }

    domain->setTemporalDebugEnabled(true);
    domain->clearTemporalTrace();
    domain->configureDomainRatesForTest(985248u, 985248u, 0, 0u, 0);
    domain->configureLineModelForTest(true, 1, 1, 1, 1, 1, 1);
    domain->configureContinuousLineSolverForTest(true, 1000, 632, 368, 2, 1);
    domain->configureNodeTimingForTest(0, driveSkewUnits, 2, 1, 2, 1);

    d8.pullClk = true;
    for (int i = 0; i < 16; ++i) {
        cable.tickHalfCycle();
    }

    return firstDriveOwnedCommitDelay(domain->getTemporalTrace());
}

} // namespace iec_topology_tests_detail

static void runIecTopologyPropagationTests() {
    using namespace iec_topology_tests_detail;

    const std::vector<IecTemporalTraceEvent> shortTrace = runTopologyTrace("PROFILE_SHORT");
    const std::vector<IecTemporalTraceEvent> longTrace = runTopologyTrace("PROFILE_LONG");

    if (shortTrace.empty() || longTrace.empty()) {
        std::cerr << "[IEC TOPOLOGY] FAIL: empty topology trace" << std::endl;
        assert(false);
    }

    const uint64_t shortDelay = firstDelayedLineModelCommit(shortTrace);
    const uint64_t longDelay = firstDelayedLineModelCommit(longTrace);
    if (shortDelay == 0 || longDelay == 0) {
        std::cerr << "[IEC TOPOLOGY] FAIL: missing delayed line-model commit in topology traces" << std::endl;
        assert(false);
    }
    if (longDelay <= shortDelay) {
        std::cerr << "[IEC TOPOLOGY] FAIL: expected long-profile delay greater than short-profile delay"
                  << " short=" << shortDelay
                  << " long=" << longDelay
                  << std::endl;
        assert(false);
    }

    bool sawDriveOwnedCommit = false;
    for (const IecTemporalTraceEvent &ev : shortTrace) {
        if (ev.phase == IecTemporalPhase::CommitEdge && ev.edgeOwner == IecEdgeOwner::Drive) {
            sawDriveOwnedCommit = true;
            break;
        }
    }
    if (!sawDriveOwnedCommit) {
        std::cerr << "[IEC TOPOLOGY] FAIL: missing drive-owned commit edge in topology trace" << std::endl;
        assert(false);
    }

    const uint64_t skew0Delay = runDriveSkewCommitDelay(0);
    const uint64_t skew3Delay = runDriveSkewCommitDelay(3);
    if (skew0Delay == 0 || skew3Delay == 0) {
        std::cerr << "[IEC TOPOLOGY] FAIL: missing drive-owned delay metadata for node skew oracle" << std::endl;
        assert(false);
    }
    if (skew3Delay <= skew0Delay) {
        std::cerr << "[IEC TOPOLOGY] FAIL: expected node drive skew to increase drive-owned commit delay"
                  << " skew0=" << skew0Delay
                  << " skew3=" << skew3Delay
                  << std::endl;
        assert(false);
    }

    std::cerr << "[IEC TOPOLOGY] PASS: per-segment skew and propagation oracle"
              << " short_delay=" << shortDelay
              << " long_delay=" << longDelay
              << " drive_skew0=" << skew0Delay
              << " drive_skew3=" << skew3Delay
              << std::endl;
}
