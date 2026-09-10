#pragma once

static void runDrive1541IecAdvancedImageMountSmoke() {
    Drive1541 drive;
    if (!drive.loadRom("roms/dos1541.rom")) {
        std::cerr << "[1541 IMG ADV] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    const std::filesystem::path tmpDir = std::filesystem::path("testdata") / "runtime_tmp";
    std::filesystem::create_directories(tmpDir);

    auto createLinearImage = [&](const std::filesystem::path &path, uint8_t seed) {
        std::vector<uint8_t> data(174848, seed);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    };

    const std::filesystem::path g64Path = tmpDir / "advanced_mount_smoke.g64";
    const std::filesystem::path nibPath = tmpDir / "advanced_mount_smoke.nib";
    const std::filesystem::path rawPath = tmpDir / "advanced_mount_smoke.raw";

    createLinearImage(g64Path, 0x5A);
    createLinearImage(nibPath, 0x6B);
    createLinearImage(rawPath, 0x7C);

    auto expectedFluxByte = [&](uint8_t seed, uint8_t track, uint8_t sector, bool syncLoss) -> uint8_t {
        uint8_t v = seed;
        uint8_t zone = 0;
        if (track <= 17) zone = 0;
        else if (track <= 24) zone = 1;
        else if (track <= 30) zone = 2;
        else zone = 3;
        const uint8_t rot = static_cast<uint8_t>(zone + 1);
        v = static_cast<uint8_t>((v >> rot) | (v << ((8 - rot) & 7)));
        if (syncLoss) {
            const size_t syncPos = static_cast<size_t>((track + sector) & 0x1F);
            if (syncPos == 0) {
                v = static_cast<uint8_t>(v ^ 0xFFu);
            }
        }
        return v;
    };

    auto verifyRead = [&](const std::string &format, const std::filesystem::path &path, uint8_t expectedSeed, bool syncLoss) {
        drive.configureMountedImage(path.string(), format, true);
        if (!drive.isMountedImageBackendActiveFor(format)) {
            std::cerr << "[1541 IMG ADV] FAIL: backend not active for format " << format << std::endl;
            assert(false);
        }
        drive.loadVirtualBlock(1, 0);
        const uint8_t expected = expectedFluxByte(expectedSeed, 1, 0, syncLoss);
        if (!drive.iecBlockBufferValid || drive.iecBlockBuffer[0] != expected) {
            std::cerr << "[1541 IMG ADV] FAIL: readBlock baseline mismatch for format " << format << std::endl;
            assert(false);
        }
    };

    verifyRead("g64", g64Path, 0x5A, true);
    verifyRead("nib", nibPath, 0x6B, true);
    verifyRead("raw", rawPath, 0x7C, true);

    auto verifyWriteProtect = [&](const std::string &format, const std::filesystem::path &path) {
        drive.configureMountedImage(path.string(), format, true);
        drive.loadVirtualBlock(1, 0);
        drive.iecBlockBuffer[0] = 0x22;
        drive.iecBlockBufferValid = true;
        drive.flushVirtualBlock(1, 0);
        if (drive.iecStatusLine.rfind("26,WRITE PROTECT ON", 0) != 0) {
            std::cerr << "[1541 IMG ADV] FAIL: expected write protect status on format " << format
                      << " got=" << drive.iecStatusLine << std::endl;
            assert(false);
        }
    };

    verifyWriteProtect("g64", g64Path);
    verifyWriteProtect("nib", nibPath);

    drive.configureMountedImage(g64Path.string(), "g64", true);
    drive.loadVirtualBlock(1, 0);
    const uint8_t g64First = drive.iecBlockBuffer[7];
    drive.loadVirtualBlock(1, 0);
    if (drive.iecBlockBuffer[7] == g64First) {
        std::cerr << "[1541 IMG ADV] FAIL: weak-bit model did not vary G64 reads." << std::endl;
        assert(false);
    }

    drive.configureMountedImage(nibPath.string(), "nib", true);
    drive.loadVirtualBlock(1, 0);
    const uint8_t nibFirst = drive.iecBlockBuffer[7];
    drive.loadVirtualBlock(1, 0);
    if (drive.iecBlockBuffer[7] == nibFirst) {
        std::cerr << "[1541 IMG ADV] FAIL: weak-bit model did not vary NIB reads." << std::endl;
        assert(false);
    }

    drive.configureMountedImage(rawPath.string(), "raw", true);
    drive.loadVirtualBlock(1, 0);
    drive.iecBlockBuffer[0] = 0x33;
    drive.iecBlockBufferValid = true;
    drive.flushVirtualBlock(1, 0);
    {
        std::ifstream in(rawPath, std::ios::binary);
        uint8_t first = 0;
        in.read(reinterpret_cast<char *>(&first), 1);
        const uint8_t expectedRawEncoded = static_cast<uint8_t>((0x33u >> 1) | (0x33u << 7));
        if (first != expectedRawEncoded) {
            std::cerr << "[1541 IMG ADV] FAIL: RAW write baseline did not persist." << std::endl;
            assert(false);
        }
    }

    std::error_code ec;
    std::filesystem::remove(g64Path, ec);
    std::filesystem::remove(nibPath, ec);
    std::filesystem::remove(rawPath, ec);

    std::cerr << "[1541 IMG ADV] PASS: g64/nib/raw backend mount+read baseline"
              << " g64=1 nib=1 raw=1"
              << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_mount_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_mount_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_raw_mount_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_write_protect_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_write_protect_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_weakbit_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_weakbit_baseline" << std::endl;
}
