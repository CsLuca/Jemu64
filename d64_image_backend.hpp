#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>

#include "image_backend.hpp"

class D64ImageBackend : public IImageBackend {
public:
    explicit D64ImageBackend(const std::string &path)
        : imagePath(path) {
    }

    bool isReady() const override {
        std::ifstream in(imagePath, std::ios::binary);
        return in.is_open();
    }

    const char *formatName() const override {
        return "d64";
    }

    bool readBlock(uint8_t track,
                   uint8_t sector,
                   std::array<uint8_t, 256> &out,
                   ImageIoError &error) const override {
        uint32_t offset = 0;
        if (!trackSectorToOffset(track, sector, offset)) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        std::ifstream in(imagePath, std::ios::binary);
        if (!in.is_open()) {
            error = ImageIoError::NotReady;
            return false;
        }
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in.read(reinterpret_cast<char *>(out.data()), 256);
        if (in.gcount() != 256) {
            error = ImageIoError::IoFailure;
            return false;
        }
        error = ImageIoError::None;
        return true;
    }

    bool writeBlock(uint8_t track,
                    uint8_t sector,
                    const std::array<uint8_t, 256> &in,
                    ImageIoError &error) override {
        uint32_t offset = 0;
        if (!trackSectorToOffset(track, sector, offset)) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        std::fstream io(imagePath, std::ios::in | std::ios::out | std::ios::binary);
        if (!io.is_open()) {
            error = ImageIoError::NotReady;
            return false;
        }
        io.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        io.write(reinterpret_cast<const char *>(in.data()), 256);
        if (io.fail()) {
            error = ImageIoError::IoFailure;
            return false;
        }
        io.flush();
        error = ImageIoError::None;
        return true;
    }

    bool readDirectoryListing(ImageDirectoryListing &listing,
                              ImageIoError &error) const override {
        std::array<uint8_t, 256> bam = {};
        if (!readBlock(18, 0, bam, error)) {
            return false;
        }

        listing = ImageDirectoryListing{};
        listing.diskName = decodeName(&bam[0x90], 16);
        if (listing.diskName.empty()) {
            listing.diskName = "D64 MOUNT";
        }

        uint16_t freeBlocks = 0;
        for (uint8_t t = 1; t <= 35; ++t) {
            const size_t off = 4 + (static_cast<size_t>(t - 1) * 4);
            if (off < bam.size()) {
                freeBlocks = static_cast<uint16_t>(freeBlocks + bam[off]);
            }
        }
        listing.freeBlocks = freeBlocks;

        uint8_t trk = 18;
        uint8_t sec = 1;
        int guard = 0;
        while (trk != 0 && guard < 64) {
            std::array<uint8_t, 256> dirSector = {};
            if (!readBlock(trk, sec, dirSector, error)) {
                return false;
            }
            const uint8_t nextTrk = dirSector[0];
            const uint8_t nextSec = dirSector[1];

            for (int entry = 0; entry < 8; ++entry) {
                const int off = 2 + (entry * 32);
                const uint8_t fileType = dirSector[off + 2];
                if ((fileType & 0x07) == 0) {
                    continue;
                }

                ImageDirectoryEntry row;
                row.name = decodeName(&dirSector[off + 5], 16);
                row.type = fileTypeToString(fileType);
                row.mode.clear();
                row.blocks = static_cast<uint16_t>(dirSector[off + 30] | (uint16_t(dirSector[off + 31]) << 8));
                listing.entries.push_back(row);
            }

            trk = nextTrk;
            sec = nextSec;
            guard++;
        }

        error = ImageIoError::None;
        return true;
    }

private:
    std::string imagePath;

    bool trackSectorToOffset(uint8_t track, uint8_t sector, uint32_t &offset) const {
        if (track < 1 || track > 35) {
            return false;
        }

        static const uint8_t sectorsPerTrack[36] = {
            0,
            21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
            19,19,19,19,19,19,19,
            18,18,18,18,18,18,
            17,17,17,17,17
        };

        const uint8_t spt = sectorsPerTrack[track];
        if (sector >= spt) {
            return false;
        }

        uint32_t sectorsBefore = 0;
        for (uint8_t t = 1; t < track; ++t) {
            sectorsBefore += sectorsPerTrack[t];
        }

        const uint32_t sectorIndex = sectorsBefore + sector;
        offset = sectorIndex * 256u;
        return true;
    }

    static std::string decodeName(const uint8_t *bytes, size_t len) {
        std::string out;
        out.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            uint8_t c = bytes[i];
            if (c == 0x00 || c == 0xA0) {
                break;
            }
            c = static_cast<uint8_t>(c & 0x7F);
            if (c >= 'a' && c <= 'z') {
                c = static_cast<uint8_t>(c - 32);
            }
            if (c >= 32 && c <= 126) {
                out.push_back(static_cast<char>(c));
            }
        }
        return out;
    }

    static std::string fileTypeToString(uint8_t ft) {
        switch (ft & 0x07) {
            case 0x00: return "DEL";
            case 0x01: return "SEQ";
            case 0x02: return "PRG";
            case 0x03: return "USR";
            case 0x04: return "REL";
            default: return "PRG";
        }
    }
};
