#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct IecKernelCiaAccessEvent {
    std::uint16_t pc = 0;
    std::uint16_t addr = 0;
    std::uint8_t val = 0;
    bool isWrite = false;
};

static bool isIecKernelCiaTapWindow(const std::uint16_t pcNow, const std::uint16_t addr) {
    return (pcNow >= 0xED40 && pcNow <= 0xEED0) &&
           (addr >= 0xDC00 && addr <= 0xDD0F);
}

static void appendIecKernelCiaTapEvent(std::vector<IecKernelCiaAccessEvent> &log,
                                       const std::size_t maxCount,
                                       const std::uint16_t pcNow,
                                       const std::uint16_t addr,
                                       const std::uint8_t val,
                                       const bool isWrite) {
    if (!isIecKernelCiaTapWindow(pcNow, addr) || log.size() >= maxCount) {
        return;
    }
    log.push_back(IecKernelCiaAccessEvent{pcNow, addr, val, isWrite});
}
