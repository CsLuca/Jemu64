#pragma once

#include <iostream>

#include "../drive_1541.hpp"
#include "../iec_bridge.hpp"

static void runDrive1541PowerMatrixDomainIndependenceTests() {
    CIA6526 cia2;
    Drive1541 drive;
    IecBridgePolarity polarity = makeRuntimeDefaultIecPolarity();
    IecBusDomain domain(cia2, drive, polarity);

    const uint64_t c64Start = domain.getC64HalfTicks();
    const uint64_t driveStart = domain.getDriveHalfTicks();
    for (int i = 0; i < 64; ++i) {
        domain.tickHalfCycle();
    }
    if (domain.getC64HalfTicks() <= c64Start || domain.getDriveHalfTicks() <= driveStart) {
        std::cerr << "[1541 PMATRIX] FAIL: baseline domains did not advance" << std::endl;
        assert(false);
    }

    const uint64_t c64BeforeOff = domain.getC64HalfTicks();
    const uint64_t driveBeforeOff = domain.getDriveHalfTicks();
    domain.setC64DomainEnabled(false);
    for (int i = 0; i < 64; ++i) {
        domain.tickHalfCycle();
    }
    if (domain.getC64HalfTicks() <= c64BeforeOff) {
        std::cerr << "[1541 PMATRIX] FAIL: C64 timeline should keep cadence while disabled" << std::endl;
        assert(false);
    }
    if (domain.getDriveHalfTicks() <= driveBeforeOff) {
        std::cerr << "[1541 PMATRIX] FAIL: drive should continue while C64 domain disabled" << std::endl;
        assert(false);
    }

    const uint64_t c64BeforeDriveOff = domain.getC64HalfTicks();
    const uint64_t driveBeforeDriveOff = domain.getDriveHalfTicks();
    domain.setDriveDomainEnabled(false);
    for (int i = 0; i < 64; ++i) {
        domain.tickHalfCycle();
    }
    if (domain.getC64HalfTicks() <= c64BeforeDriveOff) {
        std::cerr << "[1541 PMATRIX] FAIL: C64 should continue while drive domain disabled" << std::endl;
        assert(false);
    }
    if (domain.getDriveHalfTicks() != driveBeforeDriveOff) {
        std::cerr << "[1541 PMATRIX] FAIL: drive timeline should freeze while disabled" << std::endl;
        assert(false);
    }

    domain.setC64DomainEnabled(true);
    domain.setDriveDomainEnabled(true);
    const uint64_t c64BeforeResume = domain.getC64HalfTicks();
    const uint64_t driveBeforeResume = domain.getDriveHalfTicks();
    for (int i = 0; i < 64; ++i) {
        domain.tickHalfCycle();
    }
    if (domain.getC64HalfTicks() <= c64BeforeResume || domain.getDriveHalfTicks() <= driveBeforeResume) {
        std::cerr << "[1541 PMATRIX] FAIL: domains did not resume after re-enable" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 PMATRIX] PASS: C64/drive domain scheduling independence" << std::endl;
}
