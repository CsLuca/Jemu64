#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runIecMetamorphicInvariantsTests() {
    auto runScenario = [](bool sampleOnFalling, bool sampleBoth) {
        Drive1541 drive;
        drive.iecListening = true;
        drive.iecTalking = true;
        drive.iecActiveTalkChannel = 0;
        drive.iecTalkSa0Confirmed = true;
        drive.iecKernelSampleOnFallingClockEdge = sampleOnFalling;
        drive.iecKernelCompatSampleBothClockEdges = sampleBoth;
        drive.iecSenderTimeoutTicks = 32;
        drive.iecReceiverTimeoutTicks = 32;
        drive.iecSerialTimeoutHysteresisTicks = 2;
        drive.iecTxQueue.push_back(0x41u);
        drive.iecTxQueue.push_back(0x42u);

        bool atn = true;
        bool clk = true;
        bool data = true;
        for (int i = 0; i < 200; ++i) {
            if ((i % 11) == 0) { atn = !atn; }
            clk = !clk;
            if ((i % 7) == 0) { data = !data; }
            drive.setIecLines(atn, clk, data);
            drive.stepIecSerial();
        }

        return drive;
    };

    // Procedure: metamorphic invariants over scheduler/polarity-related sampling knobs.
    const Drive1541 base = runScenario(false, false);
    const Drive1541 both = runScenario(false, true);
    const Drive1541 falling = runScenario(true, false);

    if (base.iecRxTimeoutCount > base.iecClockRisingSeen + 8 ||
        both.iecRxTimeoutCount > both.iecClockRisingSeen + 8 ||
        falling.iecRxTimeoutCount > falling.iecClockRisingSeen + 8) {
        std::cerr << "[IEC METAMORPHIC] FAIL: RX timeout exceeds edge-observable envelope" << std::endl;
        assert(false);
    }

    if (both.iecTxServed < base.iecTxServed) {
        std::cerr << "[IEC METAMORPHIC] FAIL: sample-both-edges served fewer bytes than base" << std::endl;
        assert(false);
    }

    if (falling.iecClockRisingSeen != base.iecClockRisingSeen) {
        std::cerr << "[IEC METAMORPHIC] FAIL: external edge accounting changed across sampling mode" << std::endl;
        assert(false);
    }

    std::cerr << "[IEC METAMORPHIC] PASS: invariants hold across sampling/scheduler variants"
              << " base_tx=" << base.iecTxServed
              << " both_tx=" << both.iecTxServed
              << " fall_tx=" << falling.iecTxServed
              << std::endl;
}
