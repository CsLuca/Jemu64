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
        if (!readRawBlock(track, sector, out, error)) {
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

        std::array<uint8_t, 256> encoded = in;
        applyTrackZoneMapping(track, encoded);
        return writeRawBlock(track, sector, encoded, error);
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

protected:
    const std::string &path() const {
        return imagePath;
    }

    bool readRawBlock(uint8_t track,
                      uint8_t sector,
                      std::array<uint8_t, 256> &out,
                      ImageIoError &error) const {
        return readRawBlockImpl(track, sector, out, error);
    }

    bool writeRawBlock(uint8_t track,
                       uint8_t sector,
                       const std::array<uint8_t, 256> &in,
                       ImageIoError &error) {
        return writeRawBlockImpl(track, sector, in, error);
    }

    virtual bool readRawBlockImpl(uint8_t track,
                                  uint8_t sector,
                                  std::array<uint8_t, 256> &out,
                                  ImageIoError &error) const {
        uint32_t offset = 0;
        if (!advanced_image_detail::isValidChsAddress(track, sector, offset)) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        std::ifstream in(path(), std::ios::binary);
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

    virtual bool writeRawBlockImpl(uint8_t track,
                                   uint8_t sector,
                                   const std::array<uint8_t, 256> &in,
                                   ImageIoError &error) {
        uint32_t offset = 0;
        if (!advanced_image_detail::isValidChsAddress(track, sector, offset)) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        std::fstream io(path(), std::ios::in | std::ios::out | std::ios::binary);
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

private:
    struct TrackSlice {
        uint32_t dataOffset = 0;
        uint16_t dataSize = 0;
    };

    mutable bool parsed = false;
    mutable bool parseOk = false;
    mutable std::vector<uint8_t> fileBytes;
    mutable std::vector<TrackSlice> trackSlices;

    bool readRawBlockImpl(uint8_t track,
                          uint8_t sector,
                          std::array<uint8_t, 256> &out,
                          ImageIoError &error) const override {
        if (!ensureParsed()) {
            error = ImageIoError::IoFailure;
            return false;
        }

        const TrackSlice *slice = resolveTrackSlice(track);
        if (slice == nullptr || slice->dataSize < 256) {
            error = ImageIoError::InvalidAddress;
            return false;
        }

        const uint32_t span = static_cast<uint32_t>(slice->dataSize);
        const uint32_t start = (static_cast<uint32_t>(sector) * 256u) % span;
        for (size_t i = 0; i < out.size(); ++i) {
            const uint32_t idx = static_cast<uint32_t>((start + static_cast<uint32_t>(i)) % span);
            out[i] = fileBytes[slice->dataOffset + idx];
        }

        error = ImageIoError::None;
        return true;
    }

    bool ensureParsed() const {
        if (parsed) {
            return parseOk;
        }
        parsed = true;
        parseOk = false;

        std::ifstream in(path(), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        fileBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (fileBytes.size() < 0x10) {
            return false;
        }

        static const char kSig[8] = {'G', 'C', 'R', '-', '1', '5', '4', '1'};
        if (!std::equal(kSig, kSig + 8, fileBytes.begin())) {
            return false;
        }

        const uint8_t trackCount = fileBytes[0x09];
        if (trackCount == 0 || trackCount > 84) {
            return false;
        }

        const size_t tableOffset = 0x0C;
        const size_t tableBytes = static_cast<size_t>(trackCount) * 4u;
        if (fileBytes.size() < tableOffset + tableBytes) {
            return false;
        }

        trackSlices.assign(trackCount, TrackSlice{});
        for (size_t i = 0; i < trackCount; ++i) {
            const size_t o = tableOffset + i * 4u;
            const uint32_t trackOffset = static_cast<uint32_t>(fileBytes[o]) |
                                         (static_cast<uint32_t>(fileBytes[o + 1]) << 8) |
                                         (static_cast<uint32_t>(fileBytes[o + 2]) << 16) |
                                         (static_cast<uint32_t>(fileBytes[o + 3]) << 24);
            if (trackOffset == 0 || (trackOffset + 2u) > fileBytes.size()) {
                continue;
            }
            const uint16_t trackLen = static_cast<uint16_t>(fileBytes[trackOffset]) |
                                      (static_cast<uint16_t>(fileBytes[trackOffset + 1]) << 8);
            const uint32_t payloadOffset = trackOffset + 2u;
            if (trackLen < 256 || (payloadOffset + trackLen) > fileBytes.size()) {
                continue;
            }
            trackSlices[i].dataOffset = payloadOffset;
            trackSlices[i].dataSize = trackLen;
        }

        parseOk = true;
        return true;
    }

    const TrackSlice *resolveTrackSlice(uint8_t track) const {
        if (trackSlices.empty()) {
            return nullptr;
        }
        const size_t idxHalf = static_cast<size_t>(track - 1u) * 2u;
        if (idxHalf < trackSlices.size() && trackSlices[idxHalf].dataSize >= 256) {
            return &trackSlices[idxHalf];
        }
        const size_t idxWhole = static_cast<size_t>(track - 1u);
        if (idxWhole < trackSlices.size() && trackSlices[idxWhole].dataSize >= 256) {
            return &trackSlices[idxWhole];
        }
        return nullptr;
    }
};

class NIBImageBackend : public FluxMappedImageBackend {
public:
    explicit NIBImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "nib", false, true, true) {
    }

private:
    mutable bool parsed = false;
    mutable bool parseOk = false;
    mutable uint32_t trackSize = 0x2000u;
    mutable std::vector<uint8_t> fileBytes;

    bool readRawBlockImpl(uint8_t track,
                          uint8_t sector,
                          std::array<uint8_t, 256> &out,
                          ImageIoError &error) const override {
        if (!ensureParsed()) {
            error = ImageIoError::IoFailure;
            return false;
        }
        if (track < 1 || track > 35) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        const uint32_t base = static_cast<uint32_t>(track - 1u) * trackSize;
        if (base + trackSize > fileBytes.size()) {
            error = ImageIoError::InvalidAddress;
            return false;
        }
        const uint32_t start = (static_cast<uint32_t>(sector) * 256u) % trackSize;
        for (size_t i = 0; i < out.size(); ++i) {
            const uint32_t idx = static_cast<uint32_t>((start + static_cast<uint32_t>(i)) % trackSize);
            out[i] = fileBytes[base + idx];
        }
        error = ImageIoError::None;
        return true;
    }

    bool ensureParsed() const {
        if (parsed) {
            return parseOk;
        }
        parsed = true;
        parseOk = false;

        std::ifstream in(path(), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }
        fileBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (fileBytes.size() < (35u * 256u)) {
            return false;
        }

        if ((fileBytes.size() % 35u) == 0u) {
            trackSize = static_cast<uint32_t>(fileBytes.size() / 35u);
        } else {
            trackSize = 0x2000u;
        }
        if (trackSize < 256u) {
            return false;
        }
        parseOk = true;
        return true;
    }
};

class RAWImageBackend : public FluxMappedImageBackend {
public:
    explicit RAWImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "raw", true, false, true) {
    }
};
