#pragma once

#include <cassert>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecMultiListenerBetweenBytesTests() {
    // Procedure 1: multi-listener style between-bytes window prevents immediate next-byte arming.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecDeviceBetweenBytesTicks = 4;
        drive.iecTxQueue.push_back(0x11u);
        drive.iecTxQueue.push_back(0x22u);

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        drive.setIecLines(true, false, true);
        drive.stepIecSerial();
        for (int i = 0; i < 8; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
            drive.setIecLines(true, false, true);
            drive.stepIecSerial();
        }

        if (drive.iecTxByteActive || drive.iecEoiPendingAck) {
            std::cerr << "[IEC MULTILISTENER] FAIL: expected gap after first non-EOI byte" << std::endl;
            assert(false);
        }

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        drive.setIecLines(true, false, true);
        drive.stepIecSerial();
        if (drive.iecTxByteActive || drive.iecTalkStartPending) {
            std::cerr << "[IEC MULTILISTENER] FAIL: byte started before between-bytes budget" << std::endl;
            assert(false);
        }

        for (int i = 0; i < 4; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        drive.setIecLines(true, false, true);
        drive.stepIecSerial();
        if (!drive.iecTxByteActive) {
            std::cerr << "[IEC MULTILISTENER] FAIL: next byte did not start after between-bytes budget" << std::endl;
            assert(false);
        }
    }

    // Procedure 2: ATN preemption clears between-bytes readiness counter.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecDeviceBetweenBytesTicks = 6;
        drive.iecTxQueue.push_back(0x33u);
        drive.iecTxQueue.push_back(0x44u);

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        drive.setIecLines(true, false, true);
        drive.stepIecSerial();
        for (int i = 0; i < 8; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
            drive.setIecLines(true, false, true);
            drive.stepIecSerial();
        }
        for (int i = 0; i < 3; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecTalkBetweenBytesReadyTicks == 0) {
            std::cerr << "[IEC MULTILISTENER] FAIL: expected between-bytes readiness progress" << std::endl;
            assert(false);
        }

        drive.setIecLines(false, true, true);
        drive.stepIecSerial();
        if (drive.iecTalkBetweenBytesReadyTicks != 0 || drive.iecSerialState != Drive1541::IecSerialState::Command) {
            std::cerr << "[IEC MULTILISTENER] FAIL: ATN preemption did not clear between-bytes state" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[IEC MULTILISTENER] PASS: between-bytes gating behaves deterministically" << std::endl;
}
