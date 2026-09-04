#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "drive_via6522.hpp"

class Drive1541 {
public:
    enum Revision : uint8_t {
        REV_1541 = 0,
        REV_1541C = 1,
        REV_1541II = 2
    };

    struct RevisionProfile {
        Revision revision = REV_1541;
        uint8_t cpuCyclesPerStep = 1;
        bool iecStrictEoiAck = true;
        bool iecCommandNeedsAtnLow = true;
        bool iecHandshakeNeedsClockLowAck = true;
        uint32_t iecAtnAckTicksOverride = 16;
        bool iecRxAckOnAnyDataByte = false;
    };

    static constexpr RevisionProfile makeRevisionProfile(Revision rev) {
        return (rev == REV_1541C)
            ? RevisionProfile{REV_1541C, 2, false, false, false, 8, true}
            : (rev == REV_1541II)
                ? RevisionProfile{REV_1541II, 2, false, false, false, 6, true}
            : RevisionProfile{REV_1541, 1, true, true, true, 16, false};
    }

    Revision revision = REV_1541;
    RevisionProfile revisionProfile = makeRevisionProfile(REV_1541);

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

    enum class IecSerialState {
        Idle,
        Command,
        ListenData,
        TalkData,
        TalkEoiAck
    };

    static constexpr uint16_t IEC_TOTAL_VIRTUAL_BLOCKS = 664;
    static constexpr size_t IEC_MAX_VIRTUAL_CATALOG_ENTRIES = 32;

    struct VirtualCatalogEntry {
        bool used = false;
        uint8_t channel = 0xFF;
        uint8_t track = 0;
        uint8_t sector = 0;
        uint16_t blocks = 0;
        std::string name;
        std::string type = "PRG";
        std::string mode;
    };

    struct IecRxEvent {
        uint8_t byte = 0;
        bool isCommand = false;
    };

    std::array<uint8_t, 0x10000> memory = {0};
    VIA6522 via1; // $1800-$180F
    VIA6522 via2; // $1C00-$1C0F

    bool romLoaded = false;
    uint16_t pc = 0;
    uint64_t cycles = 0;

    // IEC lines (active-low on real bus; represented here as logical levels)
    bool iecATN = true;
    bool iecCLK = true;
    bool iecDATA = true;

    // Lines pulled low by the drive (open-collector model).
    bool iecDrivePullCLK = false;
    bool iecDrivePullDATA = false;

    // Minimal IEC command state (LISTEN/TALK subset)
    uint8_t iecDeviceAddress = 8;
    bool iecListening = false;
    bool iecTalking = false;
    bool iecCommandSeen = false;
    uint8_t lastIecCommand = 0;
    uint8_t iecListenSecondary = 0xFF;
    uint8_t iecTalkSecondary = 0xFF;
    bool iecExpectingNameBytes = false;
    bool iecDirectoryStubPrepared = false;
    std::vector<uint8_t> iecNameBuffer;

    std::deque<IecRxEvent> iecRxQueue;
    std::deque<uint8_t> iecTxQueue;
    uint64_t iecRxProcessed = 0;
    uint64_t iecTxServed = 0;
    uint64_t iecEoiAckCount = 0;
    uint64_t iecAtnFallingSeen = 0;
    uint64_t iecClockRisingSeen = 0;
    uint64_t iecClockRisingAtnLow = 0;

    bool iecPrevCLK = true;
    bool iecPrevATN = true;
    bool iecPrevDATA = true;
    bool iecEnableAtnAck = false;
    bool iecEnableListenerByteAck = false;
    bool iecKernelCompatSampleBothClockEdges = false;
    bool iecKernelSampleOnFallingClockEdge = false;
    bool iecKernelSampleBothCommandEdges = false;
    bool iecKernelCompatAutoTalkDirectory = false;
    bool iecKernelCompatAutoDirectoryOnTalk0 = false;
    bool iecKernelCompatForceTalkOnIcrSerial = false;
    bool iecKernelIgnoreAtnForTalkDataPhase = false;
    IecSerialState iecSerialState = IecSerialState::Idle;
    uint32_t iecAtnAckTicks = 0;
    bool iecAtnAckPullDATA = false;
    bool iecAtnHandshakeActive = false;
    bool iecAtnAckSawClockLow = false;
    uint32_t iecRxByteAckTicks = 0;
    bool iecRxByteAckPullDATA = false;
    uint8_t iecRxShift = 0;
    uint8_t iecRxBitCount = 0;
    bool iecCommandFrameSync = false;
    bool iecCommandSawStartEdge = false;
    uint32_t iecCommandFrameByteCount = 0;
    bool iecCommandFrameSawListenForDevice = false;
    bool iecCommandFrameSawTalkForDevice = false;
    bool iecCommandFrameSawListenSecondary0 = false;
    bool iecCommandFrameSawTalkSecondary0 = false;

    bool iecTxByteActive = false;
    uint8_t iecTxShift = 0;
    uint8_t iecTxBitCount = 0;
    bool iecTalkStartPending = false;
    uint8_t iecTalkStartByte = 0;
    bool iecTalkStartIsEoi = false;
    bool iecTalkSa0Confirmed = false;
    bool iecTalkFrameArmed = false;
    bool iecTalkSawStartEdge = false;
    bool iecSerialPullDATA = false;
    bool iecTxCurrentIsEoi = false;
    bool iecEoiPendingAck = false;
    bool iecEoiAckLowSeen = false;

    std::array<bool, 16> iecOpenListenChannels = {false};
    std::array<bool, 16> iecOpenTalkChannels = {false};
    uint8_t iecActiveListenChannel = 0xFF;
    uint8_t iecActiveTalkChannel = 0xFF;

    static constexpr uint32_t IEC_SERIAL_TIMEOUT_TICKS = 256;
    static constexpr uint32_t IEC_EOI_TIMEOUT_TICKS = 256;
    static constexpr uint32_t IEC_ATN_ACK_TICKS = 16;
    static constexpr uint32_t IEC_RX_BYTE_ACK_TICKS = 64;
    uint32_t iecRxIdleTicks = 0;
    uint32_t iecTxIdleTicks = 0;
    uint32_t iecEoiWaitTicks = 0;
    uint64_t iecRxTimeoutCount = 0;
    uint64_t iecTxTimeoutCount = 0;
    uint64_t iecEoiTimeoutCount = 0;

    std::string iecStatusLine = "00,OK,00,00";
    std::vector<uint8_t> iecCommandChannelBuffer;
    std::vector<uint8_t> iecCommandResponseQueue;
    uint16_t iecLastExecuteAddr = 0;
    uint8_t iecLastBlockTrack = 0;
    uint8_t iecLastBlockSector = 0;
    uint8_t iecLastBlockChannel = 0xFF;
    std::string iecLastBlockCommand;
    std::string iecLastExecDispatch;
    uint64_t iecExecDispatchCount = 0;

    std::array<uint8_t, 256> iecBlockBuffer = {0};
    bool iecBlockBufferValid = false;
    uint8_t iecBlockBufferTrack = 0;
    uint8_t iecBlockBufferSector = 0;
    std::array<uint8_t, 256> iecDiskMap = {0};
    std::array<uint8_t, 16> iecChannelBufferPos = {0};
    std::array<bool, 16> iecChannelPointerValid = {false};
    std::array<uint8_t, IEC_TOTAL_VIRTUAL_BLOCKS> iecBlockAllocated = {0};
    std::array<uint8_t, IEC_TOTAL_VIRTUAL_BLOCKS> iecAllocOwnerEntry = {0};
    uint16_t iecAllocatedBlockCount = 0;
    std::array<VirtualCatalogEntry, IEC_MAX_VIRTUAL_CATALOG_ENTRIES> iecCatalog = {};
    std::array<int8_t, 16> iecChannelCatalogEntry = {};
    std::array<std::string, 16> iecChannelOpenName = {};
    std::array<bool, 16> iecChannelOpenNameValid = {false};
    std::array<std::string, 16> iecChannelOpenType = {};
    std::array<std::string, 16> iecChannelOpenMode = {};

    bool iecDirectoryFromBlockBuffer = false;
    std::string iecDirectoryWildcardPattern;
    std::string iecDirectoryTypeFilter;
    std::string iecDirectoryModeFilter;
    bool iecDirectoryModeFilterNegated = false;

    // Drive CPU scaffold state (placeholder for real core)
    bool cpuEnabled = false;
    uint8_t cpuLastOpcode = 0;
    uint16_t cpuLastFetchAddr = 0;
    uint64_t cpuStepCount = 0;
    uint8_t cpuCyclesToNext = 0;
    bool cpuReadyEdge = false;

    uint64_t iecCommandDispatchCount = 0;
    uint64_t iecDataDispatchCount = 0;
    uint64_t iecCommandSyntaxErrorCount = 0;

    std::array<uint64_t, 16> iecChannelOpenCount = {0};
    std::array<uint64_t, 16> iecChannelCloseCount = {0};

    uint8_t cpuA = 0;
    uint8_t cpuX = 0;
    uint8_t cpuY = 0;
    uint8_t cpuSP = 0xFF;
    uint8_t cpuP = 0x24;

    bool loadRom(const std::string &romPath) {
        std::ifstream in(romPath, std::ios::binary);
        if (!in.is_open()) {
            romLoaded = false;
            return false;
        }

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() != 16384) {
            romLoaded = false;
            return false;
        }

        for (size_t i = 0; i < data.size(); ++i) {
            memory[0xC000 + i] = data[i];
        }

        romLoaded = true;

        reset();
        return true;
    }

    void reset() {
        cycles = 0;
        pc = static_cast<uint16_t>(memory[0xFFFC] | (uint16_t(memory[0xFFFD]) << 8));
        iecListening = false;
        iecTalking = false;
        iecCommandSeen = false;
        lastIecCommand = 0;
        cpuLastOpcode = 0;
        cpuLastFetchAddr = pc;
        cpuStepCount = 0;
        cpuCyclesToNext = 0;
        cpuReadyEdge = false;
        cpuA = 0;
        cpuX = 0;
        cpuY = 0;
        cpuSP = 0xFF;
        cpuP = 0x24;

        iecListenSecondary = 0xFF;
        iecTalkSecondary = 0xFF;
        iecExpectingNameBytes = false;
        iecDirectoryStubPrepared = false;
        iecNameBuffer.clear();
        iecRxQueue.clear();
        iecTxQueue.clear();
        iecRxProcessed = 0;
        iecTxServed = 0;
        iecEoiAckCount = 0;
        iecAtnFallingSeen = 0;
        iecClockRisingSeen = 0;
        iecClockRisingAtnLow = 0;

        iecPrevCLK = true;
        iecPrevATN = true;
        iecPrevDATA = true;
        iecEnableAtnAck = false;
        iecEnableListenerByteAck = false;
        iecKernelCompatSampleBothClockEdges = false;
        iecKernelSampleOnFallingClockEdge = false;
        iecKernelSampleBothCommandEdges = false;
        iecKernelCompatAutoTalkDirectory = false;
        iecKernelCompatAutoDirectoryOnTalk0 = false;
        iecKernelCompatForceTalkOnIcrSerial = false;
        iecKernelIgnoreAtnForTalkDataPhase = false;
        iecSerialState = IecSerialState::Idle;
        iecAtnAckTicks = 0;
        iecAtnAckPullDATA = false;
        iecAtnHandshakeActive = false;
        iecAtnAckSawClockLow = false;
        iecRxByteAckTicks = 0;
        iecRxByteAckPullDATA = false;
        iecRxShift = 0;
        iecRxBitCount = 0;
        iecCommandFrameSync = false;
        iecCommandSawStartEdge = false;
        iecCommandFrameByteCount = 0;
        iecCommandFrameSawListenForDevice = false;
        iecCommandFrameSawTalkForDevice = false;
        iecCommandFrameSawListenSecondary0 = false;
        iecCommandFrameSawTalkSecondary0 = false;
        iecTxByteActive = false;
        iecTxShift = 0;
        iecTxBitCount = 0;
        iecTalkStartPending = false;
        iecTalkStartByte = 0;
        iecTalkStartIsEoi = false;
        iecTalkSa0Confirmed = false;
        iecTalkFrameArmed = false;
        iecTalkSawStartEdge = false;
        iecSerialPullDATA = false;
        iecTxCurrentIsEoi = false;
        iecEoiPendingAck = false;
        iecEoiAckLowSeen = false;

        iecOpenListenChannels.fill(false);
        iecOpenTalkChannels.fill(false);
        iecActiveListenChannel = 0xFF;
        iecActiveTalkChannel = 0xFF;

        iecRxIdleTicks = 0;
        iecTxIdleTicks = 0;
        iecEoiWaitTicks = 0;
        iecRxTimeoutCount = 0;
        iecTxTimeoutCount = 0;
        iecEoiTimeoutCount = 0;

        iecStatusLine = "00,OK,00,00";
        iecCommandChannelBuffer.clear();
        iecCommandResponseQueue.clear();
        iecLastExecuteAddr = 0;
        iecLastBlockTrack = 0;
        iecLastBlockSector = 0;
        iecLastBlockChannel = 0xFF;
        iecLastBlockCommand.clear();
        iecLastExecDispatch.clear();
        iecExecDispatchCount = 0;
        iecCommandDispatchCount = 0;
        iecDataDispatchCount = 0;
        iecCommandSyntaxErrorCount = 0;
        iecChannelOpenCount.fill(0);
        iecChannelCloseCount.fill(0);

        iecBlockBuffer.fill(0);
        iecBlockBufferValid = false;
        iecBlockBufferTrack = 0;
        iecBlockBufferSector = 0;
        iecDiskMap.fill(0);
        iecChannelBufferPos.fill(0);
        iecChannelPointerValid.fill(false);
        iecDirectoryFromBlockBuffer = false;
        iecDirectoryWildcardPattern.clear();
        iecDirectoryTypeFilter.clear();
        iecDirectoryModeFilter.clear();
        iecDirectoryModeFilterNegated = false;
        iecBlockAllocated.fill(0);
        iecAllocOwnerEntry.fill(0xFF);
        iecAllocatedBlockCount = 0;
        iecChannelCatalogEntry.fill(-1);
        iecChannelOpenName.fill(std::string());
        iecChannelOpenNameValid.fill(false);
        iecChannelOpenType.fill(std::string("PRG"));
        iecChannelOpenMode.fill(std::string());
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            iecCatalog[i] = VirtualCatalogEntry{};
        }
    }

    uint8_t read(uint16_t addr) {
        if ((addr & 0xFFF0) == 0x1800) {
            return via1.read(addr);
        }
        if ((addr & 0xFFF0) == 0x1C00) {
            return via2.read(addr);
        }
        return memory[addr];
    }

    void write(uint16_t addr, uint8_t val) {
        if ((addr & 0xFFF0) == 0x1800) {
            via1.write(addr, val);
            return;
        }
        if ((addr & 0xFFF0) == 0x1C00) {
            via2.write(addr, val);
            return;
        }

        // Keep ROM read-only in upper 16KB.
        if (addr >= 0xC000) {
            return;
        }
        memory[addr] = val;
    }

    void tick() {
        tickIecHalfCycle();
    }

    void tickIecHalfCycle() {
        cycles++;

        via1.tick();
        via2.tick();

        processQueuedIecRxBurst();

        stepIecSerial();

        // Minimal IEC drive-side behavior from VIA1 PB6/PB5 as CLK/DATA outputs.
        // This is only a scaffold and not a full 1541 VIA behavior model yet.
        const uint8_t viaPrb = via1.regs[0x00];
        const uint8_t viaDdrb = via1.regs[0x02];
        bool viaPullCLK = false;
        bool viaPullDATA = false;
        if (viaDdrb & 0x40) {
            viaPullCLK = (viaPrb & 0x40) == 0;
        }
        if (viaDdrb & 0x20) {
            viaPullDATA = (viaPrb & 0x20) == 0;
        }

        // During ATN command phase, the host clocks command bytes; keep drive
        // CLK/DATA outputs released except explicit handshake pulls.
        if (iecSerialState == IecSerialState::Command) {
            viaPullCLK = false;
            viaPullDATA = false;
        }

        iecDrivePullCLK = viaPullCLK;
        iecDrivePullDATA = (viaPullDATA || iecSerialPullDATA || iecAtnAckPullDATA || iecRxByteAckPullDATA);

        stepDriveCpuCycleAccurate();
    }

    void processQueuedIecRxBurst() {
        uint32_t budget = 0;
        while (!iecRxQueue.empty() && budget < 4) {
            const IecRxEvent ev = iecRxQueue.front();
            iecRxQueue.pop_front();
            consumeReceivedByte(ev.byte, ev.isCommand);
            iecRxProcessed++;
            budget++;
        }
    }

    void stepDriveCpuCycleAccurate() {
        if (!(cpuEnabled && romLoaded)) {
            return;
        }

        cpuReadyEdge = !cpuReadyEdge;
        if (!cpuReadyEdge) {
            return;
        }

        if (cpuCyclesToNext > 0) {
            cpuCyclesToNext = static_cast<uint8_t>(cpuCyclesToNext - 1);
            return;
        }

        cpuLastFetchAddr = pc;
        cpuLastOpcode = read(pc);
        const uint8_t consumed = stepCpuScaffoldCycleAccurate(cpuLastOpcode);
        const uint8_t scale = (revisionProfile.cpuCyclesPerStep == 0) ? 1 : revisionProfile.cpuCyclesPerStep;
        const uint8_t scaledConsumed = static_cast<uint8_t>((consumed / scale) + ((consumed % scale) ? 1 : 0));
        cpuCyclesToNext = (scaledConsumed > 0) ? static_cast<uint8_t>(scaledConsumed - 1) : 0;
        cpuStepCount++;

        if (pc < 0xC000) {
            pc = static_cast<uint16_t>(memory[0xFFFC] | (uint16_t(memory[0xFFFD]) << 8));
        }
    }

    uint16_t read16(uint16_t addr) const {
        const uint8_t lo = memory[addr];
        const uint8_t hi = memory[static_cast<uint16_t>(addr + 1)];
        return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
    }

    void push(uint8_t v) {
        memory[0x0100 | cpuSP] = v;
        cpuSP = static_cast<uint8_t>(cpuSP - 1);
    }

    uint8_t pull() {
        cpuSP = static_cast<uint8_t>(cpuSP + 1);
        return memory[0x0100 | cpuSP];
    }

    uint8_t stepCpuScaffoldCycleAccurate(uint8_t op) {
        const uint16_t oldPC = pc;
        uint8_t cyclesUsed = 2;

        switch (op) {
            case 0xEA:
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x78:
                cpuP |= 0x04;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x58:
                cpuP &= static_cast<uint8_t>(~0x04);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xD8:
                cpuP &= static_cast<uint8_t>(~0x08);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xF8:
                cpuP |= 0x08;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x18:
                cpuP &= static_cast<uint8_t>(~0x01);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x38:
                cpuP |= 0x01;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xA9:
                cpuA = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0xA2:
                cpuX = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0xA0:
                cpuY = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0x4C:
                pc = read16(static_cast<uint16_t>(pc + 1));
                cyclesUsed = 3;
                break;
            case 0x20: {
                const uint16_t target = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t ret = static_cast<uint16_t>(pc + 2);
                push(static_cast<uint8_t>((ret >> 8) & 0xFF));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                cyclesUsed = 6;
                break;
            }
            case 0x60: {
                const uint8_t lo = pull();
                const uint8_t hi = pull();
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 6;
                break;
            }
            case 0xD0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    cyclesUsed = 3;
                } else {
                    cyclesUsed = 2;
                }
                break;
            }
            case 0xF0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    cyclesUsed = 3;
                } else {
                    cyclesUsed = 2;
                }
                break;
            }
            default:
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
        }

        if (pc == oldPC) {
            pc = static_cast<uint16_t>(pc + 1);
        }
        return cyclesUsed;
    }

    void stepCpuScaffold() {
        const uint8_t op = read(pc);
        const uint16_t oldPC = pc;

        switch (op) {
            case 0xEA: // NOP
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x78: // SEI
                cpuP |= 0x04;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x58: // CLI
                cpuP &= static_cast<uint8_t>(~0x04);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xD8: // CLD
                cpuP &= static_cast<uint8_t>(~0x08);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xF8: // SED
                cpuP |= 0x08;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x18: // CLC
                cpuP &= static_cast<uint8_t>(~0x01);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x38: // SEC
                cpuP |= 0x01;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xA9: // LDA #
                cpuA = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0xA2: // LDX #
                cpuX = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0xA0: // LDY #
                cpuY = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0x4C: // JMP abs
                pc = read16(static_cast<uint16_t>(pc + 1));
                break;
            case 0x20: { // JSR abs
                const uint16_t target = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t ret = static_cast<uint16_t>(pc + 2);
                push(static_cast<uint8_t>((ret >> 8) & 0xFF));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                break;
            }
            case 0x60: { // RTS
                const uint8_t lo = pull();
                const uint8_t hi = pull();
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            }
            case 0xD0: { // BNE rel
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                }
                break;
            }
            case 0xF0: { // BEQ rel
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                }
                break;
            }
            default:
                // Unknown opcode in scaffold: advance by one to keep forward progress.
                pc = static_cast<uint16_t>(pc + 1);
                break;
        }

        if (pc == oldPC) {
            pc = static_cast<uint16_t>(pc + 1);
        }
    }

    void setIecLines(bool atnHigh, bool clkHigh, bool dataHigh) {
        iecATN = atnHigh;
        iecCLK = clkHigh;
        iecDATA = dataHigh;
    }

    void enqueueIecCommandByte(uint8_t cmd) {
        iecRxQueue.push_back({cmd, true});
    }

    void enqueueIecDataByte(uint8_t data) {
        iecRxQueue.push_back({data, false});
    }

    void consumeReceivedByte(uint8_t byte, bool isCommand) {
        if (isCommand) {
            processIecCommandByte(byte);
        } else {
            processIecDataByte(byte);
        }
    }

    void stepIecSerial() {
        const bool currCLK = iecCLK;
        const bool currATN = iecATN;
        const bool currDATA = iecDATA;
        const bool fallingCLK = (iecPrevCLK && !currCLK);
        const bool risingCLK = (!iecPrevCLK && currCLK);
        const bool fallingATN = (iecPrevATN && !currATN);
        const bool commandPhase = !currATN;
        const bool hasClockEdge = (fallingCLK || risingCLK);

        if (fallingATN) {
            iecAtnFallingSeen++;
        }
        if (risingCLK) {
            iecClockRisingSeen++;
            if (!currATN) {
                iecClockRisingAtnLow++;
            }
        }

        IecSerialState nextState = IecSerialState::Idle;
        if (commandPhase) {
            nextState = IecSerialState::Command;
        } else if (iecTalking || iecKernelCompatForceTalkOnIcrSerial) {
            nextState = iecEoiPendingAck ? IecSerialState::TalkEoiAck : IecSerialState::TalkData;
        } else if (iecListening) {
            nextState = IecSerialState::ListenData;
        }
        const bool enteringCommandState = (iecSerialState != IecSerialState::Command && nextState == IecSerialState::Command);
        const bool enteringTalkData = (iecSerialState != IecSerialState::TalkData && nextState == IecSerialState::TalkData);

        if (!iecEnableAtnAck) {
            iecAtnAckPullDATA = false;
            iecAtnAckTicks = 0;
            iecAtnHandshakeActive = false;
            iecAtnAckSawClockLow = false;
        } else {
            if (fallingATN || enteringCommandState) {
                iecAtnAckTicks = revisionProfile.iecAtnAckTicksOverride;
                iecAtnHandshakeActive = true;
                iecAtnAckSawClockLow = false;
            }
            if (nextState != IecSerialState::Command) {
                iecAtnAckTicks = 0;
                iecAtnAckPullDATA = false;
                iecAtnHandshakeActive = false;
                iecAtnAckSawClockLow = false;
            } else if (iecAtnHandshakeActive) {
                // ATN handshake: listener keeps DATA low until controller pulls CLK low once.
                if (!currCLK && revisionProfile.iecHandshakeNeedsClockLowAck) {
                    iecAtnAckSawClockLow = true;
                    iecAtnAckPullDATA = true;
                    if (iecAtnAckTicks > 0) {
                        iecAtnAckTicks--;
                    }
                } else if (!revisionProfile.iecHandshakeNeedsClockLowAck) {
                    iecAtnAckPullDATA = (iecAtnAckTicks > 0);
                    if (iecAtnAckTicks > 0) {
                        iecAtnAckTicks--;
                    } else {
                        iecAtnHandshakeActive = false;
                    }
                } else if (!iecAtnAckSawClockLow) {
                    iecAtnAckPullDATA = true;
                } else {
                    iecAtnAckPullDATA = false;
                    iecAtnHandshakeActive = false;
                    iecAtnAckSawClockLow = false;
                    iecAtnAckTicks = 0;
                }
            } else {
                iecAtnAckPullDATA = false;
                iecAtnHandshakeActive = false;
            }
        }

        const bool rxPrimaryEdge = iecKernelSampleOnFallingClockEdge ? fallingCLK : risingCLK;
        bool rxClockEdge = rxPrimaryEdge || (iecKernelCompatSampleBothClockEdges && (iecKernelSampleOnFallingClockEdge ? risingCLK : fallingCLK));
        if (nextState == IecSerialState::Command && iecKernelSampleBothCommandEdges) {
            // Command phase decoding is tolerant to either clock edge.
            rxClockEdge = hasClockEdge;
        }
        const bool receiveCommand = (nextState == IecSerialState::Command);
        const bool receiveData = (nextState == IecSerialState::ListenData);
        if (enteringCommandState) {
            iecRxShift = 0;
            iecRxBitCount = 0;
            iecRxIdleTicks = 0;
            iecCommandFrameSync = true;
            iecCommandSawStartEdge = false;
            iecCommandFrameByteCount = 0;
            iecCommandFrameSawListenForDevice = false;
            iecCommandFrameSawTalkForDevice = false;
            iecCommandFrameSawListenSecondary0 = false;
            iecCommandFrameSawTalkSecondary0 = false;
        }
        if (nextState != IecSerialState::Command) {
            iecCommandFrameSync = false;
            iecCommandSawStartEdge = false;
        }
        bool commandEdgeQualified = true;
        if (receiveCommand) {
            if (iecCommandFrameSync && !iecCommandSawStartEdge) {
                if (fallingCLK) {
                    iecCommandSawStartEdge = true;
                }
                commandEdgeQualified = false;
            }
        }
        if (rxClockEdge && (receiveData || (receiveCommand && commandEdgeQualified))) {
            const uint8_t inBit = iecDATA ? 1 : 0;
            iecRxShift = static_cast<uint8_t>(iecRxShift | static_cast<uint8_t>(inBit << iecRxBitCount));
            iecRxBitCount++;
            if (iecRxBitCount >= 8) {
                const uint8_t rxByte = iecRxShift;
                const bool looksLikeCommand = ((rxByte & 0xF0) == 0x20) ||
                                              ((rxByte & 0xF0) == 0x40) ||
                                              ((rxByte & 0xF0) == 0x60) ||
                                              ((rxByte & 0xF0) == 0xE0) ||
                                              ((rxByte & 0xF0) == 0xF0) ||
                                              (rxByte == 0x3F) ||
                                              (rxByte == 0x5F);
                const bool treatAsCommand = receiveCommand || (iecKernelCompatForceTalkOnIcrSerial && looksLikeCommand);
                consumeReceivedByte(rxByte, treatAsCommand);
                iecRxProcessed++;
                if (receiveCommand) {
                    iecCommandFrameByteCount++;
                    if ((rxByte & 0xF0) == 0x20 && ((rxByte & 0x1F) == (iecDeviceAddress & 0x1F))) {
                        iecCommandFrameSawListenForDevice = true;
                    }
                    if ((rxByte & 0xF0) == 0x40 && ((rxByte & 0x1F) == (iecDeviceAddress & 0x1F))) {
                        iecCommandFrameSawTalkForDevice = true;
                    }
                    if ((rxByte & 0xF0) == 0xF0 && ((rxByte & 0x0F) == 0x00)) {
                        iecCommandFrameSawListenSecondary0 = true;
                    }
                    if ((rxByte & 0xF0) == 0x60 && ((rxByte & 0x0F) == 0x00)) {
                        iecCommandFrameSawTalkSecondary0 = true;
                    }
                    if (iecKernelCompatForceTalkOnIcrSerial &&
                        iecCommandFrameSawTalkForDevice &&
                        iecCommandFrameSawTalkSecondary0) {
                        iecTalking = true;
                        iecTalkSecondary = 0;
                        iecActiveTalkChannel = 0;
                        iecOpenTalkChannels[0] = true;
                        if (!iecDirectoryStubPrepared) {
                            iecDirectoryWildcardPattern.clear();
                            iecDirectoryTypeFilter.clear();
                            iecDirectoryModeFilter.clear();
                            iecDirectoryModeFilterNegated = false;
                            buildDirectoryStubPayload();
                        }
                    }
                }
                if (iecEnableListenerByteAck && ((receiveData && !treatAsCommand) || revisionProfile.iecRxAckOnAnyDataByte)) {
                    iecRxByteAckTicks = IEC_RX_BYTE_ACK_TICKS;
                    iecRxByteAckPullDATA = true;
                }
                iecRxShift = 0;
                iecRxBitCount = 0;
            }
        }

        if (iecEnableListenerByteAck && nextState == IecSerialState::ListenData && iecRxByteAckTicks > 0) {
            iecRxByteAckPullDATA = true;
            if (!currCLK) {
                iecRxByteAckTicks = 0;
                iecRxByteAckPullDATA = false;
            } else {
                iecRxByteAckTicks--;
            }
        } else {
            iecRxByteAckPullDATA = false;
        }

        if (iecRxBitCount > 0) {
            if (hasClockEdge) {
                iecRxIdleTicks = 0;
            } else {
                if (nextState != IecSerialState::Command) {
                    iecRxIdleTicks++;
                    if (iecRxIdleTicks > IEC_SERIAL_TIMEOUT_TICKS) {
                        iecRxTimeoutCount++;
                        iecRxBitCount = 0;
                        iecRxShift = 0;
                        iecRxIdleTicks = 0;
                        iecStatusLine = "74,DRIVE NOT READY,00,00";
                    }
                }
            }
        } else {
            iecRxIdleTicks = 0;
        }

        if (nextState == IecSerialState::TalkEoiAck) {
            iecSerialPullDATA = false;
            if (!currDATA) {
                iecEoiAckLowSeen = true;
            }
            if (iecEoiAckLowSeen && currDATA) {
                iecEoiPendingAck = false;
                iecEoiAckLowSeen = false;
                iecEoiWaitTicks = 0;
                iecEoiAckCount++;
            }
            if (!iecEoiAckLowSeen) {
                iecEoiWaitTicks++;
                if (iecEoiWaitTicks > IEC_EOI_TIMEOUT_TICKS) {
                    iecEoiPendingAck = false;
                    iecEoiWaitTicks = 0;
                    iecEoiTimeoutCount++;
                    iecStatusLine = "74,DRIVE NOT READY,00,00";
                }
            }
        } else if (nextState != IecSerialState::TalkData) {
            iecTxByteActive = false;
            iecTxBitCount = 0;
            iecTalkStartPending = false;
            iecTalkFrameArmed = false;
            iecTalkSawStartEdge = false;
            iecSerialPullDATA = false;
            iecTxCurrentIsEoi = false;
            iecEoiPendingAck = false;
            iecEoiAckLowSeen = false;
        } else {
            if (enteringTalkData) {
                iecTalkFrameArmed = true;
                iecTalkSawStartEdge = false;
            }
            const bool talkChannelConfirmed = (iecActiveTalkChannel == 0)
                                                  ? iecTalkSa0Confirmed
                                                  : (iecActiveTalkChannel != 0xFF);
            if (enteringTalkData && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty()) {
                iecTalkStartPending = talkChannelConfirmed;
                iecTalkStartByte = iecTxQueue.front();
                iecTalkStartIsEoi = (iecTxQueue.size() == 1);
                iecSerialPullDATA = false;
            }
            if (!iecTalkStartPending && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty() && talkChannelConfirmed) {
                iecTalkStartPending = true;
                iecTalkStartByte = iecTxQueue.front();
                iecTalkStartIsEoi = (iecTxQueue.size() == 1);
            }
            if (iecKernelCompatForceTalkOnIcrSerial && enteringTalkData && iecTxQueue.empty() && iecDirectoryStubPrepared) {
                buildDirectoryStubPayload();
            }
            if (!currATN && !iecKernelIgnoreAtnForTalkDataPhase) {
                // Pause data shifting while ATN is asserted, but keep TX state latched.
                iecSerialPullDATA = false;
            } else {
                bool startedTxOnFalling = false;
                if (fallingCLK) {
                    if (iecTalkFrameArmed && !iecTalkSawStartEdge) {
                        iecTalkSawStartEdge = true;
                        startedTxOnFalling = true;
                    }
                    if (iecTalkSawStartEdge && iecTalkStartPending && !iecTxByteActive && !iecEoiPendingAck && talkChannelConfirmed) {
                        iecTalkStartPending = false;
                        if (!iecTxQueue.empty()) {
                            const bool lastByte = (iecTxQueue.size() == 1);
                            iecTxShift = iecTxQueue.front();
                            iecTxQueue.pop_front();
                            iecTxServed++;
                            iecTxBitCount = 0;
                            iecTxByteActive = true;
                            iecTxCurrentIsEoi = lastByte;
                            startedTxOnFalling = true;
                        } else {
                            iecSerialPullDATA = false;
                        }
                    }

                    if (iecTalkSawStartEdge && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty() && talkChannelConfirmed) {
                        const bool lastByte = (iecTxQueue.size() == 1);
                        iecTxShift = iecTxQueue.front();
                        iecTxQueue.pop_front();
                        iecTxServed++;
                        iecTxBitCount = 0;
                        iecTxByteActive = true;
                        iecTxCurrentIsEoi = lastByte;
                        startedTxOnFalling = true;
                    }

                }

                const bool txPrimaryAdvance = risingCLK;
                const bool txAdvanceEdge = txPrimaryAdvance || (iecKernelCompatSampleBothClockEdges && (iecKernelSampleOnFallingClockEdge ? risingCLK : fallingCLK));
                if (txAdvanceEdge && iecTxByteActive) {
                    if (!(risingCLK && startedTxOnFalling)) {
                        iecTxBitCount++;
                        if (iecTxBitCount >= 8) {
                            iecTxByteActive = false;
                            iecTxBitCount = 0;
                            if (iecTxCurrentIsEoi) {
                                iecEoiPendingAck = true;
                                iecEoiAckLowSeen = false;
                                iecEoiWaitTicks = 0;
                            }
                            iecTxCurrentIsEoi = false;
                        }
                    }
                }

                if (iecTxByteActive) {
                    const uint8_t outBit = static_cast<uint8_t>((iecTxShift >> iecTxBitCount) & 0x01);
                    iecSerialPullDATA = (outBit == 0);
                }

                if (iecTxByteActive) {
                    if (hasClockEdge) {
                        iecTxIdleTicks = 0;
                    } else {
                        iecTxIdleTicks++;
                        if (iecTxIdleTicks > IEC_SERIAL_TIMEOUT_TICKS) {
                            iecTxTimeoutCount++;
                            iecTxByteActive = false;
                            iecTxBitCount = 0;
                            iecSerialPullDATA = false;
                            iecTxIdleTicks = 0;
                            iecStatusLine = "74,DRIVE NOT READY,00,00";
                        }
                    }
                } else {
                    iecTxIdleTicks = 0;
                }

                if (iecKernelCompatForceTalkOnIcrSerial && iecActiveTalkChannel == 0 && iecTxQueue.size() < 16) {
                    buildDirectoryStubPayload();
                }
            }
        }

        iecSerialState = nextState;
        iecPrevCLK = currCLK;
        iecPrevATN = currATN;
        iecPrevDATA = currDATA;
    }

    size_t pendingIecRx() const {
        return iecRxQueue.size();
    }

    size_t pendingIecTx() const {
        return iecTxQueue.size();
    }

    bool hostReadTalkByte(uint8_t &out) {
        if (iecTxQueue.empty()) {
            return false;
        }
        out = iecTxQueue.front();
        iecTxQueue.pop_front();
        iecTxServed++;
        return true;
    }

    bool processIecDataByte(uint8_t data) {
        if (!iecListening) {
            return false;
        }

        if (iecActiveListenChannel == 15) {
            iecCommandChannelBuffer.push_back(data);
            iecDataDispatchCount++;
            return true;
        }

        if (iecExpectingNameBytes) {
            iecNameBuffer.push_back(data);
            iecDataDispatchCount++;
            return true;
        }

        if (iecActiveListenChannel != 0) {
            iecStatusLine = "70,NO CHANNEL,00,00";
            iecCommandSyntaxErrorCount++;
            return false;
        }

        iecStatusLine = "64,FILE TYPE MISMATCH,00,00";
        iecCommandSyntaxErrorCount++;
        return false;
    }

    uint8_t toUpperAscii(uint8_t c) const {
        if (c >= static_cast<uint8_t>('a') && c <= static_cast<uint8_t>('z')) {
            return static_cast<uint8_t>(c - 32);
        }
        return c;
    }

    void buildStatusPayload() {
        iecTxQueue.clear();
        for (char c : iecStatusLine) {
            iecTxQueue.push_back(static_cast<uint8_t>(c));
        }
        iecTxQueue.push_back(0x0D);
    }

    static bool parseHexByte(const std::string &s, uint8_t &out) {
        if (s.size() != 2 || !std::isxdigit(static_cast<unsigned char>(s[0])) || !std::isxdigit(static_cast<unsigned char>(s[1]))) {
            return false;
        }
        const int hi = std::isdigit(static_cast<unsigned char>(s[0])) ? (s[0] - '0') : (10 + (std::toupper(static_cast<unsigned char>(s[0])) - 'A'));
        const int lo = std::isdigit(static_cast<unsigned char>(s[1])) ? (s[1] - '0') : (10 + (std::toupper(static_cast<unsigned char>(s[1])) - 'A'));
        out = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    }

    static bool parseHexWord(const std::string &s, uint16_t &out) {
        if (s.size() != 4) {
            return false;
        }
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!parseHexByte(s.substr(0, 2), hi) || !parseHexByte(s.substr(2, 2), lo)) {
            return false;
        }
        out = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
        return true;
    }

    static std::vector<std::string> splitComma(const std::string &in) {
        std::vector<std::string> parts;
        std::string token;
        for (char c : in) {
            if (c == ',') {
                parts.push_back(token);
                token.clear();
            } else {
                token.push_back(c);
            }
        }
        parts.push_back(token);
        return parts;
    }

    static std::string trimAscii(const std::string &s) {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) {
            b++;
        }
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) {
            e--;
        }
        return s.substr(b, e - b);
    }

    void buildCommandResponsePayload() {
        iecTxQueue.clear();
        for (uint8_t b : iecCommandResponseQueue) {
            iecTxQueue.push_back(b);
        }
        if (!iecTxQueue.empty()) {
            iecTxQueue.push_back(0x0D);
        }
        iecCommandResponseQueue.clear();
    }

    void buildDirectoryFromBlockBufferPayload(uint8_t channel) {
        if (!iecBlockBufferValid) {
            buildDirectoryStubPayload();
            return;
        }

        const uint8_t start = iecChannelBufferPos[channel];
        iecTxQueue.clear();
        iecTxQueue.push_back(0x01);
        iecTxQueue.push_back(0x08);
        for (uint16_t i = start; i < 256; ++i) {
            iecTxQueue.push_back(iecBlockBuffer[i]);
        }
        iecTxQueue.push_back(0x00);
        iecTxQueue.push_back(0x00);
    }

    void processCommandChannelBuffer() {
        if (iecCommandChannelBuffer.empty()) {
            return;
        }

        std::string cmd;
        cmd.reserve(iecCommandChannelBuffer.size());
        for (uint8_t c : iecCommandChannelBuffer) {
            if (c == 0x0D || c == 0x00) {
                break;
            }
            cmd.push_back(static_cast<char>(toUpperAscii(c)));
        }
        iecCommandChannelBuffer.clear();

        if (cmd.empty()) {
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            iecCommandSyntaxErrorCount++;
            return;
        }

        if (cmd.rfind("M-R", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 4) {
                uint16_t addr = 0;
                uint8_t len = 0;
                const std::string a = trimAscii(parts[1]);
                const std::string l = trimAscii(parts[3]);
                if (parseHexWord(a, addr) && parseHexByte(l, len) && len > 0) {
                    iecCommandResponseQueue.clear();
                    for (uint16_t i = 0; i < len; ++i) {
                        iecCommandResponseQueue.push_back(memory[static_cast<uint16_t>(addr + i)]);
                    }
                    iecStatusLine = "00,OK,00,00";
                    return;
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("M-W", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 5) {
                uint16_t addr = 0;
                uint8_t len = 0;
                const std::string a = trimAscii(parts[1]);
                const std::string l = trimAscii(parts[3]);
                if (!parseHexWord(a, addr) || !parseHexByte(l, len) || len == 0) {
                    iecStatusLine = "30,SYNTAX ERROR,00,00";
                    return;
                }

                if (parts.size() < static_cast<size_t>(4 + len)) {
                    iecStatusLine = "30,SYNTAX ERROR,00,00";
                    return;
                }

                for (uint16_t i = 0; i < len; ++i) {
                    uint8_t b = 0;
                    if (!parseHexByte(trimAscii(parts[4 + i]), b)) {
                        iecStatusLine = "30,SYNTAX ERROR,00,00";
                        return;
                    }
                    write(static_cast<uint16_t>(addr + i), b);
                }
                iecStatusLine = "00,OK,00,00";
                return;
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("M-E", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 2) {
                uint16_t addr = 0;
                const std::string a = trimAscii(parts[1]);
                if (parseHexWord(a, addr)) {
                    iecLastExecuteAddr = addr;
                    dispatchExecuteStub(addr);
                    return;
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("B-", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            const std::string op = (parts.size() >= 1) ? trimAscii(parts[0]) : std::string();
            if (parts.size() >= 4) {
                uint8_t ch = 0;
                uint8_t trk = 0;
                uint8_t sec = 0;
                if (parseHexByte(trimAscii(parts[1]), ch) &&
                    parseHexByte(trimAscii(parts[2]), trk) &&
                    parseHexByte(trimAscii(parts[3]), sec)) {
                    if (op == "B-R" || op == "B-W" || op == "B-P" || op == "B-A" || op == "B-F") {
                        iecLastBlockChannel = ch;
                        iecLastBlockTrack = trk;
                        iecLastBlockSector = sec;
                        iecLastBlockCommand = op;

                        if (op == "B-R") {
                            loadVirtualBlock(trk, sec);
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-W") {
                            iecStatusLine = "00,OK,00,00";
                            flushVirtualBlock(trk, sec);
                        } else if (op == "B-P") {
                            uint8_t ptr = 0;
                            if (parts.size() < 5 || !parseHexByte(trimAscii(parts[4]), ptr)) {
                                iecStatusLine = "30,SYNTAX ERROR,00,00";
                                return;
                            }
                            iecChannelBufferPos[ch & 0x0F] = ptr;
                            iecChannelPointerValid[ch & 0x0F] = true;
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-F") {
                            const bool freed = freeVirtualBlock(trk, sec);
                            if (!freed) {
                                iecStatusLine = "65,NO BLOCK,00,00";
                                return;
                            }
                            iecBlockBuffer.fill(0);
                            iecBlockBufferValid = true;
                            iecBlockBufferTrack = trk;
                            iecBlockBufferSector = sec;
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-A") {
                            const bool allocated = allocateVirtualBlock(trk, sec, ch);
                            if (!allocated) {
                                iecStatusLine = "63,FILE EXISTS,00,00";
                                return;
                            }
                            iecStatusLine = "00,OK,00,00";
                        } else {
                            iecStatusLine = "00,OK,00,00";
                        }
                        return;
                    }
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd == "U1" || cmd == "U2") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        if (cmd == "I0" || cmd == "UI" || cmd == "UJ") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        if (cmd == "N0:" || cmd == "NEW") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        iecStatusLine = "30,SYNTAX ERROR,00,00";
    }

    uint16_t blockLinearBase(uint8_t track, uint8_t sector) const {
        return static_cast<uint16_t>(((uint16_t(track) << 8) | sector) & 0xBFFF);
    }

    uint16_t blockAllocIndex(uint8_t track, uint8_t sector) const {
        const uint32_t key = static_cast<uint32_t>(track) * 21u + static_cast<uint32_t>(sector);
        return static_cast<uint16_t>(key % IEC_TOTAL_VIRTUAL_BLOCKS);
    }

    static std::string hex2(uint8_t v) {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(v);
        return oss.str();
    }

    std::string makeCatalogName(uint8_t track, uint8_t sector) const {
        std::string name = "BLK-";
        name += hex2(track);
        name += hex2(sector);
        return name;
    }

    std::string normalizeOpenName(const std::vector<uint8_t> &raw) const {
        std::string out;
        out.reserve(raw.size());
        for (uint8_t c : raw) {
            if (c == 0x00 || c == 0x0D) {
                break;
            }
            if (c >= 'a' && c <= 'z') {
                c = static_cast<uint8_t>(c - 32);
            }
            if (c == '"') {
                continue;
            }
            if (c >= 32 && c <= 126) {
                out.push_back(static_cast<char>(c));
            }
            if (out.size() >= 16) {
                break;
            }
        }
        return out;
    }

    std::string normalizeUpperAscii(const std::string &in) const {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            uint8_t u = static_cast<uint8_t>(c);
            if (u >= 'a' && u <= 'z') {
                u = static_cast<uint8_t>(u - 32);
            }
            if (u >= 32 && u <= 126) {
                out.push_back(static_cast<char>(u));
            }
        }
        return out;
    }

    void parseOpenSpec(uint8_t channel, const std::string &raw) {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        const std::string up = normalizeUpperAscii(raw);
        std::vector<std::string> parts = splitComma(up);
        if (parts.empty()) {
            iecChannelOpenName[ch].clear();
            iecChannelOpenNameValid[ch] = false;
            iecChannelOpenType[ch] = "PRG";
            iecChannelOpenMode[ch].clear();
            return;
        }

        std::string name = trimAscii(parts[0]);
        if (!name.empty() && name.front() == '"' && name.back() == '"' && name.size() >= 2) {
            name = name.substr(1, name.size() - 2);
        }

        iecChannelOpenName[ch] = name;
        iecChannelOpenNameValid[ch] = !name.empty();
        iecChannelOpenType[ch] = "PRG";
        iecChannelOpenMode[ch].clear();

        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string tok = trimAscii(parts[i]);
            if (tok == "P") {
                iecChannelOpenType[ch] = "PRG";
            } else if (tok == "S") {
                iecChannelOpenType[ch] = "SEQ";
            } else if (tok == "U") {
                iecChannelOpenType[ch] = "USR";
            } else if (tok == "L") {
                iecChannelOpenType[ch] = "REL";
            } else if (tok == "W" || tok == "R" || tok == "A") {
                iecChannelOpenMode[ch] = tok;
            }
        }
    }

    void commitOpenNameFromBuffer(uint8_t channel) {
        if (channel >= 16) {
            return;
        }
        const std::string spec = normalizeOpenName(iecNameBuffer);
        parseOpenSpec(channel, spec);
    }

    std::string pickCatalogNameForChannel(uint8_t channel, uint8_t track, uint8_t sector) const {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        if (iecChannelOpenNameValid[ch] && !iecChannelOpenName[ch].empty() && iecChannelOpenName[ch] != "$") {
            return iecChannelOpenName[ch];
        }
        return makeCatalogName(track, sector);
    }

    struct DirectoryFilters {
        std::string pattern;
        std::string type;
        std::string mode;
        bool modeNegated = false;
    };

    DirectoryFilters extractDirectoryFilters() const {
        const std::string spec = normalizeOpenName(iecNameBuffer);
        if (spec.empty()) {
            return DirectoryFilters{};
        }

        const std::vector<std::string> parts = splitComma(spec);
        std::string first = parts.empty() ? std::string() : parts[0];
        first = trimAscii(first);
        if (!first.empty() && first.front() == '"' && first.back() == '"' && first.size() >= 2) {
            first = first.substr(1, first.size() - 2);
        }

        if (first.empty() || first[0] != '$') {
            return DirectoryFilters{};
        }

        std::string pat = first.substr(1);
        pat = normalizeUpperAscii(pat);
        pat = trimAscii(pat);

        std::string typeFilter;
        std::string modeFilter;
        bool modeNegated = false;
        if (parts.size() >= 2) {
            std::string t = trimAscii(normalizeUpperAscii(parts[1]));
            if (t == "P") typeFilter = "PRG";
            else if (t == "S") typeFilter = "SEQ";
            else if (t == "U") typeFilter = "USR";
            else if (t == "L") typeFilter = "REL";
        }
        if (parts.size() >= 3) {
            std::string m = trimAscii(normalizeUpperAscii(parts[2]));
            if (!m.empty() && m[0] == '!') {
                modeNegated = true;
                m = trimAscii(m.substr(1));
            }
            if (m == "R" || m == "W" || m == "A") {
                modeFilter = m;
            } else {
                modeNegated = false;
            }
        }

        DirectoryFilters out;
        out.pattern = pat;
        out.type = typeFilter;
        out.mode = modeFilter;
        out.modeNegated = modeNegated;
        return out;
    }

    bool wildcardMatch(const std::string &pattern, const std::string &text) const {
        if (pattern.empty()) {
            return true;
        }

        const std::string upText = normalizeUpperAscii(text);
        const size_t pn = pattern.size();
        const size_t tn = upText.size();
        size_t p = 0;
        size_t t = 0;
        size_t star = std::string::npos;
        size_t match = 0;

        while (t < tn) {
            if (p < pn && (pattern[p] == '?' || pattern[p] == upText[t])) {
                p++;
                t++;
            } else if (p < pn && pattern[p] == '*') {
                star = p++;
                match = t;
            } else if (star != std::string::npos) {
                p = star + 1;
                t = ++match;
            } else {
                return false;
            }
        }

        while (p < pn && pattern[p] == '*') {
            p++;
        }
        return p == pn;
    }

    int findFreeCatalogSlot() const {
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            if (!iecCatalog[i].used) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int ensureCatalogEntryForChannel(uint8_t channel, uint8_t track, uint8_t sector) {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        const int8_t mapped = iecChannelCatalogEntry[ch];
        if (mapped >= 0) {
            const int idx = static_cast<int>(mapped);
            if (idx >= 0 && idx < static_cast<int>(iecCatalog.size()) && iecCatalog[idx].used) {
                if (iecChannelOpenNameValid[ch] && !iecChannelOpenName[ch].empty() && iecChannelOpenName[ch] != "$") {
                    iecCatalog[idx].name = iecChannelOpenName[ch];
                    iecCatalog[idx].type = iecChannelOpenType[ch];
                    iecCatalog[idx].mode = iecChannelOpenMode[ch];
                }
                return idx;
            }
        }

        const int freeIdx = findFreeCatalogSlot();
        if (freeIdx < 0) {
            return -1;
        }

        iecCatalog[freeIdx] = VirtualCatalogEntry{};
        iecCatalog[freeIdx].used = true;
        iecCatalog[freeIdx].channel = ch;
        iecCatalog[freeIdx].track = track;
        iecCatalog[freeIdx].sector = sector;
        iecCatalog[freeIdx].blocks = 0;
        iecCatalog[freeIdx].name = pickCatalogNameForChannel(ch, track, sector);
        iecCatalog[freeIdx].type = iecChannelOpenType[ch];
        iecCatalog[freeIdx].mode = iecChannelOpenMode[ch];
        iecChannelCatalogEntry[ch] = static_cast<int8_t>(freeIdx);
        return freeIdx;
    }

    void removeCatalogEntryByIndex(int idx) {
        if (idx < 0 || idx >= static_cast<int>(iecCatalog.size())) {
            return;
        }
        const uint8_t ch = iecCatalog[idx].channel;
        if (ch < 16 && iecChannelCatalogEntry[ch] == idx) {
            iecChannelCatalogEntry[ch] = -1;
        }
        iecCatalog[idx] = VirtualCatalogEntry{};
    }

    uint16_t virtualBlocksFree() const {
        if (iecAllocatedBlockCount >= IEC_TOTAL_VIRTUAL_BLOCKS) {
            return 0;
        }
        return static_cast<uint16_t>(IEC_TOTAL_VIRTUAL_BLOCKS - iecAllocatedBlockCount);
    }

    bool allocateVirtualBlock(uint8_t track, uint8_t sector, uint8_t channel) {
        const uint16_t idx = blockAllocIndex(track, sector);
        if (iecBlockAllocated[idx]) {
            return false;
        }

        const int catIdx = ensureCatalogEntryForChannel(channel, track, sector);
        if (catIdx < 0) {
            return false;
        }

        iecBlockAllocated[idx] = 1;
        iecAllocOwnerEntry[idx] = static_cast<uint8_t>(catIdx);
        iecAllocatedBlockCount = static_cast<uint16_t>(iecAllocatedBlockCount + 1);
        iecCatalog[catIdx].blocks = static_cast<uint16_t>(iecCatalog[catIdx].blocks + 1);
        return true;
    }

    bool freeVirtualBlock(uint8_t track, uint8_t sector) {
        const uint16_t idx = blockAllocIndex(track, sector);
        if (!iecBlockAllocated[idx]) {
            return false;
        }

        const uint8_t owner = iecAllocOwnerEntry[idx];
        iecBlockAllocated[idx] = 0;
        iecAllocOwnerEntry[idx] = 0xFF;
        if (iecAllocatedBlockCount > 0) {
            iecAllocatedBlockCount = static_cast<uint16_t>(iecAllocatedBlockCount - 1);
        }

        if (owner != 0xFF && owner < iecCatalog.size() && iecCatalog[owner].used) {
            if (iecCatalog[owner].blocks > 0) {
                iecCatalog[owner].blocks = static_cast<uint16_t>(iecCatalog[owner].blocks - 1);
            }
            if (iecCatalog[owner].blocks == 0) {
                removeCatalogEntryByIndex(static_cast<int>(owner));
            }
        }

        return true;
    }

    void loadVirtualBlock(uint8_t track, uint8_t sector) {
        const uint16_t base = blockLinearBase(track, sector);
        for (uint16_t i = 0; i < 256; ++i) {
            iecBlockBuffer[i] = memory[static_cast<uint16_t>((base + i) & 0xBFFF)];
        }
        iecBlockBufferValid = true;
        iecBlockBufferTrack = track;
        iecBlockBufferSector = sector;
        iecDiskMap[0] = track;
        iecDiskMap[1] = sector;
    }

    void flushVirtualBlock(uint8_t track, uint8_t sector) {
        if (!iecBlockBufferValid) {
            iecStatusLine = "66,ILLEGAL TRACK OR SECTOR,00,00";
            return;
        }
        const uint16_t base = blockLinearBase(track, sector);
        for (uint16_t i = 0; i < 256; ++i) {
            write(static_cast<uint16_t>((base + i) & 0xBFFF), iecBlockBuffer[i]);
        }
        iecDiskMap[0] = track;
        iecDiskMap[1] = sector;
    }

    void dispatchExecuteStub(uint16_t addr) {
        iecExecDispatchCount++;
        if (addr >= 0xC000) {
            iecLastExecDispatch = "DOS_ENTRY";
            iecStatusLine = "00,OK,00,00";
            return;
        }
        if (addr >= 0x0400 && addr <= 0x07FF) {
            iecLastExecDispatch = "BUFFER_ENTRY";
            iecStatusLine = "00,OK,00,00";
            return;
        }
        iecLastExecDispatch = "UNKNOWN_ENTRY";
        iecStatusLine = "31,SYNTAX ERROR,00,00";
    }

    bool isDirectoryRequest() const {
        if (iecNameBuffer.empty()) {
            return false;
        }
        return iecNameBuffer[0] == static_cast<uint8_t>('$');
    }

    void enqueueBasicLine(std::vector<uint8_t> &prg, uint16_t &cursor, uint16_t lineNo, const std::string &text) {
        const uint16_t next = static_cast<uint16_t>(cursor + 2 + 2 + text.size() + 1);
        prg.push_back(static_cast<uint8_t>(next & 0xFF));
        prg.push_back(static_cast<uint8_t>((next >> 8) & 0xFF));
        prg.push_back(static_cast<uint8_t>(lineNo & 0xFF));
        prg.push_back(static_cast<uint8_t>((lineNo >> 8) & 0xFF));
        for (char c : text) {
            prg.push_back(static_cast<uint8_t>(c));
        }
        prg.push_back(0x00);
        cursor = next;
    }

    void buildDirectoryStubPayload() {
        std::vector<uint8_t> prg;
        prg.reserve(512);

        prg.push_back(0x01);
        prg.push_back(0x08);

        uint16_t cursor = 0x0801;
        enqueueBasicLine(prg, cursor, 0, "0 \"OPENCODE 1541\" 00 2A");

        uint16_t lineNo = 1;
        bool anyCatalog = false;
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            if (!iecCatalog[i].used) {
                continue;
            }
            if (!wildcardMatch(iecDirectoryWildcardPattern, iecCatalog[i].name)) {
                continue;
            }
            if (!iecDirectoryTypeFilter.empty() && iecCatalog[i].type != iecDirectoryTypeFilter) {
                continue;
            }
            if (!iecDirectoryModeFilter.empty()) {
                const bool modeMatch = (iecCatalog[i].mode == iecDirectoryModeFilter);
                if ((!iecDirectoryModeFilterNegated && !modeMatch) ||
                    (iecDirectoryModeFilterNegated && modeMatch)) {
                    continue;
                }
            }
            anyCatalog = true;
            std::ostringstream entry;
            entry << iecCatalog[i].blocks << " \"" << iecCatalog[i].name << "\" " << iecCatalog[i].type;
            if (!iecCatalog[i].mode.empty()) {
                entry << "," << iecCatalog[i].mode;
            }
            enqueueBasicLine(prg, cursor, lineNo, entry.str());
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        if (!anyCatalog) {
            enqueueBasicLine(prg, cursor, lineNo, "1 \"$\" PRG");
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        std::ostringstream freeLine;
        freeLine << virtualBlocksFree() << " BLOCKS FREE.";
        enqueueBasicLine(prg, cursor, lineNo, freeLine.str());

        prg.push_back(0x00);
        prg.push_back(0x00);

        iecTxQueue.clear();
        for (uint8_t b : prg) {
            iecTxQueue.push_back(b);
        }
        iecDirectoryStubPrepared = true;
        iecDirectoryFromBlockBuffer = false;
    }

    bool processIecCommandByte(uint8_t cmd) {
        iecCommandSeen = true;
        lastIecCommand = cmd;
        iecCommandDispatchCount++;

        if ((cmd & 0xF0) == 0xE0) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecOpenListenChannels[ch] = false;
            iecOpenTalkChannels[ch] = false;
            iecChannelCloseCount[ch]++;
            if (iecActiveListenChannel == ch) {
                iecActiveListenChannel = 0xFF;
                iecExpectingNameBytes = false;
                if (ch == 15) {
                    iecCommandChannelBuffer.clear();
                }
            }
            if (iecActiveTalkChannel == ch) {
                iecActiveTalkChannel = 0xFF;
                if (ch == 0) {
                    iecTalkSa0Confirmed = false;
                }
            }
            return true;
        }

        // UNLISTEN / UNTALK (global)
        if (cmd == 0x3F) {
            if (iecExpectingNameBytes) {
                if (iecActiveListenChannel < 16) {
                    commitOpenNameFromBuffer(iecActiveListenChannel);
                }
                if (iecActiveListenChannel == 0 && isDirectoryRequest()) {
                    const DirectoryFilters filters = extractDirectoryFilters();
                    iecDirectoryWildcardPattern = filters.pattern;
                    iecDirectoryTypeFilter = filters.type;
                    iecDirectoryModeFilter = filters.mode;
                    iecDirectoryModeFilterNegated = filters.modeNegated;
                    buildDirectoryStubPayload();
                    if (iecBlockBufferValid) {
                        buildDirectoryFromBlockBufferPayload(0);
                        iecDirectoryFromBlockBuffer = true;
                    }
                    iecOpenTalkChannels[0] = true;
                }
                iecExpectingNameBytes = false;
            }

            if (iecActiveListenChannel == 15) {
                processCommandChannelBuffer();
            }

            iecListening = false;
            iecListenSecondary = 0xFF;
            return true;
        }
        if (cmd == 0x5F) {
            iecTalking = false;
            iecTalkSecondary = 0xFF;
            iecActiveTalkChannel = 0xFF;
            iecTalkSa0Confirmed = false;
            iecEoiPendingAck = revisionProfile.iecStrictEoiAck ? false : iecEoiPendingAck;
            iecEoiAckLowSeen = false;
            return true;
        }

        if (iecListening && (cmd & 0xF0) == 0xF0) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecListenSecondary = ch;
            iecActiveListenChannel = ch;
            iecOpenListenChannels[ch] = true;
            iecChannelOpenCount[ch]++;
            iecNameBuffer.clear();
            iecExpectingNameBytes = true;
            iecChannelOpenName[ch].clear();
            iecChannelOpenNameValid[ch] = false;
            return true;
        }

        if (iecTalking && (cmd & 0xF0) == 0x60) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecTalkSecondary = ch;
            iecActiveTalkChannel = ch;
            iecOpenTalkChannels[ch] = true;
            iecChannelOpenCount[ch]++;
            iecTalkSa0Confirmed = (ch == 0);
            if (ch == 0) {
                if (!iecDirectoryStubPrepared && iecKernelCompatAutoDirectoryOnTalk0) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
                if (iecTxQueue.empty() && iecDirectoryStubPrepared) {
                    if (iecDirectoryFromBlockBuffer) {
                        buildDirectoryFromBlockBufferPayload(ch);
                    } else {
                        buildDirectoryStubPayload();
                    }
                }
            } else if (ch == 15) {
                if (!iecCommandResponseQueue.empty()) {
                    buildCommandResponsePayload();
                } else {
                    buildStatusPayload();
                }
            } else {
                iecStatusLine = "70,NO CHANNEL,00,00";
                iecCommandSyntaxErrorCount++;
            }
            return true;
        }

        // LISTEN 0x20..0x3E, TALK 0x40..0x5E
        if ((cmd & 0xF0) == 0x20) {
            if (revisionProfile.iecCommandNeedsAtnLow && iecSerialState == IecSerialState::Command && iecATN) {
                return false;
            }
            iecListening = ((cmd & 0x1F) == (iecDeviceAddress & 0x1F));
            if (!iecListening) {
                iecExpectingNameBytes = false;
                iecListenSecondary = 0xFF;
                iecActiveListenChannel = 0xFF;
            } else {
                iecOpenListenChannels.fill(false);
                iecOpenListenChannels[0] = true;
                iecActiveListenChannel = 0;
                iecListenSecondary = 0;
                iecExpectingNameBytes = true;
                iecNameBuffer.clear();
            }
            return iecListening;
        }
        if ((cmd & 0xF0) == 0x40) {
            if (revisionProfile.iecCommandNeedsAtnLow && iecSerialState == IecSerialState::Command && iecATN) {
                return false;
            }
            iecTalking = ((cmd & 0x1F) == (iecDeviceAddress & 0x1F));
            if (iecTalking && iecKernelCompatAutoTalkDirectory) {
                if (!iecDirectoryStubPrepared) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
                if (iecActiveTalkChannel == 0xFF) {
                    iecTalkSecondary = 0;
                    iecActiveTalkChannel = 0;
                    iecOpenTalkChannels[0] = true;
                    iecTalkSa0Confirmed = true;
                }
                if (iecTxQueue.empty() && iecDirectoryStubPrepared) {
                    buildDirectoryStubPayload();
                }
            }
            if (iecTalking && iecKernelCompatForceTalkOnIcrSerial) {
                if (iecActiveTalkChannel == 0xFF) {
                    iecTalkSecondary = 0;
                    iecActiveTalkChannel = 0;
                    iecOpenTalkChannels[0] = true;
                    iecTalkSa0Confirmed = true;
                }
                if (!iecDirectoryStubPrepared) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
            }
            if (!iecTalking) {
                iecTalkSecondary = 0xFF;
                iecActiveTalkChannel = 0xFF;
                iecTalkSa0Confirmed = false;
            } else if (iecActiveTalkChannel != 0) {
                iecTalkSa0Confirmed = false;
            }
            return iecTalking;
        }

        return false;
    }
};
