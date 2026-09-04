#pragma once

bool runRomSmokeTest(Bus &bus, CPU6510 &cpu, uint16_t start, uint32_t maxHalfCycles, const char *name) {
    bus.memory[0xFFFC] = Lo(start);
    bus.memory[0xFFFD] = Hi(start);

    cpu.reset();

    uint32_t guard = 0;
    while (!cpu.isIdle() && guard < 256) {
        cpu.clock();
        guard++;
    }

    uint64_t begin = cpu.getTotalHalfCycles();
    bool halted = false;
    for (uint32_t i = 0; i < maxHalfCycles; ++i) {
        cpu.clock();
        if (cpu.halted) {
            halted = true;
            break;
        }
    }

    Registers r = cpu.getRegisters();
    uint64_t elapsed = cpu.getTotalHalfCycles() - begin;
    bool progress = (elapsed > 0);

    std::cout << "[ROM SMOKE] " << name
              << " start=$" << std::hex << start
              << " elapsed_halfcycles=" << std::dec << elapsed
              << " halted=" << (halted ? "yes" : "no")
              << " pc=$" << std::hex << r.PC
              << std::endl;

    return progress && !halted;
}
