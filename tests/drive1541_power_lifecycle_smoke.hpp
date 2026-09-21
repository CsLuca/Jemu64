#pragma once

#include <iostream>

#include "../drive_1541.hpp"

static void runDrive1541PowerLifecycleSmoke() {
    Drive1541 drive;

    if (!drive.isPoweredOn() || drive.getPowerState() != Drive1541::PowerState::On) {
        std::cerr << "[1541 POWER] FAIL: drive should start powered on" << std::endl;
        assert(false);
    }

    drive.cpuEnabled = true;
    drive.powerOff();
    if (drive.getPowerState() != Drive1541::PowerState::SpinningDown) {
        std::cerr << "[1541 POWER] FAIL: expected spinning down state" << std::endl;
        assert(false);
    }

    for (int i = 0; i < 6000; ++i) {
        drive.tickIecHalfCycle();
    }
    if (drive.getPowerState() != Drive1541::PowerState::Off || drive.isPoweredOn()) {
        std::cerr << "[1541 POWER] FAIL: expected off state after spindown" << std::endl;
        assert(false);
    }

    const std::uint64_t cyclesBefore = drive.cycles;
    const std::uint64_t cpuStepsBefore = drive.cpuStepCount;
    for (int i = 0; i < 32; ++i) {
        drive.tickIecHalfCycle();
    }
    if (drive.cycles != cyclesBefore || drive.cpuStepCount != cpuStepsBefore) {
        std::cerr << "[1541 POWER] FAIL: counters advanced while power off" << std::endl;
        assert(false);
    }
    if (drive.getIecDrivePullCLK() || drive.getIecDrivePullDATA()) {
        std::cerr << "[1541 POWER] FAIL: IEC lines not released while power off" << std::endl;
        assert(false);
    }

    drive.powerOn(true);
    if (drive.getPowerState() != Drive1541::PowerState::SpinningUp) {
        std::cerr << "[1541 POWER] FAIL: expected spinning up state" << std::endl;
        assert(false);
    }
    for (int i = 0; i < 21000; ++i) {
        drive.tickIecHalfCycle();
    }
    if (drive.getPowerState() != Drive1541::PowerState::On || !drive.isPoweredOn()) {
        std::cerr << "[1541 POWER] FAIL: expected on state after spinup" << std::endl;
        assert(false);
    }

    drive.powerReset();
    if (drive.getPowerState() != Drive1541::PowerState::Resetting) {
        std::cerr << "[1541 POWER] FAIL: expected resetting state" << std::endl;
        assert(false);
    }
    for (int i = 0; i < 3100; ++i) {
        drive.tickIecHalfCycle();
    }
    if (drive.getPowerState() != Drive1541::PowerState::On) {
        std::cerr << "[1541 POWER] FAIL: expected on state after reset" << std::endl;
        assert(false);
    }

    Drive1541 repeatA;
    Drive1541 repeatB;
    auto digestRun = [](Drive1541 &d) {
        std::uint64_t digest = 0;
        for (int i = 0; i < 3; ++i) {
            d.powerOff();
            for (int t = 0; t < 6000; ++t) d.tickIecHalfCycle();
            digest ^= static_cast<std::uint64_t>(d.getPowerState());
            digest = (digest << 7) | (digest >> 57);
            d.powerOn(true);
            for (int t = 0; t < 21000; ++t) d.tickIecHalfCycle();
            digest ^= static_cast<std::uint64_t>(d.getPowerState());
            digest = (digest << 11) | (digest >> 53);
        }
        return digest;
    };

    const std::uint64_t digestA = digestRun(repeatA);
    const std::uint64_t digestB = digestRun(repeatB);
    if (digestA != digestB) {
        std::cerr << "[1541 POWER] FAIL: non-deterministic power cycle digest" << std::endl;
        assert(false);
    }

    drive.setC64Power(false);
    auto matrix = drive.getPowerMatrixState();
    if (matrix.c64 != Drive1541::C64PowerState::Off || !matrix.drive_powered) {
        std::cerr << "[1541 POWER] FAIL: expected C64 off + drive on matrix state" << std::endl;
        assert(false);
    }
    if (matrix.mode != Drive1541::PowerMatrixMode::C64OffDriveOn) {
        std::cerr << "[1541 POWER] FAIL: expected power matrix mode C64OffDriveOn" << std::endl;
        assert(false);
    }

    drive.setIecLines(false, false, false);
    if (!drive.iecATN || !drive.iecCLK || !drive.iecDATA) {
        std::cerr << "[1541 POWER] FAIL: C64-off host lines should be forced idle high" << std::endl;
        assert(false);
    }

    drive.setC64PowerState(Drive1541::C64PowerState::Resetting);
    if (drive.getC64PowerState() != Drive1541::C64PowerState::Resetting) {
        std::cerr << "[1541 POWER] FAIL: expected C64 resetting state" << std::endl;
        assert(false);
    }
    if (!drive.getPowerMatrixState().command_rearm_required) {
        std::cerr << "[1541 POWER] FAIL: expected command rearm while C64 resetting" << std::endl;
        assert(false);
    }

    drive.setDrivePower(false);
    for (int i = 0; i < 6000; ++i) {
        drive.tickIecHalfCycle();
    }
    matrix = drive.getPowerMatrixState();
    if (matrix.c64 != Drive1541::C64PowerState::Resetting || matrix.drive_powered) {
        std::cerr << "[1541 POWER] FAIL: expected C64 resetting + drive off matrix state" << std::endl;
        assert(false);
    }
    if (matrix.mode != Drive1541::PowerMatrixMode::C64OnDriveOff) {
        std::cerr << "[1541 POWER] FAIL: expected power matrix mode C64OnDriveOff" << std::endl;
        assert(false);
    }

    drive.setDrivePower(true);
    for (int i = 0; i < 21000; ++i) {
        drive.tickIecHalfCycle();
    }
    matrix = drive.getPowerMatrixState();
    if (matrix.c64 != Drive1541::C64PowerState::Resetting || !matrix.drive_powered) {
        std::cerr << "[1541 POWER] FAIL: expected C64 resetting + drive on matrix state" << std::endl;
        assert(false);
    }
    if (matrix.mode != Drive1541::PowerMatrixMode::C64OnDriveOn) {
        std::cerr << "[1541 POWER] FAIL: expected power matrix mode C64OnDriveOn" << std::endl;
        assert(false);
    }

    drive.setC64Power(true);
    matrix = drive.getPowerMatrixState();
    if (matrix.c64 != Drive1541::C64PowerState::On || !matrix.c64_powered || !matrix.drive_powered) {
        std::cerr << "[1541 POWER] FAIL: expected C64 on + drive on matrix state" << std::endl;
        assert(false);
    }
    if (!matrix.command_rearm_required) {
        std::cerr << "[1541 POWER] FAIL: expected command rearm pending after C64 resume" << std::endl;
        assert(false);
    }

    const uint64_t rxBeforeDataDrop = drive.iecRxProcessed;
    drive.consumeReceivedByte(static_cast<uint8_t>('$'), false);
    if (drive.iecRxProcessed != rxBeforeDataDrop) {
        std::cerr << "[1541 POWER] FAIL: data should be ignored until first post-resume command" << std::endl;
        assert(false);
    }

    drive.consumeReceivedByte(static_cast<uint8_t>(0x20 | 0x08), true);
    if (drive.getPowerMatrixState().command_rearm_required) {
        std::cerr << "[1541 POWER] FAIL: command rearm should clear after first command" << std::endl;
        assert(false);
    }

    drive.setC64Power(false);
    drive.setDrivePower(false);
    for (int i = 0; i < 6000; ++i) {
        drive.tickIecHalfCycle();
    }
    matrix = drive.getPowerMatrixState();
    if (matrix.mode != Drive1541::PowerMatrixMode::C64OffDriveOff) {
        std::cerr << "[1541 POWER] FAIL: expected power matrix mode C64OffDriveOff" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 POWER] PASS: per-drive power lifecycle and deterministic cycling" << std::endl;
}
