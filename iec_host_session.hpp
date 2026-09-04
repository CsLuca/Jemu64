#pragma once

class IecHostSession {
public:
    CIA6526 &cia2;
    Drive1541 &drive;

    uint64_t stepCount = 0;
    uint64_t retryCount = 0;
    uint64_t timeoutCount = 0;

    static constexpr uint32_t WAIT_READY_TICKS = 192;
    static constexpr uint32_t DEFAULT_RETRIES = 8;

    IecHostSession(CIA6526 &c, Drive1541 &d) : cia2(c), drive(d) {}

    void driveLines(bool atnHigh, bool clkHigh, bool dataHigh) {
        iecHostDriveLines(cia2, drive, atnHigh, clkHigh, dataHigh);
    }

    void step() {
        iecHostStep(cia2, drive);
        stepCount++;
    }

    void recoverBus(uint32_t settleTicks = 4) {
        driveLines(true, true, true);
        for (uint32_t i = 0; i < settleTicks; ++i) {
            step();
        }
    }

    bool waitBusReleased(uint32_t maxTicks = WAIT_READY_TICKS, bool requireDataHigh = true) {
        for (uint32_t i = 0; i < maxTicks; ++i) {
            const uint8_t portA = cia2.getPortACombined();
            const bool clkHigh = (portA & 0x40) != 0;
            const bool dataHigh = (portA & 0x80) != 0;
            if (clkHigh && (!requireDataHigh || dataHigh)) {
                return true;
            }
            step();
        }
        timeoutCount++;
        return false;
    }

    bool sendBit(bool atnHigh, uint8_t bit, uint32_t retries = DEFAULT_RETRIES) {
        for (uint32_t attempt = 0; attempt <= retries; ++attempt) {
            if (!waitBusReleased(WAIT_READY_TICKS, false)) {
                retryCount++;
                recoverBus();
                continue;
            }
            const bool dataHigh = (bit != 0);
            driveLines(atnHigh, false, dataHigh);
            step();
            driveLines(atnHigh, true, dataHigh);
            step();
            return true;
        }
        recoverBus(8);
        return false;
    }

    bool sendByte(bool atnHigh, uint8_t byte, uint32_t retries = DEFAULT_RETRIES) {
        for (int i = 0; i < 8; ++i) {
            if (!sendBit(atnHigh, static_cast<uint8_t>((byte >> i) & 0x01), retries)) {
                return false;
            }
        }
        return true;
    }

    bool readByte(uint8_t &value, uint32_t retries = DEFAULT_RETRIES) {
        uint8_t v = 0;
        for (int i = 0; i < 8; ++i) {
            bool ok = false;
            for (uint32_t attempt = 0; attempt <= retries; ++attempt) {
                if (!waitBusReleased(WAIT_READY_TICKS, false)) {
                    retryCount++;
                    recoverBus();
                    continue;
                }
                driveLines(true, false, true);
                step();
                driveLines(true, true, true);
                step();
                const bool dataInHigh = (cia2.getPortACombined() & 0x80) != 0;
                v = static_cast<uint8_t>(v | ((dataInHigh ? 1 : 0) << i));
                ok = true;
                break;
            }
            if (!ok) {
                recoverBus(8);
                return false;
            }
        }
        value = v;
        return true;
    }

    void eoiAckPulse() {
        driveLines(true, true, false);
        step();
        driveLines(true, true, true);
        step();
    }
};
