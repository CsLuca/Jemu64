#pragma once

static void runDrive1541IecMemoryCommandSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC MEM] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    // Write two bytes in drive RAM at $0200: AA 55
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xFF);
    const std::string mw = "M-W,0200,00,02,AA,55";
    for (char c : mw) {
        iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
    }
    iecHostSendByte(cia2, drive, false, 0x3F);

    if (drive.memory[0x0200] != 0xAA || drive.memory[0x0201] != 0x55) {
        std::cerr << "[1541 IEC MEM] FAIL: M-W did not write expected bytes." << std::endl;
        assert(false);
    }

    // Read them back via M-R on channel 15
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08));
    iecHostSendByte(cia2, drive, false, 0xFF);
    const std::string mr = "M-R,0200,00,02";
    for (char c : mr) {
        iecHostSendByte(cia2, drive, true, static_cast<uint8_t>(c));
    }
    iecHostSendByte(cia2, drive, false, 0x3F);

    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08));
    iecHostSendByte(cia2, drive, false, 0x6F);
    const uint8_t r0 = iecHostReadByte(cia2, drive);
    const uint8_t r1 = iecHostReadByte(cia2, drive);
    iecHostEoiAckPulse(cia2, drive);
    iecHostSendByte(cia2, drive, false, 0x5F);
    iecHostSendByte(cia2, drive, false, 0xEF);

    if (r0 != 0xAA || r1 != 0x55) {
        std::cerr << "[1541 IEC MEM] FAIL: M-R returned unexpected bytes ($"
                  << std::hex << (int)r0 << " $" << (int)r1 << ")." << std::endl;
        assert(false);
    }

    // Unsupported talk channel should push NO CHANNEL status.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08));
    iecHostSendByte(cia2, drive, false, 0x61);
    iecHostSendByte(cia2, drive, false, 0x5F);

    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08));
    iecHostSendByte(cia2, drive, false, 0x6F);
    const std::string status = iecHostReadTextLine(cia2, drive, 64);
    iecHostEoiAckPulse(cia2, drive);
    iecHostSendByte(cia2, drive, false, 0x5F);
    iecHostSendByte(cia2, drive, false, 0xEF);

    if (status.rfind("70,NO CHANNEL", 0) != 0) {
        std::cerr << "[1541 IEC MEM] FAIL: expected NO CHANNEL status, got: " << status << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC MEM] PASS: M-W/M-R/Ux-ready command channel + status propagation "
              << "read_back=$" << std::hex << (int)r0 << " $" << (int)r1
              << " status=\"" << status << "\""
              << std::endl;
}
