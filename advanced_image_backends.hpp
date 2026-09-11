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

enum class GcrGapQuality : uint8_t {
    Poor,
    Marginal,
    Stable
};

enum class GcrErrorClass : uint8_t {
    None,
    Soft,
    Hard
};

struct GcrMetrics {
    uint32_t invalidSymbols = 0;
    uint32_t syncLossEvents = 0;
    GcrGapQuality gapQuality = GcrGapQuality::Poor;
    GcrErrorClass errorClass = GcrErrorClass::None;
};

static inline uint8_t mapMetricsToDosErrorCode(const GcrMetrics &metrics) {
    if (metrics.invalidSymbols >= 8) {
        return 27; // READ ERROR (checksum)
    }
    if (metrics.syncLossEvents >= 2) {
        return 21; // READ ERROR (sync)
    }
    if (metrics.gapQuality == GcrGapQuality::Poor) {
        return 22; // READ ERROR (data block not present)
    }
    if (metrics.invalidSymbols > 0) {
        return 23; // READ ERROR (checksum/data)
    }
    if (metrics.gapQuality == GcrGapQuality::Marginal) {
        return 20; // READ ERROR (header)
    }
    return 0;
}

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

static inline uint8_t encodeNibbleToGcr(uint8_t nibble) {
    static const uint8_t map[16] = {
        0x0A, 0x0B, 0x12, 0x13,
        0x0E, 0x0F, 0x16, 0x17,
        0x09, 0x19, 0x1A, 0x1B,
        0x0D, 0x1D, 0x1E, 0x15
    };
    return map[nibble & 0x0F];
}

static inline bool decodeGcrToNibble(uint8_t symbol, uint8_t &nibble) {
    static const uint8_t rev[32] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0x08,0x00,0x01,0xFF,0x0C,0x04,0x05,
        0xFF,0xFF,0x02,0x03,0xFF,0x0F,0x06,0x07,
        0xFF,0x09,0x0A,0x0B,0xFF,0x0D,0x0E,0xFF
    };
    const uint8_t v = rev[symbol & 0x1F];
    if (v == 0xFF) {
        return false;
    }
    nibble = v;
    return true;
}

static inline void classifyGcrErrors(GcrMetrics &metrics) {
    if (metrics.invalidSymbols >= 5 || metrics.syncLossEvents >= 2) {
        metrics.errorClass = GcrErrorClass::Hard;
    } else if (metrics.invalidSymbols > 0 || metrics.syncLossEvents == 1) {
        metrics.errorClass = GcrErrorClass::Soft;
    } else {
        metrics.errorClass = GcrErrorClass::None;
    }
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

        applyStrictGcrDecodePipeline(track, sector, out);
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
        applyStrictGcrEncodePipeline(track, sector, encoded);
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
    mutable std::array<uint8_t, 36u * 21u> relockClassState = {};
    mutable std::array<uint8_t, 36u * 21u> relockConfidence = {};
    mutable std::array<uint8_t, 36u * 21u> weakWindowPhase = {};
    mutable std::array<uint8_t, 36u * 21u> weakWindowSpanState = {};
    mutable std::array<uint8_t, 36u * 21u> bitcellSlipState = {};

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
            const size_t bitcellPos = std::min<size_t>(6u, buffer.size() - 1);
            const uint8_t zone = advanced_image_detail::zoneFromTrack(track);
            buffer[bitcellPos] = static_cast<uint8_t>(buffer[bitcellPos] ^ static_cast<uint8_t>((zone << 5) | ((track + sector) & 0x1Fu)));

            const size_t idx = modelIndex(track, sector);
            uint8_t &slip = bitcellSlipState[idx];
            slip = static_cast<uint8_t>((slip + 1u + zone) & 0x07u);
            if ((slip % 3u) == 0u) {
                const size_t slipPos = std::min<size_t>(9u + slip, buffer.size() - 1);
                buffer[slipPos] = static_cast<uint8_t>((buffer[slipPos] << 1) | (buffer[slipPos] >> 7));
            }
        }

        if (hasWeakBits) {
            const size_t weakPosBase = static_cast<size_t>((track * 7u + sector * 13u) & 0x3Fu);
            for (size_t k = 0; k < 3; ++k) {
                const size_t pos = std::min<size_t>(weakPosBase + k, buffer.size() - 1);
                const uint8_t noise = static_cast<uint8_t>(weakBitRng() & 0x0Fu);
                buffer[pos] = static_cast<uint8_t>((buffer[pos] & 0xF0u) | noise);
            }

            const size_t idx = modelIndex(track, sector);
            const uint8_t phase = weakWindowPhase[idx]++;
            const uint8_t zone = advanced_image_detail::zoneFromTrack(track);
            uint8_t &spanState = weakWindowSpanState[idx];
            spanState = static_cast<uint8_t>((spanState + 1u + zone) % 5u);
            const uint8_t windowSpan = static_cast<uint8_t>(4u + zone + spanState);
            const size_t windowPos = std::min<size_t>(8u + ((phase % windowSpan) * 2u), buffer.size() - 1);
            const uint8_t driftNibble = static_cast<uint8_t>((phase + (track * 3u) + sector) & 0x0Fu);
            buffer[windowPos] = static_cast<uint8_t>((buffer[windowPos] & 0xF0u) | driftNibble);

            if ((phase % 5u) == 0u) {
                const size_t burstPos = std::min<size_t>(windowPos + 3u, buffer.size() - 1);
                const uint8_t burstMask = static_cast<uint8_t>(0x11u << (zone & 0x03u));
                buffer[burstPos] = static_cast<uint8_t>(buffer[burstPos] ^ burstMask);
            }
        }
    }

    void applyStrictGcrDecodePipeline(uint8_t track,
                                      uint8_t sector,
                                      std::array<uint8_t, 256> &buffer) const {
        advanced_image_detail::GcrMetrics metrics;

        uint32_t maxSyncRun = 0;
        uint32_t currentSyncRun = 0;
        uint32_t maxGapRun = 0;
        uint32_t currentGapRun = 0;
        for (size_t i = 0; i < buffer.size(); ++i) {
            const uint8_t b = buffer[i];
            if (b == 0xFF) {
                currentSyncRun++;
                if (currentSyncRun > maxSyncRun) {
                    maxSyncRun = currentSyncRun;
                }
            } else {
                currentSyncRun = 0;
            }

            if (b == 0x55 || b == 0x00) {
                currentGapRun++;
                if (currentGapRun > maxGapRun) {
                    maxGapRun = currentGapRun;
                }
            } else {
                currentGapRun = 0;
            }
        }

        if (maxSyncRun < 3) {
            metrics.syncLossEvents = 1;
        }
        if (maxGapRun >= 8) {
            metrics.gapQuality = advanced_image_detail::GcrGapQuality::Stable;
        } else if (maxGapRun >= 4) {
            metrics.gapQuality = advanced_image_detail::GcrGapQuality::Marginal;
        } else {
            metrics.gapQuality = advanced_image_detail::GcrGapQuality::Poor;
        }

        std::array<uint8_t, 256> decoded = {};
        for (size_t i = 0; i < decoded.size(); ++i) {
            const uint8_t symHi = static_cast<uint8_t>(buffer[(i * 2u) % buffer.size()] & 0x1F);
            const uint8_t symLo = static_cast<uint8_t>(buffer[(i * 2u + 1u) % buffer.size()] & 0x1F);
            uint8_t hi = 0;
            uint8_t lo = 0;
            if (!advanced_image_detail::decodeGcrToNibble(symHi, hi)) {
                metrics.invalidSymbols++;
                hi = 0;
            }
            if (!advanced_image_detail::decodeGcrToNibble(symLo, lo)) {
                metrics.invalidSymbols++;
                lo = 0;
            }
            decoded[i] = static_cast<uint8_t>((hi << 4) | lo);
        }

        advanced_image_detail::classifyGcrErrors(metrics);

        if (metrics.gapQuality == advanced_image_detail::GcrGapQuality::Poor) {
            decoded[1] = static_cast<uint8_t>(decoded[1] ^ 0x3Cu);
        } else if (metrics.gapQuality == advanced_image_detail::GcrGapQuality::Marginal) {
            decoded[1] = static_cast<uint8_t>(decoded[1] ^ 0x1Cu);
        }

        if (metrics.syncLossEvents > 0) {
            decoded[2] = static_cast<uint8_t>(decoded[2] ^ 0xA5u);
        }

        if (metrics.errorClass == advanced_image_detail::GcrErrorClass::Hard) {
            decoded[3] = 0xEE;
        } else if (metrics.errorClass == advanced_image_detail::GcrErrorClass::Soft) {
            decoded[3] = 0xCC;
        }

        if (metrics.invalidSymbols >= 3u && metrics.syncLossEvents > 0u) {
            decoded[0] = static_cast<uint8_t>(decoded[0] ^ 0x5Au);
            decoded[6] = static_cast<uint8_t>(decoded[6] ^ 0xA5u);
        }

        applyRelockHysteresis(track, sector, decoded[3]);

        decoded[4] = static_cast<uint8_t>((track << 2) ^ sector);
        decoded[5] = advanced_image_detail::mapMetricsToDosErrorCode(metrics);
        buffer = decoded;
    }

    void applyStrictGcrEncodePipeline(uint8_t track,
                                      uint8_t sector,
                                      std::array<uint8_t, 256> &buffer) const {
        std::array<uint8_t, 256> encoded = {};
        for (size_t i = 0; i < buffer.size(); ++i) {
            const uint8_t byte = buffer[i];
            const uint8_t hi = static_cast<uint8_t>((byte >> 4) & 0x0F);
            const uint8_t lo = static_cast<uint8_t>(byte & 0x0F);
            const uint8_t gcrHi = advanced_image_detail::encodeNibbleToGcr(hi);
            const uint8_t gcrLo = advanced_image_detail::encodeNibbleToGcr(lo);
            encoded[i] = static_cast<uint8_t>((gcrHi << 3) ^ gcrLo ^ static_cast<uint8_t>((track + sector) & 0x1F));
        }
        buffer = encoded;
    }

    size_t modelIndex(uint8_t track, uint8_t sector) const {
        const uint8_t t = static_cast<uint8_t>(std::min<uint8_t>(35u, std::max<uint8_t>(1u, track)) - 1u);
        const uint8_t s = static_cast<uint8_t>(sector % 21u);
        return static_cast<size_t>(t) * 21u + static_cast<size_t>(s);
    }

    void applyRelockHysteresis(uint8_t track, uint8_t sector, uint8_t &classByte) const {
        const size_t idx = modelIndex(track, sector);
        uint8_t &last = relockClassState[idx];
        uint8_t &conf = relockConfidence[idx];

        if (last == 0u) {
            last = classByte;
            conf = 2u;
            return;
        }

        if (classByte == last) {
            if (conf < 7u) {
                conf = static_cast<uint8_t>(conf + 1u);
            }
            return;
        }

        if (conf > 0u) {
            conf = static_cast<uint8_t>(conf - 1u);
            classByte = last;
            return;
        }

        last = classByte;
        conf = 1u;
    }
};

class G64ImageBackend : public FluxMappedImageBackend {
public:
    explicit G64ImageBackend(const std::string &path)
        : FluxMappedImageBackend(path, "g64", false, true, true) {
    }

    uint8_t debugTrackSliceTag(uint8_t track) const {
        const TrackSlice *slice = resolveTrackSlice(track);
        if (slice == nullptr || slice->dataSize == 0) {
            return 0;
        }
        return static_cast<uint8_t>(((slice->dataOffset & 0xFFu) ^ (slice->dataSize & 0xFFu)) & 0xFFu);
    }

    bool debugHasHalfTrackSlice(uint8_t track) const {
        if (!ensureParsed() || track < 1) {
            return false;
        }
        const size_t idxHalf = static_cast<size_t>(track - 1u) * 2u;
        return idxHalf < trackSlices.size() && trackSlices[idxHalf].dataSize >= 256;
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

    uint8_t debugTrackStrideTag() const {
        if (!ensureParsed()) {
            return 0;
        }
        return static_cast<uint8_t>(trackSize & 0xFFu);
    }

    bool debugTrackWindowReadable(uint8_t track) const {
        if (!ensureParsed() || track < 1 || track > 35) {
            return false;
        }
        const uint32_t base = static_cast<uint32_t>(track - 1u) * trackSize;
        return (base + trackSize) <= fileBytes.size();
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
