#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runDrive1541IecCornerCaseTests() {
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxQueue.push_back(0x41u);
        drive.iecTxQueue.push_back(0x42u);

        drive.setIecLines(true, true, true);
        drive.stepIecSerial();

        drive.setIecLines(true, false, true);
        drive.stepIecSerial();

        if (!drive.iecTxByteActive && !drive.iecTalkStartPending) {
            std::cerr << "[1541 IEC CORNER] FAIL: expected TX activity before ATN preemption" << std::endl;
            assert(false);
        }

        // ATN assert must preempt talk flow deterministically.
        drive.setIecLines(false, true, true);
        drive.stepIecSerial();

        if (drive.iecTxByteActive ||
            drive.iecTalkStartPending ||
            drive.iecEoiPendingAck ||
            drive.iecSerialState != Drive1541::IecSerialState::Command) {
            std::cerr << "[1541 IEC CORNER] FAIL: ATN did not preempt talk/EOI flow" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxQueue.push_back(0x55u);

        // Arm and complete a single-byte transfer so EOI ack state is entered.
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
            std::cerr << "[1541 IEC CORNER] FAIL: expected EOI pending ack before ATN late preemption" << std::endl;
            assert(false);
        }

        // Late ATN during EOI ack must clear pending ack immediately.
        drive.setIecLines(false, true, true);
        drive.stepIecSerial();

        if (drive.iecEoiPendingAck ||
            drive.iecEoiAckLowSeen ||
            drive.iecSerialState != Drive1541::IecSerialState::Command) {
            std::cerr << "[1541 IEC CORNER] FAIL: ATN late did not clear EOI pending state" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 IEC CORNER] PASS: ATN preempts talk and EOI corner cases deterministically" << std::endl;
}
