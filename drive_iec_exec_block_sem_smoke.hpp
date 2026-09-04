#pragma once

static void runDrive1541IecExecBlockSemanticsSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC SEM] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    // M-E dispatch semantics
    sendCmd15("M-E,0400");
    std::string st = readStatus15();
    if (drive.iecLastExecDispatch != "BUFFER_ENTRY" || drive.iecExecDispatchCount == 0 || st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC SEM] FAIL: M-E buffer dispatch semantics mismatch." << std::endl;
        assert(false);
    }

    sendCmd15("M-E,9000");
    st = readStatus15();
    if (drive.iecLastExecDispatch != "UNKNOWN_ENTRY" || st.rfind("31,SYNTAX ERROR", 0) != 0) {
        std::cerr << "[1541 IEC SEM] FAIL: M-E unknown dispatch should report 31 error." << std::endl;
        assert(false);
    }

    // B-W without valid buffer -> 66
    sendCmd15("B-W,01,20,03");
    st = readStatus15();
    if (st.rfind("66,ILLEGAL TRACK OR SECTOR", 0) != 0) {
        std::cerr << "[1541 IEC SEM] FAIL: B-W without buffer should report 66, got: " << st << std::endl;
        assert(false);
    }

    // Prime memory for virtual block load and verify B-R/B-W round-trip
    const uint16_t base = static_cast<uint16_t>(((uint16_t(0x20) << 8) | 0x03) & 0xBFFF);
    drive.memory[base] = 0x11;
    drive.memory[static_cast<uint16_t>((base + 1) & 0xBFFF)] = 0x22;
    sendCmd15("B-R,01,20,03");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || !drive.iecBlockBufferValid || drive.iecBlockBuffer[0] != 0x11 || drive.iecBlockBuffer[1] != 0x22) {
        std::cerr << "[1541 IEC SEM] FAIL: B-R did not load expected virtual block buffer." << std::endl;
        assert(false);
    }

    drive.iecBlockBuffer[0] = 0x77;
    drive.iecBlockBuffer[1] = 0x88;
    sendCmd15("B-W,01,20,03");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || drive.memory[base] != 0x77 || drive.memory[static_cast<uint16_t>((base + 1) & 0xBFFF)] != 0x88) {
        std::cerr << "[1541 IEC SEM] FAIL: B-W did not flush virtual block buffer to memory." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC SEM] PASS: M-E dispatch + virtual B-R/B-W semantics "
              << "dispatches=" << std::dec << drive.iecExecDispatchCount
              << " base=$" << std::hex << base
              << " mem0=$" << (int)drive.memory[base]
              << " mem1=$" << (int)drive.memory[static_cast<uint16_t>((base + 1) & 0xBFFF)]
              << std::endl;
}
