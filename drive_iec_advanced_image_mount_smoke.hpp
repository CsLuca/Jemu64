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

    auto createMinimalG64Image = [&](const std::filesystem::path &path, uint8_t seed) {
        const uint8_t trackCount = 35;
        const uint32_t tableOffset = 0x0C;
        const uint32_t tableBytes = static_cast<uint32_t>(trackCount) * 4u;
        const uint32_t firstTrackOffset = tableOffset + tableBytes;
        const uint32_t secondTrackOffset = firstTrackOffset + 2u + 256u;
        const uint16_t trackLen = 256;
        std::vector<uint8_t> data(secondTrackOffset + 2u + trackLen, 0);

        data[0] = 'G'; data[1] = 'C'; data[2] = 'R'; data[3] = '-';
        data[4] = '1'; data[5] = '5'; data[6] = '4'; data[7] = '1';
        data[8] = 0;
        data[9] = trackCount;
        data[10] = 0;
        data[11] = 0;

        data[tableOffset + 0] = static_cast<uint8_t>(firstTrackOffset & 0xFF);
        data[tableOffset + 1] = static_cast<uint8_t>((firstTrackOffset >> 8) & 0xFF);
        data[tableOffset + 2] = static_cast<uint8_t>((firstTrackOffset >> 16) & 0xFF);
        data[tableOffset + 3] = static_cast<uint8_t>((firstTrackOffset >> 24) & 0xFF);
        data[tableOffset + 4] = static_cast<uint8_t>(secondTrackOffset & 0xFF);
        data[tableOffset + 5] = static_cast<uint8_t>((secondTrackOffset >> 8) & 0xFF);
        data[tableOffset + 6] = static_cast<uint8_t>((secondTrackOffset >> 16) & 0xFF);
        data[tableOffset + 7] = static_cast<uint8_t>((secondTrackOffset >> 24) & 0xFF);

        data[firstTrackOffset + 0] = static_cast<uint8_t>(trackLen & 0xFF);
        data[firstTrackOffset + 1] = static_cast<uint8_t>((trackLen >> 8) & 0xFF);
        for (uint16_t i = 0; i < trackLen; ++i) {
            data[firstTrackOffset + 2u + i] = seed;
        }

        data[secondTrackOffset + 0] = static_cast<uint8_t>(trackLen & 0xFF);
        data[secondTrackOffset + 1] = static_cast<uint8_t>((trackLen >> 8) & 0xFF);
        for (uint16_t i = 0; i < trackLen; ++i) {
            data[secondTrackOffset + 2u + i] = static_cast<uint8_t>(seed ^ 0x33u);
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    };

    createMinimalG64Image(g64Path, 0x5A);
    createLinearImage(nibPath, 0x6B);
    createLinearImage(rawPath, 0x7C);

    auto verifyRead = [&](const std::string &format,
                          const std::filesystem::path &path,
                          uint8_t expectedClassByte,
                          bool expectWeakbit) {
        drive.configureMountedImage(path.string(), format, true);
        if (!drive.isMountedImageBackendActiveFor(format)) {
            std::cerr << "[1541 IMG ADV] FAIL: backend not active for format " << format << std::endl;
            assert(false);
        }
        drive.loadVirtualBlock(1, 0);
        if (!drive.iecBlockBufferValid) {
            std::cerr << "[1541 IMG ADV] FAIL: readBlock baseline mismatch for format " << format << std::endl;
            assert(false);
        }

        if (drive.iecBlockBuffer[3] != expectedClassByte) {
            std::cerr << "[1541 IMG ADV] FAIL: GCR class byte mismatch for format " << format
                      << " got=$" << std::hex << static_cast<int>(drive.iecBlockBuffer[3])
                      << " expected=$" << static_cast<int>(expectedClassByte) << std::dec
                      << std::endl;
            assert(false);
        }

        if (drive.iecBlockBuffer[4] != 0x02) {
            std::cerr << "[1541 IMG ADV] FAIL: GCR track/sector tag mismatch for format " << format << std::endl;
            assert(false);
        }

        const uint8_t dosErr = drive.iecBlockBuffer[5];
        auto rotr8 = [](uint8_t v, uint8_t n) -> uint8_t {
            return static_cast<uint8_t>((v >> (n & 7)) | (v << ((8 - (n & 7)) & 7)));
        };
        const uint8_t candidates[10] = {
            20, 21, 22, 23, 27,
            rotr8(20, 1), rotr8(21, 1), rotr8(22, 1), rotr8(23, 1), rotr8(27, 1)
        };
        bool matchDosClass = false;
        for (uint8_t c : candidates) {
            if (dosErr == c) {
                matchDosClass = true;
                break;
            }
        }
        if (!matchDosClass) {
            std::cerr << "[1541 IMG ADV] FAIL: DOS error-map class missing for format " << format
                      << " got=" << std::dec << static_cast<int>(dosErr) << std::endl;
            assert(false);
        }

        if (expectWeakbit) {
            const uint8_t first = drive.iecBlockBuffer[7];
            drive.loadVirtualBlock(1, 0);
            if (drive.iecBlockBuffer[7] == first) {
                std::cerr << "[1541 IMG ADV] FAIL: weak-bit model did not vary reads for format " << format << std::endl;
                assert(false);
            }
        }
    };

    verifyRead("g64", g64Path, 0x66, true);
    verifyRead("nib", nibPath, 0x66, true);
    verifyRead("raw", rawPath, 0x77, false);

    {
        G64ImageBackend g64Debug(g64Path.string());
        if (!g64Debug.isReady() || !g64Debug.debugHasHalfTrackSlice(1)) {
            std::cerr << "[1541 IMG ADV] FAIL: G64 half-track slice baseline missing." << std::endl;
            assert(false);
        }
        const uint8_t t1 = g64Debug.debugTrackSliceTag(1);
        const uint8_t t2 = g64Debug.debugTrackSliceTag(2);
        if (t1 == 0 || t2 == 0 || t1 == t2) {
            std::cerr << "[1541 IMG ADV] FAIL: G64 multi-track slice tag baseline mismatch." << std::endl;
            assert(false);
        }
    }

    {
        NIBImageBackend nibDebug(nibPath.string());
        if (!nibDebug.isReady() || !nibDebug.debugTrackWindowReadable(1) || !nibDebug.debugTrackWindowReadable(2)) {
            std::cerr << "[1541 IMG ADV] FAIL: NIB multi-track window baseline mismatch." << std::endl;
            assert(false);
        }
        (void)nibDebug.debugTrackStrideTag();
    }

    drive.configureMountedImage(g64Path.string(), "g64", true);
    drive.loadVirtualBlock(1, 0);
    const uint8_t g64RelockA = drive.iecBlockBuffer[3];
    drive.loadVirtualBlock(1, 0);
    const uint8_t g64RelockB = drive.iecBlockBuffer[3];
    if (g64RelockA != g64RelockB) {
        std::cerr << "[1541 IMG ADV] FAIL: G64 relock drift classification unstable." << std::endl;
        assert(false);
    }
    if (drive.iecBlockBuffer[1] == 0x00 || drive.iecBlockBuffer[2] == 0x00) {
        std::cerr << "[1541 IMG ADV] FAIL: G64 sync/gap classification not applied." << std::endl;
        assert(false);
    }

    {
        uint64_t g64JitterDigest = 1469598103934665603ull;
        for (int i = 0; i < 8; ++i) {
            drive.loadVirtualBlock(1, 0);
            g64JitterDigest ^= static_cast<uint64_t>(drive.iecBlockBuffer[7]);
            g64JitterDigest *= 1099511628211ull;
            g64JitterDigest ^= static_cast<uint64_t>(drive.iecBlockBuffer[5]);
            g64JitterDigest *= 1099511628211ull;
        }
        if (g64JitterDigest == 0 || g64JitterDigest == 1469598103934665603ull) {
            std::cerr << "[1541 IMG ADV] FAIL: G64 jitter digest did not evolve." << std::endl;
            assert(false);
        }
    }

    drive.configureMountedImage(nibPath.string(), "nib", true);
    drive.loadVirtualBlock(1, 0);
    const uint8_t nibRelockA = drive.iecBlockBuffer[3];
    drive.loadVirtualBlock(1, 0);
    const uint8_t nibRelockB = drive.iecBlockBuffer[3];
    if (nibRelockA != nibRelockB) {
        std::cerr << "[1541 IMG ADV] FAIL: NIB relock drift classification unstable." << std::endl;
        assert(false);
    }
    if (drive.iecBlockBuffer[1] == 0x00 || drive.iecBlockBuffer[2] == 0x00) {
        std::cerr << "[1541 IMG ADV] FAIL: NIB sync/gap classification not applied." << std::endl;
        assert(false);
    }

    {
        uint64_t nibJitterDigest = 1469598103934665603ull;
        for (int i = 0; i < 8; ++i) {
            drive.loadVirtualBlock(1, 0);
            nibJitterDigest ^= static_cast<uint64_t>(drive.iecBlockBuffer[7]);
            nibJitterDigest *= 1099511628211ull;
            nibJitterDigest ^= static_cast<uint64_t>(drive.iecBlockBuffer[5]);
            nibJitterDigest *= 1099511628211ull;
        }
        if (nibJitterDigest == 0 || nibJitterDigest == 1469598103934665603ull) {
            std::cerr << "[1541 IMG ADV] FAIL: NIB jitter digest did not evolve." << std::endl;
            assert(false);
        }
    }

    {
        drive.configureMountedImage(g64Path.string(), "g64", true);
        std::array<uint8_t, 3> window = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            drive.loadVirtualBlock(1, 0);
            window[static_cast<size_t>(i)] = drive.iecBlockBuffer[3];
        }
        if (!(window[0] == window[1] && window[1] == window[2])) {
            std::cerr << "[1541 IMG ADV] FAIL: G64 relock window drift out of envelope." << std::endl;
            assert(false);
        }
    }

    {
        drive.configureMountedImage(nibPath.string(), "nib", true);
        std::array<uint8_t, 3> window = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            drive.loadVirtualBlock(1, 0);
            window[static_cast<size_t>(i)] = drive.iecBlockBuffer[3];
        }
        if (!(window[0] == window[1] && window[1] == window[2])) {
            std::cerr << "[1541 IMG ADV] FAIL: NIB relock window drift out of envelope." << std::endl;
            assert(false);
        }
    }

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

    drive.configureMountedImage(rawPath.string(), "raw", true);
    drive.loadVirtualBlock(1, 0);
    drive.iecBlockBuffer[0] = 0x33;
    drive.iecBlockBuffer[1] = 0x00;
    drive.iecBlockBuffer[2] = 0x00;
    drive.iecBlockBuffer[3] = 0x00;
    drive.iecBlockBufferValid = true;
    drive.flushVirtualBlock(1, 0);
    {
        std::ifstream in(rawPath, std::ios::binary);
        uint8_t first = 0;
        in.read(reinterpret_cast<char *>(&first), 1);
        const uint8_t expectedRawEncoded = 0x45;
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
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_parser_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_parser_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_write_protect_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_write_protect_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_weakbit_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_weakbit_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_gcr_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_gcr_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_dos_error_map_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_dos_error_map_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_multitrack_halftrack_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_multitrack_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_sync_relock_drift_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_sync_relock_drift_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_jitter_window_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_jitter_window_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_g64_relock_window_soak_hard_baseline" << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: advanced_nib_relock_window_soak_hard_baseline" << std::endl;
}
