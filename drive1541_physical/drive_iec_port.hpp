#pragma once

#include <cstddef>
#include <cstdint>

#include "iec_edge_queue.hpp"

namespace drive1541_physical {

struct IecLines {
    bool atn = true;
    bool clk = true;
    bool data = true;
};

class DriveIecPort {
public:
    void reset() noexcept {
        in_ = IecLines{};
        out_ = IecLines{};
        inputEdges_.reset();
        outputEdges_.reset();
        hostLatched_ = in_;
        driveLatched_ = out_;
    }

    void queueHostLines(std::uint64_t ts, const IecLines &lines) {
        queueLineChange(inputEdges_, ts, IecEdgeSource::Host, IecEdgeLine::Atn, hostLatched_.atn, lines.atn);
        queueLineChange(inputEdges_, ts, IecEdgeSource::Host, IecEdgeLine::Clk, hostLatched_.clk, lines.clk);
        queueLineChange(inputEdges_, ts, IecEdgeSource::Host, IecEdgeLine::Data, hostLatched_.data, lines.data);
    }

    void queueDriveLines(std::uint64_t ts, const IecLines &lines) {
        queueLineChange(outputEdges_, ts, IecEdgeSource::Drive, IecEdgeLine::Atn, driveLatched_.atn, lines.atn);
        queueLineChange(outputEdges_, ts, IecEdgeSource::Drive, IecEdgeLine::Clk, driveLatched_.clk, lines.clk);
        queueLineChange(outputEdges_, ts, IecEdgeSource::Drive, IecEdgeLine::Data, driveLatched_.data, lines.data);
    }

    void setBusLines(const IecLines &lines) noexcept {
        hostLatched_ = lines;
        in_ = lines;
    }

    void setDriveOutput(const IecLines &lines) noexcept {
        driveLatched_ = lines;
        out_ = lines;
    }

    std::size_t applyReady(std::uint64_t nowTs, bool allowDriveOutput) {
        const std::vector<IecEdgeEvent> inEvents = inputEdges_.pop_ready(nowTs);
        applyEvents(inEvents, in_);
        std::size_t applied = inEvents.size();
        if (allowDriveOutput) {
            const std::vector<IecEdgeEvent> outEvents = outputEdges_.pop_ready(nowTs);
            applyEvents(outEvents, out_);
            applied += outEvents.size();
        }
        return applied;
    }

    IecLines busDriveOutput() const noexcept {
        return out_;
    }

    IecLines busInput() const noexcept {
        return in_;
    }

    std::size_t pendingInputEdges() const noexcept {
        return inputEdges_.size();
    }

    std::size_t pendingOutputEdges() const noexcept {
        return outputEdges_.size();
    }

    void dropPendingDriveEdges() noexcept {
        outputEdges_.reset();
    }

    void dropPendingHostEdges() noexcept {
        inputEdges_.reset();
    }

private:
    static void queueLineChange(IecEdgeQueue &q,
                                std::uint64_t ts,
                                IecEdgeSource source,
                                IecEdgeLine line,
                                bool &latched,
                                bool newLevel) {
        if (latched == newLevel) {
            return;
        }
        latched = newLevel;
        q.push(ts, line, newLevel, source);
    }

    static void applyEvents(const std::vector<IecEdgeEvent> &events, IecLines &lines) {
        for (const IecEdgeEvent &ev : events) {
            switch (ev.line) {
                case IecEdgeLine::Atn: lines.atn = ev.level; break;
                case IecEdgeLine::Clk: lines.clk = ev.level; break;
                case IecEdgeLine::Data: lines.data = ev.level; break;
            }
        }
    }

    IecLines in_{};
    IecLines out_{};
    IecEdgeQueue inputEdges_{};
    IecEdgeQueue outputEdges_{};
    IecLines hostLatched_{};
    IecLines driveLatched_{};
};

} // namespace drive1541_physical
