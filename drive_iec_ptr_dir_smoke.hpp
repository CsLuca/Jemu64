#pragma once

static void runDrive1541IecBufferPointerDirectorySmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC PTR] FAIL: cannot load roms/dos1541.rom" << std::endl;
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

    const uint8_t trk = 0x22;
    const uint8_t sec = 0x05;
    const uint16_t base = static_cast<uint16_t>(((uint16_t(trk) << 8) | sec) & 0xBFFF);
    drive.memory[static_cast<uint16_t>((base + 0x10) & 0xBFFF)] = 0xA5;
    drive.memory[static_cast<uint16_t>((base + 0x11) & 0xBFFF)] = 0x5A;

    sendCmd15("B-R,00,22,05");
    std::string st = readStatus15();
    if (st.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC PTR] FAIL: B-R status not OK: " << st << std::endl;
        assert(false);
    }

    sendCmd15("B-P,00,22,05,10");
    st = readStatus15();
    if (st.rfind("00,OK", 0) != 0 || !drive.iecChannelPointerValid[0] || drive.iecChannelBufferPos[0] != 0x10) {
        std::cerr << "[1541 IEC PTR] FAIL: B-P did not latch pointer for channel 0." << std::endl;
        assert(false);
    }

    // LOAD"$",8 flow should now source from block buffer at pointer 0x10.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xF0);
    iecHostSendByte(cia2, drive, true, static_cast<uint8_t>('$'));
    iecHostSendByte(cia2, drive, false, 0x3F);
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08));
    iecHostSendByte(cia2, drive, false, 0x60);

    const uint8_t p0 = iecHostReadByte(cia2, drive);
    const uint8_t p1 = iecHostReadByte(cia2, drive);
    const uint8_t d0 = iecHostReadByte(cia2, drive);
    const uint8_t d1 = iecHostReadByte(cia2, drive);
    iecHostSendByte(cia2, drive, false, 0x5F);

    if (p0 != 0x01 || p1 != 0x08 || d0 != 0xA5 || d1 != 0x5A) {
        std::cerr << "[1541 IEC PTR] FAIL: directory pointer stream mismatch ($"
                  << std::hex << (int)p0 << " $" << (int)p1 << " $" << (int)d0 << " $" << (int)d1 << ")." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC PTR] PASS: B-P channel pointer + directory block-buffer stream "
              << "ptr=$" << std::hex << (int)drive.iecChannelBufferPos[0]
              << " data=$" << (int)d0 << " $" << (int)d1
              << std::endl;
}
