#pragma once

#include <array>
#include <iostream>
#include <vector>

#include "../drive1541_physical/gcr_codec.hpp"

static void runDrive1541GcrCodecTests() {
    using drive1541_physical::GcrCodec;

    const GcrCodec codec;

    {
        const std::array<std::uint8_t, 8> payload = {0x00u, 0x11u, 0x2Au, 0x3Fu, 0x55u, 0xA5u, 0xC3u, 0xFEu};
        const std::vector<std::uint8_t> encoded = codec.encode_4to5(payload.data(), payload.size());
        const auto decoded = codec.decode_5to4(encoded.data(), encoded.size());
        if (!decoded.ok || decoded.sync_found || decoded.data.size() != payload.size()) {
            std::cerr << "[1541 GCR CODEC] FAIL: roundtrip metadata mismatch" << std::endl;
            assert(false);
        }
        for (std::size_t i = 0; i < payload.size(); ++i) {
            if (decoded.data[i] != payload[i]) {
                std::cerr << "[1541 GCR CODEC] FAIL: roundtrip byte mismatch at idx=" << i << std::endl;
                assert(false);
            }
        }
    }

    {
        const std::array<std::uint8_t, 3> payload = {0x12u, 0x34u, 0xABu};
        std::vector<std::uint8_t> stream = codec.encode_4to5(payload.data(), payload.size());
        stream.insert(stream.begin(), 0xFFu);
        stream.push_back(0xFFu);
        const auto decoded = codec.decode_5to4(stream.data(), stream.size());
        if (!decoded.ok || !decoded.sync_found || decoded.data.size() != payload.size()) {
            std::cerr << "[1541 GCR CODEC] FAIL: sync mark detection mismatch" << std::endl;
            assert(false);
        }
        if (decoded.sync_loss_events < 1u) {
            std::cerr << "[1541 GCR CODEC] FAIL: sync loss event counter not updated" << std::endl;
            assert(false);
        }
    }

    {
        const std::uint8_t invalidStream[2] = {0x00u, 0x0Au};
        const auto decoded = codec.decode_5to4(invalidStream, 2);
        if (decoded.ok) {
            std::cerr << "[1541 GCR CODEC] FAIL: invalid symbol accepted" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 GCR CODEC] PASS: roundtrip + sync marks + invalid symbol handling" << std::endl;
}
