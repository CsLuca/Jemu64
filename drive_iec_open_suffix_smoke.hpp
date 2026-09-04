#pragma once

static void runDrive1541IecOpenSuffixSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC SUFFIX] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    // OPEN on channel 3 with type/mode suffixes
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF3);
    const std::string spec = "notes,s,w";
    for (char c : spec) {
        iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
    }
    iecHostSendByte(cia2, drive, false, 0x3F);

    if (!drive.iecChannelOpenNameValid[3] || drive.iecChannelOpenName[3] != "NOTES" ||
        drive.iecChannelOpenType[3] != "SEQ" || drive.iecChannelOpenMode[3] != "W") {
        std::cerr << "[1541 IEC SUFFIX] FAIL: OPEN suffix parsing mismatch for channel 3." << std::endl;
        assert(false);
    }

    sendCmd15("B-A,03,10,02");

    bool found = false;
    for (size_t i = 0; i < drive.iecCatalog.size(); ++i) {
        if (drive.iecCatalog[i].used && drive.iecCatalog[i].channel == 3) {
            found = true;
            if (drive.iecCatalog[i].name != "NOTES" || drive.iecCatalog[i].type != "SEQ" || drive.iecCatalog[i].mode != "W") {
                std::cerr << "[1541 IEC SUFFIX] FAIL: catalog entry did not reflect parsed type/mode." << std::endl;
                assert(false);
            }
            break;
        }
    }
    if (!found) {
        std::cerr << "[1541 IEC SUFFIX] FAIL: missing catalog entry for channel 3 allocation." << std::endl;
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

    if (dirAscii.find("NOTES") == std::string::npos || dirAscii.find("SEQ,W") == std::string::npos) {
        std::cerr << "[1541 IEC SUFFIX] FAIL: directory listing missing suffix-derived type/mode." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC SUFFIX] PASS: OPEN suffix parsing + directory rendering "
              << "name=\"" << drive.iecChannelOpenName[3] << "\""
              << " type=" << drive.iecChannelOpenType[3]
              << " mode=" << drive.iecChannelOpenMode[3]
              << std::endl;
}
