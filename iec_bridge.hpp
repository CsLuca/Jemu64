#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <queue>
#include <vector>

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
    const IecResolvedLines lines = resolveIecLinesFromPulls(sig.c64PullATN,
                                                            sig.c64PullCLK,
                                                            sig.c64PullDATA,
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
