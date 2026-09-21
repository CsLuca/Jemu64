#pragma once

#include <iostream>

#include "../drive_1541.hpp"

static void runDrive1541PowerMatrixGateScenarios() {
    auto tickFor = [](Drive1541 &drive, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            drive.tickIecHalfCycle();
        }
    };

    auto modeToText = [](Drive1541::PowerMatrixMode mode) {
        switch (mode) {
            case Drive1541::PowerMatrixMode::C64OffDriveOff: return "C64OffDriveOff";
            case Drive1541::PowerMatrixMode::C64OffDriveOn: return "C64OffDriveOn";
            case Drive1541::PowerMatrixMode::C64OnDriveOff: return "C64OnDriveOff";
            case Drive1541::PowerMatrixMode::C64OnDriveOn: return "C64OnDriveOn";
        }
        return "Unknown";
    };

    auto runCase = [&](const char *caseName,
                       Drive1541::C64PowerState c64State,
                       bool drivePowered,
                       Drive1541::PowerMatrixMode expectedMode,
                       bool expectedC64Powered,
                       bool expectedDrivePowered,
                       bool expectRearm) {
        Drive1541 drive;

        if (c64State == Drive1541::C64PowerState::Off) {
            drive.setC64Power(false);
        } else if (c64State == Drive1541::C64PowerState::Resetting) {
            drive.setC64PowerState(Drive1541::C64PowerState::Resetting);
        } else {
            drive.setC64Power(true);
        }

        drive.setDrivePower(drivePowered);
        if (drivePowered) {
            tickFor(drive, 21000);
        } else {
            tickFor(drive, 6000);
        }

        if (c64State == Drive1541::C64PowerState::Off) {
            drive.setIecLines(false, false, false);
        }

        const Drive1541::PowerMatrixState matrix = drive.getPowerMatrixState();
        bool pass = true;
        pass = pass && (matrix.mode == expectedMode);
        pass = pass && (matrix.c64_powered == expectedC64Powered);
        pass = pass && (matrix.drive_powered == expectedDrivePowered);
        pass = pass && (matrix.command_rearm_required == expectRearm);

        if (c64State == Drive1541::C64PowerState::Off) {
            pass = pass && drive.iecATN && drive.iecCLK && drive.iecDATA;
        }

        std::cerr << "[1541 PM-GATE] case=" << caseName
                  << " mode=" << modeToText(matrix.mode)
                  << " c64_powered=" << (matrix.c64_powered ? 1 : 0)
                  << " drive_powered=" << (matrix.drive_powered ? 1 : 0)
                  << " command_rearm=" << (matrix.command_rearm_required ? 1 : 0)
                  << " pass=" << (pass ? "True" : "False")
                  << std::endl;

        if (!pass) {
            std::cerr << "[1541 PM-GATE] FAIL: scenario mismatch in " << caseName << std::endl;
            assert(false);
        }
    };

    runCase("c64_off_drive_off",
            Drive1541::C64PowerState::Off,
            false,
            Drive1541::PowerMatrixMode::C64OffDriveOff,
            false,
            false,
            true);

    runCase("c64_off_drive_on",
            Drive1541::C64PowerState::Off,
            true,
            Drive1541::PowerMatrixMode::C64OffDriveOn,
            false,
            true,
            true);

    runCase("c64_on_drive_off",
            Drive1541::C64PowerState::On,
            false,
            Drive1541::PowerMatrixMode::C64OnDriveOff,
            true,
            false,
            false);

    runCase("c64_on_drive_on",
            Drive1541::C64PowerState::On,
            true,
            Drive1541::PowerMatrixMode::C64OnDriveOn,
            true,
            true,
            false);

    std::cerr << "[1541 PM-GATE] PASS: all power matrix scenarios matched" << std::endl;
}
