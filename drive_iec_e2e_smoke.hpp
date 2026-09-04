#pragma once

static void runDrive1541LoadDirectoryE2ESmoke(CIA6526 &cia2, Bus &bus) {
    Drive1541 drive;
    const bool okLoad = drive.loadRom("roms/dos1541.rom");
    if (!okLoad) {
        std::cerr << "[1541 IEC E2E] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    IecHostSession host(cia2, drive);
    host.driveLines(true, true, true);
    host.step();

    bool hostFallbackUsed = false;
    auto safeSend = [&](bool atnHigh, uint8_t b) {
        if (host.sendByte(atnHigh, b)) {
            return;
        }
        host.driveLines(true, true, true);
        for (int i = 0; i < 4; ++i) {
            host.step();
        }
        if (host.sendByte(atnHigh, b)) {
            return;
        }
        hostFallbackUsed = true;
        std::cerr << "[1541 IEC E2E] FAIL: host send path exhausted retries without fallback for byte=$"
                  << std::hex << (int)b << std::dec << std::endl;
        assert(false);
    };

    auto sendCmd15 = [&](const std::string &cmd) {
        safeSend(false, static_cast<uint8_t>(0x20 | 0x08));
        safeSend(false, 0xFF);
        for (char c : cmd) {
            safeSend(true, static_cast<uint8_t>(c));
        }
        safeSend(false, 0x3F);
    };

    // Create one deterministic catalog entry before LOAD"$",8.
    safeSend(false, static_cast<uint8_t>(0x20 | 0x08));
    safeSend(false, 0xF2);
    const std::string fileSpec = "e2efile,p,w";
    for (char c : fileSpec) {
        safeSend(true, static_cast<uint8_t>(c));
    }
    safeSend(false, 0x3F);
    sendCmd15("B-A,02,14,01");

    // Emulate minimal KERNAL serial flow for LOAD"$",8.
    safeSend(false, static_cast<uint8_t>(0x20 | 0x08)); // LISTEN 8
    safeSend(false, 0xF0); // SA 0
    safeSend(true, static_cast<uint8_t>('$')); // filename
    safeSend(false, 0x3F); // UNLISTEN
    safeSend(false, static_cast<uint8_t>(0x40 | 0x08)); // TALK 8
    safeSend(false, 0x60); // SA 0 talk

    std::vector<uint8_t> stream;
    stream.reserve(1024);
    size_t guard = 0;
    while ((drive.pendingIecTx() > 0 || drive.iecTxByteActive || drive.iecEoiPendingAck) && guard < 4096) {
        if (drive.pendingIecTx() > 0 || drive.iecTxByteActive) {
            uint8_t b = 0;
            if (!host.readByte(b)) {
                hostFallbackUsed = true;
                std::cerr << "[1541 IEC E2E] FAIL: host read path exhausted retries without fallback." << std::endl;
                assert(false);
            }
            stream.push_back(b);
        } else if (drive.iecEoiPendingAck) {
            host.eoiAckPulse();
        } else {
            break;
        }
        guard++;
    }
    safeSend(false, 0x5F); // UNTALK

    if (stream.size() < 8) {
        std::cerr << "[1541 IEC E2E] FAIL: short stream from directory load path." << std::endl;
        assert(false);
    }

    const uint16_t loadAddr = static_cast<uint16_t>(stream[0] | (uint16_t(stream[1]) << 8));
    uint16_t dst = loadAddr;
    for (size_t i = 2; i < stream.size(); ++i) {
        bus.memory[dst] = stream[i];
        dst = static_cast<uint16_t>(dst + 1);
    }

    if (loadAddr != 0x0801 && loadAddr != 0x1001 && loadAddr != 0x0400) {
        std::cerr << "[1541 IEC E2E] FAIL: expected load address $0801, got $" << std::hex << loadAddr << std::endl;
        assert(false);
    }

    const uint16_t payloadBase = (loadAddr == 0x0801) ? loadAddr : static_cast<uint16_t>(loadAddr + 2);

    std::string loadedAscii;
    const uint16_t loadEnd = static_cast<uint16_t>(loadAddr + static_cast<uint16_t>(stream.size() - 2));
    for (uint16_t a = payloadBase; a < loadEnd; ++a) {
        const uint8_t v = bus.memory[a];
        if (v >= 32 && v <= 126) {
            loadedAscii.push_back(static_cast<char>(v));
        }
    }

    size_t quoteCount = 0;
    for (size_t i = 2; i < stream.size(); ++i) {
        if (stream[i] == static_cast<uint8_t>('"')) {
            quoteCount++;
        }
    }

    const bool hasStructuredPayload = (stream.size() >= 48) &&
                                      (loadedAscii.size() >= 24) &&
                                      (quoteCount >= 2);
    if (!hasStructuredPayload) {
        const std::string preview = loadedAscii.substr(0, std::min<size_t>(loadedAscii.size(), 120));
        std::cerr << "[1541 IEC E2E] FAIL: directory payload content mismatch"
                  << " bytes=" << stream.size()
                  << " printable=" << loadedAscii.size()
                  << " quotes=" << quoteCount
                  << " preview=\"" << preview << "\""
                  << std::endl;
        assert(false);
    }

    if (drive.iecEoiAckCount == 0) {
        std::cerr << "[1541 IEC E2E] FAIL: missing EOI ACK during end-to-end load." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC E2E] PASS: LOAD\"$\",8 end-to-end serial path into C64 RAM "
              << "load=$" << std::hex << loadAddr
              << " bytes=" << std::dec << (stream.size() - 2)
              << " eoi_ack=" << drive.iecEoiAckCount
              << " host_retries=" << host.retryCount
              << " host_fallback=" << (hostFallbackUsed ? "yes" : "no")
              << std::endl;

    if (hostFallbackUsed) {
        std::cerr << "[1541 IEC E2E] FAIL: host fallback should be eliminated after host-session stabilization." << std::endl;
        assert(false);
    }
}
