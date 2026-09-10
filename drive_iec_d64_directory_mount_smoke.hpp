#pragma once

static void runDrive1541IecD64DirectoryMountSmoke() {
    auto makeD64WithSingleEntry = [](const std::filesystem::path &path,
                                     const std::string &diskName,
                                     const std::string &fileName) {
        std::vector<uint8_t> img(174848, 0);

        auto tsToOffset = [](uint8_t track, uint8_t sector) -> uint32_t {
            static const uint8_t spt[36] = {
                0,
                21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
                19,19,19,19,19,19,19,
                18,18,18,18,18,18,
                17,17,17,17,17
            };
            uint32_t sectorsBefore = 0;
            for (uint8_t t = 1; t < track; ++t) {
                sectorsBefore += spt[t];
            }
            return (sectorsBefore + sector) * 256u;
        };

        const uint32_t bamOff = tsToOffset(18, 0);
        const uint32_t dirOff = tsToOffset(18, 1);

        img[bamOff + 0] = 18;
        img[bamOff + 1] = 1;
        img[bamOff + 2] = 0x41;

        for (uint8_t t = 1; t <= 35; ++t) {
            const size_t off = bamOff + 4 + (static_cast<size_t>(t - 1) * 4);
            img[off] = 16;
        }

        const std::string upDisk = Drive1541::trimAscii(diskName);
        for (size_t i = 0; i < 16; ++i) {
            img[bamOff + 0x90 + i] = (i < upDisk.size()) ? static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(upDisk[i]))) : 0xA0;
        }

        img[dirOff + 0] = 0;
        img[dirOff + 1] = 0xFF;

        const size_t e = dirOff + 2;
        img[e + 2] = 0x82;
        img[e + 3] = 1;
        img[e + 4] = 0;
        const std::string upFile = Drive1541::trimAscii(fileName);
        for (size_t i = 0; i < 16; ++i) {
            img[e + 5 + i] = (i < upFile.size()) ? static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(upFile[i]))) : 0xA0;
        }
        img[e + 30] = 1;
        img[e + 31] = 0;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(img.data()), static_cast<std::streamsize>(img.size()));
    };

    const std::filesystem::path tmpDir = std::filesystem::path("testdata") / "runtime_tmp";
    std::filesystem::create_directories(tmpDir);
    const std::filesystem::path d64Unit8 = tmpDir / "unit8_dir_smoke.d64";
    const std::filesystem::path d64Unit9 = tmpDir / "unit9_dir_smoke.d64";
    makeD64WithSingleEntry(d64Unit8, "DISK8", "FILE8");
    makeD64WithSingleEntry(d64Unit9, "DISK9", "FILE9");

    Drive1541 drive8;
    Drive1541 drive9;
    if (!drive8.loadRom("roms/dos1541.rom") || !drive9.loadRom("roms/dos1541.rom")) {
        std::cerr << "[1541 IEC D64DIR] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }
    drive8.iecDeviceAddress = 8;
    drive9.iecDeviceAddress = 9;
    drive8.configureMountedImage(d64Unit8.string(), "d64", true);
    drive9.configureMountedImage(d64Unit9.string(), "d64", true);

    auto buildDirectoryAndCaptureAscii = [](Drive1541 &drive) {
        drive.processIecCommandByte(static_cast<uint8_t>(0x20 | drive.iecDeviceAddress));
        drive.processIecCommandByte(0xF0);
        drive.processIecDataByte(static_cast<uint8_t>('$'));
        drive.processIecCommandByte(0x3F);
        drive.processIecCommandByte(static_cast<uint8_t>(0x40 | drive.iecDeviceAddress));
        drive.processIecCommandByte(0x60);

        std::string ascii;
        uint8_t b = 0;
        int guard = 0;
        while (drive.hostReadTalkByte(b) && guard < 4096) {
            const uint8_t c = static_cast<uint8_t>(b & 0x7F);
            if (c >= 32 && c <= 126) {
                ascii.push_back(static_cast<char>(c));
            }
            guard++;
        }
        drive.processIecCommandByte(0x5F);
        return ascii;
    };

    const std::string dir8 = buildDirectoryAndCaptureAscii(drive8);
    const std::string dir9 = buildDirectoryAndCaptureAscii(drive9);

    if (dir8.find("FILE8") == std::string::npos || dir8.find("DISK8") == std::string::npos) {
        std::cerr << "[1541 IEC D64DIR] FAIL: unit 8 directory did not come from mounted D64 image." << std::endl;
        assert(false);
    }
    if (dir9.find("FILE9") == std::string::npos || dir9.find("DISK9") == std::string::npos) {
        std::cerr << "[1541 IEC D64DIR] FAIL: unit 9 directory did not come from mounted D64 image." << std::endl;
        assert(false);
    }
    if (dir8.find("FILE9") != std::string::npos || dir9.find("FILE8") != std::string::npos) {
        std::cerr << "[1541 IEC D64DIR] FAIL: cross-unit directory contamination detected." << std::endl;
        assert(false);
    }

    std::error_code ec;
    std::filesystem::remove(d64Unit8, ec);
    std::filesystem::remove(d64Unit9, ec);

    std::cerr << "[1541 IEC D64DIR] PASS: LOAD\"$\" directory payload reflects per-unit mounted D64 images"
              << " u8=FILE8"
              << " u9=FILE9"
              << std::endl;
}
