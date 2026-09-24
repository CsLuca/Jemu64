#pragma once

#include <cassert>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecTurnaroundArbitrationTests() {
    // Procedure 1: ATN-response timeout counts only while command-phase handshake is active.
    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecEnableAtnAck = true;
        drive.iecAtnResponseTimeoutTicks = 3;
        drive.iecAtnHandshakeActive = true;
        drive.iecAtnAckPullDATA = true;
        drive.iecSerialState = Drive1541::IecSerialState::Command;

        for (int i = 0; i < 4; ++i) {
            drive.setIecLines(false, true, true);
            drive.stepIecSerial();
        }

        if (drive.iecAtnResponseTimeoutCount == 0 ||
            drive.iecAtnHandshakeActive ||
            drive.iecAtnAckPullDATA) {
            std::cerr << "[IEC TURNAROUND] FAIL: ATN-response timeout did not terminate stalled handshake" << std::endl;
            assert(false);
        }
    }

    // Procedure 2: device-not-present timeout counts only in talk-data idle with no byte staged.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecDeviceNotPresentTimeoutTicks = 3;
        drive.iecSerialState = Drive1541::IecSerialState::TalkData;

        for (int i = 0; i < 4; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }

        if (drive.iecDeviceNotPresentTimeoutCount == 0) {
            std::cerr << "[IEC TURNAROUND] FAIL: device-not-present timeout did not trip in idle talk-data" << std::endl;
            assert(false);
        }
    }

    // Procedure 3: entering byte-active talk-data resets device-not-present wait window.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecDeviceNotPresentTimeoutTicks = 16;
        drive.iecSerialState = Drive1541::IecSerialState::TalkData;
        drive.iecTxQueue.push_back(0x33u);

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        drive.setIecLines(true, false, true);
        drive.stepIecSerial();

        if (!drive.iecTxByteActive || drive.iecDeviceNotPresentWaitTicks != 0) {
            std::cerr << "[IEC TURNAROUND] FAIL: talk-data activity did not clear device-not-present wait" << std::endl;
            assert(false);
        }
    }

    // Procedure 4: ATN preemption in talk-data forces command-state turnaround and clears EOI path.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxQueue.push_back(0x41u);

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
            std::cerr << "[IEC TURNAROUND] FAIL: expected EOI pending before ATN turnaround preemption" << std::endl;
            assert(false);
        }

        drive.setIecLines(false, true, true);
        drive.stepIecSerial();

        if (drive.iecSerialState != Drive1541::IecSerialState::Command ||
            drive.iecTxByteActive ||
            drive.iecEoiPendingAck) {
            std::cerr << "[IEC TURNAROUND] FAIL: ATN turnaround did not force command-state preemption" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[IEC TURNAROUND] PASS: arbitration timeouts + ATN turnaround are deterministic" << std::endl;
}
