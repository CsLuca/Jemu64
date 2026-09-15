#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace drive1541_physical {

enum class IecEdgeLine : std::uint8_t {
    Atn = 0,
    Clk = 1,
    Data = 2
};

enum class IecEdgeSource : std::uint8_t {
    Host = 0,
    Drive = 1
};

struct IecEdgeEvent {
    std::uint64_t ts = 0;
    IecEdgeLine line = IecEdgeLine::Atn;
    bool level = true;
    IecEdgeSource source = IecEdgeSource::Host;
    std::uint64_t sequence = 0;
};

class IecEdgeQueue {
public:
    void reset() noexcept {
        nextSequence_ = 0;
        queue_.clear();
    }

    void push(std::uint64_t ts, IecEdgeLine line, bool level, IecEdgeSource source) {
        IecEdgeEvent ev;
        ev.ts = ts;
        ev.line = line;
        ev.level = level;
        ev.source = source;
        ev.sequence = nextSequence_;
        nextSequence_++;

        const auto it = std::lower_bound(queue_.begin(), queue_.end(), ev, [](const IecEdgeEvent &a, const IecEdgeEvent &b) {
            if (a.ts != b.ts) {
                return a.ts < b.ts;
            }
            if (a.line != b.line) {
                return static_cast<std::uint8_t>(a.line) < static_cast<std::uint8_t>(b.line);
            }
            if (a.source != b.source) {
                return static_cast<std::uint8_t>(a.source) < static_cast<std::uint8_t>(b.source);
            }
            return a.sequence < b.sequence;
        });
        queue_.insert(it, ev);
    }

    std::vector<IecEdgeEvent> pop_ready(std::uint64_t nowTs) {
        std::vector<IecEdgeEvent> ready;
        while (!queue_.empty() && queue_.front().ts <= nowTs) {
            ready.push_back(queue_.front());
            queue_.erase(queue_.begin());
        }
        return ready;
    }

    std::size_t size() const noexcept {
        return queue_.size();
    }

private:
    std::uint64_t nextSequence_ = 0;
    std::vector<IecEdgeEvent> queue_{};
};

} // namespace drive1541_physical
