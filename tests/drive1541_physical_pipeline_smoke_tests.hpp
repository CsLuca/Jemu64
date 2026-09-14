#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static void runDrive1541PhysicalPipelineSmokeTests() {
    const std::filesystem::path tmpDir = std::filesystem::path("testdata") / "runtime_tmp";
    std::filesystem::create_directories(tmpDir);

    auto createLinearImage = [&](const std::filesystem::path &path, uint8_t seed) {
        std::vector<uint8_t> data(174848, seed);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    };

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

    const std::filesystem::path g64Path = tmpDir / "physical_pipeline_smoke.g64";
    const std::filesystem::path nibPath = tmpDir / "physical_pipeline_smoke.nib";
    const std::filesystem::path rawPath = tmpDir / "physical_pipeline_smoke.raw";

    createMinimalG64Image(g64Path, 0x5A);
    createLinearImage(nibPath, 0x6B);
    createLinearImage(rawPath, 0x7C);

    auto makeDrive = [&]() {
        Drive1541 d;
        if (!d.loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 PHYS PIPE] FAIL: cannot load roms/dos1541.rom" << std::endl;
            assert(false);
        }
        return d;
    };

    auto verifyProfileRead = [&](Drive1541::PhysicalProfile profile,
                                 const std::string &format,
                                 const std::filesystem::path &path) {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(profile);
        d.configureMountedImage(path.string(), format, true);
        d.loadVirtualBlock(1, 0);
        if (!d.iecBlockBufferValid) {
            std::cerr << "[1541 PHYS PIPE] FAIL: mounted read failed format=" << format << std::endl;
            assert(false);
        }
        if (profile == Drive1541::PhysicalProfile::Level3Physical) {
            if (d.physicalPipelineRuns == 0 || d.physicalPipelineLastCellTicks == 0) {
                std::cerr << "[1541 PHYS PIPE] FAIL: level3 pipeline did not run format=" << format << std::endl;
                assert(false);
            }
        }
    };

    verifyProfileRead(Drive1541::PhysicalProfile::Level1Functional, "g64", g64Path);
    verifyProfileRead(Drive1541::PhysicalProfile::Level1Functional, "nib", nibPath);
    verifyProfileRead(Drive1541::PhysicalProfile::Level1Functional, "raw", rawPath);
    verifyProfileRead(Drive1541::PhysicalProfile::Level3Physical, "g64", g64Path);
    verifyProfileRead(Drive1541::PhysicalProfile::Level3Physical, "nib", nibPath);
    verifyProfileRead(Drive1541::PhysicalProfile::Level3Physical, "raw", rawPath);

    {
        Drive1541 d1 = makeDrive();
        Drive1541 d3 = makeDrive();
        d1.setPhysicalProfile(Drive1541::PhysicalProfile::Level1Functional);
        d3.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d1.configureMountedImage(rawPath.string(), "raw", true);
        d3.configureMountedImage(rawPath.string(), "raw", true);
        d1.loadVirtualBlock(1, 0);
        d3.loadVirtualBlock(1, 0);
        for (size_t i = 0; i < d1.iecBlockBuffer.size(); ++i) {
            if (d1.iecBlockBuffer[i] != d3.iecBlockBuffer[i]) {
                std::cerr << "[1541 PHYS PIPE] FAIL: level1/level3 base-case mismatch" << std::endl;
                assert(false);
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove(g64Path, ec);
    std::filesystem::remove(nibPath, ec);
    std::filesystem::remove(rawPath, ec);

    std::cerr << "[1541 PHYS PIPE] PASS: level3 end-to-end read pipeline smoke" << std::endl;
}
