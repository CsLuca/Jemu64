#pragma once

static void runDrive1541IecExecBlockCommandSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC EXE] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    auto sendCmd15 = [&](const std::string &cmd) {
        iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
        iecHostSendByte(cia2, drive, false, 0xFF);
        for (char c : cmd) {
            iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
        }
        iecHostSendByte(cia2, drive, false, 0x3F);
    };

    auto readStatus15 = [&]() -> std::string {
        iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08));
        iecHostSendByte(cia2, drive, false, 0x6F);
        const std::string s = iecHostReadTextLine(cia2, drive, 64);
        iecHostEoiAckPulse(cia2, drive);
        iecHostSendByte(cia2, drive, false, 0x5F);
        iecHostSendByte(cia2, drive, false, 0xEF);
        return s;
    };

    sendCmd15("M-E,C123");
    if (drive.iecLastExecuteAddr != 0xC123) {
        std::cerr << "[1541 IEC EXE] FAIL: M-E did not latch execute address." << std::endl;
        assert(false);
    }
    std::string st = readStatus15();
    if (st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC EXE] FAIL: M-E status not OK: " << st << std::endl;
        assert(false);
    }

    sendCmd15("B-R,02,11,04");
    if (drive.iecLastBlockCommand != "B-R" || drive.iecLastBlockChannel != 0x02 ||
        drive.iecLastBlockTrack != 0x11 || drive.iecLastBlockSector != 0x04) {
        std::cerr << "[1541 IEC EXE] FAIL: B-R did not latch block job parameters." << std::endl;
        assert(false);
    }
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC EXE] FAIL: B-R status not OK: " << st << std::endl;
        assert(false);
    }

    sendCmd15("B-Q,02,11,04"); // unsupported B-* op
    st = readStatus15();
    if (st.rfind("30,SYNTAX ERROR", 0) != 0) {
        std::cerr << "[1541 IEC EXE] FAIL: expected syntax error for unsupported B-* op, got: " << st << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC EXE] PASS: M-E + B-* command scaffolds with status path "
              << "exec=$" << std::hex << drive.iecLastExecuteAddr
              << " block=" << drive.iecLastBlockCommand
              << " ch=$" << (int)drive.iecLastBlockChannel
              << " trk=$" << (int)drive.iecLastBlockTrack
              << " sec=$" << (int)drive.iecLastBlockSector
              << std::endl;
}
