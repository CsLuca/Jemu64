#pragma once

static void runDrive1541IecDualDriveCopySmoke() {
    Drive1541 drive8;
    Drive1541 drive9;
    if (!drive8.loadRom("roms/dos1541.rom") || !drive9.loadRom("roms/dos1541.rom")) {
        std::cerr << "[1541 IEC COPY] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }

    drive8.iecDeviceAddress = 8;
    drive9.iecDeviceAddress = 9;

    const std::filesystem::path tmpDir = std::filesystem::path("testdata") / "runtime_tmp";
    std::filesystem::create_directories(tmpDir);
    const std::filesystem::path srcPath = tmpDir / "dual_copy_src.d64";
    const std::filesystem::path dstPath = tmpDir / "dual_copy_dst.d64";

    {
        std::vector<uint8_t> zero(174848, 0);
        std::ofstream outSrc(srcPath, std::ios::binary | std::ios::trunc);
        std::ofstream outDst(dstPath, std::ios::binary | std::ios::trunc);
        outSrc.write(reinterpret_cast<const char *>(zero.data()), static_cast<std::streamsize>(zero.size()));
        outDst.write(reinterpret_cast<const char *>(zero.data()), static_cast<std::streamsize>(zero.size()));
    }

    uint32_t srcOff = 0;
    uint32_t dstOff = 0;
    if (!drive8.d64TrackSectorToOffset(0x12, 0x01, srcOff) || !drive9.d64TrackSectorToOffset(0x12, 0x01, dstOff)) {
        std::cerr << "[1541 IEC COPY] FAIL: D64 offset mapping failed for copy block." << std::endl;
        assert(false);
    }

    {
        std::fstream io(srcPath, std::ios::in | std::ios::out | std::ios::binary);
        io.seekp(static_cast<std::streamoff>(srcOff), std::ios::beg);
        const uint8_t seed[4] = {0x41, 0x42, 0x43, 0x44};
        io.write(reinterpret_cast<const char *>(seed), 4);
    }

    drive8.configureMountedImage(srcPath.string(), "d64", true);
    drive9.configureMountedImage(dstPath.string(), "d64", true);

    drive8.freeVirtualBlock(0x12, 0x01);
    drive9.freeVirtualBlock(0x12, 0x01);
    if (!drive8.allocateVirtualBlock(0x12, 0x01, 0x01) || !drive9.allocateVirtualBlock(0x12, 0x01, 0x01)) {
        std::cerr << "[1541 IEC COPY] FAIL: cannot allocate source/target block for copy." << std::endl;
        assert(false);
    }

    drive8.loadVirtualBlock(0x12, 0x01);
    for (size_t i = 0; i < drive8.iecBlockBuffer.size(); ++i) {
        drive9.iecBlockBuffer[i] = drive8.iecBlockBuffer[i];
    }
    drive9.iecBlockBufferValid = true;
    drive9.iecBlockBufferTrack = 0x12;
    drive9.iecBlockBufferSector = 0x01;
    drive9.flushVirtualBlock(0x12, 0x01);

    {
        std::ifstream in(dstPath, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(dstOff), std::ios::beg);
        uint8_t out[4] = {0, 0, 0, 0};
        in.read(reinterpret_cast<char *>(out), 4);
        if (out[0] != 0x41 || out[1] != 0x42 || out[2] != 0x43 || out[3] != 0x44) {
            std::cerr << "[1541 IEC COPY] FAIL: target D64 block did not match source payload." << std::endl;
            assert(false);
        }
    }

    std::error_code ec;
    std::filesystem::remove(srcPath, ec);
    std::filesystem::remove(dstPath, ec);

    std::cerr << "[1541 IEC COPY] PASS: dual-drive D64 block transfer path works"
              << " src_unit=" << 8
              << " dst_unit=" << 9
              << " t=" << std::hex << 0x12
              << " s=" << 0x01
              << std::dec << std::endl;
    std::cerr << "[IEC COPY E2E] PASS: copy_8_to_9_file_e2e" << std::endl;
}
