#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include "iec_device.hpp"

struct IecBridgePolarity {
    bool atnPullWhenBitSet = true;
    bool clkPullWhenBitSet = true;
    bool dataPullWhenBitSet = true;
    bool mirrorInputsIntoPra = true;
    bool inputClkBitSetWhenLineHigh = true;
    bool inputDataBitSetWhenLineHigh = true;
    bool swapOutputClockData = false;
    bool swapInputClockData = false;
    bool useCombinedPortAForOutputs = false;
    bool forceReadbackDd00ClockDataFromBus = false;
    bool readbackBusOnlyWhenOutputHigh = false;
    bool readbackBusOnEdWindowOnly = false;
};

struct IecC64Signals {
    bool c64AtnDriven = false;
    bool c64ClkDriven = false;
    bool c64DataDriven = false;
    uint8_t portAOut = 0;
    bool c64PullATN = false;
    bool c64PullCLK = false;
    bool c64PullDATA = false;
    bool clkBitDriven = false;
    bool dataBitDriven = false;
    bool clkOutReleases = false;
    bool dataOutReleases = false;
};

struct IecResolvedLines {
    bool atnHigh = true;
    bool clkHigh = true;
    bool dataHigh = true;
};

enum class IecTemporalPhase : uint8_t {
    Sample = 0,
    DriveUpdate = 1,
    HostUpdate = 2,
    CommitEdge = 3
};

enum class IecEdgeOwner : uint8_t {
    None = 0,
    C64 = 1,
    Drive = 2,
    LineModel = 3
};

enum class IecEdgeCause : uint8_t {
    None = 0,
    PullChange = 1,
    ReleaseDelay = 2,
    MinPulse = 3
};

struct IecTemporalTraceEvent {
    uint64_t timestamp = 0;
    uint64_t sequence = 0;
    IecTemporalPhase phase = IecTemporalPhase::Sample;
    IecEdgeOwner edgeOwner = IecEdgeOwner::None;
    IecEdgeCause edgeCause = IecEdgeCause::None;
    uint64_t effectiveDelayTicks = 0;
    bool atnHigh = true;
    bool clkHigh = true;
    bool dataHigh = true;
};

struct IecLineModelState {
    bool levelHigh = true;
    uint64_t lowSince = 0;
    bool riseEventPending = false;
    uint64_t riseEventWhen = 0;
};

struct IIecHostEndpoint {
    virtual ~IIecHostEndpoint() {}
    virtual IecC64Signals deriveSignals(const IecBridgePolarity &polarity) const = 0;
    virtual void applyInputs(const IecBridgePolarity &polarity,
                             const IecC64Signals &sig,
                             const IecResolvedLines &lines) = 0;
    virtual void setSerialPins(bool cntHigh, bool spHigh) = 0;
    virtual void tickHalfCycle() = 0;
};

struct CiaIecHostEndpoint : public IIecHostEndpoint {
    CIA6526 &cia2;

    explicit CiaIecHostEndpoint(CIA6526 &cia)
        : cia2(cia) {}

    IecC64Signals deriveSignals(const IecBridgePolarity &polarity) const override {
        return deriveIecC64Signals(cia2, polarity);
    }

    void applyInputs(const IecBridgePolarity &polarity,
                     const IecC64Signals &sig,
                     const IecResolvedLines &lines) override {
        applyIecInputsToCia(cia2, polarity, sig, lines);
    }

    void setSerialPins(bool cntHigh, bool spHigh) override {
        cia2.setSerialPins(cntHigh, spHigh);
    }

    void tickHalfCycle() override {
        cia2.cycleCore.tickHalfCycle(cia2);
    }
};

struct IIecDeviceEndpoint {
    virtual ~IIecDeviceEndpoint() {}
    virtual void tickHalfCycle() = 0;
    virtual void setLines(bool atnHigh, bool clkHigh, bool dataHigh) = 0;
    virtual bool getPullCLK() const = 0;
    virtual bool getPullDATA() const = 0;
};

struct LegacyIecDeviceEndpointAdapter : public IIecDeviceEndpoint {
    IIecDevice *device = nullptr;

    explicit LegacyIecDeviceEndpointAdapter(IIecDevice &d)
        : device(&d) {}

    void tickHalfCycle() override {
        if (device != nullptr) {
            device->tickIecHalfCycle();
        }
    }

    void setLines(bool atnHigh, bool clkHigh, bool dataHigh) override {
        if (device != nullptr) {
            device->setIecLines(atnHigh, clkHigh, dataHigh);
        }
    }

    bool getPullCLK() const override {
        return (device != nullptr) ? device->getIecDrivePullCLK() : false;
    }

    bool getPullDATA() const override {
        return (device != nullptr) ? device->getIecDrivePullDATA() : false;
    }
};

static IecC64Signals deriveIecC64Signals(const CIA6526 &cia2, const IecBridgePolarity &polarity) {
    IecC64Signals s;

    s.c64AtnDriven = (cia2.ddra & 0x08) != 0;
    s.c64ClkDriven = (cia2.ddra & 0x10) != 0;
    s.c64DataDriven = (cia2.ddra & 0x20) != 0;

    s.portAOut = polarity.useCombinedPortAForOutputs ? cia2.getPortACombined() : cia2.pra;
    const bool atnBitSet = (s.portAOut & 0x08) != 0;
    const bool clkBitSet = (s.portAOut & 0x10) != 0;
    const bool dataBitSet = (s.portAOut & 0x20) != 0;

    const bool outClkBit = polarity.swapOutputClockData ? dataBitSet : clkBitSet;
    const bool outDataBit = polarity.swapOutputClockData ? clkBitSet : dataBitSet;

    const bool useBusReadbackOutputConvention = polarity.forceReadbackDd00ClockDataFromBus;
    const bool atnPullWhenBitSet = useBusReadbackOutputConvention ? true : polarity.atnPullWhenBitSet;
    const bool clkPullWhenBitSet = useBusReadbackOutputConvention ? true : polarity.clkPullWhenBitSet;
    const bool dataPullWhenBitSet = useBusReadbackOutputConvention ? true : polarity.dataPullWhenBitSet;

    s.c64PullATN = s.c64AtnDriven && (atnPullWhenBitSet ? atnBitSet : !atnBitSet);
    s.c64PullCLK = s.c64ClkDriven && (clkPullWhenBitSet ? outClkBit : !outClkBit);
    s.c64PullDATA = s.c64DataDriven && (dataPullWhenBitSet ? outDataBit : !outDataBit);

    s.clkBitDriven = (cia2.ddra & 0x10) != 0;
    s.dataBitDriven = (cia2.ddra & 0x20) != 0;
    s.clkOutReleases = (s.portAOut & 0x10) == 0;
    s.dataOutReleases = (s.portAOut & 0x20) == 0;

    return s;
}

static IecResolvedLines resolveIecLinesFromPulls(bool c64PullATN, bool c64PullCLK, bool c64PullDATA,
                                                 bool drivePullCLK, bool drivePullDATA) {
    IecResolvedLines lines;
    lines.atnHigh = !c64PullATN;
    lines.clkHigh = !(c64PullCLK || drivePullCLK);
    lines.dataHigh = !(c64PullDATA || drivePullDATA);
    return lines;
}

static void applyIecInputsToCia(CIA6526 &cia2,
                                const IecBridgePolarity &polarity,
                                const IecC64Signals &sig,
                                const IecResolvedLines &lines) {
    if (polarity.forceReadbackDd00ClockDataFromBus) {
        uint8_t mask = 0;
        uint8_t bits = 0;

        const bool allowClkOverride = !polarity.readbackBusOnlyWhenOutputHigh || !sig.clkBitDriven || sig.clkOutReleases;
        const bool allowDataOverride = !polarity.readbackBusOnlyWhenOutputHigh || !sig.dataBitDriven || sig.dataOutReleases;

        if (allowClkOverride) {
            mask = static_cast<uint8_t>(mask | 0x10);
            if (lines.clkHigh) {
                bits = static_cast<uint8_t>(bits | 0x10);
            }
        }
        if (allowDataOverride) {
            mask = static_cast<uint8_t>(mask | 0x20);
            if (lines.dataHigh) {
                bits = static_cast<uint8_t>(bits | 0x20);
            }
        }

        cia2.praReadbackOverrideMask = mask;
        cia2.praReadbackOverrideBits = bits;
    } else {
        cia2.praReadbackOverrideMask = 0;
        cia2.praReadbackOverrideBits = 0;
    }

    const bool useSwappedInputSense = polarity.swapInputClockData;
    const bool rawClkSense = useSwappedInputSense ? lines.dataHigh : lines.clkHigh;
    const bool rawDataSense = useSwappedInputSense ? lines.clkHigh : lines.dataHigh;
    const bool clkInBitSet = polarity.inputClkBitSetWhenLineHigh ? rawClkSense : !rawClkSense;
    const bool dataInBitSet = polarity.inputDataBitSetWhenLineHigh ? rawDataSense : !rawDataSense;

    if (clkInBitSet) {
        cia2.praInput |= 0x40;
        if (polarity.mirrorInputsIntoPra) {
            cia2.pra |= 0x40;
        }
    } else {
        cia2.praInput &= static_cast<uint8_t>(~0x40);
        if (polarity.mirrorInputsIntoPra) {
            cia2.pra &= static_cast<uint8_t>(~0x40);
        }
    }

    if (dataInBitSet) {
        cia2.praInput |= 0x80;
        if (polarity.mirrorInputsIntoPra) {
            cia2.pra |= 0x80;
        }
    } else {
        cia2.praInput &= static_cast<uint8_t>(~0x80);
        if (polarity.mirrorInputsIntoPra) {
            cia2.pra &= static_cast<uint8_t>(~0x80);
        }
    }
}

static IecBridgePolarity makeRuntimeDefaultIecPolarity() {
    IecBridgePolarity p;
    p.atnPullWhenBitSet = true;
    p.clkPullWhenBitSet = true;
    p.dataPullWhenBitSet = true;
    p.mirrorInputsIntoPra = false;
    p.inputClkBitSetWhenLineHigh = true;
    p.inputDataBitSetWhenLineHigh = true;
    return p;
}

static void syncIecBusWithPolarity(CIA6526 &cia2, Drive1541 &drive, const IecBridgePolarity &polarity) {
    const IecC64Signals sig = deriveIecC64Signals(cia2, polarity);
    const bool c64Driving = drive.isC64DrivingIecLines();
    const IecResolvedLines lines = resolveIecLinesFromPulls(c64Driving ? sig.c64PullATN : false,
                                                            c64Driving ? sig.c64PullCLK : false,
                                                            c64Driving ? sig.c64PullDATA : false,
                                                            drive.iecDrivePullCLK,
                                                            drive.iecDrivePullDATA);
    drive.setIecLines(lines.atnHigh, lines.clkHigh, lines.dataHigh);
    applyIecInputsToCia(cia2, polarity, sig, lines);
}

static void syncIecBus(CIA6526 &cia2, Drive1541 &drive) {
    static const IecBridgePolarity defaultPolarity = makeRuntimeDefaultIecPolarity();
    syncIecBusWithPolarity(cia2, drive, defaultPolarity);
}

struct SharedIecClockDomain {
    CIA6526 &cia2;
    Drive1541 &drive;
    IecBridgePolarity polarity;

    struct TimedEvent {
        uint64_t when = 0;
        uint64_t seq = 0;
        std::function<void()> callback;
    };

    struct TimedEventCompare {
        bool operator()(const TimedEvent &a, const TimedEvent &b) const {
            if (a.when != b.when) {
                return a.when > b.when;
            }
            return a.seq > b.seq;
        }
    };

    static constexpr uint64_t C64_HALF_PERIOD_UNITS = 1000000ULL;

    uint64_t c64HalfRateHz = 985248ULL;
    uint64_t driveHalfRateHz = 1000000ULL;
    int32_t driveDriftPpm = 0;
    uint64_t nowUnits = 0;
    uint64_t nextC64Units = 0;
    uint64_t nextDriveUnits = 0;
    uint64_t c64HalfTicks = 0;
    uint64_t driveHalfTicks = 0;
    uint64_t eventSeq = 0;
    uint32_t ditherSeed = 0;
    int32_t ditherAmplitude = 0;
    uint64_t linkLatencyC64ToBus = 1;
    uint64_t linkLatencyDriveToBus = 1;
    uint64_t linkLatencyBusToC64 = 1;
    uint64_t linkLatencyBusToDrive = 1;
    uint64_t linkJitterUnits = 0;
    uint32_t linkJitterSeed = 0;
    bool c64DomainEnabled = true;
    bool driveDomainEnabled = true;

    bool linkC64PullATN = false;
    bool linkC64PullCLK = false;
    bool linkC64PullDATA = false;
    bool linkDrivePullCLK = false;
    bool linkDrivePullDATA = false;
    bool linkLineATNHigh = true;
    bool linkLineCLKHigh = true;
    bool linkLineDATAHigh = true;

    std::priority_queue<TimedEvent, std::vector<TimedEvent>, TimedEventCompare> events;

    SharedIecClockDomain(CIA6526 &c, Drive1541 &d, const IecBridgePolarity &p)
        : cia2(c), drive(d), polarity(p) {
        if (const char *driveHzEnv = std::getenv("IEC_DRIVE_HALF_HZ")) {
            const unsigned long long parsed = std::strtoull(driveHzEnv, nullptr, 10);
            if (parsed > 0ULL) {
                driveHalfRateHz = static_cast<uint64_t>(parsed);
            }
        }
        if (const char *c64HzEnv = std::getenv("IEC_C64_HALF_HZ")) {
            const unsigned long long parsed = std::strtoull(c64HzEnv, nullptr, 10);
            if (parsed > 0ULL) {
                c64HalfRateHz = static_cast<uint64_t>(parsed);
            }
        }
        if (const char *driftEnv = std::getenv("IEC_DRIVE_DRIFT_PPM")) {
            driveDriftPpm = static_cast<int32_t>(std::strtol(driftEnv, nullptr, 10));
        }
        if (const char *seedEnv = std::getenv("IEC_DRIVE_DITHER_SEED")) {
            ditherSeed = static_cast<uint32_t>(std::strtoul(seedEnv, nullptr, 10));
        }
        if (const char *ampEnv = std::getenv("IEC_DRIVE_DITHER_AMPLITUDE")) {
            ditherAmplitude = static_cast<int32_t>(std::strtol(ampEnv, nullptr, 10));
            if (ditherAmplitude < 0) {
                ditherAmplitude = 0;
            }
        }
        if (const char *v = std::getenv("IEC_LINK_C64_TO_BUS_UNITS")) {
            linkLatencyC64ToBus = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_DRIVE_TO_BUS_UNITS")) {
            linkLatencyDriveToBus = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_BUS_TO_C64_UNITS")) {
            linkLatencyBusToC64 = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_BUS_TO_DRIVE_UNITS")) {
            linkLatencyBusToDrive = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_JITTER_UNITS")) {
            linkJitterUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_SEED")) {
            linkJitterSeed = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        }

        bootstrapIecLink();
    }

    void configureDomainRatesForTest(uint64_t c64Hz, uint64_t driveHz, int32_t driftPpmValue, uint32_t seed, int32_t amp) {
        if (c64Hz > 0) {
            c64HalfRateHz = c64Hz;
        }
        if (driveHz > 0) {
            driveHalfRateHz = driveHz;
        }
        driveDriftPpm = driftPpmValue;
        ditherSeed = seed;
        ditherAmplitude = (amp < 0) ? 0 : amp;
    }

    uint64_t getCurrentTimeUnits() const {
        return nowUnits;
    }

    uint64_t getC64HalfTicks() const {
        return c64HalfTicks;
    }

    uint64_t getDriveHalfTicks() const {
        return driveHalfTicks;
    }

    void setC64DomainEnabled(bool enabled) {
        c64DomainEnabled = enabled;
        if (!enabled) {
            linkC64PullATN = false;
            linkC64PullCLK = false;
            linkC64PullDATA = false;
            settleBusAndPropagateSamples();
        }
    }

    void setDriveDomainEnabled(bool enabled) {
        driveDomainEnabled = enabled;
    }

    void scheduleEventAtAbsolute(uint64_t when, const std::function<void()> &callback) {
        events.push(TimedEvent{when, eventSeq++, callback});
    }

    void scheduleEventAfter(uint64_t delta, const std::function<void()> &callback) {
        scheduleEventAtAbsolute(nowUnits + delta, callback);
    }

    void scheduleEventAtNextC64Boundary(const std::function<void()> &callback) {
        scheduleEventAtAbsolute(nextC64Units, callback);
    }

    uint64_t effectiveDriveHalfRateHz() const {
        int64_t scaled = static_cast<int64_t>(driveHalfRateHz);
        scaled += static_cast<int64_t>((static_cast<long long>(driveHalfRateHz) * static_cast<long long>(driveDriftPpm)) / 1000000LL);
        if (scaled <= 0) {
            scaled = 1;
        }
        return static_cast<uint64_t>(scaled);
    }

    uint64_t nextDrivePeriodUnits() {
        const uint64_t effRate = effectiveDriveHalfRateHz();
        uint64_t base = (C64_HALF_PERIOD_UNITS * c64HalfRateHz + (effRate / 2ULL)) / effRate;
        if (base == 0) {
            base = 1;
        }

        if (ditherAmplitude <= 0) {
            return base;
        }

        ditherSeed = static_cast<uint32_t>(1664525u * ditherSeed + 1013904223u);
        const uint32_t span = static_cast<uint32_t>((ditherAmplitude * 2) + 1);
        const int32_t jitter = static_cast<int32_t>(ditherSeed % span) - ditherAmplitude;
        int64_t withJitter = static_cast<int64_t>(base) + static_cast<int64_t>(jitter);
        if (withJitter < 1) {
            withJitter = 1;
        }
        return static_cast<uint64_t>(withJitter);
    }

    uint64_t linkDelayWithJitter(uint64_t base) {
        uint64_t d = base;
        if (linkJitterUnits > 0) {
            linkJitterSeed = static_cast<uint32_t>(1664525u * linkJitterSeed + 1013904223u);
            d += (linkJitterSeed % (linkJitterUnits + 1));
        }
        if (d == 0) {
            d = 1;
        }
        return d;
    }

    void bootstrapIecLink() {
        const IecC64Signals sig = deriveIecC64Signals(cia2, polarity);
        linkC64PullATN = sig.c64PullATN;
        linkC64PullCLK = sig.c64PullCLK;
        linkC64PullDATA = sig.c64PullDATA;
        linkDrivePullCLK = drive.iecDrivePullCLK;
        linkDrivePullDATA = drive.iecDrivePullDATA;
        const IecResolvedLines lines = resolveIecLinesFromPulls(linkC64PullATN,
                                                                linkC64PullCLK,
                                                                linkC64PullDATA,
                                                                linkDrivePullCLK,
                                                                linkDrivePullDATA);
        linkLineATNHigh = lines.atnHigh;
        linkLineCLKHigh = lines.clkHigh;
        linkLineDATAHigh = lines.dataHigh;
        drive.setIecLines(linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh);
        applyIecInputsToCia(cia2, polarity, sig, lines);
    }

    void scheduleBusSettleFromC64Pulls(bool pullATN, bool pullCLK, bool pullDATA) {
        const uint64_t delay = linkDelayWithJitter(linkLatencyC64ToBus);
        scheduleEventAfter(delay, [this, pullATN, pullCLK, pullDATA]() {
            linkC64PullATN = pullATN;
            linkC64PullCLK = pullCLK;
            linkC64PullDATA = pullDATA;
            settleBusAndPropagateSamples();
        });
    }

    void scheduleBusSettleFromDrivePulls(bool pullCLK, bool pullDATA) {
        const uint64_t delay = linkDelayWithJitter(linkLatencyDriveToBus);
        scheduleEventAfter(delay, [this, pullCLK, pullDATA]() {
            linkDrivePullCLK = pullCLK;
            linkDrivePullDATA = pullDATA;
            settleBusAndPropagateSamples();
        });
    }

    void settleBusAndPropagateSamples() {
        const IecResolvedLines lines = resolveIecLinesFromPulls(linkC64PullATN,
                                                                linkC64PullCLK,
                                                                linkC64PullDATA,
                                                                linkDrivePullCLK,
                                                                linkDrivePullDATA);
        if (lines.atnHigh == linkLineATNHigh && lines.clkHigh == linkLineCLKHigh && lines.dataHigh == linkLineDATAHigh) {
            return;
        }

        linkLineATNHigh = lines.atnHigh;
        linkLineCLKHigh = lines.clkHigh;
        linkLineDATAHigh = lines.dataHigh;

        const uint64_t toDrive = linkDelayWithJitter(linkLatencyBusToDrive);
        scheduleEventAfter(toDrive, [this]() {
            drive.setIecLines(linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh);
        });

        const uint64_t toC64 = linkDelayWithJitter(linkLatencyBusToC64);
        scheduleEventAfter(toC64, [this]() {
            const IecC64Signals sigNow = deriveIecC64Signals(cia2, polarity);
            const IecResolvedLines linesNow = {linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh};
            applyIecInputsToCia(cia2, polarity, sigNow, linesNow);
        });
    }

    void executeTimedEventsAtNow() {
        while (!events.empty() && events.top().when <= nowUnits) {
            const TimedEvent ev = events.top();
            events.pop();
            if (ev.callback) {
                ev.callback();
            }
        }
    }

    void tickDriveDomainOnce() {
        if (!driveDomainEnabled) {
            nextDriveUnits += nextDrivePeriodUnits();
            return;
        }

        const bool prevPullCLK = drive.iecDrivePullCLK;
        const bool prevPullDATA = drive.iecDrivePullDATA;

        drive.tickIecHalfCycle();
        driveHalfTicks++;

        if (drive.iecDrivePullCLK != prevPullCLK || drive.iecDrivePullDATA != prevPullDATA) {
            scheduleBusSettleFromDrivePulls(drive.iecDrivePullCLK, drive.iecDrivePullDATA);
        }

        nextDriveUnits += nextDrivePeriodUnits();
    }

    void tickC64DomainOnce() {
        if (!c64DomainEnabled) {
            c64HalfTicks++;
            nextC64Units += C64_HALF_PERIOD_UNITS;
            return;
        }

        const bool cntHigh = (cia2.praInput & 0x40) != 0;
        const bool spHigh = (cia2.praInput & 0x80) != 0;
        cia2.setSerialPins(cntHigh, spHigh);

        const IecC64Signals pre = deriveIecC64Signals(cia2, polarity);
        cia2.cycleCore.tickHalfCycle(cia2);
        const IecC64Signals post = deriveIecC64Signals(cia2, polarity);

        if (post.c64PullATN != pre.c64PullATN || post.c64PullCLK != pre.c64PullCLK || post.c64PullDATA != pre.c64PullDATA) {
            scheduleBusSettleFromC64Pulls(post.c64PullATN, post.c64PullCLK, post.c64PullDATA);
        }

        c64HalfTicks++;
        nextC64Units += C64_HALF_PERIOD_UNITS;
    }

    void tickHalfCycle() {
        const uint64_t targetC64Tick = c64HalfTicks + 1;
        while (c64HalfTicks < targetC64Tick) {
            uint64_t nextTime = nextC64Units;
            if (nextDriveUnits < nextTime) {
                nextTime = nextDriveUnits;
            }
            if (!events.empty() && events.top().when < nextTime) {
                nextTime = events.top().when;
            }

            nowUnits = nextTime;
            executeTimedEventsAtNow();

            if (nextDriveUnits == nowUnits) {
                tickDriveDomainOnce();
                executeTimedEventsAtNow();
            }

            if (nextC64Units == nowUnits) {
                tickC64DomainOnce();
                executeTimedEventsAtNow();
            }
        }
    }
};

struct IecBusDomain {
    IIecHostEndpoint *hostEndpoint = nullptr;
    std::unique_ptr<IIecHostEndpoint> ownedHostEndpoint;
    IecBridgePolarity polarity;
    std::vector<IIecDeviceEndpoint *> attachedDevices;
    std::vector<std::unique_ptr<IIecDeviceEndpoint>> ownedDeviceEndpoints;
    struct TimedEvent {
        uint64_t when = 0;
        uint64_t seq = 0;
        std::function<void()> callback;
    };

    struct TimedEventCompare {
        bool operator()(const TimedEvent &a, const TimedEvent &b) const {
            if (a.when != b.when) {
                return a.when > b.when;
            }
            return a.seq > b.seq;
        }
    };

    static constexpr uint64_t C64_HALF_PERIOD_UNITS = 1000000ULL;

    uint64_t c64HalfRateHz = 985248ULL;
    uint64_t driveHalfRateHz = 1000000ULL;
    int32_t driveDriftPpm = 0;
    uint64_t nowUnits = 0;
    uint64_t nextC64Units = 0;
    uint64_t nextDriveUnits = 0;
    uint64_t c64HalfTicks = 0;
    uint64_t driveHalfTicks = 0;
    uint64_t eventSeq = 0;
    uint32_t ditherSeed = 0;
    int32_t ditherAmplitude = 0;
    uint64_t linkLatencyC64ToBus = 1;
    uint64_t linkLatencyDriveToBus = 1;
    uint64_t linkLatencyBusToC64 = 1;
    uint64_t linkLatencyBusToDrive = 1;
    uint64_t linkJitterUnits = 0;
    uint32_t linkJitterSeed = 0;
    bool c64DomainEnabled = true;
    bool driveDomainEnabled = true;
    bool temporalDebugEnabled = false;
    uint64_t temporalPhaseSeq = 0;
    uint64_t temporalCommitCount = 0;
    uint64_t temporalDoubleCommitSameTimestamp = 0;
    bool temporalHasLastCommitTimestamp = false;
    uint64_t temporalLastCommitTimestamp = 0;
    std::vector<IecTemporalTraceEvent> temporalTrace;
    IecEdgeOwner pendingEdgeOwner = IecEdgeOwner::None;
    IecEdgeCause pendingEdgeCause = IecEdgeCause::None;
    uint64_t pendingEdgeEffectiveDelayTicks = 0;
    bool lineModelEnabled = false;
    uint64_t lineAtnReleaseDelayUnits = 0;
    uint64_t lineClkReleaseDelayUnits = 0;
    uint64_t lineDataReleaseDelayUnits = 0;
    uint64_t lineAtnMinLowPulseUnits = 0;
    uint64_t lineClkMinLowPulseUnits = 0;
    uint64_t lineDataMinLowPulseUnits = 0;
    IecLineModelState atnModel;
    IecLineModelState clkModel;
    IecLineModelState dataModel;

    bool linkC64PullATN = false;
    bool linkC64PullCLK = false;
    bool linkC64PullDATA = false;
    bool linkDrivePullCLK = false;
    bool linkDrivePullDATA = false;
    bool linkLineATNHigh = true;
    bool linkLineCLKHigh = true;
    bool linkLineDATAHigh = true;

    std::priority_queue<TimedEvent, std::vector<TimedEvent>, TimedEventCompare> events;

    IecBusDomain(CIA6526 &c, IIecDevice &primaryDrive, const IecBridgePolarity &p)
        : polarity(p) {
        ownedHostEndpoint = std::unique_ptr<IIecHostEndpoint>(new CiaIecHostEndpoint(c));
        hostEndpoint = ownedHostEndpoint.get();
        ownedDeviceEndpoints.push_back(std::unique_ptr<IIecDeviceEndpoint>(new LegacyIecDeviceEndpointAdapter(primaryDrive)));
        attachedDevices.push_back(ownedDeviceEndpoints.back().get());
        initializeFromEnvironment();
        bootstrapIecLink();
    }

    IecBusDomain(IIecHostEndpoint &host, IIecDevice &primaryDrive, const IecBridgePolarity &p)
        : hostEndpoint(&host), polarity(p) {
        ownedDeviceEndpoints.push_back(std::unique_ptr<IIecDeviceEndpoint>(new LegacyIecDeviceEndpointAdapter(primaryDrive)));
        attachedDevices.push_back(ownedDeviceEndpoints.back().get());
        initializeFromEnvironment();
        bootstrapIecLink();
    }

    IecBusDomain(IIecHostEndpoint &host, IIecDeviceEndpoint &primaryDevice, const IecBridgePolarity &p)
        : hostEndpoint(&host), polarity(p) {
        attachedDevices.push_back(&primaryDevice);
        initializeFromEnvironment();
        bootstrapIecLink();
    }

    void initializeFromEnvironment() {
        if (const char *driveHzEnv = std::getenv("IEC_DRIVE_HALF_HZ")) {
            const unsigned long long parsed = std::strtoull(driveHzEnv, nullptr, 10);
            if (parsed > 0ULL) {
                driveHalfRateHz = static_cast<uint64_t>(parsed);
            }
        }
        if (const char *c64HzEnv = std::getenv("IEC_C64_HALF_HZ")) {
            const unsigned long long parsed = std::strtoull(c64HzEnv, nullptr, 10);
            if (parsed > 0ULL) {
                c64HalfRateHz = static_cast<uint64_t>(parsed);
            }
        }
        if (const char *driftEnv = std::getenv("IEC_DRIVE_DRIFT_PPM")) {
            driveDriftPpm = static_cast<int32_t>(std::strtol(driftEnv, nullptr, 10));
        }
        if (const char *seedEnv = std::getenv("IEC_DRIVE_DITHER_SEED")) {
            ditherSeed = static_cast<uint32_t>(std::strtoul(seedEnv, nullptr, 10));
        }
        if (const char *ampEnv = std::getenv("IEC_DRIVE_DITHER_AMPLITUDE")) {
            ditherAmplitude = static_cast<int32_t>(std::strtol(ampEnv, nullptr, 10));
            if (ditherAmplitude < 0) {
                ditherAmplitude = 0;
            }
        }
        if (const char *v = std::getenv("IEC_LINK_C64_TO_BUS_UNITS")) {
            linkLatencyC64ToBus = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_DRIVE_TO_BUS_UNITS")) {
            linkLatencyDriveToBus = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_BUS_TO_C64_UNITS")) {
            linkLatencyBusToC64 = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_BUS_TO_DRIVE_UNITS")) {
            linkLatencyBusToDrive = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_JITTER_UNITS")) {
            linkJitterUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINK_SEED")) {
            linkJitterSeed = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        }
        if (std::getenv("IEC_TEMPORAL_DEBUG") != nullptr) {
            temporalDebugEnabled = true;
        }
        if (std::getenv("IEC_SUBCYCLE_LINE_MODEL") != nullptr) {
            lineModelEnabled = true;
            lineAtnReleaseDelayUnits = 1;
            lineClkReleaseDelayUnits = 1;
            lineDataReleaseDelayUnits = 1;
            lineAtnMinLowPulseUnits = 1;
            lineClkMinLowPulseUnits = 1;
            lineDataMinLowPulseUnits = 1;
        }
        if (const char *v = std::getenv("IEC_LINE_ATN_RELEASE_DELAY_UNITS")) {
            lineAtnReleaseDelayUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINE_CLK_RELEASE_DELAY_UNITS")) {
            lineClkReleaseDelayUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINE_DATA_RELEASE_DELAY_UNITS")) {
            lineDataReleaseDelayUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINE_ATN_MIN_LOW_UNITS")) {
            lineAtnMinLowPulseUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINE_CLK_MIN_LOW_UNITS")) {
            lineClkMinLowPulseUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
        if (const char *v = std::getenv("IEC_LINE_DATA_MIN_LOW_UNITS")) {
            lineDataMinLowPulseUnits = static_cast<uint64_t>(std::strtoull(v, nullptr, 10));
        }
    }

    void setTemporalDebugEnabled(bool enabled) {
        temporalDebugEnabled = enabled;
        if (!enabled) {
            temporalTrace.clear();
        }
    }

    void clearTemporalTrace() {
        temporalTrace.clear();
        temporalPhaseSeq = 0;
        temporalCommitCount = 0;
        temporalDoubleCommitSameTimestamp = 0;
        temporalHasLastCommitTimestamp = false;
        temporalLastCommitTimestamp = 0;
        pendingEdgeOwner = IecEdgeOwner::None;
        pendingEdgeCause = IecEdgeCause::None;
        pendingEdgeEffectiveDelayTicks = 0;
    }

    const std::vector<IecTemporalTraceEvent> &getTemporalTrace() const {
        return temporalTrace;
    }

    uint64_t getTemporalCommitCount() const {
        return temporalCommitCount;
    }

    uint64_t getTemporalDoubleCommitSameTimestampCount() const {
        return temporalDoubleCommitSameTimestamp;
    }

    void attachDrive(IIecDevice &drive) {
        for (const std::unique_ptr<IIecDeviceEndpoint> &owned : ownedDeviceEndpoints) {
            const LegacyIecDeviceEndpointAdapter *legacy = dynamic_cast<const LegacyIecDeviceEndpointAdapter *>(owned.get());
            if (legacy != nullptr && legacy->device == &drive) {
                return;
            }
        }
        ownedDeviceEndpoints.push_back(std::unique_ptr<IIecDeviceEndpoint>(new LegacyIecDeviceEndpointAdapter(drive)));
        attachedDevices.push_back(ownedDeviceEndpoints.back().get());
        ownedDeviceEndpoints.back()->setLines(linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh);
        scheduleBusSettleFromDrivePulls(anyDrivePullCLK(), anyDrivePullDATA());
    }

    void attachDeviceEndpoint(IIecDeviceEndpoint &endpoint) {
        for (IIecDeviceEndpoint *existing : attachedDevices) {
            if (existing == &endpoint) {
                return;
            }
        }
        attachedDevices.push_back(&endpoint);
        endpoint.setLines(linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh);
        scheduleBusSettleFromDrivePulls(anyDrivePullCLK(), anyDrivePullDATA());
    }

    size_t driveCount() const {
        return attachedDevices.size();
    }

    void configureDomainRatesForTest(uint64_t c64Hz, uint64_t driveHz, int32_t driftPpmValue, uint32_t seed, int32_t amp) {
        if (c64Hz > 0) {
            c64HalfRateHz = c64Hz;
        }
        if (driveHz > 0) {
            driveHalfRateHz = driveHz;
        }
        driveDriftPpm = driftPpmValue;
        ditherSeed = seed;
        ditherAmplitude = (amp < 0) ? 0 : amp;
    }

    void configureLineModelForTest(bool enabled,
                                   uint64_t atnReleaseDelay,
                                   uint64_t clkReleaseDelay,
                                   uint64_t dataReleaseDelay,
                                   uint64_t atnMinLow,
                                   uint64_t clkMinLow,
                                   uint64_t dataMinLow) {
        lineModelEnabled = enabled;
        lineAtnReleaseDelayUnits = atnReleaseDelay;
        lineClkReleaseDelayUnits = clkReleaseDelay;
        lineDataReleaseDelayUnits = dataReleaseDelay;
        lineAtnMinLowPulseUnits = atnMinLow;
        lineClkMinLowPulseUnits = clkMinLow;
        lineDataMinLowPulseUnits = dataMinLow;
        atnModel = IecLineModelState{linkLineATNHigh, nowUnits, false, 0};
        clkModel = IecLineModelState{linkLineCLKHigh, nowUnits, false, 0};
        dataModel = IecLineModelState{linkLineDATAHigh, nowUnits, false, 0};
    }

    uint64_t getCurrentTimeUnits() const {
        return nowUnits;
    }

    uint64_t getC64HalfTicks() const {
        return c64HalfTicks;
    }

    uint64_t getDriveHalfTicks() const {
        return driveHalfTicks;
    }

    void setC64DomainEnabled(bool enabled) {
        c64DomainEnabled = enabled;
        if (!enabled) {
            linkC64PullATN = false;
            linkC64PullCLK = false;
            linkC64PullDATA = false;
            settleBusAndPropagateSamples();
        }
    }

    void setDriveDomainEnabled(bool enabled) {
        driveDomainEnabled = enabled;
    }

    void scheduleEventAtAbsolute(uint64_t when, const std::function<void()> &callback) {
        events.push(TimedEvent{when, eventSeq++, callback});
    }

    void scheduleEventAfter(uint64_t delta, const std::function<void()> &callback) {
        scheduleEventAtAbsolute(nowUnits + delta, callback);
    }

    void scheduleEventAtNextC64Boundary(const std::function<void()> &callback) {
        scheduleEventAtAbsolute(nextC64Units, callback);
    }

    uint64_t effectiveDriveHalfRateHz() const {
        int64_t scaled = static_cast<int64_t>(driveHalfRateHz);
        scaled += static_cast<int64_t>((static_cast<long long>(driveHalfRateHz) * static_cast<long long>(driveDriftPpm)) / 1000000LL);
        if (scaled <= 0) {
            scaled = 1;
        }
        return static_cast<uint64_t>(scaled);
    }

    uint64_t nextDrivePeriodUnits() {
        const uint64_t effRate = effectiveDriveHalfRateHz();
        uint64_t base = (C64_HALF_PERIOD_UNITS * c64HalfRateHz + (effRate / 2ULL)) / effRate;
        if (base == 0) {
            base = 1;
        }

        if (ditherAmplitude <= 0) {
            return base;
        }

        ditherSeed = static_cast<uint32_t>(1664525u * ditherSeed + 1013904223u);
        const uint32_t span = static_cast<uint32_t>((ditherAmplitude * 2) + 1);
        const int32_t jitter = static_cast<int32_t>(ditherSeed % span) - ditherAmplitude;
        int64_t withJitter = static_cast<int64_t>(base) + static_cast<int64_t>(jitter);
        if (withJitter < 1) {
            withJitter = 1;
        }
        return static_cast<uint64_t>(withJitter);
    }

    uint64_t linkDelayWithJitter(uint64_t base) {
        uint64_t d = base;
        if (linkJitterUnits > 0) {
            linkJitterSeed = static_cast<uint32_t>(1664525u * linkJitterSeed + 1013904223u);
            d += (linkJitterSeed % (linkJitterUnits + 1));
        }
        if (d == 0) {
            d = 1;
        }
        return d;
    }

    bool anyDrivePullCLK() const {
        for (const IIecDeviceEndpoint *device : attachedDevices) {
            if (device != nullptr && device->getPullCLK()) {
                return true;
            }
        }
        return false;
    }

    bool anyDrivePullDATA() const {
        for (const IIecDeviceEndpoint *device : attachedDevices) {
            if (device != nullptr && device->getPullDATA()) {
                return true;
            }
        }
        return false;
    }

    void propagateLinesToDrives() {
        for (IIecDeviceEndpoint *device : attachedDevices) {
            if (device != nullptr) {
                device->setLines(linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh);
            }
        }
    }

    void bootstrapIecLink() {
        const IecC64Signals sig = hostEndpoint ? hostEndpoint->deriveSignals(polarity) : IecC64Signals{};
        linkC64PullATN = sig.c64PullATN;
        linkC64PullCLK = sig.c64PullCLK;
        linkC64PullDATA = sig.c64PullDATA;
        linkDrivePullCLK = anyDrivePullCLK();
        linkDrivePullDATA = anyDrivePullDATA();
        const IecResolvedLines lines = resolveIecLinesFromPulls(linkC64PullATN,
                                                                linkC64PullCLK,
                                                                linkC64PullDATA,
                                                                linkDrivePullCLK,
                                                                linkDrivePullDATA);
        linkLineATNHigh = lines.atnHigh;
        linkLineCLKHigh = lines.clkHigh;
        linkLineDATAHigh = lines.dataHigh;
        atnModel = IecLineModelState{linkLineATNHigh, nowUnits, false, 0};
        clkModel = IecLineModelState{linkLineCLKHigh, nowUnits, false, 0};
        dataModel = IecLineModelState{linkLineDATAHigh, nowUnits, false, 0};
        propagateLinesToDrives();
        if (hostEndpoint) {
            hostEndpoint->applyInputs(polarity, sig, lines);
        }
    }

    void scheduleRiseReeval(IecLineModelState *line, uint64_t when, IecEdgeCause cause) {
        if (line->riseEventPending && line->riseEventWhen == when) {
            return;
        }
        const uint64_t effectiveDelay = (when > nowUnits) ? (when - nowUnits) : 0;
        line->riseEventPending = true;
        line->riseEventWhen = when;
        scheduleEventAtAbsolute(when, [this, line, cause, effectiveDelay]() {
            pendingEdgeOwner = IecEdgeOwner::LineModel;
            pendingEdgeCause = cause;
            pendingEdgeEffectiveDelayTicks = effectiveDelay;
            line->riseEventPending = false;
            settleBusAndPropagateSamples();
        });
    }

    bool applyLineTimingModel(bool desiredHigh,
                              IecLineModelState &line,
                              uint64_t releaseDelay,
                              uint64_t minLowPulse) {
        if (!lineModelEnabled) {
            line.levelHigh = desiredHigh;
            if (!desiredHigh) {
                line.lowSince = nowUnits;
            }
            return desiredHigh;
        }

        if (!desiredHigh) {
            line.riseEventPending = false;
            if (line.levelHigh) {
                line.levelHigh = false;
                line.lowSince = nowUnits;
            }
            return false;
        }

        if (line.levelHigh) {
            return true;
        }

        uint64_t earliestRise = line.lowSince;
        if (minLowPulse > 0) {
            earliestRise += minLowPulse;
        }
        if (releaseDelay > 0) {
            earliestRise += releaseDelay;
        }
        if (nowUnits >= earliestRise) {
            line.levelHigh = true;
            line.riseEventPending = false;
            return true;
        }

        const bool minPulseDeferred = (minLowPulse > 0) && (nowUnits < (line.lowSince + minLowPulse));
        const IecEdgeCause cause = minPulseDeferred ? IecEdgeCause::MinPulse : IecEdgeCause::ReleaseDelay;
        scheduleRiseReeval(&line, earliestRise, cause);
        return false;
    }

    void scheduleBusSettleFromC64Pulls(bool pullATN, bool pullCLK, bool pullDATA) {
        const uint64_t delay = linkDelayWithJitter(linkLatencyC64ToBus);
        scheduleEventAfter(delay, [this, pullATN, pullCLK, pullDATA, delay]() {
            pendingEdgeOwner = IecEdgeOwner::C64;
            pendingEdgeCause = IecEdgeCause::PullChange;
            pendingEdgeEffectiveDelayTicks = delay;
            linkC64PullATN = pullATN;
            linkC64PullCLK = pullCLK;
            linkC64PullDATA = pullDATA;
            settleBusAndPropagateSamples();
        });
    }

    void scheduleBusSettleFromDrivePulls(bool pullCLK, bool pullDATA) {
        const uint64_t delay = linkDelayWithJitter(linkLatencyDriveToBus);
        scheduleEventAfter(delay, [this, pullCLK, pullDATA, delay]() {
            pendingEdgeOwner = IecEdgeOwner::Drive;
            pendingEdgeCause = IecEdgeCause::PullChange;
            pendingEdgeEffectiveDelayTicks = delay;
            linkDrivePullCLK = pullCLK;
            linkDrivePullDATA = pullDATA;
            settleBusAndPropagateSamples();
        });
    }

    void logTemporalPhase(IecTemporalPhase phase,
                          IecEdgeOwner owner = IecEdgeOwner::None,
                          IecEdgeCause cause = IecEdgeCause::None,
                          uint64_t effectiveDelayTicks = 0) {
        if (!temporalDebugEnabled) {
            return;
        }
        temporalTrace.push_back(IecTemporalTraceEvent{
            nowUnits,
            temporalPhaseSeq++,
            phase,
            owner,
            cause,
            effectiveDelayTicks,
            linkLineATNHigh,
            linkLineCLKHigh,
            linkLineDATAHigh
        });
    }

    void applyTemporalBusContract(const IecResolvedLines &lines) {
        if (lines.atnHigh == linkLineATNHigh && lines.clkHigh == linkLineCLKHigh && lines.dataHigh == linkLineDATAHigh) {
            return;
        }

        linkLineATNHigh = lines.atnHigh;
        linkLineCLKHigh = lines.clkHigh;
        linkLineDATAHigh = lines.dataHigh;

        if (temporalHasLastCommitTimestamp && temporalLastCommitTimestamp == nowUnits) {
            temporalDoubleCommitSameTimestamp++;
        }
        temporalHasLastCommitTimestamp = true;
        temporalLastCommitTimestamp = nowUnits;
        temporalCommitCount++;
        logTemporalPhase(IecTemporalPhase::CommitEdge,
                         pendingEdgeOwner,
                         pendingEdgeCause,
                         pendingEdgeEffectiveDelayTicks);
        pendingEdgeOwner = IecEdgeOwner::None;
        pendingEdgeCause = IecEdgeCause::None;
        pendingEdgeEffectiveDelayTicks = 0;

        const uint64_t toDrive = linkDelayWithJitter(linkLatencyBusToDrive);
        scheduleEventAfter(toDrive, [this]() {
            propagateLinesToDrives();
        });

        const uint64_t toC64 = linkDelayWithJitter(linkLatencyBusToC64);
        scheduleEventAfter(toC64, [this]() {
            if (!hostEndpoint) {
                return;
            }
            const IecC64Signals sigNow = hostEndpoint->deriveSignals(polarity);
            const IecResolvedLines linesNow = {linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh};
            hostEndpoint->applyInputs(polarity, sigNow, linesNow);
        });
    }

    void settleBusAndPropagateSamples() {
        const IecResolvedLines desired = resolveIecLinesFromPulls(linkC64PullATN,
                                                                  linkC64PullCLK,
                                                                  linkC64PullDATA,
                                                                  linkDrivePullCLK,
                                                                  linkDrivePullDATA);
        IecResolvedLines modeled = desired;
        modeled.atnHigh = applyLineTimingModel(desired.atnHigh, atnModel, lineAtnReleaseDelayUnits, lineAtnMinLowPulseUnits);
        modeled.clkHigh = applyLineTimingModel(desired.clkHigh, clkModel, lineClkReleaseDelayUnits, lineClkMinLowPulseUnits);
        modeled.dataHigh = applyLineTimingModel(desired.dataHigh, dataModel, lineDataReleaseDelayUnits, lineDataMinLowPulseUnits);
        applyTemporalBusContract(modeled);
    }

    void executeTimedEventsAtNow() {
        while (!events.empty() && events.top().when <= nowUnits) {
            const TimedEvent ev = events.top();
            events.pop();
            if (ev.callback) {
                ev.callback();
            }
        }
    }

    void tickDriveDomainOnce() {
        if (!driveDomainEnabled) {
            nextDriveUnits += nextDrivePeriodUnits();
            return;
        }

        const bool prevPullCLK = anyDrivePullCLK();
        const bool prevPullDATA = anyDrivePullDATA();

        for (IIecDeviceEndpoint *device : attachedDevices) {
            if (device != nullptr) {
                device->tickHalfCycle();
            }
        }
        driveHalfTicks++;

        const bool nextPullCLK = anyDrivePullCLK();
        const bool nextPullDATA = anyDrivePullDATA();
        if (nextPullCLK != prevPullCLK || nextPullDATA != prevPullDATA) {
            scheduleBusSettleFromDrivePulls(nextPullCLK, nextPullDATA);
        }

        nextDriveUnits += nextDrivePeriodUnits();
    }

    void tickC64DomainOnce() {
        if (!c64DomainEnabled || hostEndpoint == nullptr) {
            c64HalfTicks++;
            nextC64Units += C64_HALF_PERIOD_UNITS;
            return;
        }

        const IecResolvedLines linesNow = {linkLineATNHigh, linkLineCLKHigh, linkLineDATAHigh};
        hostEndpoint->setSerialPins(linesNow.clkHigh, linesNow.dataHigh);

        const IecC64Signals pre = hostEndpoint->deriveSignals(polarity);
        hostEndpoint->tickHalfCycle();
        const IecC64Signals post = hostEndpoint->deriveSignals(polarity);

        if (post.c64PullATN != pre.c64PullATN || post.c64PullCLK != pre.c64PullCLK || post.c64PullDATA != pre.c64PullDATA) {
            scheduleBusSettleFromC64Pulls(post.c64PullATN, post.c64PullCLK, post.c64PullDATA);
        }

        c64HalfTicks++;
        nextC64Units += C64_HALF_PERIOD_UNITS;
    }

    void tickHalfCycle() {
        const uint64_t targetC64Tick = c64HalfTicks + 1;
        while (c64HalfTicks < targetC64Tick) {
            uint64_t nextTime = nextC64Units;
            if (nextDriveUnits < nextTime) {
                nextTime = nextDriveUnits;
            }
            if (!events.empty() && events.top().when < nextTime) {
                nextTime = events.top().when;
            }

            nowUnits = nextTime;
            executeTimedEventsAtNow();
            logTemporalPhase(IecTemporalPhase::Sample);

            if (nextDriveUnits == nowUnits) {
                logTemporalPhase(IecTemporalPhase::DriveUpdate);
                tickDriveDomainOnce();
                executeTimedEventsAtNow();
            }

            if (nextC64Units == nowUnits) {
                logTemporalPhase(IecTemporalPhase::HostUpdate);
                tickC64DomainOnce();
                executeTimedEventsAtNow();
            }
        }
    }
};
