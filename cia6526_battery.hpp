#pragma once

#include <cassert>
#include <cstdint>

static uint64_t runCia6526BatteryScenarioDigest(uint32_t seed) {
    CIA6526 cia;
    uint64_t digest = 1469598103934665603ULL;
    uint32_t irqTransitions = 0;
    bool lastIrq = false;

    cia.signalIRQ = [&](bool active) {
        if (active != lastIrq) {
            irqTransitions++;
            lastIrq = active;
        }
    };

    // Timer A continuous mode underflow behavior.
    cia.write(0xDC04, 0x03);
    cia.write(0xDC05, 0x00);
    cia.write(0xDC0D, 0x81);
    cia.write(0xDC0E, 0x11); // start + force load, continuous
    for (int i = 0; i < 40; ++i) {
        cia.tick();
    }
    assert(cia.timer1UnderflowCount >= 2);
    assert(cia.timer1_started);
    const uint8_t icrA = cia.read(0xDC0D, 0xFF);
    assert((icrA & 0x01) != 0);
    assert((icrA & 0x80) != 0);
    const uint8_t icrAAfter = cia.read(0xDC0D, 0xFF);
    assert((icrAAfter & 0x1F) == 0);

    // Timer B one-shot should stop after first underflow.
    cia.write(0xDC06, 0x02);
    cia.write(0xDC07, 0x00);
    cia.write(0xDC0D, 0x82);
    cia.write(0xDC0F, 0x19); // start + force load + one-shot
    for (int i = 0; i < 40; ++i) {
        cia.tick();
    }
    assert(cia.timer2UnderflowCount >= 1);
    assert(!cia.timer2_started);

    // ICR/IER sequencing: pending source sets bit7 once enabled.
    cia.clearIcrMask(0x1F);
    cia.write(0xDC0D, 0x02); // disable Timer B source
    cia.setIcrEvent(static_cast<uint8_t>(1u << 1));
    assert((cia.icr & 0x02) != 0);
    assert((cia.icr & 0x80) == 0);
    cia.write(0xDC0D, 0x82); // enable Timer B source
    assert((cia.icr & 0x80) != 0);

    // TOD clock + latch semantics.
    cia.write(0xDC0F, static_cast<uint8_t>(cia.t2_ctrl & static_cast<uint8_t>(~0x80))); // TOD clock write mode
    cia.write(0xDC08, 0x01);
    cia.write(0xDC09, 0x00);
    cia.write(0xDC0A, 0x00);
    cia.write(0xDC0B, 0x11);
    for (int i = 0; i < 40; ++i) {
        cia.tick();
    }
    const uint8_t expectedLatchedMin = cia.tod_minutes;
    const uint8_t expectedLatchedHour = cia.tod_hours;
    const uint8_t latchedHour = cia.read(0xDC0B, 0xFF);
    const uint8_t latchedMin = cia.read(0xDC0A, 0xFF);
    assert(latchedHour == expectedLatchedHour);
    assert(latchedMin == expectedLatchedMin);
    for (int i = 0; i < 80; ++i) {
        cia.tick();
    }
    const uint8_t stillLatchedMin = cia.read(0xDC0A, 0xFF);
    assert(stillLatchedMin == latchedMin);
    (void)cia.read(0xDC08, 0xFF); // unlatch on tenths read
    const uint8_t liveMin = cia.read(0xDC0A, 0xFF);
    assert(liveMin == cia.tod_minutes);

    // TOD alarm source routed through ICR bit2.
    cia.write(0xDC0D, 0x84);
    cia.write(0xDC0F, static_cast<uint8_t>(cia.t2_ctrl | 0x80)); // alarm write mode
    cia.write(0xDC08, cia.tod_10ths);
    cia.write(0xDC09, cia.tod_seconds);
    cia.write(0xDC0A, cia.tod_minutes);
    cia.write(0xDC0B, cia.tod_hours);
    cia.write(0xDC0F, static_cast<uint8_t>(cia.t2_ctrl & static_cast<uint8_t>(~0x80))); // back to TOD clock mode
    cia.evaluateTodAlarm();
    cia.tick();
    const uint8_t icrTod = cia.read(0xDC0D, 0xFF);
    assert((icrTod & 0x04) != 0);

    // Serial input (CNT rising edge sampling SP).
    cia.write(0xDC0E, static_cast<uint8_t>(cia.controlA & static_cast<uint8_t>(~0x40))); // input mode
    uint8_t expectedIn = 0;
    for (int i = 0; i < 8; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const uint8_t bit = static_cast<uint8_t>((seed >> 31) & 1u);
        expectedIn = static_cast<uint8_t>((expectedIn << 1) | bit);
        cia.setSerialPins(false, bit != 0);
        cia.cycleCore.tickHalfCycle(cia);
        cia.setSerialPins(true, bit != 0);
        cia.cycleCore.tickHalfCycle(cia);
    }
    cia.cycleCore.tickHalfCycle(cia);
    assert(cia.serialDataReg == expectedIn);
    assert(cia.serialRxByteCount >= 1);
    const uint8_t icrSerIn = cia.read(0xDC0D, 0xFF);
    assert((icrSerIn & 0x08) != 0);

    // Serial output paced by Timer A domain.
    cia.write(0xDC04, 0x02);
    cia.write(0xDC05, 0x00);
    cia.write(0xDC0D, 0x88);
    cia.write(0xDC0E, 0x51); // start + force load + serial output mode
    cia.write(0xDC0C, 0xA5);
    for (int i = 0; i < 400; ++i) {
        cia.tick();
        if (!cia.serialShiftInProgress) {
            break;
        }
    }
    cia.tick();
    assert(!cia.serialShiftInProgress);
    assert(cia.serialTxByteCount >= 1);
    assert((cia.icr & 0x08) != 0);
    const uint8_t icrSerOut = cia.read(0xDC0D, 0xFF);
    assert((icrSerOut & 0x08) != 0);

    digest ^= cia.timer1UnderflowCount; digest *= 1099511628211ULL;
    digest ^= cia.timer2UnderflowCount; digest *= 1099511628211ULL;
    digest ^= cia.serialRxByteCount; digest *= 1099511628211ULL;
    digest ^= cia.serialTxByteCount; digest *= 1099511628211ULL;
    digest ^= cia.todAlarmMatchCount; digest *= 1099511628211ULL;
    digest ^= irqTransitions; digest *= 1099511628211ULL;
    digest ^= cia.timer1_count; digest *= 1099511628211ULL;
    digest ^= cia.timer2_count; digest *= 1099511628211ULL;
    digest ^= cia.tod_10ths; digest *= 1099511628211ULL;
    digest ^= cia.tod_seconds; digest *= 1099511628211ULL;
    digest ^= cia.tod_minutes; digest *= 1099511628211ULL;
    digest ^= cia.tod_hours; digest *= 1099511628211ULL;

    return digest;
}

static void runCia6526EdgeCaseBattery() {
    const uint64_t d1 = runCia6526BatteryScenarioDigest(0x12345678u);
    const uint64_t d2 = runCia6526BatteryScenarioDigest(0x12345678u);
    const uint64_t d3 = runCia6526BatteryScenarioDigest(0x12345678u);

    if (!(d1 == d2 && d2 == d3)) {
        std::cerr << "[CIA BATTERY] FAIL: non-deterministic digest run-to-run."
                  << " d1=$" << std::hex << d1
                  << " d2=$" << d2
                  << " d3=$" << d3
                  << std::dec << std::endl;
        assert(false);
    }

    std::cerr << "[CIA BATTERY] PASS: timer/tod/shift edge cases deterministic"
              << " digest=$" << std::hex << d1 << std::dec
              << std::endl;
}
