#pragma once

void testADC_SBC(ALU &alu, Registers &R, Bus &bus) {
    printf("== Test ADC/SBC in BINARY mode ==\n");

    // Reset registri
    R.A = 0x00;
    R.P = 0;

    // ------------------------
    // Caso 1: A=0x01 + 0x01
    // ------------------------
    R.A = 0x01;
    alu.adc(R, 0x01);

    assert(R.A == 0x02);
    assert((R.P & CARRY) == 0);

    // ------------------------
    // Caso 2: A=0x99 + 0x01
    // ------------------------
    R.A = 0x99;
    R.P = 0; // carry=0
    alu.adc(R, 0x01);

    // 0x99 + 0x01 = 0x9A
    assert(R.A == 0x9A);
    assert((R.P & CARRY) == 0);

    // ------------------------
    // Caso 3: SBC test A=0x50 - 0x10
    // ------------------------
    R.A = 0x50;
    R.P = CARRY; // carry = 1 (no prestito iniziale)
    alu.sbc(R, 0x10);

    assert(R.A == 0x40);
    assert((R.P & CARRY) != 0);

    printf("Tutti i test (BINARY) passati!\n");
}
