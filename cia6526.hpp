#pragma once

#include <cstdint>
#include <functional>

class CIA6526;

class CIA6526CycleCore {
public:
    bool halfPhase = false;
    bool prevCntHigh = true;
    bool prevFlagHigh = true;

    void setSerialPins(CIA6526 &cia, bool cntHigh, bool spHigh);
    void setFlagPin(CIA6526 &cia, bool flagHigh);
    void tickHalfCycle(CIA6526 &cia);
    uint8_t read(CIA6526 &cia, uint8_t reg, uint8_t busVal);
    uint8_t peek(const CIA6526 &cia, uint8_t reg, uint8_t busVal) const;
    void write(CIA6526 &cia, uint8_t reg, uint8_t val);
};

class CIA6526 {
public:
    enum Revision : uint8_t {
        REV_6526 = 0,
        REV_6526A = 1,
        REV_6526R4 = 2
    };

    struct RevisionProfile {
        Revision revision = REV_6526;
        uint16_t todDividerReload = 10;
        bool serialOutputNeedsHalfPhaseHigh = true;
        bool todRead10thClearsLatch = true;
        bool alarmWriteUsesCrbBit7 = true;
        bool todWriteStopsClock = true;
        bool todReadHoursLatches = true;
        bool flagIrqImmediate = false;
        bool serialInputShiftOnFallingEdge = false;
        uint16_t serialShiftDividerReload = 8;
    };

    static constexpr RevisionProfile makeRevisionProfile(Revision rev) {
        return (rev == REV_6526A)
            ? RevisionProfile{REV_6526A, 12, true, false, true, false, true, true, true, 7}
            : (rev == REV_6526R4)
                ? RevisionProfile{REV_6526R4, 11, true, false, true, false, true, true, true, 7}
            : RevisionProfile{REV_6526, 10, true, true, true, true, true, false, false, 8};
    }

    Revision revision = REV_6526;
    RevisionProfile revisionProfile = makeRevisionProfile(REV_6526);

    void setRevision(Revision rev) {
        revision = rev;
        revisionProfile = makeRevisionProfile(rev);
    }

    Revision getRevision() const {
        return revision;
    }

    const RevisionProfile &getRevisionProfile() const {
        return revisionProfile;
    }

    uint8_t pra = 0, prb = 0;
    uint8_t praInput = 0xFF, prbInput = 0xFF;
    uint8_t ddra = 0, ddrb = 0;
    uint8_t t1l_lo = 0xFF, t1l_hi = 0xFF;
    uint8_t t1c_lo = 0xFF, t1c_hi = 0xFF;
    uint8_t t1_ctrl = 0;
    uint8_t t2l_lo = 0xFF, t2l_hi = 0xFF;
    uint8_t t2c_lo = 0xFF, t2c_hi = 0xFF;
    uint8_t t2_ctrl = 0;
    uint8_t tod_10ths = 0;
    uint8_t tod_seconds = 0;
    uint8_t tod_minutes = 0;
    uint8_t tod_hours = 1;
    uint8_t tod_alarm_10ths = 0;
    uint8_t tod_alarm_seconds = 0;
    uint8_t tod_alarm_minutes = 0;
    uint8_t tod_alarm_hours = 0;
    uint8_t tod_latch_10ths = 0;
    uint8_t tod_latch_seconds = 0;
    uint8_t tod_latch_minutes = 0;
    uint8_t tod_latch_hours = 0;
    bool todReadLatched = false;
    bool todRunning = true;
    uint16_t todTickDivider = 0;
    uint8_t ira = 0, irb = 0;
    uint8_t icr = 0;
    uint8_t ier = 0;
    uint8_t controlA = 0;

    std::function<void(bool)> signalIRQ = nullptr;

    uint16_t timer1_count = 0xFFFF;
    uint16_t timer1_latch = 0xFFFF;
    uint16_t timer2_count = 0xFFFF;
    uint16_t timer2_latch = 0xFFFF;

    bool timer1_one_shot = false;
    bool timer1_started = false;
    bool timer2_started = false;
    bool timer2_one_shot = false;
    uint8_t serialDataReg = 0;
    uint8_t serialShiftReg = 0;
    bool serialShiftInProgress = false;
    uint8_t serialShiftBitCount = 0;
    uint8_t serialOutputBitCount = 0;
    uint16_t serialShiftDivider = 0;
    static constexpr uint16_t SERIAL_SHIFT_DIVIDER_RELOAD = 8;
    bool serialCntHigh = true;
    bool serialSpHigh = true;
    bool serialPrevCntHigh = true;
    bool flagPinHigh = true;
    bool flagPrevHigh = true;
    uint8_t icrDeferredEvents = 0;

    uint64_t timer1UnderflowCount = 0;
    uint64_t timer2UnderflowCount = 0;
    uint64_t serialRxByteCount = 0;
    uint64_t serialTxByteCount = 0;
    uint64_t todAlarmMatchCount = 0;

    CIA6526CycleCore cycleCore;
    uint8_t praReadbackOverrideMask = 0;
    uint8_t praReadbackOverrideBits = 0;

    uint8_t getPortACombined() const {
        uint8_t combined = static_cast<uint8_t>((pra & ddra) | (praInput & static_cast<uint8_t>(~ddra)));
        if (praReadbackOverrideMask != 0) {
            combined = static_cast<uint8_t>((combined & static_cast<uint8_t>(~praReadbackOverrideMask)) |
                                            (praReadbackOverrideBits & praReadbackOverrideMask));
        }
        return combined;
    }

    uint8_t getPortBCombined() const {
        return static_cast<uint8_t>((prb & ddrb) | (prbInput & static_cast<uint8_t>(~ddrb)));
    }

    void setIcrEvent(uint8_t bitMask) {
        const uint8_t mask = static_cast<uint8_t>(bitMask & 0x1F);
        icr = static_cast<uint8_t>(icr | mask);
        if ((ier & mask) != 0) {
            icr = static_cast<uint8_t>(icr | 0x80);
        }
        updateIRQLine();
    }

    void deferIcrEvent(uint8_t bitMask) {
        icrDeferredEvents = static_cast<uint8_t>(icrDeferredEvents | (bitMask & 0x1F));
    }

    void clearIcrMask(uint8_t bitMask) {
        const uint8_t mask = static_cast<uint8_t>(bitMask & 0x1F);
        icr = static_cast<uint8_t>(icr & static_cast<uint8_t>(~mask));
        if ((icr & ier & 0x1F) != 0) {
            icr = static_cast<uint8_t>(icr | 0x80);
        } else {
            icr = static_cast<uint8_t>(icr & 0x7F);
        }
        updateIRQLine();
    }

    void setSerialPins(bool cntHigh, bool spHigh) {
        cycleCore.setSerialPins(*this, cntHigh, spHigh);
    }

    void setFlagPin(bool flagHigh) {
        cycleCore.setFlagPin(*this, flagHigh);
    }

    void tick() {
        cycleCore.tickHalfCycle(*this);
        cycleCore.tickHalfCycle(*this);
    }

    void updateIRQLine() {
        bool irq_active = (icr & 0x80) != 0;
        if (signalIRQ) signalIRQ(irq_active);
    }

    static bool bcdIncrement(uint8_t &v, uint8_t maxLow, uint8_t maxHigh) {
        uint8_t lo = static_cast<uint8_t>(v & 0x0F);
        uint8_t hi = static_cast<uint8_t>((v >> 4) & 0x0F);
        lo = static_cast<uint8_t>(lo + 1);
        if (lo > maxLow) {
            lo = 0;
            hi = static_cast<uint8_t>(hi + 1);
            if (hi > maxHigh) {
                hi = 0;
                v = static_cast<uint8_t>((hi << 4) | lo);
                return true;
            }
        }
        v = static_cast<uint8_t>((hi << 4) | lo);
        return false;
    }

    void incrementTodClock() {
        bool carry = bcdIncrement(tod_10ths, 9, 0);
        if (!carry) {
            return;
        }
        carry = bcdIncrement(tod_seconds, 9, 5);
        if (!carry) {
            return;
        }
        carry = bcdIncrement(tod_minutes, 9, 5);
        if (!carry) {
            return;
        }

        uint8_t hour = static_cast<uint8_t>(tod_hours & 0x1F);
        uint8_t pm = static_cast<uint8_t>(tod_hours & 0x80);
        uint8_t lo = static_cast<uint8_t>(hour & 0x0F);
        uint8_t hi = static_cast<uint8_t>((hour >> 4) & 0x01);

        const uint8_t current = static_cast<uint8_t>(hi * 10 + lo);
        uint8_t next = static_cast<uint8_t>(current + 1);
        if (next > 12) {
            next = 1;
        }
        if (next == 12) {
            pm ^= 0x80;
        }

        const uint8_t nextHi = static_cast<uint8_t>(next / 10);
        const uint8_t nextLo = static_cast<uint8_t>(next % 10);
        tod_hours = static_cast<uint8_t>(pm | (nextHi << 4) | nextLo);
    }

    void evaluateTodAlarm() {
        if (tod_10ths == tod_alarm_10ths &&
            tod_seconds == tod_alarm_seconds &&
            tod_minutes == tod_alarm_minutes &&
            tod_hours == tod_alarm_hours) {
            todAlarmMatchCount++;
            deferIcrEvent(static_cast<uint8_t>(1u << 2));
        }
    }

    uint8_t read(uint16_t addr, uint8_t busVal) {
        return cycleCore.read(*this, static_cast<uint8_t>(addr & 0x0F), busVal);
    }

    uint8_t peek(uint16_t addr, uint8_t busVal) const {
        return cycleCore.peek(*this, static_cast<uint8_t>(addr & 0x0F), busVal);
    }

    void write(uint16_t addr, uint8_t val) {
        cycleCore.write(*this, static_cast<uint8_t>(addr & 0x0F), val);
    }
};

inline void CIA6526CycleCore::setSerialPins(CIA6526 &cia, bool cntHigh, bool spHigh) {
    cia.serialCntHigh = cntHigh;
    cia.serialSpHigh = spHigh;
}

inline void CIA6526CycleCore::setFlagPin(CIA6526 &cia, bool flagHigh) {
    cia.flagPinHigh = flagHigh;
}

inline void CIA6526CycleCore::tickHalfCycle(CIA6526 &cia) {
    halfPhase = !halfPhase;

    if (cia.icrDeferredEvents != 0) {
        cia.setIcrEvent(cia.icrDeferredEvents);
        cia.icrDeferredEvents = 0;
    }

    const bool flagFalling = (cia.flagPrevHigh && !cia.flagPinHigh);
    cia.flagPrevHigh = cia.flagPinHigh;
    if (flagFalling) {
        if (cia.revisionProfile.flagIrqImmediate) {
            cia.setIcrEvent(static_cast<uint8_t>(1u << 4));
        } else {
            cia.deferIcrEvent(static_cast<uint8_t>(1u << 4));
        }
    }

    if (halfPhase) {
        if (cia.timer1_started) {
            if (cia.timer1_count == 0) {
                cia.timer1_count = cia.timer1_latch;
                cia.timer1UnderflowCount++;
                cia.deferIcrEvent(static_cast<uint8_t>(1u << 0));
                if (cia.timer1_one_shot) cia.timer1_started = false;
            } else {
                cia.timer1_count = static_cast<uint16_t>(cia.timer1_count - 1);
            }
            cia.t1c_lo = static_cast<uint8_t>(cia.timer1_count & 0xFF);
            cia.t1c_hi = static_cast<uint8_t>((cia.timer1_count >> 8) & 0xFF);
        }

        if (cia.timer2_started) {
            if (cia.timer2_count == 0) {
                cia.timer2_count = cia.timer2_latch;
                cia.timer2UnderflowCount++;
                cia.deferIcrEvent(static_cast<uint8_t>(1u << 1));
                if (cia.timer2_one_shot) cia.timer2_started = false;
            } else {
                cia.timer2_count = static_cast<uint16_t>(cia.timer2_count - 1);
            }
            cia.t2c_lo = static_cast<uint8_t>(cia.timer2_count & 0xFF);
            cia.t2c_hi = static_cast<uint8_t>((cia.timer2_count >> 8) & 0xFF);
        }

        cia.todTickDivider = static_cast<uint16_t>(cia.todTickDivider + 1);
        if (cia.todRunning && cia.todTickDivider >= cia.revisionProfile.todDividerReload) {
            cia.todTickDivider = 0;
            cia.incrementTodClock();
            cia.evaluateTodAlarm();
        }
    }

    const bool cntRising = (!prevCntHigh && cia.serialCntHigh);
    const bool cntFalling = (prevCntHigh && !cia.serialCntHigh);
    prevCntHigh = cia.serialCntHigh;
    const bool serialInputEdge = cia.revisionProfile.serialInputShiftOnFallingEdge ? cntFalling : cntRising;
    const bool serialInputMode = (cia.controlA & 0x40) == 0;
    if (serialInputMode && serialInputEdge) {
        const uint8_t inBit = cia.serialSpHigh ? 1u : 0u;
        cia.serialShiftReg = static_cast<uint8_t>((cia.serialShiftReg << 1) | inBit);
        cia.serialShiftBitCount = static_cast<uint8_t>(cia.serialShiftBitCount + 1);
        if (cia.serialShiftBitCount >= 8) {
            cia.serialDataReg = cia.serialShiftReg;
            cia.serialShiftReg = 0;
            cia.serialShiftBitCount = 0;
            cia.serialRxByteCount++;
            cia.deferIcrEvent(static_cast<uint8_t>(1u << 3));
        }
    }

    if (cia.serialShiftInProgress) {
        const bool serialOutputMode = (cia.controlA & 0x40) != 0;
        const bool phaseOk = cia.revisionProfile.serialOutputNeedsHalfPhaseHigh ? halfPhase : true;
        const bool outputClockEnabled = serialOutputMode && cia.timer1_started && phaseOk;
        if (outputClockEnabled) {
            if (cia.serialShiftDivider == 0) {
                cia.serialShiftDivider = cia.revisionProfile.serialShiftDividerReload;
                cia.serialSpHigh = ((cia.serialShiftReg & 0x80) != 0);
                cia.serialShiftReg = static_cast<uint8_t>(cia.serialShiftReg << 1);
                cia.serialOutputBitCount = static_cast<uint8_t>(cia.serialOutputBitCount + 1);
                if (cia.serialOutputBitCount >= 8) {
                    cia.serialShiftInProgress = false;
                    cia.serialOutputBitCount = 0;
                    cia.serialTxByteCount++;
                    cia.deferIcrEvent(static_cast<uint8_t>(1u << 3));
                }
            } else {
                cia.serialShiftDivider = static_cast<uint16_t>(cia.serialShiftDivider - 1);
            }
        }
    }
}

inline uint8_t CIA6526CycleCore::read(CIA6526 &cia, uint8_t reg, uint8_t busVal) {
    switch (reg & 0x0F) {
        case 0x00: return cia.getPortACombined();
        case 0x01: return cia.getPortBCombined();
        case 0x02: return cia.ddra;
        case 0x03: return cia.ddrb;
        case 0x04: return static_cast<uint8_t>(cia.timer1_count & 0xFF);
        case 0x05: return static_cast<uint8_t>((cia.timer1_count >> 8) & 0xFF);
        case 0x06: return static_cast<uint8_t>(cia.timer2_count & 0xFF);
        case 0x07: return static_cast<uint8_t>((cia.timer2_count >> 8) & 0xFF);
        case 0x08: {
            const uint8_t out = cia.todReadLatched ? cia.tod_latch_10ths : cia.tod_10ths;
            if (cia.revisionProfile.todRead10thClearsLatch) {
                cia.todReadLatched = false;
            }
            return out;
        }
        case 0x09: return cia.todReadLatched ? cia.tod_latch_seconds : cia.tod_seconds;
        case 0x0A: return cia.todReadLatched ? cia.tod_latch_minutes : cia.tod_minutes;
        case 0x0B:
            if (cia.revisionProfile.todReadHoursLatches) {
                cia.tod_latch_10ths = cia.tod_10ths;
                cia.tod_latch_seconds = cia.tod_seconds;
                cia.tod_latch_minutes = cia.tod_minutes;
                cia.tod_latch_hours = cia.tod_hours;
                cia.todReadLatched = true;
            }
            return cia.tod_latch_hours;
        case 0x0C: return cia.serialDataReg;
        case 0x0D: {
            const uint8_t v = cia.icr;
            cia.clearIcrMask(0x1F);
            return v;
        }
        case 0x0E: return cia.controlA;
        case 0x0F: return cia.t2_ctrl;
        default: return busVal;
    }
}

inline uint8_t CIA6526CycleCore::peek(const CIA6526 &cia, uint8_t reg, uint8_t busVal) const {
    switch (reg & 0x0F) {
        case 0x00: return cia.getPortACombined();
        case 0x01: return cia.getPortBCombined();
        case 0x02: return cia.ddra;
        case 0x03: return cia.ddrb;
        case 0x04: return static_cast<uint8_t>(cia.timer1_count & 0xFF);
        case 0x05: return static_cast<uint8_t>((cia.timer1_count >> 8) & 0xFF);
        case 0x06: return static_cast<uint8_t>(cia.timer2_count & 0xFF);
        case 0x07: return static_cast<uint8_t>((cia.timer2_count >> 8) & 0xFF);
        case 0x08: return cia.todReadLatched ? cia.tod_latch_10ths : cia.tod_10ths;
        case 0x09: return cia.todReadLatched ? cia.tod_latch_seconds : cia.tod_seconds;
        case 0x0A: return cia.todReadLatched ? cia.tod_latch_minutes : cia.tod_minutes;
        case 0x0B: return cia.todReadLatched ? cia.tod_latch_hours : cia.tod_hours;
        case 0x0C: return cia.serialDataReg;
        case 0x0D: return cia.icr;
        case 0x0E: return cia.controlA;
        case 0x0F: return cia.t2_ctrl;
        default: return busVal;
    }
}

inline void CIA6526CycleCore::write(CIA6526 &cia, uint8_t reg, uint8_t val) {
    switch (reg & 0x0F) {
        case 0x00: cia.pra = val; break;
        case 0x01: cia.prb = val; break;
        case 0x02: cia.ddra = val; break;
        case 0x03: cia.ddrb = val; break;
        case 0x04:
            cia.t1l_lo = val;
            cia.timer1_latch = static_cast<uint16_t>((cia.timer1_latch & 0xFF00) | val);
            break;
        case 0x05:
            cia.t1l_hi = val;
            cia.timer1_latch = static_cast<uint16_t>((cia.timer1_latch & 0x00FF) | (uint16_t(val) << 8));
            cia.timer1_count = cia.timer1_latch;
            cia.t1c_lo = static_cast<uint8_t>(cia.timer1_count & 0xFF);
            cia.t1c_hi = static_cast<uint8_t>((cia.timer1_count >> 8) & 0xFF);
            break;
        case 0x06:
            cia.t2l_lo = val;
            cia.timer2_latch = static_cast<uint16_t>((cia.timer2_latch & 0xFF00) | val);
            break;
        case 0x07:
            cia.t2l_hi = val;
            cia.timer2_latch = static_cast<uint16_t>((cia.timer2_latch & 0x00FF) | (uint16_t(val) << 8));
            cia.timer2_count = cia.timer2_latch;
            cia.t2c_lo = static_cast<uint8_t>(cia.timer2_count & 0xFF);
            cia.t2c_hi = static_cast<uint8_t>((cia.timer2_count >> 8) & 0xFF);
            break;
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: {
            const bool writeAlarm = cia.revisionProfile.alarmWriteUsesCrbBit7 && ((cia.t2_ctrl & 0x80) != 0);
            if (writeAlarm) {
                if ((reg & 0x0F) == 0x08) cia.tod_alarm_10ths = val;
                if ((reg & 0x0F) == 0x09) cia.tod_alarm_seconds = val;
                if ((reg & 0x0F) == 0x0A) cia.tod_alarm_minutes = val;
                if ((reg & 0x0F) == 0x0B) cia.tod_alarm_hours = val;
            } else {
                if ((reg & 0x0F) == 0x08) cia.tod_10ths = val;
                if ((reg & 0x0F) == 0x09) cia.tod_seconds = val;
                if ((reg & 0x0F) == 0x0A) cia.tod_minutes = val;
                if ((reg & 0x0F) == 0x0B) {
                    cia.tod_hours = val;
                    if (cia.revisionProfile.todWriteStopsClock) {
                        cia.todRunning = false;
                    }
                }
                if ((reg & 0x0F) == 0x08 && cia.revisionProfile.todWriteStopsClock) {
                    cia.todRunning = true;
                }
            }
            break;
        }
        case 0x0C:
            cia.serialDataReg = val;
            cia.serialShiftReg = val;
            cia.serialShiftInProgress = true;
            cia.serialOutputBitCount = 0;
            cia.serialShiftDivider = cia.revisionProfile.serialShiftDividerReload;
            break;
        case 0x0D:
            if (val & 0x80) cia.ier = static_cast<uint8_t>(cia.ier | (val & 0x7F));
            else cia.ier = static_cast<uint8_t>(cia.ier & ~(val & 0x7F));
            if ((cia.icr & cia.ier & 0x1F) != 0) cia.icr = static_cast<uint8_t>(cia.icr | 0x80);
            else cia.icr = static_cast<uint8_t>(cia.icr & 0x7F);
            break;
        case 0x0E:
            cia.controlA = val;
            cia.timer1_one_shot = (val & 0x08) != 0;
            if (val & 0x10) {
                cia.timer1_count = cia.timer1_latch;
                cia.t1c_lo = static_cast<uint8_t>(cia.timer1_count & 0xFF);
                cia.t1c_hi = static_cast<uint8_t>((cia.timer1_count >> 8) & 0xFF);
            }
            cia.timer1_started = (val & 0x01) != 0;
            break;
        case 0x0F:
            cia.t2_ctrl = val;
            cia.timer2_one_shot = (val & 0x08) != 0;
            if (val & 0x10) {
                cia.timer2_count = cia.timer2_latch;
                cia.t2c_lo = static_cast<uint8_t>(cia.timer2_count & 0xFF);
                cia.t2c_hi = static_cast<uint8_t>((cia.timer2_count >> 8) & 0xFF);
            }
            cia.timer2_started = (val & 0x01) != 0;
            break;
        default:
            break;
    }
    cia.updateIRQLine();
}
