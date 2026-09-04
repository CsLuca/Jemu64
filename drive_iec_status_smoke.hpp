#pragma once

static void runDrive1541IecStatusTimeoutSmoke(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC STAT] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    drive.iecTalking = true;
    drive.iecTxByteActive = true;
    drive.iecTxBitCount = 1;
    drive.iecSerialPullDATA = true;
    drive.iecRxBitCount = 3;
    drive.iecEoiPendingAck = true;
    drive.iecEoiAckLowSeen = false;
    drive.setIecLines(true, true, true);

    for (uint32_t i = 0; i < (Drive1541::IEC_SERIAL_TIMEOUT_TICKS + 8); ++i) {
        drive.tick();
    }
    for (uint32_t i = 0; i < (Drive1541::IEC_EOI_TIMEOUT_TICKS + 8); ++i) {
        drive.tick();
    }

    if (drive.iecRxTimeoutCount == 0 || drive.iecTxTimeoutCount == 0 || drive.iecEoiTimeoutCount == 0) {
        std::cerr << "[1541 IEC STAT] FAIL: timeout counters not incremented as expected." << std::endl;
        assert(false);
    }

    // Return the serial state machine to idle before issuing command-channel traffic.
    drive.iecTalking = false;
    drive.iecListening = false;
    drive.iecTxByteActive = false;
    drive.iecEoiPendingAck = false;
    drive.iecRxBitCount = 0;
    drive.iecSerialPullDATA = false;
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    // Command channel 15: send I0, then read status.
    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x20 | 0x08)); // LISTEN 8
    iecHostSendByte(cia2, drive, false, 0xFF); // SA 15 command channel
    iecHostSendByte(cia2, drive, true, static_cast<uint8_t>('I'));
    iecHostSendByte(cia2, drive, true, static_cast<uint8_t>('0'));
    iecHostSendByte(cia2, drive, false, 0x3F); // UNLISTEN

    iecHostSendByte(cia2, drive, false, static_cast<uint8_t>(0x40 | 0x08)); // TALK 8
    iecHostSendByte(cia2, drive, false, 0x6F); // SA 15 status channel

    const std::string status = iecHostReadTextLine(cia2, drive, 64);
    iecHostEoiAckPulse(cia2, drive);
    iecHostSendByte(cia2, drive, false, 0x5F); // UNTALK
    iecHostSendByte(cia2, drive, false, 0xEF); // CLOSE 15

    if (status.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 IEC STAT] FAIL: unexpected status text: " << status << std::endl;
        assert(false);
    }

    if (drive.iecOpenListenChannels[15] || drive.iecOpenTalkChannels[15]) {
        std::cerr << "[1541 IEC STAT] FAIL: CLOSE 15 did not clear channel state." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC STAT] PASS: timeout/error paths + status channel "
              << "rx_to=" << drive.iecRxTimeoutCount
              << " tx_to=" << drive.iecTxTimeoutCount
              << " eoi_to=" << drive.iecEoiTimeoutCount
              << " status=\"" << status << "\""
              << std::endl;
}
