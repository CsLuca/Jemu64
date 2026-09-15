#pragma once

#include <iostream>

#include "../drive1541_physical/drive_signal_model.hpp"

static void runDrive1541SignalModelTests() {
    using drive1541_physical::DriveSignalModel;
    using drive1541_physical::PowerState;

    {
        DriveSignalModel m;
        m.reset();
        m.begin_tick(PowerState::Off);
        const auto s = m.state();
        if (s.motor_on || s.activity || s.error || s.iec_load) {
            std::cerr << "[1541 SIGNAL] FAIL: expected all LEDs off when power is off" << std::endl;
            assert(false);
        }
    }

    {
        DriveSignalModel m;
        m.reset();
        m.begin_tick(PowerState::On);
        m.note_flux_read();
        m.begin_tick(PowerState::On);
        if (!m.state().activity) {
            std::cerr << "[1541 SIGNAL] FAIL: activity pulse missing during flux read" << std::endl;
            assert(false);
        }
    }

    {
        DriveSignalModel m;
        m.reset();
        m.begin_tick(PowerState::On);
        m.note_status_line("74,DRIVE NOT READY,00,00");
        m.begin_tick(PowerState::On);
        if (!m.state().error) {
            std::cerr << "[1541 SIGNAL] FAIL: error LED not set on fault status" << std::endl;
            assert(false);
        }
    }

    {
        DriveSignalModel m;
        m.reset();
        m.begin_tick(PowerState::On);
        m.note_iec_edges(16);
        for (int i = 0; i < 40; ++i) {
            m.begin_tick(PowerState::On);
        }
        if (!m.state().iec_load) {
            std::cerr << "[1541 SIGNAL] FAIL: IEC load LED not set on dense edge window" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 SIGNAL] PASS: physical signal model LED derivation" << std::endl;
}
