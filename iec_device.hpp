#pragma once

class IIecDevice {
public:
    virtual ~IIecDevice() {}

    virtual void tickIecHalfCycle() = 0;
    virtual void setIecLines(bool atnHigh, bool clkHigh, bool dataHigh) = 0;
    virtual bool getIecDrivePullCLK() const = 0;
    virtual bool getIecDrivePullDATA() const = 0;

    virtual void configureIecPhysicalProfile(uint64_t rxSetupTicks,
                                             uint64_t rxHoldTicks,
                                             uint64_t timeoutHysteresisTicks) {
        (void)rxSetupTicks;
        (void)rxHoldTicks;
        (void)timeoutHysteresisTicks;
    }

    virtual void configureIecProtocolTiming(uint64_t controllerBitHoldTicks,
                                            uint64_t deviceBitHoldTicks,
                                            uint64_t controllerBetweenBytesTicks,
                                            uint64_t deviceBetweenBytesTicks,
                                            uint64_t atnResponseTimeoutTicks,
                                            uint64_t deviceNotPresentTimeoutTicks,
                                            uint64_t senderTimeoutTicks,
                                            uint64_t receiverTimeoutTicks,
                                            uint64_t eoiSignalMinTicks,
                                            uint64_t eoiSignalMaxTicks,
                                            uint64_t emptyStreamTimeoutTicks) {
        (void)controllerBitHoldTicks;
        (void)deviceBitHoldTicks;
        (void)controllerBetweenBytesTicks;
        (void)deviceBetweenBytesTicks;
        (void)atnResponseTimeoutTicks;
        (void)deviceNotPresentTimeoutTicks;
        (void)senderTimeoutTicks;
        (void)receiverTimeoutTicks;
        (void)eoiSignalMinTicks;
        (void)eoiSignalMaxTicks;
        (void)emptyStreamTimeoutTicks;
    }
};
