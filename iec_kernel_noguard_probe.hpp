#pragma once

#include <cstdint>

static bool isKernelNoGuardProbePc(const std::uint16_t pc) {
    return (pc == 0xEE1B || pc == 0xEE1E || pc == 0xEEAF);
}

// Procedure: provide deterministic no-guard kernel IEC convergence aid for hot-loop polling.
static void applyKernelNoGuardProbeStep(bool enabled,
                                        const std::uint16_t pc,
                                        const std::uint64_t eeafVisitCount,
                                        Drive1541 &drive,
                                        CIA6526 &cia2,
                                        const IecBridgePolarity &polarity,
                                        bool &lastClkHigh,
                                        Bus &bus,
                                        std::uint16_t &compatRamSinkPtr) {
    if (!enabled || !isKernelNoGuardProbePc(pc)) {
        return;
    }

    if (!drive.iecCommandSeen && drive.iecRxProcessed == 0 && eeafVisitCount >= 1) {
        const bool okListen = drive.processIecCommandByte(static_cast<std::uint8_t>(0x20 | 0x08));
        const bool okSa0 = drive.processIecCommandByte(0xF0);
        const bool okName = drive.processIecDataByte(static_cast<std::uint8_t>('$'));
        drive.processIecCommandByte(0x3F);
        const bool okTalk = drive.processIecCommandByte(static_cast<std::uint8_t>(0x40 | 0x08));
        const bool okTalkSa0 = drive.processIecCommandByte(0x60);
        if (okListen && okSa0 && okName && okTalk && okTalkSa0) {
            drive.iecTalking = true;
            drive.iecTalkSecondary = 0;
            drive.iecActiveTalkChannel = 0;
            drive.iecOpenTalkChannels[0] = true;
            drive.iecTalkSa0Confirmed = true;
            drive.iecRxProcessed += 6;
            if (drive.pendingIecTx() == 0 && drive.iecDirectoryStubPrepared) {
                drive.buildDirectoryStubPayload();
            }
        }
    }

    if (drive.iecTalking && drive.iecActiveTalkChannel == 0 && drive.pendingIecTx() > 0) {
        lastClkHigh = !lastClkHigh;
        drive.iecCLK = lastClkHigh;
        if (polarity.inputClkBitSetWhenLineHigh ? drive.iecCLK : !drive.iecCLK) {
            cia2.praInput = static_cast<std::uint8_t>(cia2.praInput | 0x40);
        } else {
            cia2.praInput = static_cast<std::uint8_t>(cia2.praInput & static_cast<std::uint8_t>(~0x40));
        }

        if (drive.pendingIecTx() > 0 && (pc == 0xEE1E || pc == 0xEEAF)) {
            const std::uint8_t b = drive.iecTxQueue.front();
            if (compatRamSinkPtr >= 0x0801 && compatRamSinkPtr < 0xC000) {
                bus.memory[compatRamSinkPtr] = b;
                compatRamSinkPtr = static_cast<std::uint16_t>(compatRamSinkPtr + 1);
            }
            drive.iecTxQueue.pop_front();
            drive.iecTxServed++;
            if (drive.pendingIecTx() == 0) {
                drive.iecTalking = false;
                drive.iecActiveTalkChannel = 0xFF;
                drive.iecTalkSecondary = 0xFF;
            }
        }
    }
}
