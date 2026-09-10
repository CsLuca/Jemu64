#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "image_backend.hpp"

namespace advanced_image_detail {

static inline bool isValidChsAddress(uint8_t track, uint8_t sector, uint32_t &offset) {
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
    offset = (sectorsBefore + sector) * 256u;
    return true;
}

static inline uint8_t zoneFromTrack(uint8_t track) {
    if (track <= 17) return 0;
    if (track <= 24) return 1;
    if (track <= 30) return 2;
    return 3;
}

static inline uint8_t rotr8(uint8_t v, uint8_t n) {
    const uint8_t shift = static_cast<uint8_t>(n & 7);
    return static_cast<uint8_t>((v >> shift) | (v << ((8 - shift) & 7)));
}

} // namespace advanced_image_detail

class FluxMappedImageBackend : public IImageBackend {
public:
    FluxMappedImageBackend(const std::string &path,
                           const std::string &format,
                           bool writable,
                           bool weakBits,
                           bool syncLossModel)
        : imagePath(path),
          imageFormat(format),
          canWrite(writable),
          hasWeakBits(weakBits),
          hasSyncLossModel(syncLossModel),
          weakBitRng(0x1541D15Cull) {
    }

    bool isReady() const override {
        std::ifstream in(imagePath, std::ios::binary);
        return in.is_open();
    }

    const char *formatName() const override {
        return imageFormat.c_str();
    }

    bool readBlock(uint8_t track,
                   uint8_t sector,
                   std::array<uint8_t, 256> &out,
                   ImageIoError &error) const override {
        uint32_t offset = 0;
        if (!advanced_image_detail::isValidChsAddress(track, sector, offset)) {
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

        applyTrackZoneMapping(track, out);
        applySyncAndWeakBitModel(track, sector, out);

        error = ImageIoError::None;
        return true;
    }

    bool writeBlock(uint8_t track,
                    uint8_t sector,
                    const std::array<uint8_t, 256> &in,
                    ImageIoError &error) override {
        if (!canWrite) {
            error = ImageIoError::WriteProtected;
            return false;
        }

        uint32_t offset = 0;
        if (!advanced_image_detail::isValidChsAddress(track, sector, offset)) {
            error = ImageIoError::InvalidAddress;
            return false;
        }

        std::array<uint8_t, 256> encoded = in;
        applyTrackZoneMapping(track, encoded);

        std::fstream io(imagePath, std::ios::in | std::ios::out | std::ios::binary);
        if (!io.is_open()) {
            error = ImageIoError::NotReady;
            return false;
        }
        io.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        io.write(reinterpret_cast<const char *>(encoded.data()), 256);
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
        (void)listing;
        error = ImageIoError::IoFailure;
        return false;
    }

private:
    std::string imagePath;
    std::string imageFormat;
    bool canWrite = false;
    bool hasWeakBits = false;
    bool hasSyncLossModel = false;
    mutable std::mt19937 weakBitRng;

    void applyTrackZoneMapping(uint8_t track, std::array<uint8_t, 256> &buffer) const {
        const uint8_t zone = advanced_image_detail::zoneFromTrack(track);
        const uint8_t rot = static_cast<uint8_t>(zone + 1);
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = advanced_image_detail::rotr8(buffer[i], rot);
        }
    }

    void applySyncAndWeakBitModel(uint8_t track, uint8_t sector, std::array<uint8_t, 256> &buffer) const {
        if (hasSyncLossModel) {
            const size_t syncMark = static_cast<size_t>((track + sector) & 0x1F);
            const size_t syncPos = std::min<size_t>(syncMark, buffer.size() - 1);
            buffer[syncPos] = static_cast<uint8_t>(buffer[syncPos] ^ 0xFFu);
        }

        if (hasWeakBits) {
            const size_t weakPosBase = static_cast<size_t>((track * 7u + sector * 13u) & 0x3Fu);
            for (size_t k = 0; k < 3; ++k) {
                const size_t pos = std::min<size_t>(weakPosBase + k, buffer.size() - 1);
                const uint8_t noise = static_cast<uint8_t>(weakBitRng() & 0x0Fu);
                buffer[pos] = static_cast<uint8_t>((buffer[pos] & 0xF0u) | noise);
            }
        }
    }
};

class G64ImageBackend : public FluxMappedImageBackend {
public:
    explicit G64ImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "g64", false, true, true) {
    }
};

class NIBImageBackend : public FluxMappedImageBackend {
public:
    explicit NIBImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "nib", false, true, true) {
    }
};

class RAWImageBackend : public FluxMappedImageBackend {
public:
    explicit RAWImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "raw", true, false, true) {
    }
};
