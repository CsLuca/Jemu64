#pragma once

static void runDrive1541IecHandshakeSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    // Configure CIA2 serial lines as outputs (ATN/CLK/DATA), set released high first.
    cia2.ddra = static_cast<uint8_t>(cia2.ddra | 0x38);
    cia2.pra = static_cast<uint8_t>(cia2.pra & static_cast<uint8_t>(~0x38));

    // Drive initially does not pull lines.
    drive.iecDrivePullCLK = false;
    drive.iecDrivePullDATA = false;
    syncIecBus(cia2, drive);

    const uint8_t idlePortA = cia2.getPortACombined();
    const bool idleClkInHigh = (idlePortA & 0x40) != 0;
    const bool idleDataInHigh = (idlePortA & 0x80) != 0;
    if (!idleClkInHigh || !idleDataInHigh) {
        std::cerr << "[1541 IEC] FAIL: idle IEC lines not high." << std::endl;
        assert(false);
    }

    // C64 pulls ATN low (active).
    cia2.pra = static_cast<uint8_t>(cia2.pra | 0x08);
    syncIecBus(cia2, drive);
    if (drive.iecATN) {
        std::cerr << "[1541 IEC] FAIL: ATN should be low at drive side." << std::endl;
        assert(false);
    }

    // Drive acknowledges by pulling DATA low.
    drive.iecDrivePullDATA = true;
    syncIecBus(cia2, drive);
    const bool dataInAfterAckHigh = (cia2.getPortACombined() & 0x80) != 0;
    if (dataInAfterAckHigh) {
        std::cerr << "[1541 IEC] FAIL: DATA-IN should be low during drive ACK." << std::endl;
        assert(false);
    }

    // Release ATN and DATA back high.
    cia2.pra = static_cast<uint8_t>(cia2.pra & static_cast<uint8_t>(~0x08));
    drive.iecDrivePullDATA = false;
    syncIecBus(cia2, drive);

    const uint8_t finalPortA = cia2.getPortACombined();
    const bool finalClkInHigh = (finalPortA & 0x40) != 0;
    const bool finalDataInHigh = (finalPortA & 0x80) != 0;
    if (!finalClkInHigh || !finalDataInHigh || !drive.iecATN) {
        std::cerr << "[1541 IEC] FAIL: IEC lines did not return to released state." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC] PASS: CIA2<->drive IEC line sync scaffold works (ATN/CLK/DATA)." << std::endl;
}
