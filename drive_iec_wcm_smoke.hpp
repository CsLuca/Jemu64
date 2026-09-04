#pragma once

static void runDrive1541IecWildcardTypeModeFilterSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC WCM] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    openAndAlloc(0x02, "seq-r,s,r", "B-A,02,12,01");
    openAndAlloc(0x03, "seq-w,s,w", "B-A,03,12,02");
    openAndAlloc(0x04, "seq-a,s,a", "B-A,04,12,03");

    // Request only SEQ,W entries.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF0);
    const std::string req = "$*,S,W";
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

    if (drive.iecDirectoryTypeFilter != "SEQ" || drive.iecDirectoryModeFilter != "W") {
        std::cerr << "[1541 IEC WCM] FAIL: parsed filters mismatch for $*,S,W." << std::endl;
        assert(false);
    }

    if (dirAscii.find("SEQ-W") == std::string::npos || dirAscii.find("SEQ,W") == std::string::npos) {
        std::cerr << "[1541 IEC WCM] FAIL: filtered listing missing SEQ,W target entry." << std::endl;
        assert(false);
    }
    if (dirAscii.find("SEQ-R") != std::string::npos || dirAscii.find("SEQ-A") != std::string::npos) {
        std::cerr << "[1541 IEC WCM] FAIL: listing leaked non-W mode entries." << std::endl;
        assert(false);
    }

    std::ostringstream freeLine;
    freeLine << drive.virtualBlocksFree() << " BLOCKS FREE.";
    if (dirAscii.find(freeLine.str()) == std::string::npos) {
        std::cerr << "[1541 IEC WCM] FAIL: filtered listing missing free blocks line." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC WCM] PASS: wildcard + type+mode filter ($*,S,W) "
              << "type=" << drive.iecDirectoryTypeFilter
              << " mode=" << drive.iecDirectoryModeFilter
              << " free=" << std::dec << drive.virtualBlocksFree()
              << std::endl;
}
