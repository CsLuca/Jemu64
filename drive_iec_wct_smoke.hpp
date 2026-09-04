#pragma once

static void runDrive1541IecWildcardTypeFilterSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC WCT] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    openAndAlloc(0x02, "mix-one,p,w", "B-A,02,11,01");
    openAndAlloc(0x03, "mix-two,s,w", "B-A,03,11,02");
    openAndAlloc(0x04, "mix-three,u,w", "B-A,04,11,03");

    // Request only SEQ files.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF0);
    const std::string req = "$*,S";
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

    if (drive.iecDirectoryTypeFilter != "SEQ") {
        std::cerr << "[1541 IEC WCT] FAIL: type filter parse mismatch (expected SEQ)." << std::endl;
        assert(false);
    }

    if (dirAscii.find("MIX-TWO") == std::string::npos || dirAscii.find("SEQ,W") == std::string::npos) {
        std::cerr << "[1541 IEC WCT] FAIL: SEQ entry missing from type-filtered listing." << std::endl;
        assert(false);
    }
    if (dirAscii.find("MIX-ONE") != std::string::npos || dirAscii.find("MIX-THREE") != std::string::npos) {
        std::cerr << "[1541 IEC WCT] FAIL: non-SEQ entries leaked in type-filtered listing." << std::endl;
        assert(false);
    }

    std::ostringstream freeLine;
    freeLine << drive.virtualBlocksFree() << " BLOCKS FREE.";
    if (dirAscii.find(freeLine.str()) == std::string::npos) {
        std::cerr << "[1541 IEC WCT] FAIL: type-filtered listing missing free blocks line." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC WCT] PASS: wildcard + type filter ($*,S) "
              << "type=" << drive.iecDirectoryTypeFilter
              << " free=" << std::dec << drive.virtualBlocksFree()
              << std::endl;
}
