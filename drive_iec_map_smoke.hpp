#pragma once

static void runDrive1541IecBlockAllocMapSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC MAP] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    // Initial free count should be full map.
    drive.buildDirectoryStubPayload();
    if (drive.virtualBlocksFree() != Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS) {
        std::cerr << "[1541 IEC MAP] FAIL: unexpected initial free block count." << std::endl;
        assert(false);
    }

    sendCmd15("B-A,00,12,03");
    std::string st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || drive.virtualBlocksFree() != static_cast<uint16_t>(Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS - 1)) {
        std::cerr << "[1541 IEC MAP] FAIL: B-A did not allocate block correctly." << std::endl;
        assert(false);
    }

    drive.buildDirectoryStubPayload();
    if (drive.pendingIecTx() == 0) {
        std::cerr << "[1541 IEC MAP] FAIL: directory payload empty after allocation." << std::endl;
        assert(false);
    }

    std::string dirAscii;
    size_t guard = 0;
    while (drive.pendingIecTx() > 0 && guard < 2048) {
        uint8_t b = 0;
        if (!drive.hostReadTalkByte(b)) {
            break;
        }
        if (b >= 32 && b <= 126) {
            dirAscii.push_back(static_cast<char>(b));
        }
        guard++;
    }

    if (dirAscii.find("BLK-1203") == std::string::npos) {
        std::cerr << "[1541 IEC MAP] FAIL: directory listing missing allocated catalog entry BLK-1203." << std::endl;
        assert(false);
    }

    bool catOneBlock = false;
    for (size_t i = 0; i < drive.iecCatalog.size(); ++i) {
        if (drive.iecCatalog[i].used && drive.iecCatalog[i].name == "BLK-1203" && drive.iecCatalog[i].blocks == 1) {
            catOneBlock = true;
            break;
        }
    }
    if (!catOneBlock) {
        std::cerr << "[1541 IEC MAP] FAIL: catalog state should report 1 block after first allocation." << std::endl;
        assert(false);
    }

    std::ostringstream freeTextAfterAlloc;
    freeTextAfterAlloc << static_cast<uint16_t>(Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS - 1) << " BLOCKS FREE.";
    if (dirAscii.find(freeTextAfterAlloc.str()) == std::string::npos) {
        std::cerr << "[1541 IEC MAP] FAIL: directory listing missing updated free-block text after allocation." << std::endl;
        assert(false);
    }

    sendCmd15("B-A,00,12,03");
    st = readStatus15();
    if (st.rfind("63,FILE EXISTS", 0) != 0) {
        std::cerr << "[1541 IEC MAP] FAIL: duplicate B-A should report FILE EXISTS, got: " << st << std::endl;
        assert(false);
    }

    // Allocate second block on same channel -> same catalog entry should show 2 blocks.
    sendCmd15("B-A,00,12,04");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || drive.virtualBlocksFree() != static_cast<uint16_t>(Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS - 2)) {
        std::cerr << "[1541 IEC MAP] FAIL: second allocation on same channel failed." << std::endl;
        assert(false);
    }

    drive.buildDirectoryStubPayload();
    std::string dirAscii2;
    guard = 0;
    while (drive.pendingIecTx() > 0 && guard < 2048) {
        uint8_t b = 0;
        if (!drive.hostReadTalkByte(b)) {
            break;
        }
        if (b >= 32 && b <= 126) {
            dirAscii2.push_back(static_cast<char>(b));
        }
        guard++;
    }

    bool catTwoBlocks = false;
    for (size_t i = 0; i < drive.iecCatalog.size(); ++i) {
        if (drive.iecCatalog[i].used && drive.iecCatalog[i].name == "BLK-1203" && drive.iecCatalog[i].blocks == 2) {
            catTwoBlocks = true;
            break;
        }
    }
    if (!catTwoBlocks) {
        std::cerr << "[1541 IEC MAP] FAIL: catalog state should report 2 blocks after grouped allocation." << std::endl;
        assert(false);
    }

    std::ostringstream freeTextAfterSecondAlloc;
    freeTextAfterSecondAlloc << static_cast<uint16_t>(Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS - 2) << " BLOCKS FREE.";
    if (dirAscii2.find(freeTextAfterSecondAlloc.str()) == std::string::npos) {
        std::cerr << "[1541 IEC MAP] FAIL: directory listing missing free-block text after second allocation." << std::endl;
        assert(false);
    }

    sendCmd15("B-F,00,12,03");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || drive.virtualBlocksFree() != static_cast<uint16_t>(Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS - 1)) {
        std::cerr << "[1541 IEC MAP] FAIL: first B-F did not decrement grouped allocation correctly." << std::endl;
        assert(false);
    }

    sendCmd15("B-F,00,12,04");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || drive.virtualBlocksFree() != Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS) {
        std::cerr << "[1541 IEC MAP] FAIL: second B-F did not free grouped allocation completely." << std::endl;
        assert(false);
    }

    drive.buildDirectoryStubPayload();
    std::string dirAsciiFreed;
    guard = 0;
    while (drive.pendingIecTx() > 0 && guard < 2048) {
        uint8_t b = 0;
        if (!drive.hostReadTalkByte(b)) {
            break;
        }
        if (b >= 32 && b <= 126) {
            dirAsciiFreed.push_back(static_cast<char>(b));
        }
        guard++;
    }

    if (dirAsciiFreed.find("BLK-1203") != std::string::npos) {
        std::cerr << "[1541 IEC MAP] FAIL: directory listing still shows freed catalog entry BLK-1203." << std::endl;
        assert(false);
    }

    std::ostringstream freeTextAfterFree;
    freeTextAfterFree << Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS << " BLOCKS FREE.";
    if (dirAsciiFreed.find(freeTextAfterFree.str()) == std::string::npos) {
        std::cerr << "[1541 IEC MAP] FAIL: directory listing missing full free-block text after free." << std::endl;
        assert(false);
    }

    sendCmd15("B-F,00,12,03");
    st = readStatus15();
    if (st.rfind("65,NO BLOCK", 0) != 0) {
        std::cerr << "[1541 IEC MAP] FAIL: freeing non-allocated block should report NO BLOCK, got: " << st << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC MAP] PASS: B-A/B-F virtual allocation map + free blocks accounting "
              << "free=" << std::dec << drive.virtualBlocksFree()
              << "/" << Drive1541::IEC_TOTAL_VIRTUAL_BLOCKS
              << std::endl;
}
