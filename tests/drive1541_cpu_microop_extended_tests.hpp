#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>

#include "../drive_1541.hpp"

static void runDrive1541CpuMicroOpExtendedTests() {
    auto resetOpcodeWindow = [](Drive1541 &drive, std::uint16_t pcBase) {
        drive.pc = pcBase;
        drive.write(static_cast<std::uint16_t>(pcBase + 1u), 0x00u);
        drive.write(static_cast<std::uint16_t>(pcBase + 2u), 0x00u);
    };

    {
        Drive1541 drive;
        std::uint8_t cycles = 0;

        resetOpcodeWindow(drive, 0x0200u);
        drive.write(0x0201u, 0x40u);
        drive.write(0x0040u, 0xAAu);
        const bool laxHandled = drive.executeDriveCpuMicroOpBaseOpcode(0xA7u, cycles);
        if (!laxHandled || drive.cpuA != 0xAAu || drive.cpuX != 0xAAu || cycles != 3u || drive.pc != 0x0202u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: LAX zp semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0210u);
        drive.cpuA = 0xF0u;
        drive.cpuX = 0x0Fu;
        drive.write(0x0211u, 0x41u);
        const bool saxHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x87u, cycles);
        if (!saxHandled || drive.read(0x0041u) != 0x00u || cycles != 3u || drive.pc != 0x0212u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: SAX zp semantics mismatch" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        std::uint8_t cycles = 0;

        resetOpcodeWindow(drive, 0x0220u);
        drive.cpuA = 0x10u;
        drive.cpuP = static_cast<std::uint8_t>(drive.cpuP | 0x01u);
        drive.write(0x0221u, 0x50u);
        drive.write(0x0050u, 0x11u);
        const bool dcpHandled = drive.executeDriveCpuMicroOpBaseOpcode(0xC7u, cycles);
        if (!dcpHandled || drive.read(0x0050u) != 0x10u || (drive.cpuP & 0x01u) == 0 || (drive.cpuP & 0x02u) == 0 || cycles != 5u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: DCP zp semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0230u);
        drive.cpuA = 0x20u;
        drive.cpuP = static_cast<std::uint8_t>((drive.cpuP | 0x01u) & ~0x02u);
        drive.write(0x0231u, 0x51u);
        drive.write(0x0051u, 0x0Fu);
        const bool iscHandled = drive.executeDriveCpuMicroOpBaseOpcode(0xE7u, cycles);
        if (!iscHandled || drive.read(0x0051u) != 0x10u || drive.cpuA != 0x10u || (drive.cpuP & 0x01u) == 0 || cycles != 5u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: ISC zp semantics mismatch" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        std::uint8_t cycles = 0;

        resetOpcodeWindow(drive, 0x0240u);
        drive.cpuA = 0x01u;
        drive.cpuP = static_cast<std::uint8_t>(drive.cpuP & ~0x01u);
        drive.write(0x0241u, 0x60u);
        drive.write(0x0060u, 0x80u);
        const bool sloHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x07u, cycles);
        if (!sloHandled || drive.read(0x0060u) != 0x00u || drive.cpuA != 0x01u || (drive.cpuP & 0x01u) == 0 || cycles != 5u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: SLO zp semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0248u);
        drive.cpuA = 0x10u;
        drive.cpuP = static_cast<std::uint8_t>(drive.cpuP & ~0x01u);
        drive.write(0x0249u, 0x61u);
        drive.write(0x0061u, 0x03u);
        const bool rraHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x67u, cycles);
        if (!rraHandled || drive.read(0x0061u) != 0x01u || drive.cpuA != 0x12u || cycles != 5u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: RRA zp semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0250u);
        drive.cpuA = 0x80u;
        drive.cpuP = static_cast<std::uint8_t>(drive.cpuP & ~0x01u);
        drive.write(0x0251u, 0xFFu);
        const bool ancHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x0Bu, cycles);
        if (!ancHandled || drive.cpuA != 0x80u || (drive.cpuP & 0x01u) == 0 || cycles != 2u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: ANC semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0258u);
        drive.cpuA = 0x03u;
        drive.write(0x0259u, 0xFFu);
        const bool alrHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x4Bu, cycles);
        if (!alrHandled || drive.cpuA != 0x01u || (drive.cpuP & 0x01u) == 0 || cycles != 2u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: ALR semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0260u);
        drive.cpuA = 0xFFu;
        drive.cpuP = static_cast<std::uint8_t>(drive.cpuP & ~0x01u);
        drive.write(0x0261u, 0xFFu);
        const bool arrHandled = drive.executeDriveCpuMicroOpBaseOpcode(0x6Bu, cycles);
        if (!arrHandled || drive.cpuA != 0x7Fu || (drive.cpuP & 0x01u) == 0 || (drive.cpuP & 0x40u) != 0 || cycles != 2u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: ARR semantics mismatch" << std::endl;
            assert(false);
        }

        resetOpcodeWindow(drive, 0x0268u);
        drive.cpuA = 0x0Fu;
        drive.cpuX = 0x0Fu;
        drive.write(0x0269u, 0x01u);
        const bool axsHandled = drive.executeDriveCpuMicroOpBaseOpcode(0xCBu, cycles);
        if (!axsHandled || drive.cpuX != 0x0Eu || (drive.cpuP & 0x01u) == 0 || cycles != 2u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: AXS semantics mismatch" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 drive;
        drive.cpuUnhandledOpcodeCount = 0;
        drive.cpuLastUnhandledOpcode = 0;
        drive.cpuLastUnhandledFetchAddr = 0;

        drive.driveCpuUseMicroOpEngine = true;
        drive.pc = 0x0300u;
        drive.write(0x0300u, 0x12u);
        for (int i = 0; i < 5; ++i) {
            drive.stepDriveCpuMicroOpScaffold();
        }
        if (drive.cpuUnhandledOpcodeCount != 1u || drive.cpuLastUnhandledOpcode != 0x12u || drive.cpuLastUnhandledFetchAddr != 0x0300u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: micro-op fallback telemetry mismatch" << std::endl;
            assert(false);
        }

        drive.pc = 0x0310u;
        drive.stepCpuScaffoldCycleAccurate(0x22u);
        if (drive.cpuUnhandledOpcodeCount != 2u || drive.cpuLastUnhandledOpcode != 0x22u || drive.cpuLastUnhandledFetchAddr != 0x0310u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: cycle-accurate fallback telemetry mismatch" << std::endl;
            assert(false);
        }

        drive.pc = 0x0320u;
        drive.write(0x0320u, 0x42u);
        drive.stepCpuScaffold();
        if (drive.cpuUnhandledOpcodeCount != 3u || drive.cpuLastUnhandledOpcode != 0x42u || drive.cpuLastUnhandledFetchAddr != 0x0320u) {
            std::cerr << "[1541 CPU MICROOP EXT] FAIL: scaffold fallback telemetry mismatch" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 CPU MICROOP EXT] PASS: undocumented opcode families + fallback telemetry" << std::endl;
}
