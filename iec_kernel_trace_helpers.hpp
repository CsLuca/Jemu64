#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

struct IecKernelDd00LoopEvent {
    std::uint32_t step = 0;
    std::uint16_t pc = 0;
    std::uint8_t dd00 = 0;
    std::uint8_t dd0d = 0;
    bool lineCLK = true;
    bool lineDATA = true;
    bool lineATN = true;
    bool txActive = false;
    std::uint8_t txBit = 0;
    bool eoiPending = false;
    std::uint16_t txq = 0;
    std::uint64_t txServed = 0;
};

static bool shouldRecordIecKernelDd00LoopEvent(const std::uint16_t pc,
                                               const bool dd00Changed,
                                               const bool txProgress,
                                               const bool trackTxSequencer) {
    const bool inHotLoop = (pc == 0xEE1B || pc == 0xEE1E || pc == 0xEEAF || pc == 0xED5D || pc == 0xED5E);
    return inHotLoop || dd00Changed || txProgress || trackTxSequencer;
}

static void appendIecKernelDd00LoopEvent(std::vector<IecKernelDd00LoopEvent> &events,
                                         const std::size_t maxEvents,
                                         const IecKernelDd00LoopEvent &ev) {
    if (events.size() >= maxEvents) {
        return;
    }
    events.push_back(ev);
}

static void dumpIecKernelDd00LoopEvents(std::ostream &out,
                                        const std::vector<IecKernelDd00LoopEvent> &events,
                                        const std::size_t tailCount = 24) {
    if (events.empty()) {
        return;
    }
    out << " dd00_loop=";
    const std::size_t startIdx = (events.size() > tailCount) ? (events.size() - tailCount) : 0;
    for (std::size_t ei = startIdx; ei < events.size(); ++ei) {
        const IecKernelDd00LoopEvent &ev = events[ei];
        out << (ei == startIdx ? "" : "|")
            << "$" << std::hex << ev.pc
            << ":$" << static_cast<int>(ev.dd00)
            << ",icr=$" << static_cast<int>(ev.dd0d)
            << ",clk=" << (ev.lineCLK ? 1 : 0)
            << ",dat=" << (ev.lineDATA ? 1 : 0)
            << ",atn=" << (ev.lineATN ? 1 : 0)
            << ",txa=" << (ev.txActive ? 1 : 0)
            << ",tb=" << std::dec << static_cast<int>(ev.txBit)
            << ",eoi=" << (ev.eoiPending ? 1 : 0)
            << ",q=" << ev.txq
            << ",tx=" << ev.txServed;
    }
}
