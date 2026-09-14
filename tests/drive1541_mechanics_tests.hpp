#pragma once

#include <iostream>

#include "../drive1541_physical/mechanics_model.hpp"

static void runDrive1541MechanicsTests() {
    using drive1541_physical::MechanicsModel;

    {
        MechanicsModel m;
        m.reset();
        const double a0 = m.spindle_angle_norm();
        m.tick(50000u);
        const double a1 = m.spindle_angle_norm();
        if (a1 != a0) {
            std::cerr << "[1541 MECH] FAIL: spindle moved with motor off" << std::endl;
            assert(false);
        }
    }

    {
        MechanicsModel m;
        m.reset();
        m.set_motor_on(true);
        const double a0 = m.spindle_angle_norm();
        m.tick(50000u);
        const double a1 = m.spindle_angle_norm();
        if (!(a1 > a0 && a1 < 1.0)) {
            std::cerr << "[1541 MECH] FAIL: spindle angle did not advance in range" << std::endl;
            assert(false);
        }
        m.tick(5000000u);
        const double a2 = m.spindle_angle_norm();
        if (!(a2 >= 0.0 && a2 < 1.0)) {
            std::cerr << "[1541 MECH] FAIL: spindle angle wrap out of range" << std::endl;
            assert(false);
        }
    }

    {
        MechanicsModel m;
        m.reset();
        for (int i = 0; i < 200; ++i) {
            m.step_in();
        }
        if (m.half_track() != 84u) {
            std::cerr << "[1541 MECH] FAIL: step_in upper clamp mismatch" << std::endl;
            assert(false);
        }
        for (int i = 0; i < 400; ++i) {
            m.step_out();
        }
        if (m.half_track() != 2u) {
            std::cerr << "[1541 MECH] FAIL: step_out lower clamp mismatch" << std::endl;
            assert(false);
        }
    }

    std::cerr << "[1541 MECH] PASS: motor gating, spindle wrap, half-track limits" << std::endl;
}
