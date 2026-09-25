#pragma once

static void runDrive1541IecStatusTimeoutSmoke(CIA6526 &cia2) {
    // Procedure: deterministic host-integrated timeout coverage with explicit budgets.
    Drive1541 drive;
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);

    // RX timeout through integrated host line stepping.
    drive.iecListening = true;
    drive.iecReceiverTimeoutTicks = 24;
    drive.iecSerialTimeoutHysteresisTicks = 2;
    drive.iecRxBitCount = 1;
    const uint32_t rxBudget = drive.iecReceiverTimeoutTicks + drive.iecSerialTimeoutHysteresisTicks;
    for (uint32_t i = 0; i < rxBudget; ++i) {
        iecHostDriveLines(cia2, drive, true, true, true);
        iecHostStep(cia2, drive);
    }
    if (drive.iecRxTimeoutCount != 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated RX timeout tripped before budget" << std::endl;
        assert(false);
    }
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);
    if (drive.iecRxTimeoutCount == 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated RX timeout missing after budget" << std::endl;
        assert(false);
    }

    // TX timeout through integrated host line stepping.
    drive.iecListening = false;
    drive.iecTalking = true;
    drive.iecActiveTalkChannel = 0;
    drive.iecTalkSa0Confirmed = true;
    drive.iecSenderTimeoutTicks = 24;
    drive.iecTxByteActive = true;
    drive.iecTxBitCount = 1;
    drive.iecSerialPullDATA = true;
    drive.iecTxIdleTicks = 0;
    const uint32_t txBudget = drive.iecSenderTimeoutTicks + drive.iecSerialTimeoutHysteresisTicks;
    for (uint32_t i = 0; i < txBudget; ++i) {
        iecHostDriveLines(cia2, drive, true, true, true);
        iecHostStep(cia2, drive);
    }
    if (drive.iecTxTimeoutCount != 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated TX timeout tripped before budget" << std::endl;
        assert(false);
    }
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);
    if (drive.iecTxTimeoutCount == 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated TX timeout missing after budget" << std::endl;
        assert(false);
    }

    // EOI timeout through integrated host line stepping.
    drive.iecEoiPendingAck = true;
    drive.iecEoiAckLowSeen = false;
    drive.iecEoiWaitTicks = 0;
    drive.iecEoiTimeoutCount = 0;
    const uint32_t eoiBudget = drive.iecSenderTimeoutTicks;
    for (uint32_t i = 0; i < eoiBudget; ++i) {
        iecHostDriveLines(cia2, drive, true, true, true);
        iecHostStep(cia2, drive);
    }
    if (drive.iecEoiTimeoutCount != 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated EOI timeout tripped before budget" << std::endl;
        assert(false);
    }
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);
    if (drive.iecEoiTimeoutCount == 0) {
        std::cerr << "[1541 IEC STAT] FAIL: integrated EOI timeout missing after budget" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC STAT] PASS: integrated deterministic timeout checks "
              << "rx_to=" << drive.iecRxTimeoutCount
              << " tx_to=" << drive.iecTxTimeoutCount
              << " eoi_to=" << drive.iecEoiTimeoutCount
              << std::endl;
}
