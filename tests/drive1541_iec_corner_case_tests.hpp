#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runDrive1541IecCornerCaseTests() {
    {
        Drive1541 drive;
        drive.iecEnableDriveReleaseDelayModel = true;

        bool pull = false;
        std::uint8_t countdown = 0;
        drive.applyDriveOpenCollectorReleaseModel(true, pull, 1, countdown);
        if (!pull || countdown != 1) {
            std::cerr << "[1541 IEC CORNER] FAIL: release-delay model did not latch low drive pull" << std::endl;
            assert(false);
        }
        drive.applyDriveOpenCollectorReleaseModel(false, pull, 1, countdown);
        if (!pull || countdown != 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: release-delay model released too early" << std::endl;
            assert(false);
        }
        drive.applyDriveOpenCollectorReleaseModel(false, pull, 1, countdown);
        if (pull) {
            std::cerr << "[1541 IEC CORNER] FAIL: release-delay model did not release after delay" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecEnableRxTimingWindow = true;
        drive.iecRxSetupTicks = 1;
        drive.iecRxHoldTicks = 0;

        drive.setIecLines(true, false, true);
        drive.stepIecSerial();

        // Data edge lands on the same tick as RX clock edge: reject on setup window.
        drive.setIecLines(true, true, false);
        drive.stepIecSerial();

        if (drive.iecRxBitCount != 0 || drive.iecRxTimingWindowRejectCount == 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: expected setup-window reject on same-tick data/clock edge" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecEnableRxTimingWindow = true;
        drive.iecRxSetupTicks = 1;
        drive.iecRxHoldTicks = 0;

        drive.setIecLines(true, false, true);
        drive.stepIecSerial();

        // Data settles one tick before RX clock edge: sample must be accepted.
        drive.setIecLines(true, false, false);
        drive.stepIecSerial();
        drive.setIecLines(true, true, false);
        drive.stepIecSerial();

        if (drive.iecRxBitCount != 1 || drive.iecRxTimingWindowRejectCount != 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: expected setup-window accept with one-tick settle" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecEnableDriveReleaseDelayModel = true;
        drive.iecDriveDataReleaseDelayTicks = 1;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxByteActive = true;
        drive.iecTxBitCount = 3;
        drive.iecTalkStartPending = true;
        drive.iecTalkFrameArmed = true;
        drive.iecTalkSawStartEdge = true;
        drive.iecTxCurrentIsEoi = true;
        drive.iecEoiPendingAck = true;
        drive.iecEoiAckLowSeen = true;
        drive.iecEoiWaitTicks = 12;
        drive.iecSerialPullDATA = true;

        if (!drive.processIecCommandByte(0x5Fu)) {
            std::cerr << "[1541 IEC CORNER] FAIL: UNTALK not accepted" << std::endl;
            assert(false);
        }
        if (drive.iecTalking ||
            drive.iecTxByteActive ||
            drive.iecTalkStartPending ||
            drive.iecTxCurrentIsEoi ||
            drive.iecEoiPendingAck ||
            drive.iecEoiAckLowSeen ||
            drive.iecEoiWaitTicks != 0 ||
            drive.iecSerialPullDATA) {
            std::cerr << "[1541 IEC CORNER] FAIL: UNTALK did not clear talk/EOI state" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecExpectingNameBytes = true;
        drive.iecActiveListenChannel = 0;
        drive.iecListenSecondary = 0;
        drive.iecRxBitCount = 4;
        drive.iecRxShift = 0x0Bu;
        drive.iecRxIdleTicks = 9;
        drive.iecRxByteAckTicks = 4;
        drive.iecRxByteAckPullDATA = true;

        if (!drive.processIecCommandByte(0x3Fu)) {
            std::cerr << "[1541 IEC CORNER] FAIL: UNLISTEN not accepted" << std::endl;
            assert(false);
        }
        if (drive.iecListening ||
            drive.iecRxBitCount != 0 ||
            drive.iecRxShift != 0 ||
            drive.iecRxIdleTicks != 0 ||
            drive.iecRxByteAckTicks != 0 ||
            drive.iecRxByteAckPullDATA) {
            std::cerr << "[1541 IEC CORNER] FAIL: UNLISTEN did not clear listener transient state" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecRxBitCount = 1;
        drive.iecRxShift = 1;
        const std::uint32_t rxBudget = Drive1541::IEC_SERIAL_TIMEOUT_TICKS + Drive1541::IEC_TIMEOUT_HYSTERESIS_TICKS;
        for (std::uint32_t i = 0; i < rxBudget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecRxTimeoutCount != 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: RX timeout tripped before hysteresis budget" << std::endl;
            assert(false);
        }
        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.iecRxTimeoutCount == 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: RX timeout did not trip after hysteresis budget" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecTxByteActive = true;
        drive.iecTxShift = 0xA5u;
        drive.iecTxBitCount = 2;
        const std::uint32_t txBudget = Drive1541::IEC_SERIAL_TIMEOUT_TICKS + Drive1541::IEC_TIMEOUT_HYSTERESIS_TICKS;
        for (std::uint32_t i = 0; i < txBudget; ++i) {
            drive.setIecLines(true, true, true);
            drive.stepIecSerial();
        }
        if (drive.iecTxTimeoutCount != 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: TX timeout tripped before hysteresis budget" << std::endl;
            assert(false);
        }
        drive.setIecLines(true, true, true);
        drive.stepIecSerial();
        if (drive.iecTxTimeoutCount == 0) {
            std::cerr << "[1541 IEC CORNER] FAIL: TX timeout did not trip after hysteresis budget" << std::endl;
            assert(false);
        }
    }

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

    std::cerr << "[1541 IEC CORNER] PASS: ATN/UNLISTEN/UNTALK + setup/hold windows + timeout hysteresis are deterministic" << std::endl;
}
