#pragma once

static void runDrive1541IecMultiUnitSmoke() {
    Drive1541 drive8;
    Drive1541 drive9;
    if (!drive8.loadRom("roms/dos1541.rom") || !drive9.loadRom("roms/dos1541.rom")) {
        std::cerr << "[1541 IEC MULTI] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    drive8.iecDeviceAddress = 8;
    drive9.iecDeviceAddress = 9;

    drive8.iecCatalog[0] = Drive1541::VirtualCatalogEntry{};
    drive8.iecCatalog[0].used = true;
    drive8.iecCatalog[0].channel = 2;
    drive8.iecCatalog[0].track = 0x15;
    drive8.iecCatalog[0].sector = 0x01;
    drive8.iecCatalog[0].blocks = 1;
    drive8.iecCatalog[0].name = "UNIT8FILE";
    drive8.iecCatalog[0].type = "PRG";
    drive8.iecCatalog[0].mode = "W";

    drive9.iecCatalog[0] = Drive1541::VirtualCatalogEntry{};
    drive9.iecCatalog[0].used = true;
    drive9.iecCatalog[0].channel = 2;
    drive9.iecCatalog[0].track = 0x15;
    drive9.iecCatalog[0].sector = 0x01;
    drive9.iecCatalog[0].blocks = 1;
    drive9.iecCatalog[0].name = "UNIT9FILE";
    drive9.iecCatalog[0].type = "PRG";
    drive9.iecCatalog[0].mode = "W";

    auto broadcastCmd = [&](uint8_t cmd) {
        const bool a = drive8.processIecCommandByte(cmd);
        const bool b = drive9.processIecCommandByte(cmd);
        return std::pair<bool, bool>(a, b);
    };
    auto broadcastData = [&](uint8_t data) {
        const bool a = drive8.processIecDataByte(data);
        const bool b = drive9.processIecDataByte(data);
        return std::pair<bool, bool>(a, b);
    };

    const auto l8 = broadcastCmd(static_cast<uint8_t>(0x20 | 0x08));
    if (!l8.first || l8.second || !drive8.iecListening || drive9.iecListening) {
        std::cerr << "[1541 IEC MULTI] FAIL: LISTEN 8 addressing isolation mismatch." << std::endl;
        assert(false);
    }
    const auto sa0_8 = broadcastCmd(0xF0);
    const auto name8 = broadcastData(static_cast<uint8_t>('$'));
    const auto unlisten8 = broadcastCmd(0x3F);
    const auto talk8 = broadcastCmd(static_cast<uint8_t>(0x40 | 0x08));
    const auto talkSa0_8 = broadcastCmd(0x60);
    (void)sa0_8;
    (void)name8;
    (void)unlisten8;
    if (!talk8.first || talk8.second || !talkSa0_8.first || talkSa0_8.second) {
        std::cerr << "[1541 IEC MULTI] FAIL: TALK 8 addressing isolation mismatch." << std::endl;
        assert(false);
    }
    if (!drive8.iecTalking || drive9.iecTalking) {
        std::cerr << "[1541 IEC MULTI] FAIL: non-addressed unit talked during unit 8 request." << std::endl;
        assert(false);
    }

    uint8_t byte8 = 0;
    if (!drive8.hostReadTalkByte(byte8)) {
        std::cerr << "[1541 IEC MULTI] FAIL: unit 8 did not provide talk payload." << std::endl;
        assert(false);
    }
    if (drive9.pendingIecTx() != 0 || drive9.iecTxServed != 0) {
        std::cerr << "[1541 IEC MULTI] FAIL: unit 9 produced TX payload while unit 8 addressed." << std::endl;
        assert(false);
    }
    broadcastCmd(0x5F);

    const auto l9 = broadcastCmd(static_cast<uint8_t>(0x20 | 0x09));
    if (l9.first || !l9.second || drive8.iecListening || !drive9.iecListening) {
        std::cerr << "[1541 IEC MULTI] FAIL: LISTEN 9 addressing isolation mismatch." << std::endl;
        assert(false);
    }
    const auto sa0_9 = broadcastCmd(0xF0);
    const auto name9 = broadcastData(static_cast<uint8_t>('$'));
    const auto unlisten9 = broadcastCmd(0x3F);
    const auto talk9 = broadcastCmd(static_cast<uint8_t>(0x40 | 0x09));
    const auto talkSa0_9 = broadcastCmd(0x60);
    (void)sa0_9;
    (void)name9;
    (void)unlisten9;
    if (talk9.first || !talk9.second || talkSa0_9.first || !talkSa0_9.second) {
        std::cerr << "[1541 IEC MULTI] FAIL: TALK 9 addressing isolation mismatch." << std::endl;
        assert(false);
    }
    if (drive8.iecTalking || !drive9.iecTalking) {
        std::cerr << "[1541 IEC MULTI] FAIL: non-addressed unit talked during unit 9 request." << std::endl;
        assert(false);
    }

    uint8_t byte9 = 0;
    const uint64_t unit8TxServedBeforeUnit9Read = drive8.iecTxServed;
    if (!drive9.hostReadTalkByte(byte9)) {
        std::cerr << "[1541 IEC MULTI] FAIL: unit 9 did not provide talk payload." << std::endl;
        assert(false);
    }
    if (drive8.iecTxServed != unit8TxServedBeforeUnit9Read) {
        std::cerr << "[1541 IEC MULTI] FAIL: unit 8 produced TX payload while unit 9 addressed." << std::endl;
        assert(false);
    }

    std::cerr << "[1541 IEC MULTI] PASS: unit addressing isolates talk/listen paths"
              << " u8_first=$" << std::hex << static_cast<int>(byte8)
              << " u9_first=$" << static_cast<int>(byte9)
              << std::dec << std::endl;
}
