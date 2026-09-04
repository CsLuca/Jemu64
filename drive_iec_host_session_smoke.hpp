#pragma once

static void runDrive1541IecHostSessionSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC HOST] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    IecHostSession host(cia2, drive);
    host.driveLines(true, true, true);
    host.step();

    // Force a timeout path by keeping host CLK asserted low for a short window.
    host.driveLines(true, false, true);
    const bool blocked = !host.waitBusReleased(8);

    host.driveLines(true, true, true);
    bool recovered = host.waitBusReleased(16);
    if (!recovered) {
        host.driveLines(true, true, true);
        host.step();
        recovered = host.waitBusReleased(32);
    }

    bool sent = host.sendByte(false, static_cast<uint8_t>(0x20 | 0x08));
    sent = sent && host.sendByte(false, 0x3F);

    if (!blocked || !recovered || host.timeoutCount == 0) {
        std::cerr << "[1541 IEC HOST] FAIL: host session retry/timeout path not behaving." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC HOST] PASS: host state-machine wait/retry scaffold "
              << "timeouts=" << host.timeoutCount
              << " retries=" << host.retryCount
              << " sent=" << (sent ? "yes" : "no")
              << " steps=" << host.stepCount
              << std::endl;
}
