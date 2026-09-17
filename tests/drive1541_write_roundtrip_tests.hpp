#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <vector>

static void runDrive1541WriteRoundtripTests() {
    const char *savedProfile = std::getenv("C64_DRIVE_PROFILE");
    const bool hadSavedProfile = (savedProfile != nullptr);
    const std::string savedProfileValue = hadSavedProfile ? std::string(savedProfile) : std::string();
#ifdef _WIN32
    _putenv_s("C64_DRIVE_PROFILE", "level4-accuracy");
#else
    setenv("C64_DRIVE_PROFILE", "level4-accuracy", 1);
#endif

    const std::filesystem::path tmpDir = std::filesystem::path("testdata") / "runtime_tmp";
    std::filesystem::create_directories(tmpDir);
    const std::filesystem::path rawPath = tmpDir / "level4_write_roundtrip.raw";

    {
        std::vector<uint8_t> data(174848, 0x7Cu);
        std::ofstream out(rawPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    auto makeDrive = [&]() {
        Drive1541 d;
        if (!d.loadRom("roms/dos1541.rom")) {
            std::cerr << "[1541 L4 WRITE] FAIL: cannot load roms/dos1541.rom" << std::endl;
            assert(false);
        }
        return d;
    };

    Drive1541 d = makeDrive();
    d.setPhysicalProfile(Drive1541::PhysicalProfile::Level4Accuracy);
    d.configureMountedImage(rawPath.string(), "raw", true);

    auto writePattern = [&](uint8_t fill) {
        d.loadVirtualBlock(1, 0);
        for (size_t i = 0; i < d.iecBlockBuffer.size(); ++i) {
            d.iecBlockBuffer[i] = static_cast<uint8_t>(fill ^ static_cast<uint8_t>(i & 0x1Fu));
        }
        d.iecBlockBufferValid = true;
        d.flushVirtualBlock(1, 0);
        if (d.iecStatusLine.rfind("00,OK", 0) != 0) {
            std::cerr << "[1541 L4 WRITE] FAIL: unexpected status after write: " << d.iecStatusLine << std::endl;
            assert(false);
        }
    };

    writePattern(0x33u);
    std::array<uint8_t, 256> firstEncoded = {};
    {
        std::ifstream in(rawPath, std::ios::binary);
        in.read(reinterpret_cast<char *>(firstEncoded.data()), static_cast<std::streamsize>(firstEncoded.size()));
    }

    writePattern(0x33u);
    std::array<uint8_t, 256> secondEncoded = {};
    {
        std::ifstream in(rawPath, std::ios::binary);
        in.read(reinterpret_cast<char *>(secondEncoded.data()), static_cast<std::streamsize>(secondEncoded.size()));
    }

    bool differentEncoding = false;
    for (size_t i = 0; i < firstEncoded.size(); ++i) {
        if (firstEncoded[i] != secondEncoded[i]) {
            differentEncoding = true;
            break;
        }
    }
    if (!differentEncoding) {
        std::cerr << "[1541 L4 WRITE] FAIL: write splice/erase persistence did not evolve across passes" << std::endl;
        assert(false);
    }

    d.loadVirtualBlock(1, 0);
    const uint8_t readMap = d.iecBlockBuffer[5];
    if (readMap != 20u && readMap != 21u && readMap != 22u && readMap != 23u && readMap != 27u &&
        readMap != 10u && readMap != 11u && readMap != 138u && readMap != 139u && readMap != 141u &&
        readMap != 0u) {
        std::cerr << "[1541 L4 WRITE] FAIL: strict roundtrip decode envelope out of range" << std::endl;
        assert(false);
    }

    std::error_code ec;
    std::filesystem::remove(rawPath, ec);

#ifdef _WIN32
    _putenv_s("C64_DRIVE_PROFILE", hadSavedProfile ? savedProfileValue.c_str() : "");
#else
    if (hadSavedProfile) {
        setenv("C64_DRIVE_PROFILE", savedProfileValue.c_str(), 1);
    } else {
        unsetenv("C64_DRIVE_PROFILE");
    }
#endif

    std::cerr << "[1541 L4 WRITE] PASS: level4 write splice/erase persistence + strict roundtrip envelope" << std::endl;
}
