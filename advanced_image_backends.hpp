#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

#include "image_backend.hpp"

class LinearChsImageBackend : public IImageBackend {
public:
    LinearChsImageBackend(const std::string &path, const std::string &format, bool writable)
        : imagePath(path), imageFormat(format), canWrite(writable) {
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
        if (!canWrite) {
            error = ImageIoError::IoFailure;
            return false;
        }
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
        (void)listing;
        error = ImageIoError::IoFailure;
        return false;
    }

private:
    std::string imagePath;
    std::string imageFormat;
    bool canWrite = false;

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
};

class G64ImageBackend : public LinearChsImageBackend {
public:
    explicit G64ImageBackend(const std::string &path)
        : LinearChsImageBackend(path, "g64", false) {
    }
};

class NIBImageBackend : public LinearChsImageBackend {
public:
    explicit NIBImageBackend(const std::string &path)
        : LinearChsImageBackend(path, "nib", false) {
    }
};

class RAWImageBackend : public LinearChsImageBackend {
public:
    explicit RAWImageBackend(const std::string &path)
        : LinearChsImageBackend(path, "raw", true) {
    }
};
