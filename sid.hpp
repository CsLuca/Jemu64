#pragma once

// ==========================
// SID cycle-precise
// ==========================
class SID {
public:
    // Registri SID
    uint8_t voice1[7] = {0}; // $D400-$D406
    uint8_t voice2[7] = {0}; // $D407-$D40D
    uint8_t voice3[7] = {0}; // $D40E-$D414
    uint8_t filterVolume = 0; // $D418
    uint8_t modeTest = 0;     // $D41B
    uint8_t pulseWidthHi[3] = {0}; // $D405, $D40C, $D413 (parte alta)

    uint8_t internalBusLatch = 0xFF;

    // Stato interno clock
    int tickCounter = 0; // conta PHI2 della CPU

    // Tick: chiamato ogni mezzo ciclo PHI2
    void tick() {
        tickCounter++;

        // Qui si può simulare la propagazione ADSR, filtro, waveform, ecc.
        // Esempio logging ogni 10 tick
        if (tickCounter % 10 == 0) {
            std::cout << "[SID] Tick " << tickCounter << " cycle-precise internal update\n";
        }
    }

    // Scrittura cycle-precise
    void write(uint16_t addr, uint8_t val) {
        internalBusLatch = val;

        if (addr >= 0xD400 && addr <= 0xD414) {
            int regIndex = addr - 0xD400;
            if (regIndex < 7) voice1[regIndex] = val;
            else if (regIndex < 14) voice2[regIndex - 7] = val;
            else voice3[regIndex - 14] = val;

            std::cout << "[SID] Write $" << std::hex << (int)val
                      << " to voice register $" << addr << std::dec << "\n";

        } else if (addr == 0xD418) {
            filterVolume = val;
            uint8_t volume = val & 0x0F;
            uint8_t filter = (val >> 4) & 0x0F;
            std::cout << "[SID] Volume/Filter ($D418): volume=" << (int)volume
                      << " filter bits=" << std::hex << (int)filter << std::dec << "\n";
        } else {
            std::cout << "[SID] Write $" << std::hex << (int)val
                      << " to $" << addr << std::dec << "\n";
        }
    }

    // Dummy write (senza effetto reale, ma cycle-precise)
    void dummyWrite(uint16_t addr, uint8_t val) {
        internalBusLatch = val;
        std::cout << "[SID] Dummy write $" << std::hex << (int)val
                  << " to $" << addr << std::dec << "\n";
    }

    // Lettura attiva
    uint8_t read(uint16_t addr, uint8_t busVal) {
        uint8_t value = busVal;
        if (addr >= 0xD400 && addr <= 0xD414) {
            int regIndex = addr - 0xD400;
            if (regIndex < 7) value = voice1[regIndex];
            else if (regIndex < 14) value = voice2[regIndex - 7];
            else value = voice3[regIndex - 14];
        } else if (addr == 0xD418) {
            value = filterVolume;
        }

        internalBusLatch = value;
        return value;
    }

    // Peek passivo
    uint8_t peek(uint16_t addr, uint8_t busVal) const {
        if (addr >= 0xD400 && addr <= 0xD414) {
            int regIndex = addr - 0xD400;
            if (regIndex < 7) return voice1[regIndex];
            else if (regIndex < 14) return voice2[regIndex - 7];
            else return voice3[regIndex - 14];
        } else if (addr == 0xD418) {
            return filterVolume;
        }
        return busVal;
    }
};
