#pragma once

static void runDrive1541IecWildcardDirectorySmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC WC] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    auto openAndAlloc = [&](uint8_t sa, const std::string &nameSpec, const std::string &allocCmd) {
        iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
        iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0xF0 | (sa & 0x0F)));
        for (char c : nameSpec) {
            iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
        }
        iecHostSendByte(cia2, drive, false, 0x3F);
        sendCmd15(allocCmd);
    };

    openAndAlloc(0x02, "alpha,p,w", "B-A,02,10,01");
    openAndAlloc(0x03, "demo-seq,s,w", "B-A,03,10,02");
    openAndAlloc(0x04, "beta,p,w", "B-A,04,10,03");

    // Request wildcard directory: names starting with D and ending with Q.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF0);
    const std::string req = "$D*Q";
    for (char c : req) {
        iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
    }
    iecHostSendByte(cia2, drive, false, 0x3F);

    drive.buildDirectoryStubPayload();
    std::string dirAscii;
    size_t guard = 0;
    while (drive.pendingIecTx() > 0 && guard < 8192) {
        uint8_t b = 0;
        if (!drive.hostReadTalkByte(b)) {
            break;
        }
        if (b >= 32 && b <= 126) {
            dirAscii.push_back(static_cast<char>(b));
        }
        guard++;
    }

    if (dirAscii.find("DEMO-SEQ") == std::string::npos) {
        std::cerr << "[1541 IEC WC] FAIL: wildcard listing missing expected DEMO-SEQ entry." << std::endl;
        assert(false);
    }
    if (dirAscii.find("ALPHA") != std::string::npos || dirAscii.find("BETA") != std::string::npos) {
        std::cerr << "[1541 IEC WC] FAIL: wildcard listing leaked non-matching entries." << std::endl;
        assert(false);
    }

    std::ostringstream freeLine;
    freeLine << drive.virtualBlocksFree() << " BLOCKS FREE.";
    if (dirAscii.find(freeLine.str()) == std::string::npos) {
        std::cerr << "[1541 IEC WC] FAIL: wildcard listing missing free blocks line." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC WC] PASS: wildcard directory filter (*,?) + free blocks line "
              << "pattern=\"" << drive.iecDirectoryWildcardPattern << "\""
              << " free=" << std::dec << drive.virtualBlocksFree()
              << std::endl;
}
