#pragma once

static void runDrive1541IecNamedCatalogSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC NAME] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    // Open filename on channel 2: "demo-file"
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF2); // SA2
    const std::string fname = "demo-file";
    for (char c : fname) {
        iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
    }
    iecHostSendByte(cia2, drive, false, 0x3F);

    if (!drive.iecChannelOpenNameValid[2] || drive.iecChannelOpenName[2] != "DEMO-FILE") {
        std::cerr << "[1541 IEC NAME] FAIL: channel open filename not normalized/lached." << std::endl;
        assert(false);
    }

    sendCmd15("B-A,02,1A,01");
    std::string st = readStatus15();
    if (st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC NAME] FAIL: B-A with named channel failed: " << st << std::endl;
        assert(false);
    }

    drive.buildDirectoryStubPayload();
    std::string dirAscii;
    size_t guard = 0;
    while (drive.pendingIecTx() > 0 && guard < 4096) {
        uint8_t b = 0;
        if (!drive.hostReadTalkByte(b)) {
            break;
        }
        if (b >= 32 && b <= 126) {
            dirAscii.push_back(static_cast<char>(b));
        }
        guard++;
    }

    if (dirAscii.find("DEMO-FILE") == std::string::npos) {
        std::cerr << "[1541 IEC NAME] FAIL: directory listing missing OPEN-derived filename." << std::endl;
        assert(false);
    }

    sendCmd15("B-F,02,1A,01");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC NAME] FAIL: B-F on named file failed: " << st << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC NAME] PASS: catalog entry name derived from OPEN filename "
              << "name=\"" << drive.iecChannelOpenName[2] << "\""
              << std::endl;
}
