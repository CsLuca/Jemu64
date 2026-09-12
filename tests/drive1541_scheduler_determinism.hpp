#pragma once

#include <iostream>
#include <vector>

#include "../drive1541_physical/drive_scheduler.hpp"

static void runDrive1541SchedulerDeterminismSmoke() {
    using drive1541_physical::DriveScheduler;
    using drive1541_physical::DriveSchedulerEvent;
    using drive1541_physical::DriveSchedulerEventType;

    auto runTrace = []() {
        DriveScheduler sched;
        sched.reset();
        sched.setRateRatio(1, 1);
        sched.tickHostCycles(4);

        sched.scheduleAt(5, DriveSchedulerEventType::IecEdge, 100);
        sched.scheduleAt(5, DriveSchedulerEventType::ViaTick, 200);
        sched.scheduleAt(3, DriveSchedulerEventType::CpuTick, 300);
        sched.scheduleAt(5, DriveSchedulerEventType::Custom, 201);
        sched.scheduleAt(8, DriveSchedulerEventType::IecEdge, 400);

        return sched.runUntil(8);
    };

    const std::vector<DriveSchedulerEvent> traceA = runTrace();
    const std::vector<DriveSchedulerEvent> traceB = runTrace();

    if (traceA.size() != traceB.size()) {
        std::cerr << "[1541 SCHED] FAIL: trace size mismatch" << std::endl;
        assert(false);
    }

    for (size_t i = 0; i < traceA.size(); ++i) {
        const DriveSchedulerEvent &a = traceA[i];
        const DriveSchedulerEvent &b = traceB[i];
        if (a.timestamp != b.timestamp ||
            a.eventType != b.eventType ||
            a.payloadId != b.payloadId ||
            a.sequence != b.sequence) {
            std::cerr << "[1541 SCHED] FAIL: non-deterministic event ordering" << std::endl;
            assert(false);
        }
    }

    if (traceA.size() < 4) {
        std::cerr << "[1541 SCHED] FAIL: insufficient event trace length" << std::endl;
        assert(false);
    }

    if (traceA[0].timestamp != 3 || traceA[0].payloadId != 300) {
        std::cerr << "[1541 SCHED] FAIL: earliest event ordering mismatch" << std::endl;
        assert(false);
    }

    if (traceA[1].timestamp != 5 || traceA[1].payloadId != 100) {
        std::cerr << "[1541 SCHED] FAIL: tie-break first event mismatch" << std::endl;
        assert(false);
    }

    if (traceA[2].timestamp != 5 || traceA[2].payloadId != 200) {
        std::cerr << "[1541 SCHED] FAIL: tie-break second event mismatch" << std::endl;
        assert(false);
    }

    if (traceA[3].timestamp != 5 || traceA[3].payloadId != 201) {
        std::cerr << "[1541 SCHED] FAIL: tie-break third event mismatch" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 SCHED] PASS: deterministic scheduler ordering and replay" << std::endl;
}
