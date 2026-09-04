#pragma once

static void runDrive1541IecCommandSmoke() {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC CMD] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    const bool listen8 = drive.processIecCommandByte(static_cast<uint8_t>(0x20 | 0x08));
    if (!listen8 || !drive.iecListening) {
        std::cerr << "[1541 IEC CMD] FAIL: LISTEN 8 not accepted." << std::endl;
        assert(false);
    }

    const bool talk8 = drive.processIecCommandByte(static_cast<uint8_t>(0x40 | 0x08));
    if (!talk8 || !drive.iecTalking) {
        std::cerr << "[1541 IEC CMD] FAIL: TALK 8 not accepted." << std::endl;
        assert(false);
    }

    drive.processIecCommandByte(0x3F); // UNLISTEN
    if (drive.iecListening) {
        std::cerr << "[1541 IEC CMD] FAIL: UNLISTEN did not clear listening." << std::endl;
        assert(false);
    }

    drive.processIecCommandByte(0x5F); // UNTALK
    if (drive.iecTalking) {
        std::cerr << "[1541 IEC CMD] FAIL: UNTALK did not clear talking." << std::endl;
        assert(false);
    }

    drive.cpuEnabled = true;
    const uint16_t startPC = drive.pc;
    for (int i = 0; i < 64; ++i) {
        drive.tick();
    }

    if (drive.cpuStepCount == 0 || drive.cpuStepCount >= 64) {
        std::cerr << "[1541 IEC CMD] FAIL: drive CPU scaffold did not advance." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC CMD] PASS: LISTEN/TALK/UNLISTEN/UNTALK + CPU scaffold "
              << "start_pc=$" << std::hex << startPC
              << " last_fetch=$" << drive.cpuLastFetchAddr
              << " opcode=$" << (int)drive.cpuLastOpcode
              << " steps=" << std::dec << drive.cpuStepCount
              << std::endl;
}
