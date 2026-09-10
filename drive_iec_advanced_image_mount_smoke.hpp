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

    auto verifyRead = [&](const std::string &format, const std::filesystem::path &path, uint8_t expected) {
        drive.configureMountedImage(path.string(), format, true);
        if (!drive.isMountedImageBackendActiveFor(format)) {
            std::cerr << "[1541 IMG ADV] FAIL: backend not active for format " << format << std::endl;
            assert(false);
        }
        drive.loadVirtualBlock(1, 0);
        if (!drive.iecBlockBufferValid || drive.iecBlockBuffer[0] != expected) {
            std::cerr << "[1541 IMG ADV] FAIL: readBlock baseline mismatch for format " << format << std::endl;
            assert(false);
        }
    };

    verifyRead("g64", g64Path, 0x5A);
    verifyRead("nib", nibPath, 0x6B);
    verifyRead("raw", rawPath, 0x7C);

    drive.configureMountedImage(rawPath.string(), "raw", true);
    drive.loadVirtualBlock(1, 0);
    drive.iecBlockBuffer[0] = 0x33;
    drive.iecBlockBufferValid = true;
    drive.flushVirtualBlock(1, 0);
    {
        std::ifstream in(rawPath, std::ios::binary);
        uint8_t first = 0;
        in.read(reinterpret_cast<char *>(&first), 1);
        if (first != 0x33) {
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
}
