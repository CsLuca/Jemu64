#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecFaultInjectionTests() {
    // Procedure 1: pulse-drop fault must not deadlock the serial state machine.
    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxQueue.push_back(0xA5u);

        for (int i = 0; i < 64; ++i) {
            const bool clk = ((i % 9) == 0) ? true : ((i & 1) == 0);
            drive.setIecLines(true, clk, true);
            drive.stepIecSerial();
        }

        if (drive.iecSerialState == Drive1541::IecSerialState::Command && drive.iecTalking) {
            std::cerr << "[IEC FAULT] FAIL: pulse-drop fault forced invalid talk->command deadlock" << std::endl;
            assert(false);
        }
    }

    // Procedure 2: transient stuck-low ATN must preempt talk flow and recover.
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

        for (int i = 0; i < 4; ++i) {
            drive.setIecLines(false, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecSerialState != Drive1541::IecSerialState::Command) {
            std::cerr << "[IEC FAULT] FAIL: ATN stuck-low transient did not preempt to command" << std::endl;
            assert(false);
        }

        for (int i = 0; i < 8; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecAtnHandshakeActive && drive.iecAtnResponseWaitTicks > (drive.iecAtnResponseTimeoutTicks + 8)) {
            std::cerr << "[IEC FAULT] FAIL: ATN recovery handshake did not converge" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[IEC FAULT] PASS: pulse-drop and stuck-line transient faults recover coherently" << std::endl;
}
