#pragma once

static void runDrive1541TimingBattery(CIA6526 &cia2) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 TIMING] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    {
        Drive1541 revBase;
        Drive1541 revC;
        revBase.setRevision(Drive1541::REV_1541);
        revC.setRevision(Drive1541::REV_1541C);
        assert(revBase.getRevisionProfile().iecStrictEoiAck);
        assert(!revC.getRevisionProfile().iecStrictEoiAck);
        assert(revBase.getRevisionProfile().iecCommandNeedsAtnLow);
        assert(!revC.getRevisionProfile().iecCommandNeedsAtnLow);
        assert(revC.getRevisionProfile().cpuCyclesPerStep >= revBase.getRevisionProfile().cpuCyclesPerStep);
        assert(revBase.getRevisionProfile().iecHandshakeNeedsClockLowAck);
        assert(!revC.getRevisionProfile().iecHandshakeNeedsClockLowAck);
        assert(revC.getRevisionProfile().iecRxAckOnAnyDataByte);

        revBase.iecATN = true;
        revC.iecATN = true;
        revBase.iecSerialState = Drive1541::IecSerialState::Command;
        revC.iecSerialState = Drive1541::IecSerialState::Command;
        const bool baseListenWithoutAtn = revBase.processIecCommandByte(static_cast<uint8_t>(0x20 | 0x08));
        const bool cListenWithoutAtn = revC.processIecCommandByte(static_cast<uint8_t>(0x20 | 0x08));
        assert(!baseListenWithoutAtn);
        assert(cListenWithoutAtn);

        revBase.iecEnableAtnAck = true;
        revC.iecEnableAtnAck = true;
        revBase.iecATN = false;
        revC.iecATN = false;
        revBase.iecCLK = true;
        revC.iecCLK = true;
        revBase.iecDATA = true;
        revC.iecDATA = true;
        revBase.iecPrevATN = true;
        revC.iecPrevATN = true;
        revBase.stepIecSerial();
        revC.stepIecSerial();
        assert(revBase.iecAtnHandshakeActive);
        assert(revC.iecAtnHandshakeActive);
        assert(revBase.iecAtnAckPullDATA);
        assert(revC.iecAtnAckPullDATA);
        revBase.iecCLK = false;
        revBase.stepIecSerial();
        assert(revBase.iecAtnAckSawClockLow);
    }

    drive.cpuEnabled = true;
    drive.via1.write(0x1804, 0x05);
    drive.via1.write(0x1805, 0x00);
    drive.via1.write(0x180B, 0x00);
    drive.via1.write(0x1802, 0x60);
    drive.via1.write(0x1800, 0x00);
    const uint16_t t1Start = drive.via1.timer1Counter;

    const uint64_t startCpuSteps = drive.cpuStepCount;
    for (int i = 0; i < 400; ++i) {
        drive.tickIecHalfCycle();
    }
    const uint64_t stepDelta = drive.cpuStepCount - startCpuSteps;
    if (stepDelta == 0 || stepDelta >= 250) {
        std::cerr << "[1541 TIMING] FAIL: CPU cycle pacing not respected. step_delta=" << stepDelta << std::endl;
        assert(false);
    }
    if (drive.via1.timer1Counter == t1Start && drive.via1.ifr == 0 && drive.iecDrivePullCLK && drive.iecDrivePullDATA) {
        std::cerr << "[1541 TIMING] FAIL: VIA1 timing side effects not visible." << std::endl;
        assert(false);
    }

    // Keep command-channel stress deterministic: CPU/VIA timing already validated above.
    drive.cpuEnabled = false;
    drive.iecRxQueue.clear();
    drive.iecTxQueue.clear();

    auto injectCmd15 = [&](const std::string &cmd) {
        const bool listen = drive.processIecCommandByte(static_cast<uint8_t>(0x20 | 0x08));
        const bool sa15 = drive.processIecCommandByte(0xFF);
        if (!listen || !sa15) {
            std::cerr << "[1541 TIMING] FAIL: cannot open LISTEN/SA15 command channel." << std::endl;
            assert(false);
        }
        for (char c : cmd) {
            if (!drive.processIecDataByte(static_cast<uint8_t>(c))) {
                std::cerr << "[1541 TIMING] FAIL: command byte rejected." << std::endl;
                assert(false);
            }
        }
        drive.processIecCommandByte(0x3F); // UNLISTEN
    };

    auto readCmd15Payload = [&]() -> std::vector<uint8_t> {
        std::vector<uint8_t> out;
        out.reserve(64);
        const bool talk = drive.processIecCommandByte(static_cast<uint8_t>(0x40 | 0x08));
        const bool sa15 = drive.processIecCommandByte(0x6F);
        if (!talk || !sa15) {
            std::cerr << "[1541 TIMING] FAIL: cannot open TALK/SA15 channel." << std::endl;
            assert(false);
        }
        uint8_t b = 0;
        size_t guard = 0;
        while (drive.hostReadTalkByte(b) && guard < 128) {
            if (b == 0x0D) {
                break;
            }
            out.push_back(b);
            guard++;
        }
        drive.processIecCommandByte(0x5F); // UNTALK
        drive.processIecCommandByte(0xEF); // CLOSE 15
        return out;
    };

    injectCmd15("M-W,0400,00,02,AA,55");
    if (drive.memory[0x0400] != 0xAA || drive.memory[0x0401] != 0x55) {
        std::cerr << "[1541 TIMING] FAIL: M-W did not write expected bytes." << std::endl;
        assert(false);
    }

    injectCmd15("M-R,0400,00,02");
    std::vector<uint8_t> mr = readCmd15Payload();
    if (mr.size() < 2 || mr[0] != 0xAA || mr[1] != 0x55) {
        std::cerr << "[1541 TIMING] FAIL: M-R returned wrong payload." << std::endl;
        assert(false);
    }

    injectCmd15("B-A,02,14,01");
    if (drive.iecStatusLine.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 TIMING] FAIL: B-A status=" << drive.iecStatusLine << std::endl;
        assert(false);
    }

    injectCmd15("B-F,02,14,01");
    if (drive.iecStatusLine.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 TIMING] FAIL: B-F status=" << drive.iecStatusLine << std::endl;
        assert(false);
    }

    // Timeout stress in deterministic drive-local path.
    drive.iecTalking = true;
    drive.iecTxByteActive = true;
    drive.iecTxBitCount = 1;
    drive.iecSerialPullDATA = true;
    drive.setIecLines(true, true, true);
    const uint64_t txToBefore = drive.iecTxTimeoutCount;
    for (uint32_t i = 0; i < (Drive1541::IEC_SERIAL_TIMEOUT_TICKS + 12); ++i) {
        drive.tickIecHalfCycle();
    }
    if (drive.iecTxTimeoutCount <= txToBefore) {
        std::cerr << "[1541 TIMING] FAIL: timeout stress did not trigger tx timeout." << std::endl;
        assert(false);
    }

    injectCmd15("B-R,02,14,01");
    if (drive.iecStatusLine.rfind("00,OK", 0) != 0) {
        std::cerr << "[1541 TIMING] FAIL: B-R status after stress=" << drive.iecStatusLine << std::endl;
        assert(false);
    }

    if (drive.iecCommandSyntaxErrorCount != 0) {
        std::cerr << "[1541 TIMING] FAIL: unexpected command syntax errors=" << drive.iecCommandSyntaxErrorCount << std::endl;
        assert(false);
    }

    if (drive.iecCommandDispatchCount < 16 || drive.iecDataDispatchCount < 6) {
        std::cerr << "[1541 TIMING] FAIL: insufficient command/data dispatch coverage."
                  << " cmd=" << drive.iecCommandDispatchCount
                  << " data=" << drive.iecDataDispatchCount
                  << std::endl;
        assert(false);
    }

    std::cerr << "[1541 TIMING] PASS: CPU+VIA+IEC timing battery"
              << " cpu_steps=" << drive.cpuStepCount
              << " via1_ifr=$" << std::hex << int(drive.via1.ifr)
              << " cmd=" << std::dec << drive.iecCommandDispatchCount
              << " data=" << drive.iecDataDispatchCount
              << " tx_to=" << drive.iecTxTimeoutCount
              << std::endl;
}
