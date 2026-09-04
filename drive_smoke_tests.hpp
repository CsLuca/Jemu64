#pragma once

static void runDrive1541Smoke() {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541] FAIL: cannot load roms/dos1541.rom (expected 16384 bytes)." << std::endl;
        assert(false);
    }

    const uint16_t resetVec = static_cast<uint16_t>(drive.memory[0xFFFC] | (uint16_t(drive.memory[0xFFFD]) << 8));
    if (resetVec < 0xC000 || resetVec > 0xFFFF) {
        std::cerr << "[1541] FAIL: invalid reset vector $" << std::hex << resetVec << std::endl;
        assert(false);
    }

    const uint8_t romByte = drive.read(0xC000);
    const uint8_t romLast = drive.read(0xFFFF);

    drive.write(0x1800, 0xAA);
    drive.write(0x1C00, 0x55);
    const bool viaOk = (drive.read(0x1800) == 0xAA) && (drive.read(0x1C00) == 0x55);
    if (!viaOk) {
        std::cerr << "[1541] FAIL: VIA mapping read/write failed." << std::endl;
        assert(false);
    }

    for (int i = 0; i < 1000; ++i) {
        drive.tick();
    }

    std::cerr << "[1541] PASS: rom load + reset vector + VIA map + tick smoke "
              << "reset=$" << std::hex << resetVec
              << " rom[$C000]=$" << (int)romByte
              << " rom[$FFFF]=$" << (int)romLast
              << " cycles=" << std::dec << drive.cycles
              << std::endl;
}
