#pragma once

#include <cassert>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecSpecTimingWindowTests() {
    // Procedure 1: verify baseline timing windows keep a coherent ordering.
    {
        const int deviceNotPresentTimeout = 256;
        const int senderTimeout = 512;
        const int eoiMin = 200;
        const int eoiMax = 512;
        const int atnResponseTimeout = 1000;
        const int receiverTimeout = 1000;
        const int controllerHold = 20;
        const int deviceHold = 60;

        if (!(controllerHold < deviceHold &&
              eoiMin <= deviceNotPresentTimeout &&
              deviceNotPresentTimeout < senderTimeout &&
              senderTimeout <= eoiMax &&
              senderTimeout < atnResponseTimeout &&
              senderTimeout < receiverTimeout)) {
            std::cerr << "[IEC SPEC TIMING] FAIL: baseline IEC timing windows are inconsistent" << std::endl;
            assert(false);
        }
    }

    // Procedure 2: verify sender-timeout budget is applied to EOI wait state.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxQueue.push_back(0x42u);

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

        if (!drive.iecEoiPendingAck) {
            std::cerr << "[IEC SPEC TIMING] FAIL: expected EOI pending-ack state" << std::endl;
            assert(false);
        }

        const std::uint32_t budget = Drive1541::IEC_EOI_TIMEOUT_TICKS;
        for (std::uint32_t i = 0; i < budget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.statusCodeOf(drive.iecStatusLine) != 0) {
            std::cerr << "[IEC SPEC TIMING] FAIL: EOI timeout tripped before sender-timeout window" << std::endl;
            assert(false);
        }

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.statusCodeOf(drive.iecStatusLine) == 0) {
            std::cerr << "[IEC SPEC TIMING] FAIL: EOI timeout did not trip after sender-timeout window" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[IEC SPEC TIMING] PASS: timing windows and EOI timeout budget are coherent" << std::endl;
}
