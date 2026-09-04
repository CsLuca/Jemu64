#pragma once

static void runDrive1541IecDirectoryStubSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC DIR] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    // Clocked serial sequence approximating LOAD"$",8 command flow.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08)); // LISTEN 8
    iecHostSendByte(cia2, drive, false, 0xF0); // SA 0
    iecHostSendByte(cia2, drive, true, static_cast<uint8_t>('$')); // filename byte in data phase
    iecHostSendByte(cia2, drive, false, 0x3F); // UNLISTEN
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08)); // TALK 8
    iecHostSendByte(cia2, drive, false, 0x60); // SA 0 talk

    if (!drive.iecOpenListenChannels[0] || !drive.iecOpenTalkChannels[0] ||
        drive.iecActiveListenChannel != 0 || drive.iecActiveTalkChannel != 0) {
        std::cerr << "[1541 IEC DIR] FAIL: channel open/activation state not coherent for SA0." << std::endl;
        assert(false);
    }

    if (!drive.iecDirectoryStubPrepared) {
        std::cerr << "[1541 IEC DIR] FAIL: directory stub payload not prepared." << std::endl;
        assert(false);
    }

    const uint8_t b0 = iecHostReadByte(cia2, drive);
    const uint8_t b1 = iecHostReadByte(cia2, drive);

    if (b0 != 0x01 || b1 != 0x08) {
        std::cerr << "[1541 IEC DIR] FAIL: invalid PRG load address prefix ($"
                  << std::hex << (int)b0 << " $" << (int)b1 << ")." << std::endl;
        assert(false);
    }

    if (drive.pendingIecTx() == 0) {
        std::cerr << "[1541 IEC DIR] FAIL: directory stream unexpectedly empty." << std::endl;
        assert(false);
    }

    while (drive.pendingIecTx() > 1) {
        (void)iecHostReadByte(cia2, drive);
    }

    if (drive.pendingIecTx() != 1) {
        std::cerr << "[1541 IEC DIR] FAIL: cannot isolate final EOI byte." << std::endl;
        assert(false);
    }

    const uint8_t lastByte = iecHostReadByte(cia2, drive);
    (void)lastByte;
    iecHostEoiAckPulse(cia2, drive);

    if (drive.iecEoiAckCount == 0) {
        std::cerr << "[1541 IEC DIR] FAIL: expected at least one EOI ACK." << std::endl;
        assert(false);
    }

    iecHostSendByte(cia2, drive, false, 0xE0); // CLOSE 0
    iecHostSendByte(cia2, drive, false, 0x5F); // UNTALK

    if (drive.iecOpenListenChannels[0] || drive.iecOpenTalkChannels[0]) {
        std::cerr << "[1541 IEC DIR] FAIL: CLOSE 0 did not clear channel state." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC DIR] PASS: clocked serial command/data + directory stub stream "
              << "rx_processed=" << std::dec << drive.iecRxProcessed
              << " tx_served=" << drive.iecTxServed
              << " eoi_ack=" << drive.iecEoiAckCount
              << " tx_remaining=" << drive.pendingIecTx()
              << std::endl;
}
