#pragma once

#include <array>
#include <string>

#include "../advanced_image_backends.hpp"
#include "i_flux_image_backend.hpp"

class G64FluxImageBackend : public IFluxImageBackend {
public:
    explicit G64FluxImageBackend(const std::string &path)
        : backend_(path) {
    }

    bool isReady() const override {
        return backend_.isReady();
    }

    const char *formatName() const override {
        return backend_.formatName();
    }

    bool readBlock(uint8_t track,
                   uint8_t sector,
                   std::array<uint8_t, 256> &out,
                   ImageIoError &error) const override {
        return backend_.readBlock(track, sector, out, error);
    }

    bool writeBlock(uint8_t track,
                    uint8_t sector,
                    const std::array<uint8_t, 256> &in,
                    ImageIoError &error) override {
        return backend_.writeBlock(track, sector, in, error);
    }

    bool readDirectoryListing(ImageDirectoryListing &listing,
                              ImageIoError &error) const override {
        return backend_.readDirectoryListing(listing, error);
    }

private:
    G64ImageBackend backend_;
};
