#pragma once

#include <cassert>
#include <iostream>

#include "drive_1541.hpp"

static void runDrive1541IecStatusTimeoutMinimalTests() {
    // Procedure 1: RX timeout deterministically trips after receiver+hysteresis budget.
    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecReceiverTimeoutTicks = 24;
        drive.iecSerialTimeoutHysteresisTicks = 2;
        drive.iecRxBitCount = 3;

        const uint32_t budget = drive.iecReceiverTimeoutTicks + drive.iecSerialTimeoutHysteresisTicks;
        for (uint32_t i = 0; i < budget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecRxTimeoutCount != 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: RX timeout tripped before budget" << std::endl;
            assert(false);
        }
        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.iecRxTimeoutCount == 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: RX timeout missing after budget" << std::endl;
            assert(false);
        }
    }

    // Procedure 2: TX timeout deterministically trips after sender+hysteresis budget.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecSenderTimeoutTicks = 24;
        drive.iecSerialTimeoutHysteresisTicks = 2;
        drive.iecTxByteActive = true;
        drive.iecTxBitCount = 1;
        drive.iecSerialPullDATA = true;

        const uint32_t budget = drive.iecSenderTimeoutTicks + drive.iecSerialTimeoutHysteresisTicks;
        for (uint32_t i = 0; i < budget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecTxTimeoutCount != 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: TX timeout tripped before budget" << std::endl;
            assert(false);
        }
        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.iecTxTimeoutCount == 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: TX timeout missing after budget" << std::endl;
            assert(false);
        }
    }

    // Procedure 3: EOI timeout deterministically trips after sender budget.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecSenderTimeoutTicks = 24;
        drive.iecEoiPendingAck = true;
        drive.iecEoiAckLowSeen = false;

        const uint32_t budget = drive.iecSenderTimeoutTicks;
        for (uint32_t i = 0; i < budget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecEoiTimeoutCount != 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: EOI timeout tripped before budget" << std::endl;
            assert(false);
        }
        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.iecEoiTimeoutCount == 0) {
            std::cerr << "[1541 IEC STAT MINI] FAIL: EOI timeout missing after budget" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 IEC STAT MINI] PASS: isolated deterministic timeout checks" << std::endl;
}
