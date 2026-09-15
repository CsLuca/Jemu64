#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static void runDrive1541BackendLayeringTests() {
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
        const uint16_t trackLen = 256;
        std::vector<uint8_t> data(firstTrackOffset + 2u + trackLen, 0);

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

        data[firstTrackOffset + 0] = static_cast<uint8_t>(trackLen & 0xFF);
        data[firstTrackOffset + 1] = static_cast<uint8_t>((trackLen >> 8) & 0xFF);
        for (uint16_t i = 0; i < trackLen; ++i) {
            data[firstTrackOffset + 2u + i] = seed;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    };

    const std::filesystem::path g64Path = tmpDir / "backend_layering.g64";
    const std::filesystem::path nibPath = tmpDir / "backend_layering.nib";
    const std::filesystem::path rawPath = tmpDir / "backend_layering.raw";
    const std::filesystem::path d64Path = tmpDir / "backend_layering.d64";

    createMinimalG64Image(g64Path, 0x5A);
    createLinearImage(nibPath, 0x6B);
    createLinearImage(rawPath, 0x7C);
    createLinearImage(d64Path, 0x2D);

    auto makeDrive = [&]() {
        Drive1541 d;
        if (!d.loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: cannot load roms/dos1541.rom" << std::endl;
            assert(false);
        }
        return d;
    };

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level1Functional);
        d.configureMountedImage(g64Path.string(), "g64", true);
        if (!d.isMountedImageBackendActiveFor("g64") || d.isMountedBackendRoutedToFlux()) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: g64 level1 dispatch routing mismatch" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d.configureMountedImage(g64Path.string(), "g64", true);
        if (!d.isMountedImageBackendActiveFor("g64") ||
            !d.isMountedFluxBackendActiveFor("g64") ||
            !d.isMountedBackendRoutedToFlux()) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: g64 level3 dispatch routing mismatch" << std::endl;
            assert(false);
        }
        d.loadVirtualBlock(1, 0);
        if (!d.iecBlockBufferValid) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: g64 mount/read smoke failed" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d.configureMountedImage(nibPath.string(), "nib", true);
        if (!d.isMountedImageBackendActiveFor("nib") ||
            !d.isMountedFluxBackendActiveFor("nib") ||
            !d.isMountedBackendRoutedToFlux()) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: nib level3 dispatch routing mismatch" << std::endl;
            assert(false);
        }
        d.loadVirtualBlock(1, 0);
        if (!d.iecBlockBufferValid) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: nib mount/read smoke failed" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d.configureMountedImage(rawPath.string(), "raw", true);
        if (!d.isMountedImageBackendActiveFor("raw") ||
            !d.isMountedFluxBackendActiveFor("raw") ||
            !d.isMountedBackendRoutedToFlux()) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: raw level3 dispatch routing mismatch" << std::endl;
            assert(false);
        }
        d.loadVirtualBlock(1, 0);
        if (!d.iecBlockBufferValid) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: raw mount/read smoke failed" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d.configureMountedImage(d64Path.string(), "d64", true);
        d.loadVirtualBlock(1, 0);
        if (!d.isMountedImageBackendActiveFor("d64") ||
            d.isMountedFluxBackendActiveFor("d64") ||
            d.isMountedBackendRoutedToFlux()) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: d64 routing regression" << std::endl;
            assert(false);
        }
        if (!d.iecBlockBufferValid || d.iecBlockBuffer[0] != 0x2D) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: d64 read regression" << std::endl;
            assert(false);
        }
    }

    {
        Drive1541 d = makeDrive();
        d.setPhysicalProfile(Drive1541::PhysicalProfile::Level3Physical);
        d.configureMountedImage(g64Path.string(), "g64", false);
        if (d.isMountedImageBackendActiveFor("g64") || d.isMountedFluxBackendActiveFor("g64")) {
            std::cerr << "[1541 BACKEND LAYER] FAIL: fallback routing should stay inactive when mount is unavailable" << std::endl;
            assert(false);
        }
    }

    std::error_code ec;
    std::filesystem::remove(g64Path, ec);
    std::filesystem::remove(nibPath, ec);
    std::filesystem::remove(rawPath, ec);
    std::filesystem::remove(d64Path, ec);

    std::cerr << "[1541 BACKEND LAYER] PASS: logical/flux layered routing and smoke checks" << std::endl;
}
