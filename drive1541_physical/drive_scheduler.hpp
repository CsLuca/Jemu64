#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

namespace drive1541_physical {

enum class DriveSchedulerEventType : std::uint8_t {
    None = 0,
    IecEdge = 1,
    CpuTick = 2,
    ViaTick = 3,
    Custom = 255
};

struct DriveSchedulerEvent {
    std::uint64_t timestamp = 0;
    DriveSchedulerEventType eventType = DriveSchedulerEventType::None;
    std::uint32_t payloadId = 0;
    std::uint64_t sequence = 0;
};

class DriveScheduler {
public:
    void reset() noexcept {
        hostTicks_ = 0;
        driveCycles_ = 0;
        ratioNumerator_ = 1;
        ratioDenominator_ = 1;
        ratioRemainder_ = 0;
        nextSequence_ = 0;
        queue_.clear();
    }

    void tickHostCycles(std::uint64_t hostCycles) noexcept {
        hostTicks_ += hostCycles;
        const std::uint64_t scaled = hostCycles * ratioNumerator_ + ratioRemainder_;
        driveCycles_ += (scaled / ratioDenominator_);
        ratioRemainder_ = (scaled % ratioDenominator_);
    }

    void setRateRatio(std::uint64_t numerator, std::uint64_t denominator) noexcept {
        ratioNumerator_ = (numerator == 0) ? 1 : numerator;
        ratioDenominator_ = (denominator == 0) ? 1 : denominator;
        ratioRemainder_ = 0;
    }

    void scheduleAt(std::uint64_t ts, DriveSchedulerEventType eventType, std::uint32_t payloadId) {
        DriveSchedulerEvent ev;
        ev.timestamp = ts;
        ev.eventType = eventType;
        ev.payloadId = payloadId;
        ev.sequence = nextSequence_;
        nextSequence_++;
        queue_.push_back(ev);
        std::stable_sort(queue_.begin(), queue_.end(), [](const DriveSchedulerEvent &a, const DriveSchedulerEvent &b) {
            if (a.timestamp != b.timestamp) {
                return a.timestamp < b.timestamp;
            }
            return a.sequence < b.sequence;
        });
    }

    void schedule_at(std::uint64_t ts, DriveSchedulerEventType eventType, std::uint32_t payloadId) {
        scheduleAt(ts, eventType, payloadId);
    }

    std::vector<DriveSchedulerEvent> runUntil(std::uint64_t ts) {
        std::vector<DriveSchedulerEvent> out;
        while (!queue_.empty() && queue_.front().timestamp <= ts) {
            out.push_back(queue_.front());
            queue_.erase(queue_.begin());
        }
        if (driveCycles_ < ts) {
            driveCycles_ = ts;
        }
        return out;
    }

    void runUntil(std::uint64_t ts, const std::function<void(const DriveSchedulerEvent &)> &consume) {
        const std::vector<DriveSchedulerEvent> events = runUntil(ts);
        for (const DriveSchedulerEvent &ev : events) {
            consume(ev);
        }
    }

    std::vector<DriveSchedulerEvent> run_until(std::uint64_t ts) {
        return runUntil(ts);
    }

    std::uint64_t hostTicks() const noexcept {
        return hostTicks_;
    }

    std::uint64_t driveCycles() const noexcept {
        return driveCycles_;
    }

    std::uint64_t now() const noexcept {
        return driveCycles_;
    }

    std::size_t queuedEvents() const noexcept {
        return queue_.size();
    }

private:
    std::uint64_t hostTicks_ = 0;
    std::uint64_t driveCycles_ = 0;
    std::uint64_t ratioNumerator_ = 1;
    std::uint64_t ratioDenominator_ = 1;
    std::uint64_t ratioRemainder_ = 0;
    std::uint64_t nextSequence_ = 0;
    std::vector<DriveSchedulerEvent> queue_{};
};

} // namespace drive1541_physical
