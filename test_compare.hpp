#pragma once

void testCompare(ALU &alu, Registers &R) {
    std::cout << "[COMPARE] Running CMP sanity checks..." << std::endl;

    // A == M  -> C=1, Z=1, N=0
    R.A = 0x42;
    R.P = UNUSED;
    alu.cmp(R, 0x42);
    assert((R.P & CARRY) != 0);
    assert((R.P & ZERO) != 0);
    assert((R.P & NEGATIVE) == 0);

    // A > M   -> C=1, Z=0, N=0
    R.A = 0x50;
    R.P = UNUSED;
    alu.cmp(R, 0x10);
    assert((R.P & CARRY) != 0);
    assert((R.P & ZERO) == 0);
    assert((R.P & NEGATIVE) == 0);

    // A < M   -> C=0, Z=0, N=1 (0x10 - 0x20 = 0xF0)
    R.A = 0x10;
    R.P = UNUSED;
    alu.cmp(R, 0x20);
    assert((R.P & CARRY) == 0);
    assert((R.P & ZERO) == 0);
    assert((R.P & NEGATIVE) != 0);

    std::cout << "[COMPARE] CMP sanity checks passed." << std::endl;
}
