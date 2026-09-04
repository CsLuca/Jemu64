#pragma once

// ==========================
// Flags CPU 6510 (6502)
// ==========================
enum StatusFlags : uint8_t {
    CARRY            = 1 << 0,
    ZERO             = 1 << 1,
    INTERRUPT_DISABLE= 1 << 2,
    DECIMAL          = 1 << 3,
    BREAK            = 1 << 4,
    UNUSED           = 1 << 5,
    OVERFLOW         = 1 << 6,
    NEGATIVE         = 1 << 7
};

// ==========================
// Blocco Registri
// ==========================
struct Registers {
    uint16_t PC = 0;
    uint8_t  SP = 0xFD;
    uint8_t  A = 0, X = 0, Y = 0;
    uint8_t  P = UNUSED | INTERRUPT_DISABLE;
};
