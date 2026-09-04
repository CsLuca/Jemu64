#pragma once

void runFullRegressionSuite(Bus &bus, CPU6510 &cpu, VICII &vic) {
    std::cout << "[REGRESSION] Starting full regression suite..." << std::endl;

    bool ok = true;

    Registers r = cpu.getRegisters();
    ALU alu;
    testCompare(alu, r);

    // 1) CPU timing matrix (official + unofficial subset)
    runOpcodeTimingSelfCheck(bus, cpu);

    // 2) VIC-II timing/visual checklist
    runViciiChecklist(vic, bus);

    // 3) ROM smoke passes (do not assert detailed behavior here; only liveness/stability)
    ok = runRomSmokeTest(bus, cpu, 0xFCE2, 300000, "KERNAL reset smoke") && ok;
    ok = runRomSmokeTest(bus, cpu, 0xFD02, 100000, "Autostart scan smoke") && ok;
    ok = runRomSmokeTest(bus, cpu, 0xFD50, 200000, "RAM test routine smoke") && ok;

    if (!ok) {
        std::cerr << "[REGRESSION] FAIL: at least one smoke check failed." << std::endl;
        assert(false);
    }

    std::cout << "[REGRESSION] PASS: timing + VIC checklist + ROM smoke." << std::endl;
}
