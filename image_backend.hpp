#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class ImageIoError {
    None,
    NotReady,
    InvalidAddress,
    WriteProtected,
    IoFailure
};

struct ImageDirectoryEntry {
    std::string name;
    std::string type;
    std::string mode;
    uint16_t blocks = 0;
};

struct ImageDirectoryListing {
    std::string diskName;
    uint16_t freeBlocks = 0;
    std::vector<ImageDirectoryEntry> entries;
};

class IImageBackend {
public:
    virtual ~IImageBackend() {}

    virtual bool isReady() const = 0;
    virtual const char *formatName() const = 0;

    virtual bool readBlock(uint8_t track,
                           uint8_t sector,
                           std::array<uint8_t, 256> &out,
                           ImageIoError &error) const = 0;

    virtual bool writeBlock(uint8_t track,
                            uint8_t sector,
                            const std::array<uint8_t, 256> &in,
                            ImageIoError &error) = 0;

    virtual bool readDirectoryListing(ImageDirectoryListing &listing,
                                      ImageIoError &error) const = 0;
};
