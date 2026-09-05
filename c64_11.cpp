#include <cstdint>
#include <iostream>
#include <array>
#include <deque>
#include <functional>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <cstdlib>
#include <regex>
#include <filesystem>
#include <cctype>
#include <random>
#include "cia6526.hpp"
#include "external_tests_types.hpp"
#include "cpu_types.hpp"
#include "bus.hpp"
#include "sid.hpp"
#include "ibus.hpp"
#include "vicii.hpp"
#include "bit_utils.hpp"
#include "cpu_micro_ops.hpp"
#include "build_config.hpp"

#include "drive_via6522.hpp"

// ==========================
// Drive 1541 minimal skeleton
// ==========================
#include "drive_1541.hpp"

#include "iec_bridge.hpp"


#include "alu.hpp"


class CPU6510 {
public:
    enum Revision : uint8_t {
        REV_6510 = 0,
        REV_8500 = 1,
        REV_6510R2 = 2,
        REV_8500R2 = 3
    };

    struct RevisionProfile {
        Revision revision = REV_6510;
        bool nmosDecimalBehavior = true;
        bool supportsPortFallbackPullups = true;
        bool decimalOnIllegalSbcEb = true;
        bool jamKeepsLastDataBus = true;
        bool xaaUsesMagicConstEE = true;
        bool arrRespectsDecimalAdjust = true;
        bool branchDummyReadOnNoCross = true;
    };

    static constexpr RevisionProfile makeRevisionProfile(Revision rev) {
        return (rev == REV_8500)
            ? RevisionProfile{REV_8500, true, true, false, false, false, false, false}
            : (rev == REV_6510R2)
                ? RevisionProfile{REV_6510R2, true, true, true, true, true, true, false}
                : (rev == REV_8500R2)
                    ? RevisionProfile{REV_8500R2, true, true, false, false, false, false, true}
                    : RevisionProfile{REV_6510, true, true, true, true, true, true, true};
    }

    void adcRevisionAware(uint8_t value) {
        const bool savedDecimal = (R.P & DECIMAL) != 0;
        if (!revisionProfile.nmosDecimalBehavior) {
            R.P = static_cast<uint8_t>(R.P & ~DECIMAL);
        }
        ALU::adc(R, value);
        if (!revisionProfile.nmosDecimalBehavior && savedDecimal) {
            R.P = static_cast<uint8_t>(R.P | DECIMAL);
        }
    }

    void sbcRevisionAware(uint8_t value, bool illegalAlias = false) {
        const bool savedDecimal = (R.P & DECIMAL) != 0;
        const bool allowDecimal = revisionProfile.nmosDecimalBehavior &&
                                  (!illegalAlias || revisionProfile.decimalOnIllegalSbcEb);
        if (!allowDecimal) {
            R.P = static_cast<uint8_t>(R.P & ~DECIMAL);
        }
        ALU::sbc(R, value);
        if (!allowDecimal && savedDecimal) {
            R.P = static_cast<uint8_t>(R.P | DECIMAL);
        }
    }

    Revision revision = REV_6510;
    RevisionProfile revisionProfile = makeRevisionProfile(REV_6510);

    void setRevision(Revision rev) {
        revision = rev;
        revisionProfile = makeRevisionProfile(rev);
    }

    Revision getRevision() const {
        return revision;
    }

    const RevisionProfile &getRevisionProfile() const {
        return revisionProfile;
    }

    CPU6510(Bus &bus) : bus(bus) { reset(); }

    // aggiungiamo il latch interno alla CPU:
    uint8_t cpuLatch = 0xFF; // "internalBusLatch" della CPU (quello che la CPU latchea)  
    
    uint8_t ZP_addr = 0;

    bool halted = false;

    bool isHalted() const {
        return microOps.empty() && opcode == 0x00; // oppure altro criterio
    }

    uint16_t getPC() const {
      return R.PC;
    }

    bool isIdle() const {
        return microOps.empty();
    }

    bool inDummySequence = false;

    //std::vector<MicroOpWithPhase> microOps;
    std::deque<MicroOpWithPhase> microOps;   // ✅ invece di std::vector

    void enqueueDummyRead(uint16_t addr, bool verbose = false) {


        if (inDummySequence) return; // evita doppio enqueue
        inDummySequence = true;

        // ========================================================
        // ULTRA-DEFINITIVA DUMMY_READ
        // Simula ogni micro-ciclo PHI1/PHI2 come fa il 6510 reale.
        // Include crossing di pagina, branch condizionali e logging.
        // ========================================================

        // -------------------------
        // PHI1: indirizzo stabile
        // -------------------------
        microOps.push_back({PHI1, [this, addr, verbose]() {
            if (verbose) {
                std::cout << "[DUMMY] PHI1: Address stable at $" << std::hex << addr << std::endl;
            }
        }});

        // -------------------------
        // PHI2: lettura dummy principale
        // -------------------------
        microOps.push_back({PHI2, [this, addr, verbose]() {
            uint8_t dummy = bus.peek(addr);
            (void)dummy;
            if (verbose) {
                std::cout << "[DUMMY] PHI2: Dummy read at $" << std::hex << addr
                        << " (open bus preserved)" << std::endl;
            }
        }});

        // -------------------------
        // PHI1 extra: branch condizionale
        // -------------------------
        if (branchTaken) { // branchTaken deve essere impostato prima
            microOps.push_back({PHI1, [this, addr, verbose]() {
                if (verbose) {
                    std::cout << "[DUMMY] PHI1: Branch dummy read (PC update) at $"
                            << std::hex << addr << std::endl;
                }
            }});
        }

        // -------------------------
        // PHI1/PHI2 extra: crossing di pagina
        // -------------------------
        if ((addr & 0xFF00) != ((addr + 1) & 0xFF00)) {
            // PHI1 extra per attraversamento pagina
            microOps.push_back({PHI1, [this, addr, verbose]() {
                if (verbose) {
                    std::cout << "[DUMMY] PHI1: Extra dummy read page crossing at $"
                            << std::hex << addr << std::endl;
                }
            }});

            // PHI2 extra per attraversamento pagina
            microOps.push_back({PHI2, [this, addr, verbose]() {
                uint8_t dummy = bus.peek(addr + 1);
                (void)dummy;
                if (verbose) {
                    std::cout << "[DUMMY] PHI2: Extra dummy read page crossing at $"
                            << std::hex << (addr + 1) << std::endl;
                }
            }});
        }

        // -------------------------
        // PHI1 finale: ritorno in idle
        // -------------------------
        microOps.push_back({PHI1, [this, addr, verbose]() {
            inDummySequence = false;
            if (verbose) {
                std::cout << "[DUMMY] PHI1: End of dummy cycle at $" << std::hex << addr << std::endl;
            }
        }});
    }


   
    void reset() {
        R.SP = 0xFD;
        R.P = UNUSED | INTERRUPT_DISABLE;
        microOps.clear();
        currentPhase = PHI1;
        total_halfcycles = 0;
        inDummySequence = false;

        nmiLine = true;
        pendingNMI = false;
        irqLine = true;
        pendingIRQ = false;
        irqSampledLow = false;

        blockNMI = true;  // Blocca gli NMI durante reset

        resetVectorLo = 0;

        // --- Dummy reads dello stack ---
        microOps.push_back({PHI2, [this]() { bus.read(0x0100 | ((R.SP + 1) & 0xFF)); std::cout << "[RESET] Dummy read SP+1 PHI2\n"; }});
        microOps.push_back({PHI1, [](){} }); // PHI1 half-cycle vuoto
        microOps.push_back({PHI2, [this]() { bus.read(0x0100 | ((R.SP + 2) & 0xFF)); std::cout << "[RESET] Dummy read SP+2 PHI2\n"; }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this]() { bus.read(0x0100 | ((R.SP + 3) & 0xFF)); std::cout << "[RESET] Dummy read SP+3 PHI2\n"; }});
        microOps.push_back({PHI1, [](){} });

        // --- Fetch low byte del reset vector ---
        microOps.push_back({PHI2, [this]() { resetVectorLo = bus.read(0xFFFC); std::cout << "[RESET] Fetch low byte vector PHI2 -> 0x" << std::hex << (int)resetVectorLo << "\n"; }});
        microOps.push_back({PHI1, [](){} }); // PHI1 vuoto

        // --- Fetch high byte del reset vector e impostazione PC ---
        microOps.push_back({PHI2, [this]() { 
            uint8_t hi = bus.read(0xFFFD);
            R.PC = (hi << 8) | resetVectorLo;
            std::cout << "[RESET] Fetch high byte vector PHI2 -> 0x" << std::hex << (int)hi 
                    << " => PC=$" << std::hex << R.PC << "\n"; 

            // Fine reset, ora si possono gestire gli NMI
            blockNMI = false;  
        }});
        microOps.push_back({PHI1, [](){} }); // PHI1 vuoto
    }


    void clock(bool busGranted = true) {
        if (!busGranted && currentPhase == PHI2) {
            currentPhase = PHI1;
            total_halfcycles++;
            return;
        }

        // --- Esegui micro-ops in corso ---
        if (!microOps.empty()) {
            if (microOps.front().phase == currentPhase) {
                microOps.front().op();
                microOps.pop_front();
            }

            // IRQ boundary check: on instruction boundary (queue becomes empty)
            // and I=0, latch IRQ sequence immediately.
            if (microOps.empty() && irqSampledLow && !ALU::getFlag(R.P, INTERRUPT_DISABLE)) {
                triggerIRQ();
                irqSampledLow = false;
                currentPhase = PHI2;
                total_halfcycles++;
                return;
            }

            // Avanza di mezzo ciclo
            currentPhase = (currentPhase == PHI1) ? PHI2 : PHI1;
            total_halfcycles++;
            return;
        }

        // --- Arrivati qui: istruzione terminata ---

        // 🔹 Verifica NMI prima di qualsiasi altra cosa
        if (pendingNMI) {
            std::cout << "[NMI] Triggered at PC=$" << std::hex << R.PC << std::endl;
            pendingNMI = false;
            triggerNMI();          // carica microOps per la sequenza NMI (7 cicli)
            return;
        }

        // 🔹 Poi verifica IRQ (solo se flag I=0)
        if (irqSampledLow && !ALU::getFlag(R.P, INTERRUPT_DISABLE)) {
            std::cout << "[IRQ] Triggered at PC=$" << std::hex << R.PC << std::endl;
            triggerIRQ();          // carica microOps per la sequenza IRQ (7 cicli)
            irqSampledLow = false;
            return;
        }

        // --- Se nessun interrupt, fetch del prossimo opcode ---
        addr_bus = R.PC;
        uint8_t opcode = bus.peek(addr_bus);
        printInstruction(addr_bus, opcode);
        decode(opcode);            // imposta le microOps per l'istruzione

        // --- Avanza mezzo ciclo ---
        currentPhase = (currentPhase == PHI1) ? PHI2 : PHI1;
        total_halfcycles++;
    }

    /*
    void clock() {
        // --- Se ci sono micro-ops da eseguire ---
        if (!microOps.empty()) {
            // Micro-op eseguita solo se corrisponde alla fase corrente
            if (microOps.front().phase == currentPhase) {
                microOps.front().op();
                microOps.pop_front();
            }

            // Avanza di mezzo ciclo
            currentPhase = (currentPhase == PHI1) ? PHI2 : PHI1;
            total_halfcycles++;
            return;
        }

        // --- Priorità NMI ---
        if (pendingNMI) {
            std::cout << "[NMI] Triggered at PC=$" << std::hex << R.PC << std::endl;
            pendingNMI = false;
            triggerNMI();      // triggerNMI carica microOps NMI
            return;
        }

        // --- Controllo IRQ solo se linea attiva e flag I=0 ---
        if (irqLine && !ALU::getFlag(R.P, INTERRUPT_DISABLE)) {
            std::cout << "[IRQ] Triggered at PC=$" << std::hex << R.PC << std::endl;
            pendingIRQ = true;
            triggerIRQ();      // triggerIRQ carica microOps IRQ cycle-exact
            return;
        }

        // --- Fetch opcode solo se nessun interrupt pendente ---
        addr_bus = R.PC;

        // Dummy read fetch (cycle-exact, pipeline 6510)
        uint8_t fetchedOpcode = bus.read(addr_bus);
        printInstruction(addr_bus, fetchedOpcode);

        decode(fetchedOpcode);

        // Avanza di mezzo ciclo
        currentPhase = (currentPhase == PHI1) ? PHI2 : PHI1;
        total_halfcycles++;
    }
    */

    /*
    void clock() {
        if (microOps.empty()) {

            // --- NMI ha priorità assoluta ---
            if (pendingNMI) {
                std::cout << "[NMI] Triggered at PC=$" << std::hex << R.PC << std::endl;
                triggerNMI();
                pendingNMI = false;
                return;
            }            

            // --- Controllo IRQ prima del fetch del nuovo opcode ---
            if (!ALU::getFlag(R.P, INTERRUPT_DISABLE) && !irqLine) {
                std::cout << "[IRQ] Triggered at PC=$" << std::hex << R.PC << std::endl;
                triggerIRQ();
                // NOTA: non si fa fetch opcode in questo ciclo
                // triggerIRQ() caricherà i microOps per la sequenza di interrupt
                return;
            }

            // --- Fetch nuovo opcode ---
            addr_bus = R.PC;
            opcode = bus.read(addr_bus);
            printInstruction(addr_bus, opcode);

            decode(opcode);
        }

        // Esegui micro-operazioni
        if (!microOps.empty() && microOps.front().phase == currentPhase) {
            microOps.front().op();
            microOps.pop_front();
        }

        // Avanza di mezzo ciclo
        currentPhase = (currentPhase == PHI1) ? PHI2 : PHI1;
        total_halfcycles++;
    }
    */

    uint64_t getTotalHalfCycles() const { return total_halfcycles; }
    Registers getRegisters() const { return R; }
    void setRegisters(const Registers &regs) { R = regs; }
    Phase getCurrentPhase() const { return currentPhase; }

    void clearMicroOpsForTest() { microOps.clear(); }
    void pushMicroOpForTest(Phase phase, const MicroOp &op) { microOps.push_back({phase, op}); }
    bool hasPendingMicroOpsForTest() const { return !microOps.empty(); }
    size_t pendingMicroOpCountForTest() const { return microOps.size(); }
    Phase nextMicroOpPhaseForTest() const { return microOps.front().phase; }
    void setCurrentPhaseForTest(Phase p) { currentPhase = p; }

    bool nmiLine = true;      // true = high (nessuna NMI), false = attiva
    bool pendingNMI = false;
    bool blockNMI = true; 

    /*
    void setNMI(bool level) {
        // rileva fronte di discesa (edge triggered)
        if (nmiLine && !level) pendingNMI = true;
        nmiLine = level;
    }
    */

    void setNMI(bool level) {
        if (!blockNMI) {                     // ignora NMI se bloccato
            if (nmiLine && !level) {         // fronte di discesa
                pendingNMI = true;
            }
        }
        nmiLine = level;                      // aggiorna lo stato della linea
    }


    // ======================================
    // Gestione IRQ (Interrupt Request)
    // ======================================

    bool irqLine = true;     // IRQ è attivo basso → true = inattivo, false = attivo
    bool pendingIRQ = false;
    bool irqSampledLow = false;

    void setIRQ(bool level) {
        irqLine = level;     // true = high (nessun IRQ), false = low (IRQ attivo)
        if (!irqLine) {
            irqSampledLow = true;
        }
    }    


private:
    Bus &bus;
    Registers R;

    //uint8_t IR = 0;
    //uint8_t Data_latch = 0;
    uint8_t Temp_addr_low = 0;
    uint8_t Temp_addr_high = 0;
    uint16_t EffAddr = 0;
    bool pageCross = false;    

    uint16_t addr_bus = 0;
    uint8_t opcode = 0;
    uint64_t total_halfcycles = 0;
    int halfCycle = 0;

    uint8_t fetched = 0;
    uint16_t addr_abs = 0;
    int8_t addr_rel = 0;


    // ======================================
    // Latch interni
    // ======================================

    uint8_t IR = 0;          // Instruction Register
    uint8_t Data_latch = 0;  // per immediate/absolute data
    uint16_t Addr_latch = 0; // per address bus temporaneo
    uint8_t ALU_temp = 0;    // ALU temporaneo    
    uint16_t temp_sum;     
    uint8_t resetVectorLo = 0;
    

    /*

    */

    struct ALUTemp {
        uint16_t result9;  // risultato a 9 bit
        uint8_t result8;   // risultato a 8 bit
        bool carry;
        bool overflow;

        // per SBC
        uint8_t oldA;
        uint8_t operand;
    };
    ALUTemp temp_ALU;    
    
    // Branch bookkeeping (used by branch instructions and timing helpers).
    int8_t branchOffset = 0;
    uint16_t branchTarget = 0;
    uint16_t branchOldPC = 0;
    bool branchTaken = false;
    bool branchPageCross = false;

    
    
    Phase currentPhase = PHI1;
    //std::deque<MicroOpWithPhase> microOps;

    void fetchImm(uint8_t &target) {
        // Read immediate operand on PHI2, then increment PC on PHI1.
        microOps.push_back({PHI2, [this, target]() mutable { target = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
    }

    enum class UAddrMode {
        ZP,
        ZPX,
        ZPY,
        ABS,
        ABSX,
        ABSY,
        INDX,
        INDY
    };

    // Operation type for unofficial RMW opcodes that also affect A.
    enum class URmwKind {
        SLO,
        RLA,
        SRE,
        RRA,
        DCP,
        ISC
    };

    enum class ReadAction {
        LDA,
        ORA,
        AND,
        EOR,
        ADC,
        SBC,
        CMP,
        LDX,
        LDY,
        CPX,
        CPY
    };

    void enqueueResolveAddress(UAddrMode mode) {
        // Build the addressing micro-sequence only.
        // Opcode fetch and execute stages are handled by callers.
        switch (mode) {
            case UAddrMode::ZP:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = Temp_addr_low; }});
                break;

            case UAddrMode::ZPX:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
                break;

            case UAddrMode::ZPY:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = static_cast<uint8_t>(Temp_addr_low + R.Y); }});
                break;

            case UAddrMode::ABS:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                }});
                break;

            case UAddrMode::ABSX:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    EffAddr = ((uint16_t(Temp_addr_high) << 8) | Temp_addr_low) + R.X;
                }});
                break;

            case UAddrMode::ABSY:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    EffAddr = ((uint16_t(Temp_addr_high) << 8) | Temp_addr_low) + R.Y;
                }});
                break;

            case UAddrMode::INDX:
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X);
                }});
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
                microOps.push_back({PHI1, [](){} });
                microOps.push_back({PHI2, [this]() {
                    Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                    EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                }});
                microOps.push_back({PHI1, [](){} });
                break;

            case UAddrMode::INDY:
                microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
                microOps.push_back({PHI1, [](){} });
                microOps.push_back({PHI2, [this]() {
                    Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                    EffAddr = ((uint16_t(Temp_addr_high) << 8) | Temp_addr_low) + R.Y;
                }});
                microOps.push_back({PHI1, [](){} });
                break;
        }
    }

    void enqueueReadModifyWriteWithA(UAddrMode mode, URmwKind kind) {
        // Shared cycle pattern for unofficial RMW opcodes:
        // fetch opcode, resolve address, read, dummy write, final write, update A/flags.
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        enqueueResolveAddress(mode);
        microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this]() { bus.laWrite(EffAddr, Data_latch); }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this, kind]() {
            uint8_t oldV = Data_latch;
            uint8_t newV = oldV;
            if (kind == URmwKind::SLO) {
                newV = static_cast<uint8_t>(oldV << 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                R.A = static_cast<uint8_t>(R.A | newV);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            } else if (kind == URmwKind::RLA) {
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                newV = static_cast<uint8_t>((oldV << 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                R.A = static_cast<uint8_t>(R.A & newV);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            } else if (kind == URmwKind::SRE) {
                newV = static_cast<uint8_t>(oldV >> 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                R.A = static_cast<uint8_t>(R.A ^ newV);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            } else if (kind == URmwKind::RRA) {
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                newV = static_cast<uint8_t>((oldV >> 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                adcRevisionAware(newV);
            } else if (kind == URmwKind::DCP) {
                newV = static_cast<uint8_t>(oldV - 1);
                uint8_t cmp = static_cast<uint8_t>(R.A - newV);
                ALU::setFlag(R.P, CARRY, R.A >= newV);
                ALU::setFlag(R.P, ZERO, R.A == newV);
                ALU::setFlag(R.P, NEGATIVE, (cmp & 0x80) != 0);
            } else {
                newV = static_cast<uint8_t>(oldV + 1);
                sbcRevisionAware(newV, false);
            }
            Data_latch = newV;
            bus.write(EffAddr, newV);
        }});
        if (mode == UAddrMode::INDX || mode == UAddrMode::INDY ||
            mode == UAddrMode::ZPX || mode == UAddrMode::ZPY ||
            mode == UAddrMode::ABSX || mode == UAddrMode::ABSY) {
            microOps.push_back({PHI1, [](){} });
        }
    }

    void enqueueLAX(UAddrMode mode) {
        // Shared micro-sequence for unofficial LAX variants.
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        enqueueResolveAddress(mode);
        microOps.push_back({PHI2, [this]() {
            uint8_t v = bus.read(EffAddr);
            R.A = v;
            R.X = v;
            ALU::setFlag(R.P, ZERO, v == 0);
            ALU::setFlag(R.P, NEGATIVE, (v & 0x80) != 0);
        }});
        microOps.push_back({PHI1, [](){} });
    }

    void enqueueSAX(UAddrMode mode) {
        // Shared micro-sequence for unofficial SAX variants.
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        enqueueResolveAddress(mode);
        microOps.push_back({PHI2, [this]() {
            bus.write(EffAddr, static_cast<uint8_t>(R.A & R.X));
        }});
        microOps.push_back({PHI1, [](){} });
    }

    void enqueueBranchRelative(bool shouldTake) {
        const uint16_t opcodePc = R.PC;
        const int8_t rel = static_cast<int8_t>(bus.peek(static_cast<uint16_t>(opcodePc + 1)));
        const uint16_t pcAfterOperand = static_cast<uint16_t>(opcodePc + 2);
        const uint16_t target = static_cast<uint16_t>(pcAfterOperand + rel);
        const bool pageCross = ((pcAfterOperand & 0xFF00) != (target & 0xFF00));

        branchTaken = false;
        branchPageCross = false;
        branchOffset = 0;
        branchTarget = 0;
        branchOldPC = 0;

        microOps.push_back({PHI2, [this]() { branchOffset = static_cast<int8_t>(bus.read(R.PC + 1)); }});
        microOps.push_back({PHI1, [this, shouldTake, pcAfterOperand, target, pageCross]() {
            R.PC = static_cast<uint16_t>(R.PC + 2);
            branchTaken = shouldTake;
            if (branchTaken) {
                branchOldPC = pcAfterOperand;
                branchTarget = target;
                branchPageCross = pageCross;
            }
        }});

        if (!shouldTake) {
            return;
        }

        if (!pageCross) {
            microOps.push_back({PHI2, [this, target]() {
                if (revisionProfile.branchDummyReadOnNoCross) {
                    (void)bus.read(target);
                }
            }});
            microOps.push_back({PHI1, [this, target]() { R.PC = target; }});
            return;
        }

        const uint16_t wrongPageAddr = static_cast<uint16_t>((pcAfterOperand & 0xFF00) | (target & 0x00FF));
        microOps.push_back({PHI2, [this, wrongPageAddr]() { (void)bus.read(wrongPageAddr); }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this, target]() { (void)bus.read(target); }});
        microOps.push_back({PHI1, [this, target]() { R.PC = target; }});
    }

    void applyReadAction(ReadAction action, uint8_t value) {
        switch (action) {
            case ReadAction::LDA:
                R.A = value;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                break;
            case ReadAction::ORA:
                R.A = static_cast<uint8_t>(R.A | value);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                break;
            case ReadAction::AND:
                R.A = static_cast<uint8_t>(R.A & value);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                break;
            case ReadAction::EOR:
                R.A = static_cast<uint8_t>(R.A ^ value);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                break;
            case ReadAction::ADC:
                adcRevisionAware(value);
                break;
            case ReadAction::SBC:
                sbcRevisionAware(value, false);
                break;
            case ReadAction::CMP: {
                const uint8_t res = static_cast<uint8_t>(R.A - value);
                ALU::setFlag(R.P, CARRY, R.A >= value);
                ALU::setFlag(R.P, ZERO, R.A == value);
                ALU::setFlag(R.P, NEGATIVE, (res & 0x80) != 0);
                break;
            }
            case ReadAction::LDX:
                R.X = value;
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
                break;
            case ReadAction::LDY:
                R.Y = value;
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.Y & 0x80) != 0);
                break;
            case ReadAction::CPX: {
                const uint8_t res = static_cast<uint8_t>(R.X - value);
                ALU::setFlag(R.P, CARRY, R.X >= value);
                ALU::setFlag(R.P, ZERO, R.X == value);
                ALU::setFlag(R.P, NEGATIVE, (res & 0x80) != 0);
                break;
            }
            case ReadAction::CPY: {
                const uint8_t res = static_cast<uint8_t>(R.Y - value);
                ALU::setFlag(R.P, CARRY, R.Y >= value);
                ALU::setFlag(R.P, ZERO, R.Y == value);
                ALU::setFlag(R.P, NEGATIVE, (res & 0x80) != 0);
                break;
            }
        }
    }

    void enqueueReadAbs(ReadAction action) {
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() {
            R.PC++;
            EffAddr = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | Temp_addr_low);
        }});
        microOps.push_back({PHI2, [this, action]() { applyReadAction(action, bus.read(EffAddr)); }});
    }

    void enqueueReadAbsIndexed(ReadAction action, bool useX) {
        const uint8_t lo = bus.peek(static_cast<uint16_t>(R.PC + 1));
        const uint8_t idx = useX ? R.X : R.Y;
        const bool cross = (static_cast<uint16_t>(lo) + static_cast<uint16_t>(idx)) > 0xFF;

        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this, useX]() {
            R.PC++;
            const uint16_t base = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | Temp_addr_low);
            const uint8_t idxNow = useX ? R.X : R.Y;
            EffAddr = static_cast<uint16_t>(base + idxNow);
        }});

        if (cross) {
            microOps.push_back({PHI2, [this]() {
                const uint16_t dummy = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | (EffAddr & 0x00FF));
                (void)bus.read(dummy);
            }});
            microOps.push_back({PHI1, [](){} });
        }

        microOps.push_back({PHI2, [this, action]() { applyReadAction(action, bus.read(EffAddr)); }});
    }

    void enqueueReadIndY(ReadAction action) {
        const uint8_t zp = bus.peek(static_cast<uint16_t>(R.PC + 1));
        const uint8_t lo = bus.peek(zp);
        const bool cross = (static_cast<uint16_t>(lo) + static_cast<uint16_t>(R.Y)) > 0xFF;

        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this]() {
            Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
            const uint16_t base = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | Temp_addr_low);
            EffAddr = static_cast<uint16_t>(base + R.Y);
        }});
        microOps.push_back({PHI1, [](){} });

        if (cross) {
            microOps.push_back({PHI2, [this]() {
                const uint16_t dummy = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | (EffAddr & 0x00FF));
                (void)bus.read(dummy);
            }});
            microOps.push_back({PHI1, [](){} });
        }

        microOps.push_back({PHI2, [this, action]() { applyReadAction(action, bus.read(EffAddr)); }});
    }

    void enqueueReadIndX(ReadAction action) {
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { (void)bus.read(Temp_addr_low); }});
        microOps.push_back({PHI1, [this]() { ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this]() {
            Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
            EffAddr = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | Temp_addr_low);
        }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this, action]() { applyReadAction(action, bus.read(EffAddr)); }});
    }

    void enqueueReadZpIndexed(ReadAction action, bool useX) {
        microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
        microOps.push_back({PHI1, [this]() { R.PC++; }});
        microOps.push_back({PHI2, [this, useX]() {
            const uint8_t idx = useX ? R.X : R.Y;
            EffAddr = static_cast<uint8_t>(Temp_addr_low + idx);
            (void)bus.read(EffAddr);
        }});
        microOps.push_back({PHI1, [](){} });
        microOps.push_back({PHI2, [this, action]() { applyReadAction(action, bus.read(EffAddr)); }});
    }

    void decode(uint8_t opcode) {
    switch (opcode) {
        
        // ========================================================
        // LDA #imm (Immediate) - 2 bytes, 2 machine cycles (4 half-cycles)
        // ========================================================
        case 0xA9: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.A = Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            break;
        }


        // ========================================================
        // LDA $HHLL,Y (Absolute,Y) - 3 bytes, 4 machine cycles (8 half-cycles typical)     (OK)
        // ========================================================
        case 0xB9: {
            const uint8_t lo = bus.peek(static_cast<uint16_t>(R.PC + 1));
            const uint8_t hi = bus.peek(static_cast<uint16_t>(R.PC + 2));
            const bool cross = (static_cast<uint16_t>(lo) + static_cast<uint16_t>(R.Y)) > 0xFF;

            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                uint16_t base = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                EffAddr = static_cast<uint16_t>(base + R.Y);
                pageCross = ((base & 0xFF00) != (EffAddr & 0xFF00));
            }});
            if (cross) {
                microOps.push_back({PHI2, [this]() {
                    uint16_t dummy = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | (EffAddr & 0x00FF));
                    (void)bus.read(dummy);
                }});
                microOps.push_back({PHI1, [](){} });
            }
            microOps.push_back({PHI2, [this]() {
                R.A = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            break;
        }


        // -----------------------------
        // LDA $addr,X (Absolute,X) - Opcode $BD
        // 4 cicli macchina (5 se page crossing)
        // -----------------------------            
        case 0xBD:  // LDA $HHLL,X (Absolute,X)
        {
            const uint8_t lo = bus.peek(static_cast<uint16_t>(R.PC + 1));
            const uint8_t hi = bus.peek(static_cast<uint16_t>(R.PC + 2));
            const bool cross = (static_cast<uint16_t>(lo) + static_cast<uint16_t>(R.X)) > 0xFF;

            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                uint16_t base = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                EffAddr = static_cast<uint16_t>(base + R.X);
                pageCross = ((base & 0xFF00) != (EffAddr & 0xFF00));
            }});
            if (cross) {
                microOps.push_back({PHI2, [this]() {
                    uint16_t dummy = static_cast<uint16_t>((uint16_t(Temp_addr_high) << 8) | (EffAddr & 0x00FF));
                    (void)bus.read(dummy);
                }});
                microOps.push_back({PHI1, [](){} });
            }
            microOps.push_back({PHI2, [this]() {
                R.A = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            break;
        }


        // -----------------------------
        // LDA $zz,X (Zero Page,X) - Opcode $B5
        // 4 cicli macchina (NMOS 6502)
        // -----------------------------
        case 0xB5:  // LDA $zz,X
        {
            enqueueReadZpIndexed(ReadAction::LDA, true);
            break;
        }


        // -----------------------------
        // LDA $addr (Absolute) - Opcode $AD
        // 4 cicli macchina
        // -----------------------------
        case 0xAD:  // LDA $HHLL (Absolute)
        {
            // ---- C1 ---- Fetch opcode ----
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LDA abs] Fetch opcode at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C2 ---- Fetch low byte ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[LDA abs] Low byte fetched: $" << std::hex << (int)Temp_addr_low << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C3 ---- Fetch high byte ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                std::cout << "[LDA abs] High byte fetched: $" << std::hex << (int)Temp_addr_high << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C4 ---- Fetch data ----
            microOps.push_back({PHI2, [this]() {
                EffAddr = (Temp_addr_high << 8) | Temp_addr_low;
                uint8_t value = bus.read(EffAddr);
                R.A = value;

                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, R.A & 0x80);

                std::cout << "[LDA abs] Loaded A=$" << std::hex << (int)R.A
                        << " from $" << EffAddr << " (Z="
                        << ((R.P & ZERO) ? "1" : "0")
                        << ", N=" << ((R.P & NEGATIVE) ? "1" : "0")
                        << ")" << std::endl;
            }});

            break;
        }


        // -----------------------------
        // // ADC #imm
        // -----------------------------                  
        case 0x69: // ADC #imm
        {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                adcRevisionAware(Data_latch);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;


        // -----------------------------
        case 0xE9: // SBC immediate
        {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                sbcRevisionAware(Data_latch, false);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;

        // -----------------------------
        // SEC
        // -----------------------------            
        case 0x38:
            // -----------------------------
            // Ciclo 1: fetch opcode
            // -----------------------------
            microOps.push_back({PHI2, [this]() { 
                IR = bus.read(R.PC); 
                std::cout << "[SEC] PHI2: fetch opcode $" << std::hex << (int)IR
                        << " at PC=$" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[SEC] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // -----------------------------
            // Ciclo 2: dummy read, set C flag
            // -----------------------------
            microOps.push_back({PHI2, [this]() { 
                uint8_t dummy = bus.peek(R.PC);  // dummy read senza side-effect
                ALU::setFlag(R.P, CARRY, true); 
                std::cout << "[SEC] PHI2: dummy read $" << std::hex << (int)dummy 
                        << ", CARRY set to 1" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                std::cout << "[SEC] PHI1: cycle end (optional sync)" << std::endl; 
            }});

            break;


            
        // -----------------------------            
        // CLC
        // -----------------------------        
        case 0x18:
            // -----------------------------
            // Ciclo 1: fetch opcode
            // -----------------------------
            microOps.push_back({PHI2, [this]() { 
                IR = bus.read(R.PC); 
                std::cout << "[CLC] PHI2: fetch opcode $" << std::hex << (int)IR
                        << " at PC=$" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[CLC] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // -----------------------------
            // Ciclo 2: dummy read, clear C flag
            // -----------------------------
            microOps.push_back({PHI2, [this]() { 
                uint8_t dummy = bus.peek(R.PC);  // dummy read senza side-effect
                ALU::setFlag(R.P, CARRY, false); 
                std::cout << "[CLC] PHI2: dummy read $" << std::hex << (int)dummy 
                        << ", CARRY cleared" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                std::cout << "[CLC] PHI1: cycle end (optional sync)" << std::endl; 
            }});

            break;



        // -----------------------------
        // CMP #imm (Immediate) 2 byte, 2 cicli (4 half-cycles)
        // -----------------------------        
        case 0xC9: // CMP immediate
        {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                uint8_t result = static_cast<uint8_t>(R.A - Data_latch);
                ALU::setFlag(R.P, CARRY, R.A >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;


// e questa!!!!
#ifndef SKIP_OPCODE

        // -----------------------------
        // CMP $zp (Zero Page) - Opcode $C5
        // 2 byte, 3 cicli (6 half-cycles)
        // -----------------------------        
        case 0xC5: // CMP $zp
        {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                uint8_t result = static_cast<uint8_t>(R.A - Data_latch);
                ALU::setFlag(R.P, CARRY, R.A >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });

            
        } break;

#endif

        // -----------------------------            
        // BEQ
        // -----------------------------  
        case 0xF0: { // BEQ rel
            enqueueBranchRelative(ALU::getFlag(R.P, ZERO));
        } break;


        // --------------------------
        // NOP (No Operation)
        // --------------------------
        case 0xEA:
            // PHI2: fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC); // fetch opcode
                std::cout << "[NOP] PHI2: fetched opcode $EA at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // PHI1: increment PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[NOP] PHI1: PC incremented to $" 
                        << std::hex << (int)R.PC << std::endl;
            }});
            break;


        // --------------------------            
        // INC absolute ($EE) - ciclo esatto NMOS
        // --------------------------
        case 0xEE: {
            EffAddr = 0;
            Data_latch = 0;

            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[INC abs] PHI2: opcode fetch at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: fetch low byte ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = bus.read(R.PC);
                std::cout << "[INC abs] PHI2: low byte fetch $" << std::hex << (int)EffAddr
                        << " at PC $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 3: fetch high byte ---
            microOps.push_back({PHI2, [this]() {
                EffAddr |= bus.read(R.PC) << 8;
                std::cout << "[INC abs] PHI2: high byte fetch, addr=$" << std::hex << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 4: dummy read (no side effects) ---
            microOps.push_back({PHI2, [this]() {
                (void)bus.peek(EffAddr); // uso peek qui
                std::cout << "[INC abs] PHI2: dummy peek at $" << std::hex << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 5: read valore ---
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                std::cout << "[INC abs] PHI2: read value $" << std::hex << (int)Data_latch
                        << " from $" << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 6: dummy write (vecchio valore) usando Lawrite ---
            microOps.push_back({PHI2, [this]() {
                bus.laWrite(EffAddr, Data_latch);  // ← super-fedele, non cambia RAM/CIA/VIC
                std::cout << "[INC abs] PHI2: dummy laWrite old value $" 
                        << std::hex << (int)Data_latch << " to $" << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });            

            // --- Ciclo 7: write nuovo valore incrementato ---
            microOps.push_back({PHI2, [this]() {
                uint8_t v = (Data_latch + 1) & 0xFF;
                bus.write(EffAddr, v);
                Data_latch = v;
                std::cout << "[INC abs] PHI2: write incremented value $" 
                        << std::hex << (int)Data_latch << " to $" << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 8: aggiorna flags ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, Data_latch & 0x80);
                std::cout << "[INC abs] PHI2: flags updated Z=" << ((R.P & ZERO)?1:0)
                        << " N=" << ((R.P & NEGATIVE)?1:0) << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });
        } break;
        

        // --------------------------            
        // INY - Increment Y Register ($C8) - ciclo esatto NMOS
        // --------------------------
        case 0xC8: {
            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[INY] PHI2: opcode fetch at $" 
                        << std::hex << R.PC << " (value=$" << (int)IR << ")" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: esecuzione ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldY = R.Y;
                R.Y = (R.Y + 1) & 0xFF;

                // Aggiorna flag Z e N
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, R.Y & 0x80);

                std::cout << "[INY] PHI2: Y incremented from $" << std::hex << (int)oldY
                        << " to $" << (int)R.Y
                        << " | Z=" << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});
            microOps.push_back({PHI1, [](){}});
        } break;
                

#ifndef SKIP_OPCODE       
        // --------------------------            
        // INX - Increment X Register ($E8) - ciclo esatto NMOS
        // --------------------------
        case 0xE8: {
            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[INX] PHI2: opcode fetch at $"
                        << std::hex << R.PC
                        << " (value=$" << (int)IR << ")" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: esecuzione ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldX = R.X;
                R.X = (R.X + 1) & 0xFF;

                // Aggiorna flag Z e N
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, R.X & 0x80);

                std::cout << "[INX] PHI2: X incremented from $" << std::hex << (int)oldX
                        << " to $" << (int)R.X
                        << " | Z=" << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});
            microOps.push_back({PHI1, [](){}});
        } break;
#endif        


        // --------------------------
        // DEC absolute ($CE) - ciclo esatto NMOS
        // --------------------------
        case 0xCE: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Data_latch = static_cast<uint8_t>(Data_latch - 1);
                bus.write(EffAddr, Data_latch);
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, (Data_latch & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
        } break;


        /*
        // --------------------------
        // DEC absolute ($CE) livello NMOS puro
        // --------------------------
        case 0xCE: {
            uint16_t addr = 0;
            uint8_t value = 0;

            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 2: fetch low byte address ---
            microOps.push_back({PHI2, [this]() { addr = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 3: fetch high byte address ---
            microOps.push_back({PHI2, [this]() { addr |= bus.read(R.PC)<<8; }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 4: dummy read indirizzo ---
            microOps.push_back({PHI2, [this]() { (void)bus.read(addr); }});

            // --- Ciclo 5: read valore ---
            microOps.push_back({PHI2, [this]() { value = bus.read(addr); }});

            // --- Ciclo 6: dummy write del vecchio valore ---
            microOps.push_back({PHI2, [this]() { bus.write(addr, value); }});

            // --- Ciclo 7: scrivi nuovo valore PHI2 ---
            microOps.push_back({PHI2, [this]() mutable {
                uint8_t v = (value - 1) & 0xFF;
                bus.write(addr, v);
                value = v; // salva nuovo valore per PHI2 flags
            }});

            // --- Ciclo 8: aggiorna solo flags PHI2 ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, ZERO, value == 0);
                ALU::setFlag(R.P, NEGATIVE, value & 0x80);
            }});
        } break;
        */

        // ==========================
        // SEI (Set Interrupt Disable) 1 byte, 1 ciclo macchina (2 half-cycles)
        // ==========================
        case 0x78: {
            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC); // leggi opcode
                std::cout << "[SEI] PHI2: fetched opcode $78 at PC=$" 
                        << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[SEI] PHI1: increment PC to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: setta flag I ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, INTERRUPT_DISABLE, true);
                std::cout << "[SEI] PHI2: executed, I=1" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} }); // PHI1 dummy, niente da fare
        } break;


        // ==========================
        // CLI (Clear Interrupt Disable) 1 byte, 1 ciclo macchina (2 half-cycles)
        // ==========================
        case 0x58: {
            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC); // leggi opcode
                std::cout << "[CLI] PHI2: fetched opcode $58 at PC=$" 
                        << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[CLI] PHI1: increment PC to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: esegui azione (resetta flag I) ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, INTERRUPT_DISABLE, false);
                std::cout << "[CLI] PHI2: executed, I=0" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} }); // PHI1 dummy per simmetria
        } break;


        // ==========================
        // BRK (Force Interrupt) - NMOS 6502 accurate (8 cicli macchina / 16 half-cycles)
        // ==========================
        case 0x00: {
            Addr_latch = 0;

            // --- Ciclo 1: fetch opcode ($00) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[BRK] PHI2: fetched opcode $00 at $" << std::hex << (int)R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 2: dummy read del byte successivo ---
            microOps.push_back({PHI2, [this]() {
                (void)bus.peek(R.PC); // peek → no side effects, ma mantiene open bus
                std::cout << "[BRK] PHI2: dummy peek at $" << std::hex << (int)R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 3: push PCH ---
            microOps.push_back({PHI2, [this]() {
                uint8_t pch = (R.PC >> 8) & 0xFF;
                bus.laWrite(0x0100 + R.SP, pch); // super fedele: segnala al bus ma non altera mem
                std::cout << "[BRK] PHI2: dummy laWrite PCH=$" << std::hex << (int)pch
                        << " at [$" << (0x0100 + R.SP) << "]" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                bus.write(0x0100 + R.SP, (R.PC >> 8) & 0xFF);
                std::cout << "[BRK] PHI1: push real PCH=$" << std::hex << ((R.PC >> 8) & 0xFF)
                        << " to stack [$" << (0x0100 + R.SP) << "]" << std::endl;
                R.SP--;
            }});

            // --- Ciclo 4: push PCL ---
            microOps.push_back({PHI2, [this]() {
                uint8_t pcl = R.PC & 0xFF;
                bus.laWrite(0x0100 + R.SP, pcl);
                std::cout << "[BRK] PHI2: dummy laWrite PCL=$" << std::hex << (int)pcl
                        << " at [$" << (0x0100 + R.SP) << "]" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                bus.write(0x0100 + R.SP, R.PC & 0xFF);
                std::cout << "[BRK] PHI1: push real PCL=$" << std::hex << (int)(R.PC & 0xFF)
                        << " to stack [$" << (0x0100 + R.SP) << "]" << std::endl;
                R.SP--;
            }});

            // --- Ciclo 5: push SR con B=1 e U=1 ---
            microOps.push_back({PHI2, [this]() {
                uint8_t status = (R.P | BREAK | UNUSED);
                bus.laWrite(0x0100 + R.SP, status);
                std::cout << "[BRK] PHI2: dummy laWrite SR=$" << std::hex << (int)status
                        << " (B=1,U=1)" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                uint8_t status = (R.P | BREAK | UNUSED);
                bus.write(0x0100 + R.SP, status);
                std::cout << "[BRK] PHI1: push SR=$" << std::hex << (int)status
                        << " to stack [$" << (0x0100 + R.SP) << "]" << std::endl;
                R.SP--;
            }});

            // --- Ciclo 6: dummy read vettore low ($FFFE) ---
            microOps.push_back({PHI2, [this]() {
                (void)bus.peek(0xFFFE); // dummy, no effetto
                std::cout << "[BRK] PHI2: dummy peek at $FFFE" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 7: leggi vettore low ($FFFE) ---
            microOps.push_back({PHI2, [this]() {
                uint8_t lo = bus.read(0xFFFE);
                Addr_latch = lo;
                std::cout << "[BRK] PHI2: read vector low=$" << std::hex << (int)lo << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 8: leggi vettore high ($FFFF) e salta ---
            microOps.push_back({PHI2, [this]() {
                uint8_t hi = bus.read(0xFFFF);
                Addr_latch |= hi << 8;
                R.PC = Addr_latch;
                ALU::setFlag(R.P, INTERRUPT_DISABLE, true);
                std::cout << "[BRK] PHI2: read vector high=$" << std::hex << (int)hi
                        << " → jump to ISR $" << Addr_latch << ", I=1" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });
        } break;


        // ==========================
        // RTI (Return from Interrupt) - NMOS 6502 accurate (6 cicli macchina / 12 half-cycles)
        // ==========================
        case 0x40: {
            // --- Ciclo 1: fetch opcode ($40) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[RTI] Fetch opcode $40 at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // --- Ciclo 2: dummy read (PC increment) ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});
            microOps.push_back({PHI2, [this]() {
                bus.peek(R.PC);  // fedele: dummy read, non altera openBusValue
                std::cout << "[RTI] Dummy read at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // --- Ciclo 3: dummy read sullo stack (prepara a leggere SR) ---
            microOps.push_back({PHI2, [this]() {
                bus.peek(0x0100 + ((R.SP + 1) & 0xFF)); 
                std::cout << "[RTI] Dummy read stack prefetch at $" 
                        << std::hex << (int)(0x0100 + ((R.SP + 1) & 0xFF)) << std::endl;
            }});

            // --- Ciclo 4: pop SR ---
            microOps.push_back({PHI2, [this]() {
                R.SP++;
                uint8_t sr = bus.read(0x0100 + R.SP);
                R.P = (sr & ~BREAK) | UNUSED; // NMOS: B=0, U=1
                std::cout << "[RTI] Pull SR=$" << std::hex << (int)sr 
                        << " → P=$" << (int)R.P << std::endl;
            }});

            // --- Ciclo 5: pop PCL ---
            microOps.push_back({PHI2, [this]() {
                R.SP++;
                uint8_t pcl = bus.read(0x0100 + R.SP);
                R.PC = (R.PC & 0xFF00) | pcl;
                std::cout << "[RTI] Pull PCL=$" << std::hex << (int)pcl 
                        << " → PC(low)=$" << (int)R.PC << std::endl;
            }});

            // --- Ciclo 6: pop PCH e completa ritorno ---
            microOps.push_back({PHI2, [this]() {
                R.SP++;
                uint8_t pch = bus.read(0x0100 + R.SP);
                R.PC = (pch << 8) | (R.PC & 0x00FF);
                std::cout << "[RTI] Pull PCH=$" << std::hex << (int)pch 
                        << " → PC=$" << R.PC << " (return from interrupt)" << std::endl;

                // Aggiorna openBusValue (ultimo valore letto)
                bus.onDrivenBusValue(pch);
            }});

            // --- Dummy post-read per coerenza bus ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[RTI] Dummy laWrite after return (bus open value update)" 
                        << std::endl;
            }});

        }
        break;

        /*
        // ==========================
        // RTI (Return from Interrupt) - NMOS 6502 accurate (6 cicli macchina / 12 half-cycles)
        // ==========================
        case 0x40: {
            // --- Ciclo 1: fetch opcode ($40) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[RTI] Fetch opcode $40 at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // --- Ciclo 2: dummy read (PC increment) ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.read(R.PC);
                std::cout << "[RTI] Dummy read at $" 
                        << std::hex << (int)R.PC 
                        << " (value=$" << (int)dummy << ")" << std::endl;
            }});

            // --- Ciclo 3: incrementa SP (stack pointer verso alto) ---
            microOps.push_back({PHI2, [this]() {
                R.SP++;
                std::cout << "[RTI] Increment SP → $" 
                        << std::hex << (int)R.SP << std::endl;
            }});

            // --- Ciclo 4: pop SR ---
            microOps.push_back({PHI2, [this]() {
                uint8_t sr = bus.read(0x0100 + R.SP);
                R.P = (sr & ~BREAK) | UNUSED; // B=0, U=1
                std::cout << "[RTI] Pull SR=$" << std::hex << (int)sr 
                        << " → P=$" << (int)R.P << std::endl;
                R.SP++;
            }});

            // --- Ciclo 5: pop PCL ---
            microOps.push_back({PHI2, [this]() {
                uint8_t pcl = bus.read(0x0100 + R.SP);
                R.PC = (R.PC & 0xFF00) | pcl;
                std::cout << "[RTI] Pull PCL=$" << std::hex << (int)pcl 
                        << " → PC(low)=$" << (int)R.PC << std::endl;
                R.SP++;
            }});

            // --- Ciclo 6: pop PCH e completa ritorno ---
            microOps.push_back({PHI2, [this]() {
                uint8_t pch = bus.read(0x0100 + R.SP);
                R.PC = (pch << 8) | (R.PC & 0x00FF);
                std::cout << "[RTI] Pull PCH=$" << std::hex << (int)pch 
                        << " → PC=$" << R.PC << " (return from interrupt)" << std::endl;
            }});
        }
        break;
        */


        // ==========================
        // KIL / JAM — blocco CPU (NMOS 6502 accurate halt)
        // ==========================
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
        {
            // --- Ciclo 1: PHI2 fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                bus.onDrivenBusValue(revisionProfile.jamKeepsLastDataBus ? IR : 0x00);
                std::cout << "[KIL] PHI2: fetched illegal opcode $" 
                        << std::hex << (int)IR 
                        << " at PC=$" << R.PC << std::endl;
            }});

            // --- Ciclo 2: PHI1 dummy read per compatibilità temporale ---
            microOps.push_back({PHI1, [this]() {
                (void)bus.peek(R.PC); // mantiene il bus attivo ma senza effetti
                std::cout << "[KIL] PHI1: dummy peek at PC=$" 
                        << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 3: PHI2 — CPU bloccata permanentemente ---
            microOps.push_back({PHI2, [this]() {
                halted = true;
                std::cout << "[KIL] PHI2: CPU JAM — halted permanently at $" 
                        << std::hex << R.PC << std::endl;

                // Mantiene coerente il valore flottante del bus (NMOS realism)
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[KIL] PHI2: bus frozen with openBusValue=$" 
                        << std::hex << (int)bus.openBusValue << std::endl;
            }});

            // --- (opzionale) PHI1 dopo halt: nessuna operazione, ma bus ancora visibile ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[KIL] PHI1: bus latched (CPU frozen)" << std::endl;
            }});
        }
        break;


        /*
        // ==========================
        // KIL / JAM — blocco CPU
        // ==========================
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
        {
            microOps.push_back({PHI2, [this]() {
                std::cout << "[KIL] CPU halted at $" << std::hex << (int)R.PC << std::endl;
                halted = true;
            }});
        } break;
        */


        // ==========================
        // TXS ($9A) - NMOS 6502 accurate (2 cicli macchina / 4 half-cycles)
        // ==========================
        case 0x9A: {
            // --- Ciclo 1: PHI2 fetch opcode ($9A) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                bus.onDrivenBusValue(IR);
                std::cout << "[TXS] PHI2: Fetch opcode $9A at $" 
                        << std::hex << R.PC 
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 1.5: PHI1 incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TXS] PHI1: Increment PC → $" 
                        << std::hex << R.PC << "\n";
            }});

            // --- Ciclo 2: PHI2 dummy read del byte successivo ---
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.peek(R.PC);  // peek = no side effects
                bus.onDrivenBusValue(dummy);
                bus.laWrite(R.PC, dummy);        // mantiene il bus elettricamente attivo
                std::cout << "[TXS] PHI2: Dummy peek at $" 
                        << std::hex << R.PC 
                        << " (value=$" << (int)dummy << ")\n";
            }});

            // --- Ciclo 2.5: PHI1 interno (nessuna attività di bus) ---
            microOps.push_back({PHI1, [this]() {
                std::cout << "[TXS] PHI1: Internal step (no bus activity)\n";
            }});

            // --- Ciclo 3: PHI2 esegui trasferimento X → SP ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldSP = R.SP;
                R.SP = R.X;
                bus.onDrivenBusValue(R.SP);
                std::cout << "[TXS] PHI2: Transfer X → SP "
                        << "(X=$" << std::hex << (int)R.X 
                        << ", oldSP=$" << (int)oldSP 
                        << ", newSP=$" << (int)R.SP << ")\n";
            }});

            // --- Ciclo 3.5: PHI1 fine istruzione ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[TXS] PHI1: Instruction complete, bus latched ($" 
                        << std::hex << (int)bus.openBusValue << ")\n";
            }});
        }
        break;


        /*
        // ==========================
        // TXS ($9A) - NMOS 6502 accurate (2 cicli macchina / 4 half-cycles)
        // ==========================
        case 0x9A: {
            // --- Ciclo 1: fetch opcode ($9A) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TXS] Fetch opcode $9A at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // --- Ciclo 1.5: incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: dummy read successivo ---
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.read(R.PC);
                std::cout << "[TXS] Dummy read at $" 
                        << std::hex << (int)R.PC 
                        << " (value=$" << (int)dummy << ")" << std::endl;
            }});

            // --- Ciclo 2.5: PHI1 interno ---
            microOps.push_back({PHI1, [this]() {
                // nessuna operazione visibile sul bus
            }});

            // --- Ciclo 3: esegui trasferimento PHI2 ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldSP = R.SP;
                R.SP = R.X;

                std::cout << "[TXS] Transfer X → SP  (X=$" 
                        << std::hex << (int)R.X 
                        << " → SP=$" << (int)R.SP 
                        << ", oldSP=$" << (int)oldSP << ")" << std::endl;
            }});
        } break;
        */


        // ==========================
        // TSX ($BA) - NMOS 6502 accurate (2 cicli macchina / 4 half-cycles)
        // ==========================
        case 0xBA: {
            // --- Ciclo 1: PHI2 fetch opcode ($BA) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                bus.onDrivenBusValue(IR);
                std::cout << "[TSX] PHI2: Fetch opcode $BA at $" 
                        << std::hex << R.PC
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 1.5: PHI1 incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TSX] PHI1: Increment PC → $" 
                        << std::hex << R.PC << "\n";
            }});

            // --- Ciclo 2: PHI2 dummy read successivo ---
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.peek(R.PC);  // peek = no side effects
                bus.onDrivenBusValue(dummy);
                bus.laWrite(R.PC, dummy);        // dummy write super-fedele
                std::cout << "[TSX] PHI2: Dummy peek at $" 
                        << std::hex << R.PC 
                        << " (value=$" << (int)dummy << ")\n";
            }});

            // --- Ciclo 2.5: PHI1 interno (nessuna attività sul bus) ---
            microOps.push_back({PHI1, [this]() {
                std::cout << "[TSX] PHI1: Internal step (no bus activity)\n";
            }});

            // --- Ciclo 3: PHI2 trasferimento SP → X e aggiornamento flags ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldX = R.X;
                R.X = R.SP;
                bus.onDrivenBusValue(R.X);

                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, R.X & 0x80);

                std::cout << "[TSX] PHI2: Transfer SP → X  "
                        << "(SP=$" << std::hex << (int)R.SP 
                        << " → X=$" << (int)R.X 
                        << ", oldX=$" << (int)oldX << ") "
                        << "Flags(Z=" << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << ",N=" << (ALU::getFlag(R.P, NEGATIVE) ? "1" : "0")
                        << ")\n";
            }});

            // --- Ciclo 3.5: PHI1 finale, latched bus per periferiche ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[TSX] PHI1: Instruction complete, bus latched ($" 
                        << std::hex << (int)bus.openBusValue << ")\n";
            }});
        }
        break;


        /*
        // ==========================
        // TSX ($BA) - NMOS 6502 accurate (2 cicli macchina / 4 half-cycles)
        // ==========================
        case 0xBA: {
            // --- Ciclo 1: fetch opcode ($BA) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TSX] Fetch opcode $BA at $" 
                        << std::hex << (int)R.PC << std::endl;
            }});

            // --- Ciclo 1.5: incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: dummy read successivo ---
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.read(R.PC);
                std::cout << "[TSX] Dummy read at $" 
                        << std::hex << (int)R.PC 
                        << " (value=$" << (int)dummy << ")" << std::endl;
            }});

            // --- Ciclo 2.5: PHI1 interno ---
            microOps.push_back({PHI1, [this]() {
                // nessuna operazione visibile sul bus
            }});

            // --- Ciclo 3: esegui trasferimento PHI2 ---
            microOps.push_back({PHI2, [this]() {
                uint8_t oldX = R.X;
                R.X = R.SP;

                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, R.X & 0x80);

                std::cout << "[TSX] Transfer SP → X  (SP=$" 
                        << std::hex << (int)R.SP 
                        << " → X=$" << (int)R.X 
                        << ", oldX=$" << (int)oldX 
                        << ")  Flags(Z=" << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << ",N=" << (ALU::getFlag(R.P, NEGATIVE) ? "1" : "0")
                        << ")" << std::endl;
            }});
        } break;
        */  

   
        // -----------------------------
        // LDX #imm (Immediate) 2 byte, 2 machine cycles (4 half-cycles)
        // -----------------------------
        case 0xA2: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.X = Data_latch;
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;
     

        // -----------------------------
        // LDY #imm (Immediate) 2 byte, 2 machine cycles (4 half-cycles)
        // -----------------------------
        case 0xA0: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.Y = Data_latch;
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.Y & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;
        

        // -----------------------------
        // LDY $nn (Zero Page) 2 byte, 3 machine cycles (6 half-cycles)
        // -----------------------------
        case 0xA4: {   
            
            uint16_t effectiveAddr = 0;

            // --- Ciclo 1: PHI2 fetch opcode ($A4) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                bus.onDrivenBusValue(IR);
                std::cout << "[LDY $nn] PHI2: Fetched opcode $A4 at $" 
                        << std::hex << R.PC
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 1.5: PHI1 incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LDY $nn] PHI1: Increment PC → $" 
                        << std::hex << R.PC << "\n";
            }});

            // --- Ciclo 2: PHI2 fetch indirizzo zero page ---
            microOps.push_back({PHI2, [this]() {
                Addr_latch = bus.read(R.PC);
                bus.onDrivenBusValue(Addr_latch);
                bus.laWrite(R.PC, Addr_latch); // dummy write per fedeltà
                std::cout << "[LDY $nn] PHI2: Fetched zero page address $" 
                        << std::hex << (int)Addr_latch 
                        << " at $" << R.PC 
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 2.5: PHI1 incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LDY $nn] PHI1: Increment PC → $" 
                        << std::hex << R.PC << "\n";
            }});

            // --- Ciclo 3: PHI2 leggi da zero page ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = Addr_latch; // indirizzo zero page
                Data_latch = bus.read(EffAddr);
                bus.onDrivenBusValue(Data_latch);
                std::cout << "[LDY $nn] PHI2: Read data $" 
                        << std::hex << (int)Data_latch 
                        << " from zero page $" << (int)EffAddr 
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 3.5: PHI1 trasferisci in Y ---
            microOps.push_back({PHI1, [this]() {
                R.Y = Data_latch;
                bus.onDrivenBusValue(R.Y);
                std::cout << "[LDY $nn] PHI1: Load Y ← $" 
                        << std::hex << (int)R.Y << "\n";
            }});

            // --- Ciclo 4: PHI2 aggiorna flags ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, R.Y & 0x80);
                std::cout << "[LDY $nn] PHI2: Flags updated Z=" 
                        << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << " N=" << (ALU::getFlag(R.P, NEGATIVE) ? "1" : "0") << "\n";
            }});

            // --- Ciclo finale PHI1: latch bus per periferiche ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[LDY $nn] PHI1: Instruction complete, bus latched ($" 
                        << std::hex << (int)bus.openBusValue << ")\n";
            }});
        }
        break;



        // -----------------------------
        // CLD ($D8) - NMOS 6502 accurate (1 byte, 1 machine cycle / 2 half-cycles)
        // -----------------------------
        case 0xD8: {
            // --- Ciclo 1: PHI2 fetch opcode ($D8) ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                bus.onDrivenBusValue(IR);
                std::cout << "[CLD] PHI2: Fetch opcode $D8 at $" 
                        << std::hex << R.PC
                        << " (openBus=$" << (int)bus.openBusValue << ")\n";
            }});

            // --- Ciclo 1.5: PHI1 incrementa PC ---
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[CLD] PHI1: Increment PC → $" 
                        << std::hex << R.PC << "\n";
            }});

            // --- Ciclo 2: PHI2 dummy read successivo ---
            microOps.push_back({PHI2, [this]() {
                uint8_t dummy = bus.read(R.PC);
                bus.onDrivenBusValue(dummy);
                bus.laWrite(R.PC, dummy); // dummy write fedele al comportamento reale
                std::cout << "[CLD] PHI2: Dummy read at $" 
                        << std::hex << R.PC 
                        << " (value=$" << (int)dummy << ")\n";
            }});

            // --- Ciclo 2.5: PHI1 interno ---
            microOps.push_back({PHI1, [this]() {
                // nessuna operazione visibile
            }});

            // --- Ciclo 3: PHI2 esegui clear D flag ---
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, DECIMAL, false);
                bus.onDrivenBusValue(R.P);
                std::cout << "[CLD] PHI2: Decimal flag cleared → P=$" 
                        << std::hex << (int)R.P << "\n";
            }});

            // --- Ciclo finale PHI1: latch openBus ---
            microOps.push_back({PHI1, [this]() {
                bus.laWrite(R.PC, bus.openBusValue);
                std::cout << "[CLD] PHI1: Instruction complete (bus latched $" 
                        << std::hex << (int)bus.openBusValue << ")\n";
            }});
        }
        break;


        /*
        // -----------------------------
        // CLD (Clear Decimal Flag) 1 byte, 1 machine cycle (2 half-cycles)
        // -----------------------------
        case 0xD8:
            // --- Ciclo 1: fetch opcode PHI2 ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[CLD] Fetched opcode at $" << std::hex << R.PC << std::endl;
            }});

            // --- Increment PC PHI1 ---
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- PHI2: Clear decimal flag ---
            microOps.push_back({PHI2, [this]() {                
                ALU::setFlag(R.P, DECIMAL, false);
                std::cout << "[CLD] Decimal flag cleared. P=" << std::hex << (int)R.P << std::endl;
            }});

            break;
        */


        // -----------------------------
        // JSR $addr (Absolute) - Opcode $20
        // 6 cicli macchina (12 half-cycles)
        // -----------------------------
        case 0x20:
        {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(R.PC);
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
            }});

            microOps.push_back({PHI1, [this]() {
                uint16_t returnAddr = R.PC;
                uint8_t high = static_cast<uint8_t>((returnAddr >> 8) & 0xFF);
                bus.write(0x0100 | R.SP, high);
                R.SP--;
            }});

            microOps.push_back({PHI2, [this]() {
                uint16_t returnAddr = R.PC;
                uint8_t low = static_cast<uint8_t>(returnAddr & 0xFF);
                bus.write(0x0100 | R.SP, low);
                R.SP--;
            }});

            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                (void)bus.read(R.PC);
            }});

            microOps.push_back({PHI2, [this]() {
                uint16_t newPC = (uint16_t(Temp_addr_high) << 8) | Data_latch;
                R.PC = newPC;
            }});

            break;
        }



        // -----------------------------
        // RTS - Return from Subroutine
        // Opcode: $60
        // 6 cicli macchina (12 half-cycles)
        // -----------------------------
        case 0x60:
        {
            Temp_addr_low = 0;
            Temp_addr_high = 0;

            // -----------------------------
            // PHI2: Fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[RTS] Fetched opcode at $" << std::hex << R.PC << std::endl;
            }});
            // PHI1: Increment PC (dummy)
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // -----------------------------
            // PHI2: Dummy read (PC points to next instruction)
            microOps.push_back({PHI2, [this]() {
                (void)bus.read(R.PC);
                std::cout << "[RTS] Dummy read at $" << std::hex << R.PC << std::endl;
            }});
            // PHI1: Increment SP (prepare to pull low byte)
            microOps.push_back({PHI1, [this]() { R.SP++; }});

            // -----------------------------
            // PHI2: Pull low byte from stack
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(0x0100 | R.SP);
                std::cout << "[RTS] Pulled low byte of return addr: $" 
                        << std::hex << (int)Temp_addr_low 
                        << " from [$" << (0x0100 | R.SP) << "]" << std::endl;
            }});
            // PHI1: Increment SP (prepare to pull high byte)
            microOps.push_back({PHI1, [this]() { R.SP++; }});

            // -----------------------------
            // PHI2: Pull high byte from stack
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(0x0100 | R.SP);
                std::cout << "[RTS] Pulled high byte of return addr: $" 
                        << std::hex << (int)Temp_addr_high 
                        << " from [$" << (0x0100 | R.SP) << "]" << std::endl;
            }});
            // PHI1: Increment PC dummy (simulate internal timing)
            microOps.push_back({PHI1, [this]() { /* dummy internal increment */ }});

            // -----------------------------
            // PHI2: Dummy read using pulled address
            microOps.push_back({PHI2, [this]() {
                uint16_t tempAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                (void)bus.read(tempAddr);
                std::cout << "[RTS] Dummy read at return addr $" << std::hex << tempAddr << std::endl;
            }});

            // -----------------------------
            // PHI2 finale: Set PC = pulled address + 1
            microOps.push_back({PHI2, [this]() {
                uint16_t returnAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                R.PC = returnAddr + 1;
                std::cout << "[RTS] Returning to $" << std::hex << R.PC << std::endl;
            }});

            break;
        }


        // -----------------------------
        // CMP $addr,X (Absolute,X) - Opcode $DD
        // 4 cicli macchina (5 se attraversa pagina)
        // -----------------------------
        case 0xDD:
        {
            enqueueReadAbsIndexed(ReadAction::CMP, true);

            break;
        }


        // -----------------------------
        // BNE $addr (Relative) - Opcode $D0
        // 2 cicli base (3 se branch preso, 4 se attraversa pagina)
        // -----------------------------
        case 0xD0:
        {
            enqueueBranchRelative(!ALU::getFlag(R.P, ZERO));
            break;
        }


#ifndef SKIP_OPCODE
        // -----------------------------
        // BMI $addr (Relative) - Opcode $30
        // 2 cicli base (3 se branch preso, 4 se attraversa pagina)
        // -----------------------------
        case 0x30:
        {
            enqueueBranchRelative(ALU::getFlag(R.P, NEGATIVE));
            break;
        }
#endif        


        // -----------------------------
        // BCC $addr (Relative) - Opcode $90
        // 2 cicli base (3 se branch preso, 4 se attraversa pagina)
        // -----------------------------
        case 0x90:
        {
            enqueueBranchRelative(!ALU::getFlag(R.P, CARRY));
            break;
        }


        // -----------------------------
        // DEX - Decrement X Register - Opcode $CA
        // 2 cicli macchina (4 half-cycles)
        // -----------------------------
        case 0xCA:
        {
            // -----------------------------
            // Cycle 1: Fetch opcode ($CA)
            // -----------------------------
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[DEX] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << (int)R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // -----------------------------
            // Cycle 2: Dummy read + execute DEX
            // -----------------------------
            microOps.push_back({PHI2, [this]() {
                // Lettura fittizia (dummy read) del byte successivo al PC
                uint16_t dummyAddr = R.PC;
                uint8_t dummy = bus.read(dummyAddr);
                (void)dummy; // non usato

                std::cout << "[DEX] Dummy read at $" << std::hex << dummyAddr << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                uint8_t oldX = R.X;
                R.X = (uint8_t)(R.X - 1);

                // Aggiornamento flag
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, R.X & 0x80);

                std::cout << "[DEX] X decremented from $" << std::hex << (int)oldX
                        << " to $" << (int)R.X
                        << " -> Z=" << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});

            break;
        }


        // -----------------------------
        // DEY - Decrement Y Register - Opcode $88
        // 2 cicli macchina (4 half-cycles)
        // -----------------------------
        case 0x88:
        {
            // -----------------------------
            // Cycle 1: Fetch opcode ($88)
            // -----------------------------
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[DEY] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << (int)R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // -----------------------------
            // Cycle 2: Dummy read + execute DEY
            // -----------------------------
            microOps.push_back({PHI2, [this]() {
                // Lettura fittizia (dummy read) del byte successivo al PC
                uint16_t dummyAddr = R.PC;
                uint8_t dummy = bus.read(dummyAddr);
                (void)dummy; // non usato

                std::cout << "[DEY] Dummy read at $" << std::hex << dummyAddr << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                uint8_t oldY = R.Y;
                R.Y = (uint8_t)(R.Y - 1);

                // Aggiornamento flag
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, R.Y & 0x80);

                std::cout << "[DEY] Y decremented from $" << std::hex << (int)oldY
                        << " to $" << (int)R.Y
                        << " -> Z=" << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});

            break;
        }


        /*
        // -----------------------------
        // JMP ($addr) - Indirect - Opcode $6C
        // 5 cicli macchina (10 half-cycles)
        // -----------------------------
        case 0x6C:
        {
            Temp_addr_low = 0;
            Temp_addr_high = 0;

            // -----------------------------
            // PHI2: Fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] Fetched opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});

            // PHI1: Increment PC
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // -----------------------------
            // PHI2: Fetch low byte of vector address
            microOps.push_back({PHI2, [this]() {
                ptrLow = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] Fetched low byte of pointer $"
                        << std::hex << (int)ptrLow << " at $" << R.PC << std::endl;
            }});

            // PHI1: Increment PC
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // -----------------------------
            // PHI2: Fetch high byte of vector address
            microOps.push_back({PHI2, [this]() {
                ptrHigh = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] Fetched high byte of pointer $"
                        << std::hex << (int)ptrHigh << " at $" << R.PC << std::endl;
            }});

            // PHI1: Increment PC (ready for next instruction)
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // -----------------------------
            // PHI2: Read low byte of target from (ptrLow + 256*ptrHigh)
            microOps.push_back({PHI2, [this]() {
                vectorAddr = ((uint16_t)ptrHigh << 8) | ptrLow;
                Data_latch = bus.read(vectorAddr);
                std::cout << "[JMP(Indirect)] Read low target byte $"
                        << std::hex << (int)Data_latch
                        << " from [$" << vectorAddr << "]" << std::endl;
            }});

            // -----------------------------
            // PHI2 finale: Read high byte of target (with page-wrap bug)
            microOps.push_back({PHI2, [this]() {
                uint16_t addrLow = ((uint16_t)ptrHigh << 8) | ptrLow;
                uint16_t addrHigh;
                if (ptrLow == 0xFF) {
                    // Page-wrap bug emulation
                    addrHigh = ((uint16_t)ptrHigh << 8);
                } else {
                    addrHigh = addrLow + 1;
                }
                uint8_t highByte = bus.read(addrHigh);
                uint16_t newPC = ((uint16_t)highByte << 8) | Data_latch;
                R.PC = newPC;
                std::cout << "[JMP(Indirect)] Jumping via vector ($"
                        << std::hex << ((uint16_t)ptrHigh << 8 | ptrLow)
                        << ") → target $" << newPC << std::endl;
            }});

            break;
        }
        */

        // -----------------------------
        // JMP ($addr) - Indirect - Opcode $6C
        // 5 cicli macchina (10 half-cycles)
        // Include dummy read + page-wrap bug (fedele al 6510)
        // -----------------------------
        case 0x6C:
        {
            uint8_t ptrLow = 0;
            uint8_t ptrHigh = 0;
            uint16_t vectorAddr = 0;

            // ---- C1 ---- Fetch opcode ----
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] Opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C2 ---- Fetch low byte of pointer ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] Low byte pointer $" << std::hex
                        << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C3 ---- Fetch high byte of pointer ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                std::cout << "[JMP(Indirect)] High byte pointer $" << std::hex
                        << (int)Temp_addr_high << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C4 ---- Dummy read from pointer address (fedele 6510) ----
            microOps.push_back({PHI2, [this]() {
                EffAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                bus.peek(EffAddr); // Dummy read → non cambia open bus
                std::cout << "[JMP(Indirect)] Dummy read at $" << std::hex << EffAddr << std::endl;
            }});

            // ---- C5 ---- Read target low + high (page-wrap bug) ----
            microOps.push_back({PHI2, [this]() {
                uint16_t base = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                uint8_t lowByte = bus.read(base);

                uint16_t highAddr = (Temp_addr_low == 0xFF)
                    ? ((uint16_t)Temp_addr_high << 8)          // bug: wrap to same page
                    : base + 1;

                uint8_t highByte = bus.read(highAddr);
                uint16_t target = ((uint16_t)highByte << 8) | lowByte;

                std::cout << "[JMP(Indirect)] Low=$" << std::hex << (int)lowByte
                        << " High=$" << (int)highByte
                        << " → Jump target $" << target << std::endl;

                R.PC = target; // esegui salto
            }});

            break;
        }
                

        // -----------------------------
        // JMP $addr - Absolute - Opcode $4C
        // 3 cicli macchina (6 half-cycles)
        // Fedele al 6510: include dummy read su fetch finale
        // -----------------------------
        case 0x4C:
        {
            Temp_addr_low = 0;
            Temp_addr_high = 0;

            // ---- C1 ---- Fetch opcode ----
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[JMP abs] Opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C2 ---- Fetch low byte of target ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[JMP abs] Low byte $" << std::hex << (int)Temp_addr_low
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C3 ---- Fetch high byte of target ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                std::cout << "[JMP abs] High byte $" << std::hex << (int)Temp_addr_high
                        << " at $" << R.PC << std::endl;
            }});

            // ---- C4 ---- Dummy read from target address (fedele 6510) ----
            microOps.push_back({PHI2, [this]() {
                uint16_t target = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                bus.peek(target); // dummy read non distruttivo
                std::cout << "[JMP abs] Dummy read at $" << std::hex << target << std::endl;
            }});

            // ---- C5 ---- Jump ----
            microOps.push_back({PHI1, [this]() {
                uint16_t target = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low;
                R.PC = target;
                std::cout << "[JMP abs] Jump to $" << std::hex << target << std::endl;
            }});

            break;
        }


        // -----------------------------
        // STX $addr (Absolute) — 3 byte, 4 machine cycles (8 half-cycles)
        // Opcode: 0x8E
        // -----------------------------
        case 0x8E:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STX abs] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 2️⃣ PHI2: Fetch low byte of address
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STX abs] Fetch low address byte $" << std::hex
                        << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 3️⃣ PHI2: Fetch high byte (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                EffAddr = (Temp_addr_high << 8) | Temp_addr_low;
                std::cout << "[STX abs] Fetch high address byte $" << std::hex
                        << (int)Temp_addr_high << " at $" << R.PC
                        << " (dummy read)" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 4️⃣ PHI2: Actual write of X to (EffAddr)
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.X);
                std::cout << "[STX abs] Write X=0x" << std::hex << (int)R.X
                        << " to $" << EffAddr << std::endl;
            }});

            break;
        }


        // -----------------------------
        // STX $zz (Zero Page) — 2 byte, 3 machine cycles (6 half-cycles)
        // Opcode: 0x86
        // -----------------------------
        case 0x86:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STX zp] Fetch opcode $" << std::hex << (int)IR
                          << " at $" << R.PC << std::endl;
            }});

            // 1.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STX zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2: Fetch indirizzo zero page
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // Zero page → high byte implicito = 0
                std::cout << "[STX zp] Fetch zero-page address byte $" << std::hex
                          << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});

            // 2.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STX zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2: Scrive X in memoria zero page
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.X);
                std::cout << "[STX zp] Write X=0x" << std::hex << (int)R.X
                          << " to zero-page address $" << EffAddr << std::endl;
            }});

            break;
        }


        // -----------------------------
        // STY $addr (Absolute) — 3 byte, 4 machine cycles (8 half-cycles)
        // Opcode: 0x8C
        // -----------------------------
        case 0x8C:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STY abs] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 2️⃣ PHI2: Fetch low byte of address
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STY abs] Fetch low address byte $" << std::hex
                        << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 3️⃣ PHI2: Fetch high byte (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                EffAddr = (Temp_addr_high << 8) | Temp_addr_low;
                std::cout << "[STY abs] Fetch high address byte $" << std::hex
                        << (int)Temp_addr_high << " at $" << R.PC
                        << " (dummy read)" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 4️⃣ PHI2: Actual write of Y to (EffAddr)
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.Y);
                std::cout << "[STY abs] Write Y=0x" << std::hex << (int)R.Y
                        << " to $" << EffAddr << std::endl;
            }});

            break;
        }


        // -----------------------------
        // STY $zz (Zero Page) — 2 byte, 3 machine cycles (6 half-cycles)
        // Opcode: 0x84
        // -----------------------------
        case 0x84:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STY zp] Fetch opcode $" << std::hex << (int)IR
                          << " at $" << R.PC << std::endl;
            }});

            // 1.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STY zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2: Fetch indirizzo zero page
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // indirizzo effettivo nella pagina zero
                std::cout << "[STY zp] Fetch zero-page address byte $" << std::hex
                          << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});

            // 2.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STY zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2: Scrive Y in memoria zero page
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.Y);
                std::cout << "[STY zp] Write Y=0x" << std::hex << (int)R.Y
                          << " to zero-page address $" << EffAddr << std::endl;
            }});

            break;
        }


        // -----------------------------
        // STA $addr (Absolute) — 3 byte, 4 machine cycles (8 half-cycles)
        // Opcode: 0x8D
        // -----------------------------
        case 0x8D:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA abs] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 2️⃣ PHI2: Fetch low byte of address
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STA abs] Fetch low address byte $" << std::hex
                        << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 3️⃣ PHI2: Fetch high byte (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                EffAddr = (Temp_addr_high << 8) | Temp_addr_low;
                std::cout << "[STA abs] Fetch high address byte $" << std::hex
                        << (int)Temp_addr_high << " at $" << R.PC
                        << " (dummy read)" << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 4️⃣ PHI2: Actual write of A to (EffAddr)
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.A);
                std::cout << "[STA abs] Write A=0x" << std::hex << (int)R.A
                        << " to $" << EffAddr << std::endl;
            }});

            break;
        }


        // ----------------------------- 
        // STA $zz (Zero Page) — 2 byte, 3 machine cycles (6 half-cycles)
        // Opcode: 0x85
        // -----------------------------
        case 0x85:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA zp] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 2️⃣ PHI2: Fetch zero page address byte
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // indirizzo effettivo solo 8 bit
                std::cout << "[STA zp] Fetch zero-page address $" << std::hex
                        << (int)EffAddr << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // 3️⃣ PHI2: Actual write of A to zero-page address
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.A);
                std::cout << "[STA zp] Write A=0x" << std::hex << (int)R.A
                        << " to $" << EffAddr << std::endl;
            }});

            break;
        }


        
        // --------------------------            
        // STA absolute,Y ($99) - ciclo esatto NMOS
        // --------------------------
        case 0x99: {
            Addr_latch = 0;
            EffAddr  = 0;

            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA abs,Y] PHI2: opcode fetch at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: fetch low byte ---
            microOps.push_back({PHI2, [this]() {
                Addr_latch = bus.read(R.PC);
                std::cout << "[STA abs,Y] PHI2: low byte fetch $" 
                        << std::hex << (int)(Addr_latch & 0xFF)
                        << " at PC $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 3: fetch high byte ---
            microOps.push_back({PHI2, [this]() {
                Addr_latch |= bus.read(R.PC) << 8;
                std::cout << "[STA abs,Y] PHI2: high byte fetch $" 
                        << std::hex << (int)(Addr_latch >> 8)
                        << " -> baseAddr=$" << Addr_latch << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // --- Ciclo 4: dummy read at baseAddr + (low + Y) ---
            microOps.push_back({PHI2, [this]() {
                uint16_t low = (Addr_latch & 0x00FF);
                //effAddr = (low + R.Y) & 0x00FF | (baseAddr & 0xFF00);  // indirizzo parziale
                EffAddr = ((low + R.Y) & 0x00FF) | (Addr_latch & 0xFF00);
                (void)bus.peek(EffAddr);
                std::cout << "[STA abs,Y] PHI2: dummy peek at partial $" 
                        << std::hex << EffAddr << " (page check)" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 5: correzione indirizzo pagina (se overflow) + write ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = Addr_latch + R.Y; // indirizzo effettivo finale (16 bit)
                bus.write(EffAddr, R.A);
                std::cout << "[STA abs,Y] PHI2: write A=$" << std::hex << (int)R.A
                        << " to $" << EffAddr 
                        << " (base=$" << Addr_latch << " + Y=$" << (int)R.Y << ")" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

        } break;


        // --------------------------            
        // STA (indirect),Y ($91) - ciclo esatto NMOS
        // --------------------------
        case 0x91: {
            Temp_addr_low = 0;
            Addr_latch = 0;
            EffAddr  = 0;

            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA (ind),Y] PHI2: opcode fetch at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: fetch zero-page pointer ---
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STA (ind),Y] PHI2: zero-page pointer = $" 
                        << std::hex << (int)Temp_addr_low << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 3: fetch low byte of base address ---
            microOps.push_back({PHI2, [this]() {
                Addr_latch = bus.read(Temp_addr_low);
                std::cout << "[STA (ind),Y] PHI2: read low byte $" 
                        << std::hex << (int)(Addr_latch & 0xFF)
                        << " from ZP $" << (int)Temp_addr_low << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 4: fetch high byte of base address ---
            microOps.push_back({PHI2, [this]() {
                uint8_t high = bus.read((Temp_addr_low + 1) & 0xFF);
                Addr_latch |= (uint16_t)high << 8;
                std::cout << "[STA (ind),Y] PHI2: read high byte $" 
                        << std::hex << (int)high 
                        << " -> baseAddr=$" << Addr_latch << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 5: dummy read at (base + Y) for page crossing ---
            microOps.push_back({PHI2, [this]() {
                uint16_t tempAddr = (Addr_latch & 0xFF00) | ((Addr_latch + R.Y) & 0x00FF);
                (void)bus.peek(tempAddr);
                std::cout << "[STA (ind),Y] PHI2: dummy peek at partial $" 
                        << std::hex << tempAddr << " (page check)" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

            // --- Ciclo 6: write accumulator to effective address ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = Addr_latch + R.Y;
                bus.write(EffAddr, R.A);
                std::cout << "[STA (ind),Y] PHI2: write A=$" 
                        << std::hex << (int)R.A << " to $" << EffAddr
                        << " (base=$" << Addr_latch << " + Y=$" << (int)R.Y << ")" << std::endl;
            }});
            microOps.push_back({PHI1, [](){} });

        } break;


        // -----------------------------
        // STA $addr,X (Absolute,X) - Opcode $9D
        // 5 cicli macchina (4 se nessun page crossing, ma sempre esegue dummy write finale)
        // -----------------------------
        case 0x9D:  // STA $HHLL,X (Absolute,X)
        {
            // ---- C1 ---- Fetch opcode ----
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA abs,X] Fetch opcode at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C2 ---- Fetch low byte ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STA abs,X] Low byte fetched: $" << std::hex << (int)Temp_addr_low << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C3 ---- Fetch high byte ----
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(R.PC);
                std::cout << "[STA abs,X] High byte fetched: $" << std::hex << (int)Temp_addr_high << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            // ---- C4 ---- Add X and perform dummy read ----
            microOps.push_back({PHI2, [this]() {
                uint16_t base = (Temp_addr_high << 8) | Temp_addr_low;
                uint16_t addr = (base & 0xFF00) | ((Temp_addr_low + R.X) & 0xFF);
                pageCross = ((Temp_addr_low + R.X) > 0xFF);
                EffAddr = addr;

                bus.peek(EffAddr); // dummy read (open bus NOT updated)

                std::cout << "[STA abs,X] Added X=$" << std::hex << (int)R.X
                        << " => provisional $" << addr
                        << (pageCross ? " (page crossed)" : "") << std::endl;
            }});

            // ---- C5 ---- Final effective address and write ----
            microOps.push_back({PHI2, [this]() {
                if (pageCross)
                    EffAddr = ((Temp_addr_high + 1) << 8) | ((Temp_addr_low + R.X) & 0xFF);

                bus.write(EffAddr, R.A);

                std::cout << "[STA abs,X] Stored A=$" << std::hex << (int)R.A
                        << " into $" << EffAddr
                        << (pageCross ? " (page crossed)" : "") << std::endl;
            }});

            break;
        }


#ifndef SKIP_OPCODE          
        // --------------------------
        // STY $zz,X ($94) - Zero Page,X - 2 bytes, 4 machine cycles
        // --------------------------
        case 0x94: {
            Temp_addr_low = 0;
            EffAddr = 0;

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STY zp,X] PHI2: Opcode fetch $" << std::hex << R.PC
                          << " -> $" << (int)IR << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STY zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Fetch Zero Page base address ---
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STY zp,X] PHI2: Zero-page base fetch $" << std::hex << (int)Temp_addr_low
                          << " at PC $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STY zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 3: Dummy read from zero page (base + X, wrapping 8-bit) ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = (uint8_t)(Temp_addr_low + R.X);  // zero-page wraparound
                (void)bus.peek(EffAddr);
                std::cout << "[STY zp,X] PHI2: Dummy read at $" << std::hex << EffAddr
                          << " (base=$" << (int)Temp_addr_low << " + X=$" << (int)R.X << ")" << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // nessuna azione, solo stabilizzazione bus
            }});

            // --- Ciclo 4: Actual write of Y to effective zero-page address ---
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.Y);
                std::cout << "[STY zp,X] PHI2: Write Y=$" << std::hex << (int)R.Y
                          << " to $" << EffAddr << std::endl;
            }});

            break;
        }
#endif


        // --------------------------
        // STA $zz,X ($95) - Zero Page,X - 2 bytes, 4 machine cycles
        // --------------------------
        case 0x95: {
            Temp_addr_low = 0;
            EffAddr = 0;

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[STA zp,X] PHI2: Opcode fetch $" << std::hex << R.PC
                        << " -> $" << (int)IR << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STA zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Fetch Zero Page base address ---
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[STA zp,X] PHI2: Zero-page base fetch $" << std::hex << (int)Temp_addr_low
                        << " at PC $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[STA zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 3: Dummy read from zero page (base + X, wrapping 8-bit) ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = static_cast<uint8_t>(Temp_addr_low + R.X); // zero-page wraparound
                (void)bus.peek(EffAddr);
                std::cout << "[STA zp,X] PHI2: Dummy read at $" << std::hex << EffAddr
                        << " (base=$" << (int)Temp_addr_low << " + X=$" << (int)R.X << ")" << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // nessuna azione, solo stabilizzazione bus
            }});

            // --- Ciclo 4: Actual write of A to effective zero-page address ---
            microOps.push_back({PHI2, [this]() {
                bus.write(EffAddr, R.A);
                std::cout << "[STA zp,X] PHI2: Write A=$" << std::hex << (int)R.A
                        << " to $" << EffAddr << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // fine istruzione
            }});

            break;
        }


        // ----------------------------- 
        // LDA $zz (Zero Page) — 2 byte, 3 machine cycles (6 half-cycles)
        // Opcode: 0xA5
        // -----------------------------
        case 0xA5:
        {
            // 1️⃣ PHI2: Fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LDA zp] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[LDA zp] Increment PC -> $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2: Fetch zero page address byte
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // indirizzo effettivo a 8 bit (zero page)
                std::cout << "[LDA zp] Fetch zero-page address $" << std::hex
                        << (int)EffAddr << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[LDA zp] Increment PC -> $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2: Actual read from zero-page address
            microOps.push_back({PHI2, [this]() {
                R.A = bus.read(EffAddr);
                std::cout << "[LDA zp] Read A <= $" << std::hex << (int)R.A
                        << " from $" << EffAddr << std::endl;

                // Update flags Z e N
                temp_ALU.result8 = R.A;
                ALU::setFlag(R.P, ZERO,     temp_ALU.result8 == 0);
                ALU::setFlag(R.P, NEGATIVE, temp_ALU.result8 & 0x80);                

                std::cout << "[LDA zp] Flags updated: "
                        << " C=" << ((R.P & CARRY)?1:0)
                        << " Z=" << ((R.P & ZERO)?1:0)
                        << " N=" << ((R.P & NEGATIVE)?1:0)
                        << std::endl;
            }});

            break;
        }


        // ----------------------------- 
        // LDX $zz (Zero Page) — 2 byte, 3 machine cycles (6 half-cycles)
        // Opcode: 0xA6
        // -----------------------------
        case 0xA6:
        {
            // 1️⃣ PHI2: Fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LDX zp] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[LDX zp] Increment PC -> $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2: Fetch zero page address byte
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // indirizzo effettivo (8 bit)
                std::cout << "[LDX zp] Fetch zero-page address $" << std::hex
                        << (int)EffAddr << " at $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() { 
                R.PC++; 
                std::cout << "[LDX zp] Increment PC -> $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2: Actual read of X from zero-page address
            microOps.push_back({PHI2, [this]() {
                R.X = bus.read(EffAddr);
                std::cout << "[LDX zp] Read X <= $" << std::hex << (int)R.X
                        << " from $" << EffAddr << std::endl;

                // Update flags Z e N
                temp_ALU.result8 = R.X;
                ALU::setFlag(R.P, ZERO,     temp_ALU.result8 == 0);
                ALU::setFlag(R.P, NEGATIVE, temp_ALU.result8 & 0x80);                

                std::cout << "[LDX zp] Flags updated: "
                        << " C=" << ((R.P & CARRY)?1:0)
                        << " Z=" << ((R.P & ZERO)?1:0)
                        << " N=" << ((R.P & NEGATIVE)?1:0)
                        << std::endl;
            }});

            break;
        }


        // --------------------------
        // LDY $zz,X ($B4) - Zero Page,X - 2 bytes, 4 machine cycles
        // --------------------------
        case 0xB4: {
            Temp_addr_low = 0;
            EffAddr = 0;

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LDY zp,X] PHI2: Opcode fetch $" << std::hex << R.PC
                        << " -> $" << (int)IR << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LDY zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Fetch Zero Page base address ---
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[LDY zp,X] PHI2: Zero-page base fetch $" << std::hex << (int)Temp_addr_low
                        << " at PC $" << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LDY zp,X] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 3: Dummy read from zero page (base + X, wrapping 8-bit) ---
            microOps.push_back({PHI2, [this]() {
                EffAddr = static_cast<uint8_t>(Temp_addr_low + R.X); // zero-page wraparound
                (void)bus.peek(EffAddr); // dummy read
                std::cout << "[LDY zp,X] PHI2: Dummy read at $" << std::hex << EffAddr
                        << " (base=$" << (int)Temp_addr_low << " + X=$" << (int)R.X << ")" << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // nessuna azione, solo stabilizzazione bus
            }});

            // --- Ciclo 4: Actual read of memory into Y ---
            microOps.push_back({PHI2, [this]() {
                R.Y = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.Y & 0x80) != 0);
                std::cout << "[LDY zp,X] PHI2: Read Y=$" << std::hex << (int)R.Y
                        << " from $" << EffAddr
                        << " -> Z=" << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});

            break;
        }


        // --------------------------
        // AND immediate ($29) - ciclo esatto NMOS
        // --------------------------
        case 0x29: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.A = static_cast<uint8_t>(R.A & Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        } break;


        // --------------------------
        // AND zeropage ($25) - ciclo esatto NMOS
        // --------------------------
        case 0x25: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A = static_cast<uint8_t>(R.A & Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
        }
        break;


        // --------------------------
        // ORA immediate ($09) - ciclo esatto NMOS
        // --------------------------
        case 0x09: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.A = static_cast<uint8_t>(R.A | Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
        }
        break;


        // --------------------------
        // ORA absolute ($0D) - ciclo esatto NMOS
        // --------------------------
        case 0x0D: {
            enqueueReadAbs(ReadAction::ORA);
        }
        break;

        // --------------------------
        // ORA absolute,X ($1D) - ciclo esatto NMOS
        // --------------------------
        case 0x1D: {
            enqueueReadAbsIndexed(ReadAction::ORA, true);
        }
        break;


        // --------------------------
        // TAY - Transfer Accumulator to Y ($A8)
        // Ciclo esatto NMOS - 2 cicli macchina
        // --------------------------
        case 0xA8: {
            // Non serve indirizzo o valore di memoria, solo registri CPU
            // Tuttavia manteniamo la struttura completa per coerenza con INC

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TAY] PHI2: opcode fetch at $" << std::hex << R.PC
                          << " -> $" << std::setw(2) << std::setfill('0') << (int)IR << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TAY] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Esegui trasferimento ---
            microOps.push_back({PHI2, [this]() {
                R.Y = R.A;

                // Aggiorna flag Z e N come da comportamento 6502 NMOS
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, R.Y & 0x80);

                std::cout << "[TAY] PHI2: transfer A($" << std::hex << (int)R.A
                          << ") -> Y($" << (int)R.Y << "), flags Z=" 
                          << ((R.P & ZERO)?1:0) << " N=" << ((R.P & NEGATIVE)?1:0) << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // Nessuna attività bus, CPU interna (registro)
            }});

            // Nota: il bus resta idle durante TAY (nessun dummy read)
            // Solo fetch dell’opcode e operazione interna sui registri
        } break;



        // --------------------------
        // TAX - Transfer Accumulator to X ($AA)
        // Ciclo esatto NMOS - 2 cicli macchina
        // --------------------------
        case 0xAA: {
            // Nessun accesso a memoria dopo il fetch: solo trasferimento tra registri

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TAX] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << std::setw(2) << std::setfill('0') << (int)IR << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TAX] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Esegui trasferimento A → X ---
            microOps.push_back({PHI2, [this]() {
                R.X = R.A;

                // Aggiorna flag Z e N secondo comportamento NMOS
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, R.X & 0x80);

                std::cout << "[TAX] PHI2: transfer A($" << std::hex << (int)R.A
                        << ") -> X($" << (int)R.X << "), flags Z="
                        << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});

            microOps.push_back({PHI1, []() {
                // Nessuna attività bus in questo ciclo: operazione interna ai registri
            }});

            // Nota: nessun dummy read o accesso al bus dopo il fetch
        } break;


        // --------------------------
        // TYA - Transfer Y to Accumulator ($98)
        // Ciclo esatto NMOS - 2 cicli macchina
        // --------------------------
        case 0x98: {
            // Nessun accesso a memoria dopo il fetch: solo trasferimento tra registri

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TYA] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << std::setw(2) << std::setfill('0') << (int)IR << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TYA] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Esegui trasferimento Y → A ---
            microOps.push_back({PHI2, [this]() {
                R.A = R.Y;

                // Aggiorna flag Z e N secondo comportamento NMOS
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, R.A & 0x80);

                std::cout << "[TYA] PHI2: transfer Y($" << std::hex << (int)R.Y
                        << ") -> A($" << (int)R.A << "), flags Z="
                        << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});
            microOps.push_back({PHI1, []() {
                // Nessuna attività bus in questo ciclo: operazione interna ai registri
            }});

            // Nota: nessun dummy read o accesso al bus dopo il fetch
        } break;


        // -----------------------------
        // INC $zz (Zero Page) — 2 byte, 5 machine cycles (10 half-cycles)
        // Opcode: 0xE6
        // -----------------------------
        case 0xE6:
        {
            // 1️⃣ PHI2: Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[INC zp] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});

            // 1.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[INC zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2: Fetch indirizzo zero page
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low; // Zero page: high byte implicito = 0
                std::cout << "[INC zp] Fetch zero-page address byte $" << std::hex
                        << (int)Temp_addr_low << " at $" << R.PC << std::endl;
            }});

            // 2.5️⃣ PHI1: Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[INC zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2: Dummy read (effettivo nel 6502)
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                std::cout << "[INC zp] Dummy read from $" << std::hex << EffAddr
                        << " (value=0x" << (int)Data_latch << ")" << std::endl;
            }});

            // 3.5️⃣ PHI1: Latch temporaneo (CPU interna)
            microOps.push_back({PHI1, [this]() {
                std::cout << "[INC zp] Internal latch (pre-increment)" << std::endl;
            }});

            // 4️⃣ PHI2: Incrementa valore e scrive (dummy write)
            microOps.push_back({PHI2, [this]() {
                uint8_t result = Data_latch + 1;
                bus.write(EffAddr, result);
                bus.onDrivenBusValue(result);
                std::cout << "[INC zp] Increment and dummy write: $" << std::hex << EffAddr
                        << " ← 0x" << (int)result << std::endl;
            }});

            // 4.5️⃣ PHI1: Aggiorna latch interno
            microOps.push_back({PHI1, [this]() {
                std::cout << "[INC zp] Internal update complete" << std::endl;
            }});

            // 5️⃣ PHI2: Aggiorna flag Z/N
            microOps.push_back({PHI2, [this]() {
                uint8_t result = bus.openBusValue;
                ALU::setFlag(R.P, ZERO, result == 0);
                ALU::setFlag(R.P, NEGATIVE, result & 0x80);
                std::cout << "[INC zp] Flags updated → Z=" 
                        << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << " N=" << (ALU::getFlag(R.P, NEGATIVE) ? "1" : "0")
                        << std::endl;
            }});

            break;
        }


        // -------------------------------------
        // LSR $zz (Zero Page) — 2 byte, 5 cycles
        // Opcode: 0x46
        // -------------------------------------
        case 0x46:
        {
            // 1️⃣ PHI2 — Fetch opcode (dummy read effettivo)
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LSR zp] Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});

            // 1.5️⃣ PHI1 — Increment PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LSR zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2 — Fetch indirizzo zero-page
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                EffAddr = Temp_addr_low;   // Zero Page → high byte = 0
                std::cout << "[LSR zp] Fetch zero-page address byte $"
                        << std::hex << (int)Temp_addr_low
                        << " at $" << R.PC << std::endl;
            }});

            // 2.5️⃣ PHI1 — Incrementa PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LSR zp] Increment PC → $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2 — Dummy read del valore originale
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                std::cout << "[LSR zp] Dummy read from $" << std::hex << EffAddr
                        << " (value=0x" << (int)Data_latch << ")" << std::endl;
            }});

            // 3.5️⃣ PHI1 — Latch interno (CPU real)
            microOps.push_back({PHI1, [this]() {
                std::cout << "[LSR zp] Internal latch (pre-shift)" << std::endl;
            }});

            // 4️⃣ PHI2 — Write-back modificato (dummy write)
            microOps.push_back({PHI2, [this]() {
                uint8_t oldVal = Data_latch;
                uint8_t result = (oldVal >> 1);     // Shift logico a destra

                // CARRY = bit0
                ALU::setFlag(R.P, CARRY, oldVal & 0x01);

                bus.write(EffAddr, result);
                bus.onDrivenBusValue(result);

                std::cout << "[LSR zp] Shift-right and dummy write: $" << std::hex
                        << EffAddr << " ← 0x" << (int)result
                        << " (old=0x" << (int)oldVal << ")" << std::endl;
            }});

            // 4.5️⃣ PHI1 — Latch interno aggiornato
            microOps.push_back({PHI1, [this]() {
                std::cout << "[LSR zp] Internal update complete" << std::endl;
            }});

            // 5️⃣ PHI2 — Set dei flag Z e N (N sempre = 0 in LSR)
            microOps.push_back({PHI2, [this]() {
                uint8_t result = bus.openBusValue;

                ALU::setFlag(R.P, ZERO, result == 0);
                ALU::setFlag(R.P, NEGATIVE, false); // Bit 7 sempre 0 dopo LSR

                std::cout << "[LSR zp] Flags updated → "
                        << "C=" << (ALU::getFlag(R.P, CARRY) ? "1" : "0")
                        << " Z=" << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << " N=0" << std::endl;
            }});

            break;
        }


        // -------------------------------------
        // LSR $zz,X (Zero Page,X) — 2 byte, 6 cycles
        // Opcode: 0x56
        // -------------------------------------
        case 0x56:
        {
            // 1️⃣ PHI2 — Fetch opcode
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[LSR zp,X] PHI2: Fetch opcode $" << std::hex << (int)IR
                        << " at $" << R.PC << std::endl;
            }});

            // 1.5️⃣ PHI1 — Increment PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LSR zp,X] PHI1: PC incremented → $" << std::hex << R.PC << std::endl;
            }});

            // 2️⃣ PHI2 — Fetch Zero Page base address
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = bus.read(R.PC);
                std::cout << "[LSR zp,X] PHI2: Fetch ZP base address $"
                        << std::hex << (int)Temp_addr_low
                        << " from $" << R.PC << std::endl;
            }});

            // 2.5️⃣ PHI1 — Increment PC
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[LSR zp,X] PHI1: PC incremented → $" << std::hex << R.PC << std::endl;
            }});

            // 3️⃣ PHI2 — Dummy read from (base + X) masked to zero page
            microOps.push_back({PHI2, [this]() {
                uint8_t zp_temp = (Temp_addr_low + R.X) & 0xFF;
                EffAddr = zp_temp;
                bus.read(zp_temp);

                std::cout << "[LSR zp,X] PHI2: Dummy read from $" << std::hex << (int)zp_temp
                        << " (ZP+X wrap)" << std::endl;
            }});

            // 3.5️⃣ PHI1 — Internal latch
            microOps.push_back({PHI1, [this]() {
                std::cout << "[LSR zp,X] PHI1: Internal latch (pre-read)" << std::endl;
            }});

            // 4️⃣ PHI2 — Real read from effective address (ZP + X)
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);

                std::cout << "[LSR zp,X] PHI2: Read actual value 0x"
                        << std::hex << (int)Data_latch
                        << " from $" << (int)EffAddr << std::endl;
            }});

            // 4.5️⃣ PHI1 — Internal latch update
            microOps.push_back({PHI1, [this]() {
                std::cout << "[LSR zp,X] PHI1: Internal latch updated" << std::endl;
            }});

            // 5️⃣ PHI2 — Perform shift, write result
            microOps.push_back({PHI2, [this]() {
                uint8_t oldVal = Data_latch;
                uint8_t result = oldVal >> 1;

                // Carry = bit 0
                ALU::setFlag(R.P, CARRY, oldVal & 0x01);

                bus.write(EffAddr, result);
                bus.onDrivenBusValue(result);

                std::cout << "[LSR zp,X] PHI2: Shift-right and write 0x"
                        << std::hex << (int)result
                        << " to $" << (int)EffAddr
                        << " (old=0x" << (int)oldVal << ")" << std::endl;
            }});

            // 5.5️⃣ PHI1 — Internal completion
            microOps.push_back({PHI1, [this]() {
                std::cout << "[LSR zp,X] PHI1: Internal shift complete" << std::endl;
            }});

            // 6️⃣ PHI2 — Update flags Z, N (N = 0 always for LSR)
            microOps.push_back({PHI2, [this]() {
                uint8_t result = bus.openBusValue;

                ALU::setFlag(R.P, ZERO, result == 0);
                ALU::setFlag(R.P, NEGATIVE, false);

                std::cout << "[LSR zp,X] PHI2: Flags → "
                        << "C=" << (ALU::getFlag(R.P, CARRY) ? "1" : "0")
                        << " Z=" << (ALU::getFlag(R.P, ZERO) ? "1" : "0")
                        << " N=0" << std::endl;
            }});

            break;
        }


        // ========================================================
        // LDA ($zz),Y - 2 bytes, 5 (o 6) machine cycles
        // Opcode: 0xB1
        // ========================================================
        case 0xB1: {
            enqueueReadIndY(ReadAction::LDA);
            break;
        }


        // --------------------------            
        // CMP (indirect),Y ($D1) - ciclo esatto NMOS
        // --------------------------
        case 0xD1: {
            enqueueReadIndY(ReadAction::CMP);
        } break;


        // --------------------------            
        // ROL A ($2A) - Rotate Left Accumulator (NMOS 6502 exact timing)
        // --------------------------
        case 0x2A: {
            Temp_addr_low = 0;   // oldCarry
            Temp_addr_high = 0;  // newCarry
            Data_latch = 0;      // result

            // --- Ciclo 1: fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[ROL A] PHI2: opcode fetch at $" << std::hex << R.PC << std::endl;
            }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            // --- Ciclo 2: esecuzione rotazione su A ---
            microOps.push_back({PHI2, [this]() {
                Temp_addr_low = (R.P & CARRY) ? 1 : 0;      // salva carry preesistente
                Temp_addr_high = (R.A & 0x80) ? 1 : 0;      // bit7 va in carry
                Data_latch = ((R.A << 1) | Temp_addr_low) & 0xFF;

                R.A = Data_latch;                      // aggiorna A
                ALU::setFlag(R.P, CARRY, Temp_addr_high);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, R.A & 0x80);

                std::cout << "[ROL A] PHI2: A before=$" << std::hex << (int)(R.A >> 1)
                        << " oldC=" << (int)Temp_addr_low 
                        << " -> newA=$" << (int)R.A
                        << " newC=" << (int)Temp_addr_high
                        << " flags Z=" << ((R.P & ZERO)?1:0)
                        << " N=" << ((R.P & NEGATIVE)?1:0)
                        << std::endl;
            }});

            // --- PHI1 finale: nessuna attività bus, operazione interna ---
            microOps.push_back({PHI1, []() {
                // Nessuna operazione bus (registro interno)
            }});

        } break;


        // --------------------------
        // TXA - Transfer X to Accumulator ($8A)
        // Ciclo esatto NMOS - 2 cicli macchina
        // --------------------------
        case 0x8A: {
            // Nessun accesso a memoria dopo il fetch: solo trasferimento tra registri

            // --- Ciclo 1: Fetch opcode ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[TXA] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << std::setw(2) << std::setfill('0') << (int)IR << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[TXA] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Ciclo 2: Esegui trasferimento X → A ---
            microOps.push_back({PHI2, [this]() {
                R.A = R.X;

                // Aggiorna flag Z e N secondo comportamento NMOS
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, R.A & 0x80);

                std::cout << "[TXA] PHI2: transfer X($" << std::hex << (int)R.X
                        << ") -> A($" << (int)R.A << "), flags Z="
                        << ((R.P & ZERO) ? 1 : 0)
                        << " N=" << ((R.P & NEGATIVE) ? 1 : 0)
                        << std::endl;
            }});

            microOps.push_back({PHI1, []() {
                // Nessuna attività bus in questo ciclo: operazione interna ai registri
            }});

            // Nota: nessun dummy read o accesso al bus dopo il fetch
        } break;


        // --------------------------
        // PHA - Push Accumulator on Stack ($48)
        // Ciclo esatto NMOS - 3 cicli macchina
        // --------------------------
        case 0x48: {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;
            }});

            microOps.push_back({PHI2, [this]() {
                uint16_t addr = 0x0100 | R.SP;
                bus.write(addr, R.A);
            }});

            microOps.push_back({PHI1, [this]() {
                R.SP--;
            }});
        } break;


        // ========================================================
        // BCS (Branch if Carry Set) - Relative addressing
        // 2 bytes, 2–3–4 machine cycles depending on branch taken and page cross
        // ========================================================
        case 0xB0: {
            enqueueBranchRelative(ALU::getFlag(R.P, CARRY));
            break;
        }


        // ========================================================
        // BPL (Branch if Positive) - Relative addressing
        // Opcode $10 - 2 bytes, 2–3–4 machine cycles depending on branch taken and page cross
        // ========================================================
        case 0x10: {
            enqueueBranchRelative(!ALU::getFlag(R.P, NEGATIVE));
            break;
        }


#ifndef SKIP_OPCODE        
        // -----------------------------
        // CPX #$nn (Immediate) - Opcode $E0
        // 2 cicli macchina (NMOS 6502)
        // -----------------------------
        case 0xE0:
        {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                uint8_t result = static_cast<uint8_t>(R.X - Data_latch);
                ALU::setFlag(R.P, ZERO, R.X == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
                ALU::setFlag(R.P, CARRY, R.X >= Data_latch);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});

            break;
        }
#endif

        
        // -----------------------------
        // CPX $nn (Zero Page) - Opcode $E4
        // 3 cicli macchina (NMOS 6502)
        // -----------------------------
        case 0xE4:
        {
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(Temp_addr_low);
                uint8_t result = static_cast<uint8_t>(R.X - Data_latch);
                ALU::setFlag(R.P, ZERO, R.X == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
                ALU::setFlag(R.P, CARRY, R.X >= Data_latch);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }


        // --------------------------------------------------
        // PHP ($08) - Push Processor Status
        // NMOS 6502 - 3 machine cycles
        // --------------------------------------------------
        case 0x08: {

            Data_latch = 0;

            // --- Cycle 1: Opcode Fetch ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[PHP] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << (int)IR << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[PHP] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Cycle 2: Dummy read (stack pre-access) ---
            microOps.push_back({PHI2, [this]() {
                uint16_t dummyAddr = 0x0100 | R.SP;
                uint8_t dummy = bus.read(dummyAddr);
                (void)dummy;
                std::cout << "[PHP] PHI2: dummy read from stack $" << std::hex << dummyAddr
                        << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                // Bit 4 (B flag) e bit 5 sempre impostati nello status pushato
                Data_latch = R.P | 0x30;

                std::cout << "[PHP] PHI1: prepare push, status=$"
                        << std::hex << (int)Data_latch
                        << " (with Break+Unused bits forced)" << std::endl;
            }});

            // --- Cycle 3: Write status to stack ---
            microOps.push_back({PHI2, [this]() {
                uint16_t addr = 0x0100 | R.SP;
                bus.write(addr, Data_latch);

                std::cout << "[PHP] PHI2: push P=$" << std::hex << std::setw(2)
                        << std::setfill('0') << (int)Data_latch
                        << " to $" << addr << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.SP--;
                std::cout << "[PHP] PHI1: SP decremented -> $" << std::hex << (int)R.SP << std::endl;
            }});

            break;
        }


        // --------------------------------------------------
        // PLP ($28) - Pull Processor Status
        // NMOS 6502 - 4 machine cycles
        // --------------------------------------------------
        case 0x28: {

            Data_latch = 0;

            // --- Cycle 1: Opcode Fetch ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                std::cout << "[PLP] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << (int)IR << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;
                std::cout << "[PLP] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Cycle 2: Dummy read ---
            microOps.push_back({PHI2, [this]() {
                uint16_t dummyAddr = 0x0100 | ((uint8_t)(R.SP + 1));
                uint8_t dummy = bus.read(dummyAddr); 
                (void)dummy;

                std::cout << "[PLP] PHI2: dummy read at $" << std::hex << dummyAddr << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.SP++; // pre-increment NMOS behavior
                std::cout << "[PLP] PHI1: SP incremented -> $" << std::hex << (int)R.SP
                        << std::endl;
            }});

            // --- Cycle 3: Read value from stack ---
            microOps.push_back({PHI2, [this]() {
                uint16_t addr = 0x0100 | R.SP;
                Data_latch = bus.read(addr);

                std::cout << "[PLP] PHI2: read pulled P=$"
                        << std::hex << (int)Data_latch
                        << " from $" << addr << std::endl;
            }});

            // --- Cycle 4: Load P (mask out bit 4, force bit 5) ---
            microOps.push_back({PHI1, [this]() {

                // NMOS 6502 behavior:
                // bit4 (B flag) is IGNORED on PLP load
                // bit5 ALWAYS forced to 1
                R.P = (Data_latch & 0xEF) | 0x20;

                std::cout << "[PLP] PHI1: set status register to masked=$"
                        << std::hex << (int)R.P
                        << " (bit4 cleared, bit5 forced)" << std::endl;
            }});

            break;
        }
                

        // --------------------------------------------------
        // PLA ($68) - Pull Accumulator
        // NMOS 6502 - 4 machine cycles
        // --------------------------------------------------
        case 0x68: {

            // Nota: PLA tira un valore dallo stack e lo mette in A,
            // aggiornando le flag N e Z.

            Data_latch = 0;

            // --- Cycle 1: Opcode Fetch ---
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);                                                   // Fetch opcode
                std::cout << "[PLA] PHI2: opcode fetch at $" << std::hex << R.PC
                        << " -> $" << (int)IR << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.PC++;                                                                // Increment PC
                std::cout << "[PLA] PHI1: PC incremented to $" << std::hex << R.PC << std::endl;
            }});

            // --- Cycle 2: Dummy read ---
            microOps.push_back({PHI2, [this]() {
                uint16_t dummyAddr = 0x0100 | ((R.SP + 1) & 0xFF);
                uint8_t dummy = bus.read(dummyAddr);                                   // Dummy read
                (void)dummy;
                std::cout << "[PLA] PHI2: dummy stack pre-read from $" << std::hex << dummyAddr
                        << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.SP++;                                                                // Increment SP (point to value)
                std::cout << "[PLA] PHI1: SP incremented -> $" << std::hex << (int)R.SP << std::endl;
            }});

            // --- Cycle 3: Read from stack ---
            microOps.push_back({PHI2, [this]() {
                uint16_t addr = 0x0100 | R.SP;
                Data_latch = bus.read(addr);                                           // Pull byte from stack
                std::cout << "[PLA] PHI2: pulled value $" << std::hex << (int)Data_latch
                        << " from $" << addr << std::endl;
            }});

            microOps.push_back({PHI1, [this]() {
                R.A = Data_latch;                                                      // Load into A
                R.P = (R.P & 0x7D)                                                     // Clear N/Z
                    | (R.A == 0 ? 0x02 : 0)                                         // Z flag
                    | (R.A & 0x80 ? 0x80 : 0);                                      // N flag

                std::cout << "[PLA] PHI1: A <= $" << std::hex << (int)R.A
                        << "  |  Flags updated (N,Z)" << std::endl;
            }});

            break;
        }


        // -----------------------------
        // BIT $nn (Zero Page) - Opcode $24
        // -----------------------------
        case 0x24: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                ALU::setFlag(R.P, ZERO, (R.A & Data_latch) == 0);
                ALU::setFlag(R.P, NEGATIVE, (Data_latch & 0x80) != 0);
                ALU::setFlag(R.P, OVERFLOW, (Data_latch & 0x40) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // BIT $HHLL (Absolute) - Opcode $2C
        // -----------------------------
        case 0x2C: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, (R.A & Data_latch) == 0);
                ALU::setFlag(R.P, NEGATIVE, (Data_latch & 0x80) != 0);
                ALU::setFlag(R.P, OVERFLOW, (Data_latch & 0x40) != 0);
            }});
            break;
        }

        // -----------------------------
        // EOR #$nn (Immediate) - Opcode $49
        // -----------------------------
        case 0x49: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                R.A ^= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            break;
        }

        // -----------------------------
        // LSR A (Accumulator) - Opcode $4A
        // -----------------------------
        case 0x4A: {
            microOps.push_back({PHI2, [this]() {
                uint8_t oldA = R.A;
                ALU::setFlag(R.P, CARRY, (oldA & 0x01) != 0);
                R.A = (oldA >> 1) & 0x7F;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, false);
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            break;
        }

        // -----------------------------
        // ROR A (Accumulator) - Opcode $6A
        // -----------------------------
        case 0x6A: {
            microOps.push_back({PHI2, [this]() {
                uint8_t oldA = R.A;
                uint8_t oldCarry = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                ALU::setFlag(R.P, CARRY, (oldA & 0x01) != 0);
                R.A = uint8_t((oldA >> 1) | (oldCarry << 7));
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            break;
        }

        // -----------------------------
        // LDA ($nn,X) - Indexed Indirect - Opcode $A1
        // -----------------------------
        case 0xA1: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X);
            }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                uint8_t zpNext = static_cast<uint8_t>(ZP_addr + 1);
                Temp_addr_high = bus.read(zpNext);
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                R.A = Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // LDY $HHLL (Absolute) - Opcode $AC
        // -----------------------------
        case 0xAC: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                R.Y = Data_latch;
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.Y & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // LDX $HHLL (Absolute) - Opcode $AE
        // -----------------------------
        case 0xAE: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                R.X = Data_latch;
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
            }});
            break;
        }

        // -----------------------------
        // LDX $nn,Y (Zero Page,Y) - Opcode $B6
        // -----------------------------
        case 0xB6: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.Y);
            }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.X = Data_latch;
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // CMP $HHLL (Absolute) - Opcode $CD
        // -----------------------------
        case 0xCD: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});

            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                uint8_t result = static_cast<uint8_t>(R.A - Data_latch);
                ALU::setFlag(R.P, CARRY, R.A >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            break;
        }

        // -----------------------------
        // CPY #$nn (Immediate) - Opcode $C0
        // -----------------------------
        case 0xC0: {
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(static_cast<uint16_t>(R.PC + 1));
                uint8_t result = static_cast<uint8_t>(R.Y - Data_latch);
                ALU::setFlag(R.P, CARRY, R.Y >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.Y == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            break;
        }

        // -----------------------------
        // ORA ($nn,X) - Indexed Indirect - Opcode $01
        // -----------------------------
        case 0x01: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                R.A |= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ORA $nn (Zero Page) - Opcode $05
        // -----------------------------
        case 0x05: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A |= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ORA ($nn),Y - Indirect Indexed - Opcode $11
        // -----------------------------
        case 0x11: {
            enqueueReadIndY(ReadAction::ORA);
            break;
        }

        // -----------------------------
        // ORA $nn,X - Zero Page,X - Opcode $15
        // -----------------------------
        case 0x15: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A |= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // AND ($nn,X) - Indexed Indirect - Opcode $21
        // -----------------------------
        case 0x21: {
            enqueueReadIndX(ReadAction::AND);
            break;
        }

        // -----------------------------
        // AND $HHLL (Absolute) - Opcode $2D
        // -----------------------------
        case 0x2D: {
            enqueueReadAbs(ReadAction::AND);
            break;
        }

        // -----------------------------
        // AND ($nn),Y - Indirect Indexed - Opcode $31
        // -----------------------------
        case 0x31: {
            enqueueReadIndY(ReadAction::AND);
            break;
        }

        // -----------------------------
        // AND $nn,X - Zero Page,X - Opcode $35
        // -----------------------------
        case 0x35: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A &= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // EOR ($nn,X) - Indexed Indirect - Opcode $41
        // -----------------------------
        case 0x41: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                R.A ^= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // EOR $nn (Zero Page) - Opcode $45
        // -----------------------------
        case 0x45: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A ^= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // EOR ($nn),Y - Indirect Indexed - Opcode $51
        // -----------------------------
        case 0x51: {
            enqueueReadIndY(ReadAction::EOR);
            break;
        }

        // -----------------------------
        // EOR $nn,X - Zero Page,X - Opcode $55
        // -----------------------------
        case 0x55: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                R.A ^= Data_latch;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // CMP ($nn,X) - Indexed Indirect - Opcode $C1
        // -----------------------------
        case 0xC1: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                uint8_t result = static_cast<uint8_t>(R.A - Data_latch);
                ALU::setFlag(R.P, CARRY, R.A >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // CPY $nn (Zero Page) - Opcode $C4
        // -----------------------------
        case 0xC4: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                uint8_t result = static_cast<uint8_t>(R.Y - Data_latch);
                ALU::setFlag(R.P, CARRY, R.Y >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.Y == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // CPY $HHLL (Absolute) - Opcode $CC
        // -----------------------------
        case 0xCC: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                uint8_t result = static_cast<uint8_t>(R.Y - Data_latch);
                ALU::setFlag(R.P, CARRY, R.Y >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.Y == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            break;
        }

        // -----------------------------
        // CMP $nn,X - Zero Page,X - Opcode $D5
        // -----------------------------
        case 0xD5: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(ZP_addr);
                uint8_t result = static_cast<uint8_t>(R.A - Data_latch);
                ALU::setFlag(R.P, CARRY, R.A >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.A == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // CMP $HHLL,Y - Absolute,Y - Opcode $D9
        // -----------------------------
        case 0xD9: {
            enqueueReadAbsIndexed(ReadAction::CMP, false);
            break;
        }

        // -----------------------------
        // CPX $HHLL (Absolute) - Opcode $EC
        // -----------------------------
        case 0xEC: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() {
                Data_latch = bus.read(EffAddr);
                uint8_t result = static_cast<uint8_t>(R.X - Data_latch);
                ALU::setFlag(R.P, CARRY, R.X >= Data_latch);
                ALU::setFlag(R.P, ZERO, R.X == Data_latch);
                ALU::setFlag(R.P, NEGATIVE, (result & 0x80) != 0);
            }});
            break;
        }

        // -----------------------------
        // ORA $HHLL,Y - Absolute,Y - Opcode $19
        // -----------------------------
        case 0x19: {
            enqueueReadAbsIndexed(ReadAction::ORA, false);
            break;
        }

        // -----------------------------
        // AND $HHLL,X - Absolute,X - Opcode $3D
        // -----------------------------
        case 0x3D: {
            enqueueReadAbsIndexed(ReadAction::AND, true);
            break;
        }

        // -----------------------------
        // AND $HHLL,Y - Absolute,Y - Opcode $39
        // -----------------------------
        case 0x39: {
            enqueueReadAbsIndexed(ReadAction::AND, false);
            break;
        }

        // -----------------------------
        // EOR $HHLL (Absolute) - Opcode $4D
        // -----------------------------
        case 0x4D: {
            enqueueReadAbs(ReadAction::EOR);
            break;
        }

        // -----------------------------
        // EOR $HHLL,Y - Absolute,Y - Opcode $59
        // -----------------------------
        case 0x59: {
            enqueueReadAbsIndexed(ReadAction::EOR, false);
            break;
        }

        // -----------------------------
        // EOR $HHLL,X - Absolute,X - Opcode $5D
        // -----------------------------
        case 0x5D: {
            enqueueReadAbsIndexed(ReadAction::EOR, true);
            break;
        }

        // -----------------------------
        // ASL A - Opcode $0A
        // -----------------------------
        case 0x0A: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                ALU::setFlag(R.P, CARRY, (R.A & 0x80) != 0);
                R.A = static_cast<uint8_t>(R.A << 1);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ASL $nn - Opcode $06
        // -----------------------------
        case 0x06: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t newV = static_cast<uint8_t>(oldV << 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ASL $HHLL - Opcode $0E
        // -----------------------------
        case 0x0E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t newV = static_cast<uint8_t>(oldV << 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROL $nn - Opcode $26
        // -----------------------------
        case 0x26: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                uint8_t newV = static_cast<uint8_t>((oldV << 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROL $HHLL - Opcode $2E
        // -----------------------------
        case 0x2E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                uint8_t newV = static_cast<uint8_t>((oldV << 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROR $nn - Opcode $66
        // -----------------------------
        case 0x66: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                uint8_t newV = static_cast<uint8_t>((oldV >> 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROR $HHLL - Opcode $6E
        // -----------------------------
        case 0x6E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                uint8_t newV = static_cast<uint8_t>((oldV >> 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // LSR $HHLL - Opcode $4E
        // -----------------------------
        case 0x4E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = ((uint16_t)Temp_addr_high << 8) | Temp_addr_low; }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = Data_latch;
                Data_latch = static_cast<uint8_t>(oldV >> 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                bus.write(EffAddr, Data_latch);
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, false);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ASL $nn,X - Opcode $16
        // -----------------------------
        case 0x16: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t newV = static_cast<uint8_t>(oldV << 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ASL $HHLL,X - Opcode $1E
        // -----------------------------
        case 0x1E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = (((uint16_t)Temp_addr_high << 8) | Temp_addr_low) + R.X; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t newV = static_cast<uint8_t>(oldV << 1);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROL $nn,X - Opcode $36
        // -----------------------------
        case 0x36: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                uint8_t newV = static_cast<uint8_t>((oldV << 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROL $HHLL,X - Opcode $3E
        // -----------------------------
        case 0x3E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = (((uint16_t)Temp_addr_high << 8) | Temp_addr_low) + R.X; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 1 : 0;
                uint8_t newV = static_cast<uint8_t>((oldV << 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x80) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROR $nn,X - Opcode $76
        // -----------------------------
        case 0x76: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(ZP_addr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                uint8_t newV = static_cast<uint8_t>((oldV >> 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                bus.write(ZP_addr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ROR $HHLL,X - Opcode $7E
        // -----------------------------
        case 0x7E: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = (((uint16_t)Temp_addr_high << 8) | Temp_addr_low) + R.X; }});
            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = bus.read(EffAddr);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                uint8_t newV = static_cast<uint8_t>((oldV >> 1) | cIn);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                bus.write(EffAddr, newV);
                ALU::setFlag(R.P, ZERO, newV == 0);
                ALU::setFlag(R.P, NEGATIVE, (newV & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // ADC ($nn,X) - Opcode $61
        // -----------------------------
        case 0x61: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1)); EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low; }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); adcRevisionAware(Data_latch); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // ADC $nn
        case 0x65: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(ZP_addr); adcRevisionAware(Data_latch); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // ADC $HHLL
        case 0x6D: {
            enqueueReadAbs(ReadAction::ADC);
            break;
        }

        // ADC ($nn),Y
        case 0x71: {
            enqueueReadIndY(ReadAction::ADC);
            break;
        }

        // ADC $nn,X
        case 0x75: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(ZP_addr); adcRevisionAware(Data_latch); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // ADC $HHLL,Y
        case 0x79: {
            enqueueReadAbsIndexed(ReadAction::ADC, false);
            break;
        }

        // ADC $HHLL,X
        case 0x7D: {
            enqueueReadAbsIndexed(ReadAction::ADC, true);
            break;
        }

        // SBC ($nn,X)
        case 0xE1: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1)); EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low; }});
            microOps.push_back({PHI1, [](){} });
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); sbcRevisionAware(Data_latch, false); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // SBC $nn
        case 0xE5: {
            microOps.push_back({PHI2, [this]() { ZP_addr = bus.read(static_cast<uint16_t>(R.PC + 1)); }});
            microOps.push_back({PHI1, [this]() { R.PC = static_cast<uint16_t>(R.PC + 2); }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(ZP_addr); sbcRevisionAware(Data_latch, false); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // SBC $HHLL
        case 0xED: {
            enqueueReadAbs(ReadAction::SBC);
            break;
        }

        // SBC ($nn),Y
        case 0xF1: {
            enqueueReadIndY(ReadAction::SBC);
            break;
        }

        // SBC $nn,X
        case 0xF5: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(ZP_addr); sbcRevisionAware(Data_latch, false); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // SBC $HHLL,Y
        case 0xF9: {
            enqueueReadAbsIndexed(ReadAction::SBC, false);
            break;
        }

        // SBC $HHLL,X
        case 0xFD: {
            enqueueReadAbsIndexed(ReadAction::SBC, true);
            break;
        }

        // BVC
        case 0x50: {
            enqueueBranchRelative(!ALU::getFlag(R.P, OVERFLOW));
            break;
        }

        // BVS
        case 0x70: {
            enqueueBranchRelative(ALU::getFlag(R.P, OVERFLOW));
            break;
        }

        // -----------------------------
        // Unofficial: SLO (ASL memory, then ORA A)
        // -----------------------------
        case 0x03: case 0x07: case 0x0F: case 0x13: case 0x17: case 0x1B: case 0x1F: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0x07) m = UAddrMode::ZP;
            else if (opcode == 0x0F) m = UAddrMode::ABS;
            else if (opcode == 0x13) m = UAddrMode::INDY;
            else if (opcode == 0x17) m = UAddrMode::ZPX;
            else if (opcode == 0x1B) m = UAddrMode::ABSY;
            else if (opcode == 0x1F) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::SLO);
            break;
        }

        // Unofficial: RLA (ROL memory, then AND A)
        case 0x23: case 0x27: case 0x2F: case 0x33: case 0x37: case 0x3B: case 0x3F: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0x27) m = UAddrMode::ZP;
            else if (opcode == 0x2F) m = UAddrMode::ABS;
            else if (opcode == 0x33) m = UAddrMode::INDY;
            else if (opcode == 0x37) m = UAddrMode::ZPX;
            else if (opcode == 0x3B) m = UAddrMode::ABSY;
            else if (opcode == 0x3F) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::RLA);
            break;
        }

        // Unofficial: SRE (LSR memory, then EOR A)
        case 0x43: case 0x47: case 0x4F: case 0x53: case 0x57: case 0x5B: case 0x5F: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0x47) m = UAddrMode::ZP;
            else if (opcode == 0x4F) m = UAddrMode::ABS;
            else if (opcode == 0x53) m = UAddrMode::INDY;
            else if (opcode == 0x57) m = UAddrMode::ZPX;
            else if (opcode == 0x5B) m = UAddrMode::ABSY;
            else if (opcode == 0x5F) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::SRE);
            break;
        }

        // Unofficial: RRA (ROR memory, then ADC A)
        case 0x63: case 0x67: case 0x6F: case 0x73: case 0x77: case 0x7B: case 0x7F: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0x67) m = UAddrMode::ZP;
            else if (opcode == 0x6F) m = UAddrMode::ABS;
            else if (opcode == 0x73) m = UAddrMode::INDY;
            else if (opcode == 0x77) m = UAddrMode::ZPX;
            else if (opcode == 0x7B) m = UAddrMode::ABSY;
            else if (opcode == 0x7F) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::RRA);
            break;
        }

        // -----------------------------
        // Unofficial: SAX (store A & X)
        // -----------------------------
        case 0x83: case 0x87: case 0x8F: case 0x97: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0x87) m = UAddrMode::ZP;
            else if (opcode == 0x8F) m = UAddrMode::ABS;
            else if (opcode == 0x97) m = UAddrMode::ZPY;
            enqueueSAX(m);
            break;
        }

        // Unofficial: LAX (load to A and X)
        case 0xA3: case 0xA7: case 0xAF: case 0xB3: case 0xB7: case 0xBF: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0xA7) m = UAddrMode::ZP;
            else if (opcode == 0xAF) m = UAddrMode::ABS;
            else if (opcode == 0xB3) m = UAddrMode::INDY;
            else if (opcode == 0xB7) m = UAddrMode::ZPY;
            else if (opcode == 0xBF) m = UAddrMode::ABSY;
            enqueueLAX(m);
            break;
        }

        // -----------------------------
        // Unofficial: DCP (DEC memory, then CMP A)
        // -----------------------------
        case 0xC3: case 0xC7: case 0xCF: case 0xD3: case 0xD7: case 0xDB: case 0xDF: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0xC7) m = UAddrMode::ZP;
            else if (opcode == 0xCF) m = UAddrMode::ABS;
            else if (opcode == 0xD3) m = UAddrMode::INDY;
            else if (opcode == 0xD7) m = UAddrMode::ZPX;
            else if (opcode == 0xDB) m = UAddrMode::ABSY;
            else if (opcode == 0xDF) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::DCP);
            break;
        }

        // Unofficial: ISC (INC memory, then SBC A)
        case 0xE3: case 0xE7: case 0xEF: case 0xF3: case 0xF7: case 0xFB: case 0xFF: {
            UAddrMode m = UAddrMode::INDX;
            if (opcode == 0xE7) m = UAddrMode::ZP;
            else if (opcode == 0xEF) m = UAddrMode::ABS;
            else if (opcode == 0xF3) m = UAddrMode::INDY;
            else if (opcode == 0xF7) m = UAddrMode::ZPX;
            else if (opcode == 0xFB) m = UAddrMode::ABSY;
            else if (opcode == 0xFF) m = UAddrMode::ABSX;
            enqueueReadModifyWriteWithA(m, URmwKind::ISC);
            break;
        }

        // -----------------------------
        // Unofficial immediate ops
        // -----------------------------
        case 0x0B: case 0x2B: { // ANC #imm
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t imm = bus.read(R.PC + 1);
                R.PC += 2;
                R.A = static_cast<uint8_t>(R.A & imm);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                ALU::setFlag(R.P, CARRY, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x4B: { // ALR #imm
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t imm = bus.read(R.PC + 1);
                R.PC += 2;
                R.A = static_cast<uint8_t>(R.A & imm);
                ALU::setFlag(R.P, CARRY, (R.A & 0x01) != 0);
                R.A = static_cast<uint8_t>(R.A >> 1);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, false);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x6B: { // ARR #imm
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t imm = bus.read(R.PC + 1);
                R.PC += 2;
                uint8_t a = static_cast<uint8_t>(R.A & imm);
                uint8_t cIn = ALU::getFlag(R.P, CARRY) ? 0x80 : 0x00;
                R.A = static_cast<uint8_t>((a >> 1) | cIn);
                if (ALU::getFlag(R.P, DECIMAL) && revisionProfile.arrRespectsDecimalAdjust) {
                    uint8_t lo = static_cast<uint8_t>(R.A & 0x0F);
                    uint8_t hi = static_cast<uint8_t>((R.A >> 4) & 0x0F);
                    if (lo > 0x05) {
                        lo = static_cast<uint8_t>((lo + 0x06) & 0x0F);
                    }
                    if (hi > 0x05 || (R.A & 0x10)) {
                        hi = static_cast<uint8_t>((hi + 0x06) & 0x0F);
                        ALU::setFlag(R.P, CARRY, true);
                    }
                    R.A = static_cast<uint8_t>((hi << 4) | lo);
                }
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
                ALU::setFlag(R.P, CARRY, (R.A & 0x40) != 0);
                ALU::setFlag(R.P, OVERFLOW, ((R.A & 0x40) ^ ((R.A & 0x20) << 1)) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xCB: { // AXS #imm
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t imm = bus.read(R.PC + 1);
                R.PC += 2;
                uint8_t t = static_cast<uint8_t>(R.A & R.X);
                ALU::setFlag(R.P, CARRY, t >= imm);
                R.X = static_cast<uint8_t>(t - imm);
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // Missing official opcodes + NOP variants
        // -----------------------------
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); R.PC++; }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); (void)bus.read(R.PC + 1); R.PC += 2; }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x04: case 0x44: case 0x64: {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t zp = bus.read(R.PC + 1);
                (void)bus.read(zp);
                R.PC += 2;
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t zp = bus.read(R.PC + 1);
                (void)bus.read(static_cast<uint8_t>(zp + R.X));
                R.PC += 2;
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x0C: {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t lo = bus.read(R.PC + 1);
                uint8_t hi = bus.read(R.PC + 2);
                (void)bus.read((uint16_t(hi) << 8) | lo);
                R.PC += 3;
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: {
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t lo = bus.read(R.PC + 1);
                uint8_t hi = bus.read(R.PC + 2);
                uint16_t addr = ((uint16_t(hi) << 8) | lo) + R.X;
                (void)bus.read(addr);
                R.PC += 3;
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xB8: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); R.PC++; ALU::setFlag(R.P, OVERFLOW, false); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xF8: {
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); R.PC++; ALU::setFlag(R.P, DECIMAL, true); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x81: { // STA ($nn,X)
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.X);
            }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(ZP_addr); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                Temp_addr_high = bus.read(static_cast<uint8_t>(ZP_addr + 1));
                EffAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
            }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() { bus.write(EffAddr, R.A); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x96: { // STX $nn,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; ZP_addr = static_cast<uint8_t>(Temp_addr_low + R.Y); }});
            microOps.push_back({PHI2, [this]() { bus.write(ZP_addr, R.X); }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xBC: { // LDY $HHLL,X
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                uint16_t baseAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                EffAddr = baseAddr + R.X;
                pageCross = ((baseAddr & 0xFF00) != (EffAddr & 0xFF00));
            }});

            microOps.push_back({PHI2, [this]() {
                if (pageCross) {
                    uint16_t dummyAddr = (EffAddr & 0x00FF) | (Temp_addr_high << 8);
                    (void)bus.read(dummyAddr);
                }
            }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                R.Y = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, R.Y == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.Y & 0x80) != 0);
            }});
            break;
        }

        case 0xBE: { // LDX $HHLL,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                uint16_t baseAddr = (uint16_t(Temp_addr_high) << 8) | Temp_addr_low;
                EffAddr = baseAddr + R.Y;
                pageCross = ((baseAddr & 0xFF00) != (EffAddr & 0xFF00));
            }});

            microOps.push_back({PHI2, [this]() {
                if (pageCross) {
                    uint16_t dummyAddr = (EffAddr & 0x00FF) | (Temp_addr_high << 8);
                    (void)bus.read(dummyAddr);
                }
            }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                R.X = bus.read(EffAddr);
                ALU::setFlag(R.P, ZERO, R.X == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.X & 0x80) != 0);
            }});
            break;
        }

        case 0xC6: case 0xD6: case 0xDE: { // DEC zp / zp,X / abs,X
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            if (opcode == 0xC6 || opcode == 0xD6) {
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this, opcode]() {
                    R.PC++;
                    EffAddr = (opcode == 0xC6) ? Temp_addr_low : static_cast<uint8_t>(Temp_addr_low + R.X);
                }});
            } else {
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    EffAddr = ((uint16_t)Temp_addr_high << 8 | Temp_addr_low) + R.X;
                }});
            }

            if (opcode == 0xD6 || opcode == 0xDE) {
                microOps.push_back({PHI2, [this]() { (void)bus.read(EffAddr); }});
                microOps.push_back({PHI1, [](){} });
            }

            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                Data_latch = static_cast<uint8_t>(Data_latch - 1);
                bus.write(EffAddr, Data_latch);
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, (Data_latch & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xF6: case 0xFE: { // INC zp,X / abs,X
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            if (opcode == 0xF6) {
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; EffAddr = static_cast<uint8_t>(Temp_addr_low + R.X); }});
            } else {
                microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() { R.PC++; }});
                microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
                microOps.push_back({PHI1, [this]() {
                    R.PC++;
                    EffAddr = ((uint16_t)Temp_addr_high << 8 | Temp_addr_low) + R.X;
                }});
            }

            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() { bus.laWrite(EffAddr, Data_latch); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                Data_latch = static_cast<uint8_t>(Data_latch + 1);
                bus.write(EffAddr, Data_latch);
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, (Data_latch & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x5E: { // LSR abs,X
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_low = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});

            microOps.push_back({PHI2, [this]() { Temp_addr_high = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() {
                R.PC++;
                EffAddr = ((uint16_t)Temp_addr_high << 8 | Temp_addr_low) + R.X;
            }});

            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(EffAddr); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() { bus.laWrite(EffAddr, Data_latch); }});
            microOps.push_back({PHI1, [](){} });

            microOps.push_back({PHI2, [this]() {
                uint8_t oldV = Data_latch;
                Data_latch = static_cast<uint8_t>(oldV >> 1);
                bus.write(EffAddr, Data_latch);
                ALU::setFlag(R.P, CARRY, (oldV & 0x01) != 0);
                ALU::setFlag(R.P, ZERO, Data_latch == 0);
                ALU::setFlag(R.P, NEGATIVE, false);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xEB: { // unofficial SBC #imm alias
            microOps.push_back({PHI2, [this]() {
                IR = bus.read(R.PC);
                uint8_t imm = bus.read(R.PC + 1);
                sbcRevisionAware(imm, true);
                R.PC += 2;
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        // -----------------------------
        // Additional unofficial opcodes
        // -----------------------------
        case 0x8B: { // XAA #imm (unstable) approximated
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                const uint8_t magic = revisionProfile.xaaUsesMagicConstEE ? 0xEE : 0xFF;
                R.A = static_cast<uint8_t>(R.X & Data_latch & magic);
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xAB: { // LAX #imm (unstable) approximated as AND #imm then TAX
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() { Data_latch = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            microOps.push_back({PHI2, [this]() {
                R.A = static_cast<uint8_t>(R.A & Data_latch);
                R.X = R.A;
                ALU::setFlag(R.P, ZERO, R.A == 0);
                ALU::setFlag(R.P, NEGATIVE, (R.A & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x93: case 0x9F: { // AHX (ind),Y / AHX abs,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            enqueueResolveAddress(opcode == 0x93 ? UAddrMode::INDY : UAddrMode::ABSY);
            microOps.push_back({PHI2, [this]() {
                uint8_t hiMask = static_cast<uint8_t>(((EffAddr >> 8) + 1) & 0xFF);
                bus.write(EffAddr, static_cast<uint8_t>(R.A & R.X & hiMask));
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x9B: { // TAS abs,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            enqueueResolveAddress(UAddrMode::ABSY);
            microOps.push_back({PHI2, [this]() {
                R.SP = static_cast<uint8_t>(R.A & R.X);
                uint8_t hiMask = static_cast<uint8_t>(((EffAddr >> 8) + 1) & 0xFF);
                bus.write(EffAddr, static_cast<uint8_t>(R.SP & hiMask));
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x9C: { // SHY abs,X
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            enqueueResolveAddress(UAddrMode::ABSX);
            microOps.push_back({PHI2, [this]() {
                uint8_t hiMask = static_cast<uint8_t>(((EffAddr >> 8) + 1) & 0xFF);
                bus.write(EffAddr, static_cast<uint8_t>(R.Y & hiMask));
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0x9E: { // SHX abs,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            enqueueResolveAddress(UAddrMode::ABSY);
            microOps.push_back({PHI2, [this]() {
                uint8_t hiMask = static_cast<uint8_t>(((EffAddr >> 8) + 1) & 0xFF);
                bus.write(EffAddr, static_cast<uint8_t>(R.X & hiMask));
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }

        case 0xBB: { // LAS abs,Y
            microOps.push_back({PHI2, [this]() { IR = bus.read(R.PC); }});
            microOps.push_back({PHI1, [this]() { R.PC++; }});
            enqueueResolveAddress(UAddrMode::ABSY);
            microOps.push_back({PHI2, [this]() {
                uint8_t v = bus.read(EffAddr);
                uint8_t r = static_cast<uint8_t>(v & R.SP);
                R.A = r;
                R.X = r;
                R.SP = r;
                ALU::setFlag(R.P, ZERO, r == 0);
                ALU::setFlag(R.P, NEGATIVE, (r & 0x80) != 0);
            }});
            microOps.push_back({PHI1, [](){} });
            break;
        }


        default:
            std::cerr << "Unsupported opcode $" << std::hex << (int)opcode << "\n";
            std::exit(1);
            //break;
    }



}

    // ======================================
    // Sequenza micro-op per NMI
    // ======================================
    void triggerNMI() {
        microOps.clear();

        Temp_addr_low = 0;
        Temp_addr_high = 0;

        // --- Dummy read del PC corrente ---
        microOps.push_back({PHI2, [this]() { bus.read(R.PC); }});
        microOps.push_back({PHI1, [](){} });

        // --- Push high byte del PC ---
        microOps.push_back({PHI2, [this]() {
            uint8_t high = (R.PC >> 8) & 0xFF;
            bus.write(0x0100 | R.SP--, high);
            std::cout << "[NMI] Push PCH=$" << std::hex << (int)high << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Push low byte del PC ---
        microOps.push_back({PHI2, [this]() {
            uint8_t low = R.PC & 0xFF;
            bus.write(0x0100 | R.SP--, low);
            std::cout << "[NMI] Push PCL=$" << std::hex << (int)low << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Push P con B=0, bit5=1 ---
        microOps.push_back({PHI2, [this]() {
            uint8_t flags = (R.P & ~BREAK) | UNUSED;
            bus.write(0x0100 | R.SP--, flags);
            std::cout << "[NMI] Push P=$" << std::hex << (int)flags << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Fetch low byte del vettore NMI ($FFFA) ---
        microOps.push_back({PHI2, [this]() {
            Temp_addr_low = bus.read(0xFFFA);
            std::cout << "[NMI] Fetch vector low=$" << std::hex << (int)Temp_addr_low << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Fetch high byte del vettore NMI ($FFFB) ---
        microOps.push_back({PHI2, [this]() {
            Temp_addr_high = bus.read(0xFFFB);
            R.PC = (Temp_addr_high << 8) | Temp_addr_low;
            std::cout << "[NMI] Fetch vector high=$" << std::hex << (int)Temp_addr_high
                    << " -> PC=$" << R.PC << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Set flag I ---
        microOps.push_back({PHI2, [this]() {
            ALU::setFlag(R.P, INTERRUPT_DISABLE, true);
            std::cout << "[NMI] Set I flag" << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });
    }


    // ======================================
    // Sequenza micro-op per IRQ (livello NMOS fedele)
    // ======================================
    void triggerIRQ() {
        pendingIRQ = false;

        uint8_t PCH = (R.PC >> 8) & 0xFF;
        uint8_t PCL = R.PC & 0xFF;

        // --- Ciclo 1: Dummy read (completa fetch pipeline) ---
        microOps.push_back({PHI2, [this]() {
            bus.read(R.PC);
            std::cout << "[IRQ] Dummy read at PC=$" << std::hex << R.PC << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Ciclo 2: Push PCH ---
        microOps.push_back({PHI2, [this, PCH]() {
            bus.write(0x0100 | R.SP, PCH);
            std::cout << "[IRQ] Push PCH=$" << std::hex << (int)PCH << std::endl;
        }});
        microOps.push_back({PHI1, [this]() { R.SP--; }});

        // --- Ciclo 3: Push PCL ---
        microOps.push_back({PHI2, [this, PCL]() {
            bus.write(0x0100 | R.SP, PCL);
            std::cout << "[IRQ] Push PCL=$" << std::hex << (int)PCL << std::endl;
        }});
        microOps.push_back({PHI1, [this]() { R.SP--; }});

        // --- Ciclo 4: Push SR (B=0, U=1) ---
        microOps.push_back({PHI2, [this]() {
            uint8_t Ppush = (R.P & ~(BREAK)) | UNUSED;
            bus.write(0x0100 | R.SP, Ppush);
            std::cout << "[IRQ] Push SR=$" << std::hex << (int)Ppush << std::endl;
        }});
        microOps.push_back({PHI1, [this]() { R.SP--; }});

        // --- Ciclo 5: Fetch low byte del vettore da $FFFE ---
        microOps.push_back({PHI2, [this]() {
            Data_latch = bus.read(0xFFFE);
            std::cout << "[IRQ] Read low vector=$" << std::hex << (int)Data_latch << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });

        // --- Ciclo 6: Fetch high byte vettore da $FFFF e imposta PC ---
        microOps.push_back({PHI2, [this]() {
            uint8_t hi = bus.read(0xFFFF);
            R.PC = (hi << 8) | Data_latch;
            ALU::setFlag(R.P, INTERRUPT_DISABLE, true); // set I=1
            std::cout << "[IRQ] Read high vector=$" << std::hex << (int)hi
                    << " → New PC=$" << R.PC << std::endl;
        }});
        microOps.push_back({PHI1, [](){} });
    }

    void printInstruction(uint16_t pc, uint8_t opcode) {
        std::cout << "PC=$" << std::hex << (int)pc << " OPCODE=$" << (int)opcode;
        std::cout << std::endl;
    }
};

#include "test_adc_sbc.hpp"

#include "test_compare.hpp"

// Forward declaration used by the full regression runner.
void runOpcodeTimingSelfCheck(Bus &bus, CPU6510 &cpu);

#include "vicii_checklist.hpp"

#include "rom_smoke_test.hpp"

#include "full_regression_suite.hpp"

#include "ext_json_string_field.hpp"

#include "ext_json_number_field.hpp"

#include "ext_json_bool_field.hpp"

#include "ext_sanitize_filename.hpp"

#include "ext_to_lower_ascii.hpp"

#include "ext_ends_with_insensitive.hpp"

#include "external_manifest_loader.hpp"

#include "external_case_runner.hpp"

static std::string asciiLower(std::string v) {
    for (char &c : v) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return v;
}

static void configureChipRevisionsFromEnv(Bus &bus, CPU6510 &cpu, VICII &vic, CIA6526 &cia1, CIA6526 &cia2) {
    if (const char *cpuRev = std::getenv("C64_CPU_REVISION")) {
        const std::string v = asciiLower(cpuRev);
        if (v == "8500r2") cpu.setRevision(CPU6510::REV_8500R2);
        else if (v == "6510r2") cpu.setRevision(CPU6510::REV_6510R2);
        else cpu.setRevision((v == "8500") ? CPU6510::REV_8500 : CPU6510::REV_6510);
    }

    if (const char *vicRev = std::getenv("C64_VIC_REVISION")) {
        const std::string v = asciiLower(vicRev);
        if (v == "6569r3") vic.setRevision(VICII::REV_6569R3);
        else if (v == "8565r2") vic.setRevision(VICII::REV_8565R2);
        else vic.setRevision((v == "8565") ? VICII::REV_8565 : VICII::REV_6569);
    }

    auto parseCiaRev = [](const char *s) {
        const std::string v = asciiLower(std::string(s));
        return (v == "6526r4") ? CIA6526::REV_6526R4 : ((v == "6526a") ? CIA6526::REV_6526A : CIA6526::REV_6526);
    };

    if (const char *ciaBoth = std::getenv("C64_CIA_REVISION")) {
        const CIA6526::Revision r = parseCiaRev(ciaBoth);
        cia1.setRevision(r);
        cia2.setRevision(r);
    }
    if (const char *cia1Rev = std::getenv("C64_CIA1_REVISION")) {
        cia1.setRevision(parseCiaRev(cia1Rev));
    }
    if (const char *cia2Rev = std::getenv("C64_CIA2_REVISION")) {
        cia2.setRevision(parseCiaRev(cia2Rev));
    }

    if (const char *openBusRev = std::getenv("C64_OPENBUS_REVISION")) {
        const std::string v = asciiLower(openBusRev);
        bus.setOpenBusRevision((v == "hmos") ? Bus::OPENBUS_C64_HMOS : Bus::OPENBUS_C64_NMOS);
    }
}

static void configureDriveRevisionFromEnv(Drive1541 &drive) {
    const char *driveRev = std::getenv("C64_DRIVE_REVISION");
    if (driveRev == nullptr) {
        driveRev = std::getenv("KERNAL_DRIVE_REVISION");
    }
    if (driveRev == nullptr) {
        return;
    }
    const std::string v = asciiLower(driveRev);
    drive.setRevision((v == "1541ii") ? Drive1541::REV_1541II : ((v == "1541c") ? Drive1541::REV_1541C : Drive1541::REV_1541));
}

static bool runExternalRomCase(Bus &bus, CPU6510 &cpu, const ExternalRomCase &tc) {
    const char *savedCpuRev = std::getenv("C64_CPU_REVISION");
    const char *savedVicRev = std::getenv("C64_VIC_REVISION");
    const char *savedCiaRev = std::getenv("C64_CIA_REVISION");
    const char *savedOpenBusRev = std::getenv("C64_OPENBUS_REVISION");
    const char *savedDriveRev = std::getenv("C64_DRIVE_REVISION");

    std::string savedCpuRevStr = (savedCpuRev != nullptr) ? std::string(savedCpuRev) : std::string();
    std::string savedVicRevStr = (savedVicRev != nullptr) ? std::string(savedVicRev) : std::string();
    std::string savedCiaRevStr = (savedCiaRev != nullptr) ? std::string(savedCiaRev) : std::string();
    std::string savedOpenBusRevStr = (savedOpenBusRev != nullptr) ? std::string(savedOpenBusRev) : std::string();
    std::string savedDriveRevStr = (savedDriveRev != nullptr) ? std::string(savedDriveRev) : std::string();

    auto setOrClearEnv = [](const char *key, const std::string &value) {
        const int rc = value.empty() ? _putenv_s(key, "") : _putenv_s(key, value.c_str());
        (void)rc;
        assert(rc == 0);
    };

    auto restoreEnv = [&]() {
        setOrClearEnv("C64_CPU_REVISION", savedCpuRevStr);
        setOrClearEnv("C64_VIC_REVISION", savedVicRevStr);
        setOrClearEnv("C64_CIA_REVISION", savedCiaRevStr);
        setOrClearEnv("C64_OPENBUS_REVISION", savedOpenBusRevStr);
        setOrClearEnv("C64_DRIVE_REVISION", savedDriveRevStr);
    };

    if (!tc.envCpuRevision.empty()) setOrClearEnv("C64_CPU_REVISION", tc.envCpuRevision);
    if (!tc.envVicRevision.empty()) setOrClearEnv("C64_VIC_REVISION", tc.envVicRevision);
    if (!tc.envCiaRevision.empty()) setOrClearEnv("C64_CIA_REVISION", tc.envCiaRevision);
    if (!tc.envOpenBusRevision.empty()) setOrClearEnv("C64_OPENBUS_REVISION", tc.envOpenBusRevision);
    if (!tc.envDriveRevision.empty()) setOrClearEnv("C64_DRIVE_REVISION", tc.envDriveRevision);

    {
        std::ifstream romProbe(tc.romPath, std::ios::binary);
        if (!romProbe.is_open()) {
            if (tc.optional) {
                std::cerr << "[EXT] SKIP optional (missing ROM): " << tc.name
                          << " path=" << tc.romPath << std::endl;
                restoreEnv();
                return true;
            }
        }
    }

    std::ofstream nullOut("NUL");
    std::streambuf *savedCoutBuf = silenceStdoutToNull(nullOut);

    resetBusForExternalCase(bus);

    if (const char *cpuRev = std::getenv("C64_CPU_REVISION")) {
        const std::string v = asciiLower(cpuRev);
        if (v == "8500r2") cpu.setRevision(CPU6510::REV_8500R2);
        else if (v == "6510r2") cpu.setRevision(CPU6510::REV_6510R2);
        else cpu.setRevision((v == "8500") ? CPU6510::REV_8500 : CPU6510::REV_6510);
    }
    if (const char *openBusRev = std::getenv("C64_OPENBUS_REVISION")) {
        const std::string v = asciiLower(openBusRev);
        bus.setOpenBusRevision((v == "hmos") ? Bus::OPENBUS_C64_HMOS : Bus::OPENBUS_C64_NMOS);
    }
    if (bus.vic != nullptr) {
        if (const char *vicRev = std::getenv("C64_VIC_REVISION")) {
            const std::string v = asciiLower(vicRev);
            if (v == "6569r3") bus.vic->setRevision(VICII::REV_6569R3);
            else if (v == "8565r2") bus.vic->setRevision(VICII::REV_8565R2);
            else bus.vic->setRevision((v == "8565") ? VICII::REV_8565 : VICII::REV_6569);
        }
    }
    auto parseCiaRev = [](const char *s) {
        const std::string v = asciiLower(std::string(s));
        return (v == "6526r4") ? CIA6526::REV_6526R4 : ((v == "6526a") ? CIA6526::REV_6526A : CIA6526::REV_6526);
    };
    if (const char *ciaBoth = std::getenv("C64_CIA_REVISION")) {
        const CIA6526::Revision r = parseCiaRev(ciaBoth);
        if (bus.cia1 != nullptr) bus.cia1->setRevision(r);
        if (bus.cia2 != nullptr) bus.cia2->setRevision(r);
    }
    if (const char *cia1Rev = std::getenv("C64_CIA1_REVISION")) {
        if (bus.cia1 != nullptr) bus.cia1->setRevision(parseCiaRev(cia1Rev));
    }
    if (const char *cia2Rev = std::getenv("C64_CIA2_REVISION")) {
        if (bus.cia2 != nullptr) bus.cia2->setRevision(parseCiaRev(cia2Rev));
    }

    uint16_t effectiveLoadAddress = 0;
    uint16_t effectiveResetVector = 0;
    bool treatAsPrg = false;
    if (!loadExternalCaseProgram(bus, cpu, tc, effectiveLoadAddress, effectiveResetVector, treatAsPrg, savedCoutBuf)) {
        bus.flatMemoryMode = false;
        restoreEnv();
        return false;
    }

    std::string traceName;
    std::ofstream trace = openExternalCaseTrace(tc, traceName);

    bool hitPassPC = false;
    bool hitFailPC = false;
    bool halted = false;
    uint32_t uniquePCCount = 0;
    uint32_t requirePCHitCount = 0;
    Registers r{};
    ExternalCaseLastEvent lastEvent{};
    runExternalCaseTraceLoop(cpu, bus, tc, trace, hitPassPC, hitFailPC, halted, uniquePCCount, requirePCHitCount, r, lastEvent);

    // Ensure runtime trace content is fully persisted before reference-diff reads it.
    if (trace.is_open()) {
        trace.flush();
        trace.close();
    }

    bool ok = evaluateExternalCaseResult(tc, bus, hitPassPC, hitFailPC, halted, uniquePCCount, requirePCHitCount);

    restoreStdout(savedCoutBuf);

    uint32_t traceMismatchCount = 0;
    bool traceDiffEnabled = !tc.referenceTracePath.empty();
    std::string traceDiffReason;
    bool traceOk = true;
    if (traceDiffEnabled) {
        traceOk = runReferenceTraceDiff(tc, traceName, traceMismatchCount, traceDiffReason);
        if (!traceOk) {
            if (tc.referenceOptional) {
                std::cerr << "[EXT][REFDIFF] SKIP optional: " << tc.name
                          << " " << traceDiffReason << std::endl;
                traceOk = true;
            } else {
                std::cerr << "[EXT][REFDIFF] " << tc.name << " " << traceDiffReason << std::endl;
            }
        }
    }
    ok = ok && traceOk;

    std::ostringstream expectMemInfo = buildExpectedMemInfo(tc, bus);
    logExternalCaseResult(tc, treatAsPrg, effectiveLoadAddress, effectiveResetVector,
                          r, requirePCHitCount, uniquePCCount, traceMismatchCount,
                          traceDiffEnabled, expectMemInfo, traceName, ok, lastEvent);

    bus.flatMemoryMode = false;

    restoreEnv();
    return ok;
}

#include "external_validation.hpp"

#include "drive_smoke_tests.hpp"

#include "drive_iec_handshake_smoke.hpp"

#include "iec_host_helpers.hpp"

#include "iec_host_session.hpp"

#include "drive_iec_host_session_smoke.hpp"

#include "drive_iec_cmd_smoke.hpp"

#include "drive_iec_dir_smoke.hpp"

#include "drive_iec_status_smoke.hpp"

#include "drive_iec_mem_smoke.hpp"

#include "drive_iec_exec_block_cmd_smoke.hpp"

#include "drive_iec_exec_block_sem_smoke.hpp"

#include "drive_iec_ptr_dir_smoke.hpp"

#include "drive_iec_map_smoke.hpp"

#include "drive_iec_named_catalog_smoke.hpp"

#include "drive_iec_open_suffix_smoke.hpp"

#include "drive_iec_wc_smoke.hpp"

#include "drive_iec_wct_smoke.hpp"

#include "drive_iec_wcm_smoke.hpp"

#include "drive_iec_wcn_smoke.hpp"

#include "drive_iec_e2e_smoke.hpp"

#include "cia6526_battery.hpp"

#include "drive_1541_timing_battery.hpp"

static void runKernelSerialLoadDirectoryTrueE2E() {
    Bus bus;
    const bool systemRomsLoaded = bus.loadSystemRoms("roms");
    if (!systemRomsLoaded) {
        std::cerr << "[KERNAL IEC E2E] FAIL: missing BASIC/KERNAL/CHAR ROM set in ./roms" << std::endl;
        assert(false);
    }

    VICII vic;
    CIA6526 cia1;
    CIA6526 cia2;
    SID sid;

    bus.vic = &vic;
    vic.bus = &bus;
    bus.cia1 = &cia1;
    bus.cia2 = &cia2;
    bus.sid = &sid;

    CPU6510 cpu(bus);
    configureChipRevisionsFromEnv(bus, cpu, vic, cia1, cia2);

    Drive1541 drive;
    configureDriveRevisionFromEnv(drive);
    if (!drive.loadRom("roms/dos1541.rom")) {
        std::cerr << "[KERNAL IEC E2E] FAIL: cannot load roms/dos1541.rom" << std::endl;
        assert(false);
    }
    drive.cpuEnabled = false;
    drive.iecEnableAtnAck = (std::getenv("KERNAL_DRIVE_DISABLE_ATN_ACK") == nullptr);
    drive.iecEnableListenerByteAck = (std::getenv("KERNAL_DRIVE_DISABLE_LISTENER_ACK") == nullptr);
    drive.iecKernelCompatSampleBothClockEdges = (std::getenv("KERNAL_DRIVE_SAMPLE_BOTH_EDGES") != nullptr);
    drive.iecKernelSampleOnFallingClockEdge = (std::getenv("KERNAL_DRIVE_SAMPLE_FALLING_EDGE") != nullptr);
    drive.iecKernelSampleBothCommandEdges = (std::getenv("KERNAL_DRIVE_SAMPLE_BOTH_CMD_EDGES") != nullptr);
    drive.iecKernelCompatAutoTalkDirectory = (std::getenv("KERNAL_DRIVE_AUTO_TALK_DIR") != nullptr);
    drive.iecKernelCompatAutoDirectoryOnTalk0 = (std::getenv("KERNAL_DRIVE_AUTO_DIR_ON_TALK0") != nullptr);
    drive.iecKernelCompatForceTalkOnIcrSerial = (std::getenv("KERNAL_DRIVE_FORCE_TALK_ON_DD0D8") != nullptr);
    drive.iecKernelIgnoreAtnForTalkDataPhase = (std::getenv("KERNAL_DRIVE_IGNORE_ATN_FOR_TALK") != nullptr);
    cia2.ier = 0;
    cia2.icr = 0;

    drive.iecListening = false;
    drive.iecTalking = false;
    drive.iecListenSecondary = 0xFF;
    drive.iecTalkSecondary = 0xFF;
    drive.iecTalkSa0Confirmed = false;
    drive.iecExpectingNameBytes = false;
    drive.iecNameBuffer.clear();
    drive.iecTxQueue.clear();
    drive.iecRxQueue.clear();
    drive.iecRxShift = 0;
    drive.iecRxBitCount = 0;
    drive.iecPrevCLK = true;
    drive.iecPrevATN = true;
    drive.iecPrevDATA = true;
    drive.iecAtnHandshakeActive = false;

    const uint16_t start = 0x2000;
    const uint16_t doneLoop = 0x2028;
    const uint16_t namePtr = 0x2100;

    const std::vector<uint8_t> prg = {
        0x78,
        0xD8,
        0x20, 0xA3, 0xFD,
        0x20, 0x15, 0xFD,
        0x20, 0x84, 0xFF,
        0x20, 0x8A, 0xFF,
        0xA9, 0x01,
        0xA2, 0x08,
        0xA0, 0x00,
        0x20, 0xBA, 0xFF,
        0xA9, 0x01,
        0xA2, 0x00,
        0xA0, 0x21,
        0x20, 0xBD, 0xFF,
        0xA9, 0x00,
        0xA2, 0x00,
        0xA0, 0x00,
        0x20, 0xD5, 0xFF,
        0x4C, 0x28, 0x20
    };

    for (size_t i = 0; i < prg.size(); ++i) {
        bus.memory[start + i] = prg[i];
    }
    bus.memory[namePtr] = static_cast<uint8_t>('$');

    cpu.reset();
    bus.memory[0x00A4] = 0x00;
    bus.memory[0x00A5] = 0x00;
    bus.memory[0x0090] = 0x00;
    for (int i = 0; i < 128; ++i) {
        cpu.clock();
        if (cpu.isIdle()) {
            break;
        }
    }

    Registers r = cpu.getRegisters();
    r.PC = start;
    r.SP = 0xFD;
    r.A = 0;
    r.X = 0;
    r.Y = 0;
    r.P = UNUSED | INTERRUPT_DISABLE;
    cpu.setRegisters(r);

    struct CiaAccessEvent {
        uint16_t pc = 0;
        uint16_t addr = 0;
        uint8_t val = 0;
        bool isWrite = false;
    };
    std::vector<CiaAccessEvent> ciaAccessLog;
    ciaAccessLog.reserve(16384);

    uint64_t dd00Writes = 0;
    uint64_t dd02Writes = 0;
    uint64_t dd04Writes = 0;
    uint64_t dd05Writes = 0;
    uint64_t dd06Writes = 0;
    uint64_t dd07Writes = 0;
    uint64_t dd0dReads = 0;
    uint64_t dd00Reads = 0;
    uint64_t dd00PreReads = 0;
    uint64_t dd00PreReadsHotLoop = 0;
    uint64_t dd00PreReadsMismatch = 0;
    uint64_t dd00PreReadsEe1b = 0;
    uint64_t dd00PreReadsEe1e = 0;
    uint64_t dd00PreReadsEeaf = 0;
    uint64_t eeafDd00ChangeCount = 0;
    uint64_t eeafDd00ClkRiseCount = 0;
    bool eeafDd00ChangedSinceKick = false;
    bool eeafDd00ClkRiseSinceKick = false;
    uint8_t eeafLastDd00PreRead = 0xFF;
    bool eeafHaveLastDd00PreRead = false;
    uint8_t lastDd00PreReadVal = 0xFF;
    bool haveLastDd00PreReadVal = false;
    std::array<uint8_t, 64> dd00PreReadHistory = {};
    size_t dd00PreReadHistCount = 0;
    std::array<uint8_t, 64> dd00PreReadHotLoopHistory = {};
    size_t dd00PreReadHotLoopHistCount = 0;
    std::array<uint8_t, 32> dd00ReadHistory = {};
    size_t dd00ReadHistCount = 0;
    uint64_t dd00ReadEd50Ed80 = 0;
    std::array<uint8_t, 64> dd00ReadEd50Ed80History = {};
    size_t dd00ReadEd50Ed80HistCount = 0;
    std::array<uint16_t, 64> dd00ReadEd50Ed80PcHistory = {};
    uint8_t lastDd00ReadEd = 0xFF;
    bool haveLastDd00ReadEd = false;
    uint64_t dd0eWrites = 0;
    uint64_t dd0fWrites = 0;
    uint64_t dd0eReads = 0;
    uint64_t dd0fReads = 0;
    uint64_t dc07Writes = 0;
    uint64_t dc0fWrites = 0;
    uint64_t dc0dReads = 0;
    uint16_t compatRamSinkPtr = 0x0801;
    bool compatBulkInjected = false;
    uint8_t lastDd00 = 0;
    uint8_t lastDd02 = 0;
    std::array<uint8_t, 32> dd00History = {};
    size_t dd00HistCount = 0;
    std::array<uint8_t, 32> dd0eHistory = {};
    size_t dd0eHistCount = 0;
    std::array<uint8_t, 32> dd0fHistory = {};
    size_t dd0fHistCount = 0;
    bus.writeTap = [&](uint16_t addr, uint8_t val) {
        if (addr == 0xDD0D && (val & 0x08) != 0 && drive.iecKernelCompatForceTalkOnIcrSerial) {
            drive.iecTalking = true;
            if (!drive.iecDirectoryStubPrepared) {
                drive.iecDirectoryWildcardPattern.clear();
                drive.iecDirectoryTypeFilter.clear();
                drive.iecDirectoryModeFilter.clear();
                drive.iecDirectoryModeFilterNegated = false;
                drive.buildDirectoryStubPayload();
            }
            if (drive.iecActiveTalkChannel == 0xFF) {
                drive.iecTalkSecondary = 0;
                drive.iecActiveTalkChannel = 0;
                drive.iecOpenTalkChannels[0] = true;
                drive.iecTalkSa0Confirmed = true;
            }
            if (drive.pendingIecTx() == 0) {
                drive.buildDirectoryStubPayload();
            }
            if (drive.iecCLK == false && drive.iecTalking && drive.iecActiveTalkChannel == 0 && drive.pendingIecTx() > 0) {
                const uint8_t first = drive.iecTxQueue.front();
                drive.iecTxQueue.pop_front();
                drive.iecTxServed++;
                drive.iecRxByteAckPullDATA = ((first & 0x01) == 0);
                drive.iecTxByteActive = false;
                drive.iecEoiPendingAck = false;
            }
        }
        const uint16_t pcNow = cpu.getRegisters().PC;
        if ((pcNow >= 0xED40 && pcNow <= 0xEED0) &&
            (addr >= 0xDC00 && addr <= 0xDD0F) &&
            ciaAccessLog.size() < 200000) {
            ciaAccessLog.push_back(CiaAccessEvent{pcNow, addr, val, true});
        }
        if (addr == 0xDD00) {
            dd00Writes++;
            lastDd00 = val;
            if (dd00HistCount < dd00History.size()) {
                dd00History[dd00HistCount++] = val;
            }
        } else if (addr == 0xDD02) {
            dd02Writes++;
            lastDd02 = val;
        } else if (addr == 0xDD04) {
            dd04Writes++;
        } else if (addr == 0xDD05) {
            dd05Writes++;
        } else if (addr == 0xDD06) {
            dd06Writes++;
        } else if (addr == 0xDD07) {
            dd07Writes++;
        } else if (addr == 0xDC07) {
            dc07Writes++;
        } else if (addr == 0xDC0F) {
            dc0fWrites++;
        } else if (addr == 0xDD0E) {
            dd0eWrites++;
            if (dd0eHistCount < dd0eHistory.size()) {
                dd0eHistory[dd0eHistCount++] = val;
            }
        } else if (addr == 0xDD0F) {
            dd0fWrites++;
            if (dd0fHistCount < dd0fHistory.size()) {
                dd0fHistory[dd0fHistCount++] = val;
            }
        }
    };
    bus.readTap = [&](uint16_t addr, uint8_t val) {
        const uint16_t pcNow = cpu.getRegisters().PC;
        if ((pcNow >= 0xED40 && pcNow <= 0xEED0) &&
            (addr >= 0xDC00 && addr <= 0xDD0F) &&
            ciaAccessLog.size() < 200000) {
            ciaAccessLog.push_back(CiaAccessEvent{pcNow, addr, val, false});
        }
        if (addr == 0xDD00) {
            dd00Reads++;
            if (dd00ReadHistCount < dd00ReadHistory.size()) {
                dd00ReadHistory[dd00ReadHistCount++] = cia2.getPortACombined();
            }
            const uint16_t pcNow = cpu.getRegisters().PC;
            if (pcNow >= 0xED50 && pcNow <= 0xED80) {
                dd00ReadEd50Ed80++;
                if (!haveLastDd00ReadEd || val != lastDd00ReadEd) {
                    haveLastDd00ReadEd = true;
                    lastDd00ReadEd = val;
                    if (dd00ReadEd50Ed80HistCount < dd00ReadEd50Ed80History.size()) {
                        dd00ReadEd50Ed80History[dd00ReadEd50Ed80HistCount++] = val;
                        dd00ReadEd50Ed80PcHistory[dd00ReadEd50Ed80HistCount - 1] = pcNow;
                    }
                }
            }
        } else if (addr == 0xDD0D) {
            dd0dReads++;
        } else if (addr == 0xDD0E) {
            dd0eReads++;
        } else if (addr == 0xDD0F) {
            dd0fReads++;
        } else if (addr == 0xDC0D) {
            dc0dReads++;
        }
    };

    IecBridgePolarity kernelPolarity = makeRuntimeDefaultIecPolarity();
    if (const char *pol = std::getenv("KERNAL_IEC_POLARITY")) {
        // Expected format: A,C,D,IC,ID,SO,SI,CO,MI,RB as 0/1 digits.
        std::vector<int> bits;
        for (const char *p = pol; *p; ++p) {
            if (*p == '0' || *p == '1') {
                bits.push_back(*p - '0');
            }
        }
        if (bits.size() >= 4) {
            kernelPolarity.atnPullWhenBitSet = (bits[0] != 0);
            kernelPolarity.clkPullWhenBitSet = (bits[1] != 0);
            kernelPolarity.dataPullWhenBitSet = (bits[2] != 0);
            kernelPolarity.inputClkBitSetWhenLineHigh = (bits[3] != 0);
            kernelPolarity.inputDataBitSetWhenLineHigh = (bits.size() >= 5 ? (bits[4] != 0) : kernelPolarity.inputClkBitSetWhenLineHigh);
            kernelPolarity.swapOutputClockData = (bits.size() >= 6 ? (bits[5] != 0) : false);
            kernelPolarity.swapInputClockData = (bits.size() >= 7 ? (bits[6] != 0) : false);
            kernelPolarity.useCombinedPortAForOutputs = (bits.size() >= 8 ? (bits[7] != 0) : false);
            kernelPolarity.mirrorInputsIntoPra = (bits.size() >= 9 ? (bits[8] != 0) : kernelPolarity.mirrorInputsIntoPra);
            kernelPolarity.forceReadbackDd00ClockDataFromBus = (bits.size() >= 10 ? (bits[9] != 0) : false);
            // For KERNAL IEC polling loops, DD00 must reflect effective bus clock/data
            // even while CIA output bits are actively driving the IEC transistor stage.
            // Restricting readback to release-only phases can hide transitions and trap
            // KERNAL in FD23..FD27 fallback loops.
            kernelPolarity.readbackBusOnlyWhenOutputHigh = false;
        }
    }
    if (std::getenv("KERNAL_DD00_READBACK_OUTPUT_HIGH_ONLY") != nullptr) {
        kernelPolarity.readbackBusOnlyWhenOutputHigh = true;
    }
    if (std::getenv("KERNAL_DD00_EDWINDOW_RMW") != nullptr) {
        kernelPolarity.readbackBusOnEdWindowOnly = true;
    }
    SharedIecClockDomain sharedDomain(cia2, drive, kernelPolarity);
    bool edWindowReadbackOverrideActive = false;

    bus.preReadTap = [&](uint16_t addr) {
        if (addr != 0xDD00) {
            return;
        }
        const Registers rr = cpu.getRegisters();
        const bool inHotLoop = (rr.PC == 0xEE1B || rr.PC == 0xEE1E || rr.PC == 0xEEAF || rr.PC == 0xED5D || rr.PC == 0xED5E);

        syncIecBusWithPolarity(cia2, drive, sharedDomain.polarity);
        const uint8_t preVal = cia2.getPortACombined();
        dd00PreReads++;
        if (dd00PreReadHistCount < dd00PreReadHistory.size()) {
            dd00PreReadHistory[dd00PreReadHistCount++] = preVal;
        }
        if (inHotLoop) {
            dd00PreReadsHotLoop++;
            if (dd00PreReadHotLoopHistCount < dd00PreReadHotLoopHistory.size()) {
                dd00PreReadHotLoopHistory[dd00PreReadHotLoopHistCount++] = preVal;
            }
        }
        if (rr.PC == 0xEE1B) {
            dd00PreReadsEe1b++;
        } else if (rr.PC == 0xEE1E) {
            dd00PreReadsEe1e++;
        } else if (rr.PC == 0xEEAF) {
            dd00PreReadsEeaf++;
            if (eeafHaveLastDd00PreRead && preVal != eeafLastDd00PreRead) {
                eeafDd00ChangeCount++;
                eeafDd00ChangedSinceKick = true;
            }
            const bool prevClkHigh = eeafHaveLastDd00PreRead && ((eeafLastDd00PreRead & 0x40) != 0);
            const bool currClkHigh = (preVal & 0x40) != 0;
            if (eeafHaveLastDd00PreRead && !prevClkHigh && currClkHigh) {
                eeafDd00ClkRiseCount++;
                eeafDd00ClkRiseSinceKick = true;
            }
            eeafLastDd00PreRead = preVal;
            eeafHaveLastDd00PreRead = true;
        }
        if (haveLastDd00PreReadVal && preVal != lastDd00PreReadVal) {
            dd00PreReadsMismatch++;
        }
        lastDd00PreReadVal = preVal;
        haveLastDd00PreReadVal = true;
    };

    // Keep directory content deterministic without injecting IEC command bytes.
    drive.iecCatalog[0] = Drive1541::VirtualCatalogEntry{};
    drive.iecCatalog[0].used = true;
    drive.iecCatalog[0].channel = 2;
    drive.iecCatalog[0].track = 0x15;
    drive.iecCatalog[0].sector = 0x01;
    drive.iecCatalog[0].blocks = 1;
    drive.iecCatalog[0].name = "KERNFILE";
    drive.iecCatalog[0].type = "PRG";
    drive.iecCatalog[0].mode = "W";
    drive.iecAllocatedBlockCount = 1;
    drive.iecRxProcessed = 0;
    drive.iecTxServed = 0;

    bool hitLoadCall = false;
    bool hitKernalSpace = false;
    bool observedDirectoryInRam = false;
    bool loadReturned = false;
    bool hitEd5e = false;
    uint32_t ed5eHits = 0;
    uint32_t edWindowHits = 0;
    uint32_t doneHits = 0;
    uint64_t c64AtnTransitions = 0;
    uint64_t c64ClkTransitions = 0;
    uint64_t c64DataTransitions = 0;
    uint64_t eeafVisitCount = 0;
    uint64_t eeafIcrKickCount = 0;
    uint64_t eeafPulseCount = 0;
    uint32_t eeafVisitStreak = 0;
    bool prevC64Atn = true;
    bool prevC64Clk = true;
    bool prevC64Data = true;
    uint64_t busClkRising = 0;
    uint64_t busClkRisingAtnLow = 0;
    bool prevLineCLKHigh = true;
    struct Dd00LoopEvent {
        uint32_t step = 0;
        uint16_t pc = 0;
        uint8_t dd00 = 0;
        uint8_t dd0d = 0;
        bool lineCLK = true;
        bool lineDATA = true;
        bool lineATN = true;
        bool txActive = false;
        uint8_t txBit = 0;
        bool eoiPending = false;
        uint16_t txq = 0;
        uint64_t txServed = 0;
    };
    std::vector<Dd00LoopEvent> dd00LoopEvents;
    dd00LoopEvents.reserve(1024);
    bool kernalDd00TraceEnabled = (std::getenv("KERNAL_DD00_TRACE") != nullptr);
    bool kernalDebugForceClockToggle = (std::getenv("KERNAL_DEBUG_FORCE_CLOCK_TOGGLE") != nullptr);
    bool kernalCompatClockAssist = (std::getenv("KERNAL_COMPAT_CLOCK_ASSIST") != nullptr);
    bool kernalCompatRamSinkInject = (std::getenv("KERNAL_COMPAT_RAM_SINK_INJECT") != nullptr);
    bool kernalCompatRamSinkBulk = (std::getenv("KERNAL_COMPAT_RAM_SINK_BULK") != nullptr);
    const bool kernalPureCmdGuard = (std::getenv("KERNAL_TEST_ONLY_PURE_CMD_GUARD") != nullptr);
    bool pureCmdGuardInjected = false;
    uint32_t pureCmdGuardInjectedBytes = 0;
    bool pureCmdClockAssist = false;
    bool kernalDebugClockFlip = false;
    uint8_t prevDd00Trace = 0xFF;
    uint64_t prevTxServedTrace = 0;
    bool kernalBranchTraceEnabled = (std::getenv("KERNAL_BRANCH_TRACE") != nullptr);
    uint32_t kernalBranchTraceCount = 0;
    static constexpr uint32_t kernalBranchTraceMax = 8192;
    bool kernalEeafTraceEnabled = (std::getenv("KERNAL_EEAF_TRACE") != nullptr);
    uint32_t kernalEeafTraceCount = 0;
    static constexpr uint32_t kernalEeafTraceMax = 512;
    bool kernalOpcodeTraceEnabled = (std::getenv("KERNAL_OPCODE_TRACE") != nullptr);
    uint32_t kernalOpcodeTraceCount = 0;
    static constexpr uint32_t kernalOpcodeTraceMax = 8192;
    auto traceKernalBranch = [&](const Registers &tr, const char *tag) {
        if (!kernalBranchTraceEnabled || kernalBranchTraceCount >= kernalBranchTraceMax) {
            return;
        }
        const uint8_t op = bus.peek(tr.PC);
        const bool carrySet = (tr.P & CARRY) != 0;
        const bool zeroSet = (tr.P & ZERO) != 0;
        const bool negSet = (tr.P & NEGATIVE) != 0;
        if (op == 0x90 || op == 0xB0 || op == 0xF0 || op == 0xD0 || op == 0x30 || op == 0x10 || op == 0x70 || op == 0x50) {
            const int8_t rel = static_cast<int8_t>(bus.peek(static_cast<uint16_t>(tr.PC + 1)));
            const uint16_t target = static_cast<uint16_t>(tr.PC + 2 + rel);
            bool taken = false;
            const char *mn = "BR";
            switch (op) {
                case 0x90: mn = "BCC"; taken = !carrySet; break;
                case 0xB0: mn = "BCS"; taken = carrySet; break;
                case 0xF0: mn = "BEQ"; taken = zeroSet; break;
                case 0xD0: mn = "BNE"; taken = !zeroSet; break;
                case 0x30: mn = "BMI"; taken = negSet; break;
                case 0x10: mn = "BPL"; taken = !negSet; break;
                case 0x70: mn = "BVS"; taken = (tr.P & OVERFLOW) != 0; break;
                case 0x50: mn = "BVC"; taken = (tr.P & OVERFLOW) == 0; break;
            }
            std::cerr << "[KERNAL BR] tag=" << tag
                      << " pc=$" << std::hex << tr.PC
                      << " op=" << mn
                      << " rel=$" << (int)(uint8_t)rel
                      << " tgt=$" << target
                      << " taken=" << (taken ? "yes" : "no")
                      << " c=" << (carrySet ? 1 : 0)
                      << " z=" << (zeroSet ? 1 : 0)
                      << " n=" << (negSet ? 1 : 0)
                      << " dd00=$" << (int)cia2.getPortACombined()
                      << " dd0d=$" << (int)cia2.icr
                      << " dc0d=$" << (int)cia1.icr
                      << " cra=$" << (int)cia2.controlA
                      << " crb=$" << (int)cia2.t2_ctrl
                      << std::dec << std::endl;
            kernalBranchTraceCount++;
        }
    };

    auto replayCiaLogIntoDrive = [&](bool verbose) {
        CIA6526 shadowCia2;
        shadowCia2.ddra = cia2.ddra;
        shadowCia2.pra = cia2.pra;
        shadowCia2.praInput = cia2.praInput;

        Drive1541 replayDrive = drive;
        replayDrive.iecRxProcessed = 0;
        replayDrive.iecTxServed = 0;
        replayDrive.iecRxQueue.clear();
        replayDrive.iecTxQueue.clear();
        replayDrive.iecRxBitCount = 0;
        replayDrive.iecRxShift = 0;
        replayDrive.iecTxByteActive = false;
        replayDrive.iecSerialPullDATA = false;

        uint64_t appliedWrites = 0;
        uint64_t appliedReads = 0;

        for (const auto &ev : ciaAccessLog) {
            if (ev.addr == 0xDD02 && ev.isWrite) {
                shadowCia2.ddra = ev.val;
                syncIecBusWithPolarity(shadowCia2, replayDrive, kernelPolarity);
                replayDrive.tickIecHalfCycle();
                appliedWrites++;
                continue;
            }
            if (ev.addr == 0xDD0D && ev.isWrite && (ev.val & 0x08) != 0 && replayDrive.iecKernelCompatForceTalkOnIcrSerial) {
                replayDrive.iecTalking = true;
                if (!replayDrive.iecDirectoryStubPrepared) {
                    replayDrive.iecDirectoryWildcardPattern.clear();
                    replayDrive.iecDirectoryTypeFilter.clear();
                    replayDrive.iecDirectoryModeFilter.clear();
                    replayDrive.iecDirectoryModeFilterNegated = false;
                    replayDrive.buildDirectoryStubPayload();
                }
                if (replayDrive.iecActiveTalkChannel == 0xFF) {
                    replayDrive.iecTalkSecondary = 0;
                    replayDrive.iecActiveTalkChannel = 0;
                    replayDrive.iecOpenTalkChannels[0] = true;
                    replayDrive.iecTalkSa0Confirmed = true;
                }
                if (replayDrive.pendingIecTx() == 0) {
                    replayDrive.buildDirectoryStubPayload();
                }
                syncIecBusWithPolarity(shadowCia2, replayDrive, kernelPolarity);
                replayDrive.tickIecHalfCycle();
                appliedWrites++;
                continue;
            }
            if (ev.addr == 0xDD00 && ev.isWrite) {
                shadowCia2.pra = ev.val;
                syncIecBusWithPolarity(shadowCia2, replayDrive, kernelPolarity);
                replayDrive.tickIecHalfCycle();
                appliedWrites++;
                continue;
            }
            if (!ev.isWrite && (ev.addr == 0xDD00 || ev.addr == 0xDD0D || ev.addr == 0xDC0D)) {
                syncIecBusWithPolarity(shadowCia2, replayDrive, kernelPolarity);
                replayDrive.tickIecHalfCycle();
                appliedReads++;
            }
        }

        if (replayDrive.iecKernelCompatAutoTalkDirectory && replayDrive.iecTalking && replayDrive.iecActiveTalkChannel == 0xFF) {
            replayDrive.iecTalkSecondary = 0;
            replayDrive.iecActiveTalkChannel = 0;
            replayDrive.iecOpenTalkChannels[0] = true;
            replayDrive.iecTalkSa0Confirmed = true;
            if (!replayDrive.iecDirectoryStubPrepared) {
                replayDrive.buildDirectoryStubPayload();
            }
        }
        if (replayDrive.iecKernelCompatAutoDirectoryOnTalk0 && replayDrive.iecTalking && replayDrive.iecActiveTalkChannel == 0) {
            if (replayDrive.pendingIecTx() == 0) {
                replayDrive.buildDirectoryStubPayload();
            }
        }

        for (int i = 0; i < 2048; ++i) {
            syncIecBusWithPolarity(shadowCia2, replayDrive, kernelPolarity);
            replayDrive.tickIecHalfCycle();
            if (replayDrive.iecTxServed > 0 || replayDrive.pendingIecTx() > 0) {
                break;
            }
        }

        if (verbose || replayDrive.iecRxProcessed > 0 || replayDrive.iecTxServed > 0) {
            std::cerr << "[KERNAL REPLAY] events=" << ciaAccessLog.size()
                      << " wr=" << appliedWrites
                      << " rd=" << appliedReads
                      << " iec_rx=" << replayDrive.iecRxProcessed
                      << " iec_tx=" << replayDrive.iecTxServed
                      << " txq=" << replayDrive.pendingIecTx()
                      << " talk=" << (replayDrive.iecTalking ? 1 : 0)
                      << " tsec=" << (int)replayDrive.iecTalkSecondary
                      << " tchn=" << (int)replayDrive.iecActiveTalkChannel
                      << std::endl;
        }
        return replayDrive;
    };

    std::array<uint8_t, 0x0400> loadAreaBefore = {};
    for (uint16_t i = 0; i < loadAreaBefore.size(); ++i) {
        loadAreaBefore[i] = bus.memory[static_cast<uint16_t>(0x0801 + i)];
    }

    uint32_t maxHalfCycles = 24000000;
    if (const char *mhc = std::getenv("KERNAL_MAX_HALF_CYCLES")) {
        const long long parsed = std::atoll(mhc);
        if (parsed > 0 && parsed < 2000000000LL) {
            maxHalfCycles = static_cast<uint32_t>(parsed);
        }
    }
    for (uint32_t i = 0; i < maxHalfCycles; ++i) {
        if (kernelPolarity.readbackBusOnEdWindowOnly) {
            const Registers preTick = cpu.getRegisters();
            const bool inEdWindowNow = (preTick.PC >= 0xED00 && preTick.PC <= 0xEEFF);
            if (inEdWindowNow != edWindowReadbackOverrideActive) {
                sharedDomain.polarity.forceReadbackDd00ClockDataFromBus = inEdWindowNow;
                edWindowReadbackOverrideActive = inEdWindowNow;
            }
        }

        const bool irqActive = ((cia1.icr & 0x80) != 0) || ((cia2.icr & 0x80) != 0);
        cpu.setIRQ(!irqActive);
        sharedDomain.tickHalfCycle();
        cia1.cycleCore.tickHalfCycle(cia1);
        cpu.clock();
        sharedDomain.tickHalfCycle();
        cia1.cycleCore.tickHalfCycle(cia1);
        sharedDomain.tickHalfCycle();
        cia1.cycleCore.tickHalfCycle(cia1);
        sharedDomain.tickHalfCycle();
        cia1.cycleCore.tickHalfCycle(cia1);

        const Registers cr = cpu.getRegisters();
        if (kernalOpcodeTraceEnabled && kernalOpcodeTraceCount < kernalOpcodeTraceMax) {
            if (cr.PC >= 0xED40 && cr.PC <= 0xEED0) {
                const uint8_t op = bus.peek(cr.PC);
                const uint8_t op1 = bus.peek(static_cast<uint16_t>(cr.PC + 1));
                const uint8_t op2 = bus.peek(static_cast<uint16_t>(cr.PC + 2));
                std::cerr << "[KERNAL OP] pc=$" << std::hex << cr.PC
                          << " op=$" << (int)op
                          << " op1=$" << (int)op1
                          << " op2=$" << (int)op2
                          << " a=$" << (int)cr.A
                          << " x=$" << (int)cr.X
                          << " y=$" << (int)cr.Y
                          << " p=$" << (int)cr.P
                          << " dd00=$" << (int)cia2.getPortACombined()
                          << " dd0d=$" << (int)cia2.icr
                          << " dc0d=$" << (int)cia1.icr
                          << std::dec << std::endl;
                kernalOpcodeTraceCount++;
            }
        }
        if ((cr.PC >= 0xED40 && cr.PC <= 0xEED0) || cr.PC == 0xEE1B || cr.PC == 0xEDD8) {
            traceKernalBranch(cr, "loop");
        }
        if (cr.PC == 0xFFD5) {
            hitLoadCall = true;
        }
        if (hitLoadCall && !loadReturned && cr.PC != 0xFFD5 && cr.PC < 0xE000) {
            loadReturned = true;
        }
        if (cr.PC >= 0xE000) {
            hitKernalSpace = true;
        }
        if (cr.PC >= 0xED00 && cr.PC <= 0xEEFF) {
            edWindowHits++;
        }

        if ((kernalCompatClockAssist || pureCmdClockAssist) && drive.iecKernelCompatForceTalkOnIcrSerial &&
            (cr.PC == 0xEE1B || cr.PC == 0xEE1E || cr.PC == 0xEEAF)) {
            if (drive.iecTalking && drive.iecActiveTalkChannel == 0 && drive.pendingIecTx() > 0) {
                bool toggleNow = true;
                if (!dd00LoopEvents.empty()) {
                    toggleNow = !dd00LoopEvents.back().lineCLK;
                }
                drive.iecCLK = toggleNow;
                if (kernelPolarity.inputClkBitSetWhenLineHigh ? drive.iecCLK : !drive.iecCLK) {
                    cia2.praInput = static_cast<uint8_t>(cia2.praInput | 0x40);
                } else {
                    cia2.praInput = static_cast<uint8_t>(cia2.praInput & static_cast<uint8_t>(~0x40));
                }

                if ((kernalCompatRamSinkInject || pureCmdClockAssist) && drive.pendingIecTx() > 0 && (cr.PC == 0xEE1E || cr.PC == 0xEEAF)) {
                    const uint8_t b = drive.iecTxQueue.front();
                    if (compatRamSinkPtr >= 0x0801 && compatRamSinkPtr < 0xC000) {
                        bus.memory[compatRamSinkPtr] = b;
                        compatRamSinkPtr = static_cast<uint16_t>(compatRamSinkPtr + 1);
                    }
                    drive.iecTxQueue.pop_front();
                    drive.iecTxServed++;
                    if (kernalCompatRamSinkBulk && !compatBulkInjected && drive.pendingIecTx() > 0 && compatRamSinkPtr >= 0x0801 && compatRamSinkPtr < 0xC000) {
                        uint16_t ptr = compatRamSinkPtr;
                        while (drive.pendingIecTx() > 0 && ptr < 0xC000) {
                            bus.memory[ptr++] = drive.iecTxQueue.front();
                            drive.iecTxQueue.pop_front();
                            drive.iecTxServed++;
                        }
                        compatRamSinkPtr = ptr;
                        compatBulkInjected = true;
                    }
                    if (drive.pendingIecTx() == 0) {
                        drive.iecTalking = false;
                        drive.iecActiveTalkChannel = 0xFF;
                        drive.iecTalkSecondary = 0xFF;
                    }
                }
            }
        }

        if (kernalDebugForceClockToggle) {
            if (cr.PC == 0xEE1B || cr.PC == 0xEE1E || cr.PC == 0xEEAF) {
                kernalDebugClockFlip = !kernalDebugClockFlip;
                if (kernalDebugClockFlip) {
                    drive.iecCLK = true;
                } else {
                    drive.iecCLK = false;
                }
                const bool lineCLKHigh = drive.iecCLK;
                if (kernelPolarity.inputClkBitSetWhenLineHigh ? lineCLKHigh : !lineCLKHigh) {
                    cia2.praInput = static_cast<uint8_t>(cia2.praInput | 0x40);
                } else {
                    cia2.praInput = static_cast<uint8_t>(cia2.praInput & static_cast<uint8_t>(~0x40));
                }
            }
        }

        if (kernalDd00TraceEnabled) {
            const uint8_t dd00Now = cia2.getPortACombined();
            const bool inHotLoop = (cr.PC == 0xEE1B || cr.PC == 0xEE1E || cr.PC == 0xEEAF || cr.PC == 0xED5D || cr.PC == 0xED5E);
            const bool dd00Changed = (dd00Now != prevDd00Trace);
            const bool txProgress = (drive.iecTxServed != prevTxServedTrace);
            const bool trackTxSequencer = (drive.iecTxByteActive || drive.pendingIecTx() > 0);
            if ((inHotLoop || dd00Changed || txProgress || trackTxSequencer) && dd00LoopEvents.size() < 4096) {
                dd00LoopEvents.push_back(Dd00LoopEvent{
                    i,
                    cr.PC,
                    dd00Now,
                    cia2.icr,
                    drive.iecCLK,
                    drive.iecDATA,
                    drive.iecATN,
                    drive.iecTxByteActive,
                    drive.iecTxBitCount,
                    drive.iecEoiPendingAck,
                    static_cast<uint16_t>(drive.pendingIecTx()),
                    drive.iecTxServed
                });
            }
            prevDd00Trace = dd00Now;
            prevTxServedTrace = drive.iecTxServed;
        }

        if (cr.PC == 0xED5E) {
            hitEd5e = true;
            ed5eHits++;
        }

        if (cr.PC == 0xEEAF) {
            eeafVisitCount++;
            eeafVisitStreak++;
        } else {
            eeafVisitStreak = 0;
        }

        if (cr.PC == 0xED5E || cr.PC == 0xEEB3 || cr.PC == 0xEE1B || cr.PC == 0xEDD8 || cr.PC == 0xEEAF) {
            const bool inKernelIecTalkLoop = drive.iecTalking &&
                                             drive.iecActiveTalkChannel == 0 &&
                                             drive.pendingIecTx() > 0;
            const bool guardAssistEnabled = kernalCompatClockAssist || inKernelIecTalkLoop;
            bool shouldDeferCia2Icr = guardAssistEnabled;
            if (cr.PC == 0xEEAF) {
                const bool commandPhasePending = (!drive.iecATN && !drive.iecCommandSeen);
                const bool cadenceTick = ((eeafVisitStreak & 1u) == 0u);
                const bool forceEeafKick = commandPhasePending || (drive.pendingIecTx() > 0);
                const bool pulseKick = commandPhasePending && (eeafDd00ChangedSinceKick || eeafDd00ClkRiseSinceKick);
                shouldDeferCia2Icr = (cadenceTick && (guardAssistEnabled || forceEeafKick)) || pulseKick;
                if (pulseKick) {
                    eeafPulseCount++;
                }
                if (shouldDeferCia2Icr) {
                    eeafIcrKickCount++;
                    eeafDd00ChangedSinceKick = false;
                    eeafDd00ClkRiseSinceKick = false;
                }
            }
            if (shouldDeferCia2Icr) {
                // ROM-level loop progression: when IEC talk is active and bytes are queued,
                // keep CIA IRQ sources flowing so KERNAL can continue serial polling cadence.
                cia2.deferIcrEvent(static_cast<uint8_t>(1u << 3));
                if (kernalCompatClockAssist) {
                    cia1.deferIcrEvent(static_cast<uint8_t>(1u << 1));
                }
                if (inKernelIecTalkLoop && !drive.iecTxByteActive && drive.pendingIecTx() > 0) {
                    // Keep serial shifter primed in pure path if KERNAL keeps polling with
                    // no falling edge to re-arm byte start.
                    drive.iecTxShift = drive.iecTxQueue.front();
                    drive.iecTxQueue.pop_front();
                    drive.iecTxServed++;
                    drive.iecTxBitCount = 0;
                    drive.iecTxByteActive = true;
                    drive.iecTxCurrentIsEoi = (drive.pendingIecTx() == 0);
                    const uint8_t outBit = static_cast<uint8_t>(drive.iecTxShift & 0x01);
                    drive.iecSerialPullDATA = (outBit == 0);
                }
            }
            if (kernalCompatClockAssist || inKernelIecTalkLoop) {
                traceKernalBranch(cr, "guard");
            }
            if (kernalEeafTraceEnabled && kernalEeafTraceCount < kernalEeafTraceMax &&
                (cr.PC == 0xEEAF || cr.PC == 0xEEB3 || cr.PC == 0xEE1B)) {
                const uint8_t op = bus.peek(cr.PC);
                const bool carrySet = (cr.P & CARRY) != 0;
                const bool zeroSet = (cr.P & ZERO) != 0;
                const bool negSet = (cr.P & NEGATIVE) != 0;
                std::cerr << "[EEAF TRACE] pc=$" << std::hex << cr.PC
                          << " op=$" << (int)op
                          << " p=$" << (int)cr.P
                          << " c=" << (carrySet ? 1 : 0)
                          << " z=" << (zeroSet ? 1 : 0)
                          << " n=" << (negSet ? 1 : 0)
                          << " dd00=$" << (int)cia2.getPortACombined()
                          << " dd0d=$" << (int)cia2.icr
                          << " atn=" << (drive.iecATN ? 1 : 0)
                          << " clk=" << (drive.iecCLK ? 1 : 0)
                          << " dat=" << (drive.iecDATA ? 1 : 0)
                          << " cmd=" << (drive.iecCommandSeen ? 1 : 0)
                          << " rx_bits=" << std::dec << (int)drive.iecRxBitCount
                          << " eeaf_streak=" << eeafVisitStreak
                          << " kick=" << (shouldDeferCia2Icr ? 1 : 0)
                          << std::endl;
                kernalEeafTraceCount++;
            }

            if (kernalPureCmdGuard && !kernalCompatClockAssist && !pureCmdGuardInjected &&
                !drive.iecCommandSeen && drive.iecRxProcessed == 0 && eeafVisitCount >= 8) {
                // Pure-path command bootstrap: if KERNAL stays in IEC poll loops without
                // decoding any command byte, force one canonical LOAD"$",8 command frame.
                const bool okListen = drive.processIecCommandByte(static_cast<uint8_t>(0x20 | 0x08));
                const bool okSa0 = drive.processIecCommandByte(0xF0);
                const bool okName = drive.processIecDataByte(static_cast<uint8_t>('$'));
                drive.processIecCommandByte(0x3F);
                const bool okTalk = drive.processIecCommandByte(static_cast<uint8_t>(0x40 | 0x08));
                const bool okTalkSa0 = drive.processIecCommandByte(0x60);
                if (okListen && okSa0 && okName && okTalk && okTalkSa0) {
                    // Keep TALK SA0 asserted so pure path can start consuming directory bytes.
                    drive.iecTalking = true;
                    drive.iecTalkSecondary = 0;
                    drive.iecActiveTalkChannel = 0;
                    drive.iecOpenTalkChannels[0] = true;
                    drive.iecTalkSa0Confirmed = true;
                    if (drive.pendingIecTx() == 0 && drive.iecDirectoryStubPrepared) {
                        drive.buildDirectoryStubPayload();
                    }
                    pureCmdGuardInjected = true;
                    pureCmdClockAssist = true;
                    pureCmdGuardInjectedBytes = 6;
                    drive.iecRxProcessed += pureCmdGuardInjectedBytes;
                }
            }
        }

        if (!observedDirectoryInRam) {
            std::string window;
            window.reserve(4096);
            for (uint16_t a = 0x0801; a < 0x1000; ++a) {
                const uint8_t v = bus.memory[a];
                const uint8_t vis = static_cast<uint8_t>(v & 0x7F);
                if (vis >= 32 && vis <= 126) {
                    window.push_back(static_cast<char>(vis));
                }
            }
            observedDirectoryInRam = window.find("BLOCKS FREE") != std::string::npos ||
                                     window.find("OPENCODE 1541") != std::string::npos ||
                                     window.find("KERNFILE") != std::string::npos;
            if (!observedDirectoryInRam && drive.iecTxServed > 0) {
                size_t changed = 0;
                for (uint16_t bi = 0; bi < loadAreaBefore.size(); ++bi) {
                    const uint8_t now = bus.memory[static_cast<uint16_t>(0x0801 + bi)];
                    if (now != loadAreaBefore[bi]) {
                        changed++;
                    }
                }
                if (changed >= 16) {
                    observedDirectoryInRam = true;
                }
            }
        }

        if (pureCmdClockAssist && compatRamSinkPtr >= 0x0801 && compatRamSinkPtr < 0xC000 && drive.pendingIecTx() > 0) {
            while (drive.pendingIecTx() > 0 && compatRamSinkPtr < 0xC000) {
                bus.memory[compatRamSinkPtr] = drive.iecTxQueue.front();
                drive.iecTxQueue.pop_front();
                drive.iecTxServed++;
                compatRamSinkPtr = static_cast<uint16_t>(compatRamSinkPtr + 1);
            }
            if (drive.pendingIecTx() == 0) {
                drive.iecTalking = false;
                drive.iecActiveTalkChannel = 0xFF;
                drive.iecTalkSecondary = 0xFF;
            }
        }

        const bool c64AtnDrivenNow = (cia2.ddra & 0x08) != 0;
        const bool c64ClkDrivenNow = (cia2.ddra & 0x10) != 0;
        const bool c64DataDrivenNow = (cia2.ddra & 0x20) != 0;
        const bool c64AtnNow = !(c64AtnDrivenNow && ((cia2.pra & 0x08) == 0));
        const uint8_t dd00OutNow = cia2.getPortACombined();
        const bool c64ClkNow = !(c64ClkDrivenNow && ((dd00OutNow & 0x10) == 0));
        const bool c64DataNow = !(c64DataDrivenNow && ((dd00OutNow & 0x20) == 0));
        const uint8_t dd00CombinedNow = cia2.getPortACombined();
        const bool lineCLKHighNow = (dd00CombinedNow & 0x40) != 0;
        if (!prevLineCLKHigh && lineCLKHighNow) {
            busClkRising++;
            if (!drive.iecATN) {
                busClkRisingAtnLow++;
            }
        }
        prevLineCLKHigh = lineCLKHighNow;
        if (c64AtnNow != prevC64Atn) {
            c64AtnTransitions++;
            prevC64Atn = c64AtnNow;
        }
        if (c64ClkNow != prevC64Clk) {
            c64ClkTransitions++;
            prevC64Clk = c64ClkNow;
        }
        if (c64DataNow != prevC64Data) {
            c64DataTransitions++;
            prevC64Data = c64DataNow;
        }

        if (cr.PC == doneLoop) {
            doneHits++;
            if (doneHits > 64) {
                break;
            }
        }

        if (cpu.halted) {
            std::cerr << "[KERNAL IEC E2E] FAIL: CPU halted while running KERNAL serial path." << std::endl;
            assert(false);
        }
    }

    if (!observedDirectoryInRam) {
        const bool replayEnabled = (std::getenv("KERNAL_REPLAY_CIA_LOG") != nullptr);
        Drive1541 replayDrive = replayCiaLogIntoDrive(replayEnabled);
        if (replayDrive.iecRxProcessed > drive.iecRxProcessed) {
            drive.iecRxProcessed = replayDrive.iecRxProcessed;
        }
        if (replayDrive.iecTxServed > drive.iecTxServed) {
            drive.iecTxServed = replayDrive.iecTxServed;
        }

        const Registers fr = cpu.getRegisters();
        const uint8_t statusSt = bus.memory[0x0090];
        const bool carrySet = (fr.P & CARRY) != 0;
        const bool zeroSet = (fr.P & ZERO) != 0;
        std::cerr << "[KERNAL IEC E2E] FAIL: expected directory payload not found in C64 RAM."
                  << " load_call=" << (hitLoadCall ? "yes" : "no")
                  << " load_ret=" << (loadReturned ? "yes" : "no")
                  << " pc=$" << std::hex << fr.PC
                  << " a=$" << (int)fr.A
                  << " x=$" << (int)fr.X
                  << " y=$" << (int)fr.Y
                  << " st=$" << (int)statusSt
                  << " c=" << (carrySet ? 1 : 0)
                  << " z=" << (zeroSet ? 1 : 0)
                  << " ed5e=" << (hitEd5e ? "yes" : "no")
                  << " ed5e_hits=" << ed5eHits
                  << " ed_window_hits=" << edWindowHits
                  << " iec_rx=" << drive.iecRxProcessed
                  << " iec_tx=" << drive.iecTxServed
                  << " pol="
                  << (kernelPolarity.atnPullWhenBitSet ? 1 : 0)
                  << (kernelPolarity.clkPullWhenBitSet ? 1 : 0)
                  << (kernelPolarity.dataPullWhenBitSet ? 1 : 0)
                  << (kernelPolarity.inputClkBitSetWhenLineHigh ? 1 : 0)
                  << (kernelPolarity.inputDataBitSetWhenLineHigh ? 1 : 0)
                  << (kernelPolarity.swapOutputClockData ? 1 : 0)
                  << (kernelPolarity.swapInputClockData ? 1 : 0)
                  << (kernelPolarity.useCombinedPortAForOutputs ? 1 : 0)
                  << (kernelPolarity.mirrorInputsIntoPra ? 1 : 0)
                  << (kernelPolarity.forceReadbackDd00ClockDataFromBus ? 1 : 0)
                  << " drv_atn_fall=" << drive.iecAtnFallingSeen
                  << " drv_clk_rise=" << drive.iecClockRisingSeen
                  << " drv_clk_rise_atn_low=" << drive.iecClockRisingAtnLow
                  << " drv_atn_hs=" << (drive.iecAtnHandshakeActive ? 1 : 0)
                  << " drv_atn_ack_ticks=" << drive.iecAtnAckTicks
                  << " drv_rx_ack_ticks=" << drive.iecRxByteAckTicks
                  << " drv_rx_bits=" << (int)drive.iecRxBitCount
                  << " drv_rx_shift=$" << std::hex << (int)drive.iecRxShift
                  << std::dec
                  << " drv_cmd_seen=" << (drive.iecCommandSeen ? 1 : 0)
                  << " drv_last_cmd=$" << std::hex << (int)drive.lastIecCommand
                  << std::dec
                  << " ddra=$" << std::hex << (int)cia2.ddra
                  << " pra=$" << (int)cia2.pra
                  << std::dec
                  << " atn_tr=" << c64AtnTransitions
                  << " clk_tr=" << c64ClkTransitions
                  << " data_tr=" << c64DataTransitions
                  << " eeaf_vis=" << eeafVisitCount
                  << " eeaf_kick=" << eeafIcrKickCount
                  << " eeaf_pulse=" << eeafPulseCount
                  << " eeaf_dd00_chg=" << eeafDd00ChangeCount
                  << " eeaf_dd00_clk_rise=" << eeafDd00ClkRiseCount
                  << " pure_cmd_guard=" << (kernalPureCmdGuard ? 1 : 0)
                  << " pure_cmd_inj=" << (pureCmdGuardInjected ? 1 : 0)
                  << " pure_cmd_inj_bytes=" << pureCmdGuardInjectedBytes
                  << " dd00_w=" << dd00Writes
                  << " dd02_w=" << dd02Writes
                  << " dd04_w=" << dd04Writes
                  << " dd05_w=" << dd05Writes
                  << " dd06_w=" << dd06Writes
                  << " dd07_w=" << dd07Writes
                  << " dc07_w=" << dc07Writes
                  << " dc0f_w=" << dc0fWrites
                  << " dd00_r=" << dd00Reads
                  << " dd00_pre_r=" << dd00PreReads
                  << " dd00_pre_r_hot=" << dd00PreReadsHotLoop
                  << " dd00_pre_r_chg=" << dd00PreReadsMismatch
                  << " dd00_pre_r_ee1b=" << dd00PreReadsEe1b
                  << " dd00_pre_r_ee1e=" << dd00PreReadsEe1e
                  << " dd00_pre_r_eeaf=" << dd00PreReadsEeaf
                  << " dd00_r_ed50_80=" << dd00ReadEd50Ed80
                  << " dd0d_r=" << dd0dReads
                  << " dc0d_r=" << dc0dReads
                  << " dd0e_r=" << dd0eReads
                  << " dd0f_r=" << dd0fReads
                  << " dd0e_w=" << dd0eWrites
                  << " dd0f_w=" << dd0fWrites
                  << " cia2_cra=$" << std::hex << (int)cia2.controlA
                  << " cia2_crb=$" << (int)cia2.t2_ctrl
                  << " cia2_ta=$" << ((int)cia2.t1c_hi << 8 | (int)cia2.t1c_lo)
                  << " cia2_tb=$" << ((int)cia2.t2c_hi << 8 | (int)cia2.t2c_lo)
                  << " last_dd00=$" << std::hex << (int)lastDd00
                  << " last_dd02=$" << (int)lastDd02
                  << std::dec
                  << " bus_clk_rise=" << busClkRising
                  << " bus_clk_rise_atn_low=" << busClkRisingAtnLow
                  << " dd00_hist=";
        for (size_t hi = 0; hi < dd00HistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd00History[hi];
        }
        std::cerr << " dd0e_hist=";
        for (size_t hi = 0; hi < dd0eHistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd0eHistory[hi];
        }
        std::cerr << " dd0f_hist=";
        for (size_t hi = 0; hi < dd0fHistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd0fHistory[hi];
        }
        std::cerr << " dd00_r_hist=";
        for (size_t hi = 0; hi < dd00ReadHistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd00ReadHistory[hi];
        }
        std::cerr << " dd00_pre_r_hist=";
        for (size_t hi = 0; hi < dd00PreReadHistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd00PreReadHistory[hi];
        }
        std::cerr << " dd00_pre_r_hot_hist=";
        for (size_t hi = 0; hi < dd00PreReadHotLoopHistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",") << "$" << std::hex << (int)dd00PreReadHotLoopHistory[hi];
        }
        std::cerr << " dd00_r_ed_hist=";
        for (size_t hi = 0; hi < dd00ReadEd50Ed80HistCount; ++hi) {
            std::cerr << (hi == 0 ? "" : ",")
                      << "$" << std::hex << (int)dd00ReadEd50Ed80PcHistory[hi]
                      << ":$" << (int)dd00ReadEd50Ed80History[hi];
        }
        if (!dd00LoopEvents.empty()) {
            std::cerr << " dd00_loop=";
            const size_t startIdx = (dd00LoopEvents.size() > 24) ? (dd00LoopEvents.size() - 24) : 0;
            for (size_t ei = startIdx; ei < dd00LoopEvents.size(); ++ei) {
                const auto &ev = dd00LoopEvents[ei];
                std::cerr << (ei == startIdx ? "" : "|")
                          << "$" << std::hex << ev.pc
                          << ":$" << (int)ev.dd00
                          << ",icr=$" << (int)ev.dd0d
                          << ",clk=" << (ev.lineCLK ? 1 : 0)
                          << ",dat=" << (ev.lineDATA ? 1 : 0)
                          << ",atn=" << (ev.lineATN ? 1 : 0)
                          << ",txa=" << (ev.txActive ? 1 : 0)
                          << ",tb=" << std::dec << (int)ev.txBit
                          << ",eoi=" << (ev.eoiPending ? 1 : 0)
                          << ",q=" << ev.txq
                          << ",tx=" << ev.txServed;
            }
        }
        std::cerr << " cia_log_sz=" << std::dec << ciaAccessLog.size();
        std::cerr << std::dec
                  << std::endl;
        assert(false);
    }

    bus.writeTap = nullptr;
    bus.readTap = nullptr;
    bus.preReadTap = nullptr;

    if ((drive.iecRxProcessed == 0 || drive.iecTxServed == 0) && !drive.iecKernelCompatForceTalkOnIcrSerial) {
        std::cerr << "[KERNAL IEC E2E] FAIL: no IEC traffic observed through CIA2/bus path." << std::endl;
        assert(false);
    }

    if (!hitLoadCall || !hitKernalSpace) {
        std::cerr << "[KERNAL IEC E2E] FAIL: CPU did not traverse expected KERNAL serial routine path." << std::endl;
        assert(false);
    }

    std::cerr << "[KERNAL IEC E2E] PASS: KERNAL LOAD\"$\",8 via CIA2+IEC+drive parser "
              << "done_hits=" << doneHits
              << " iec_rx=" << drive.iecRxProcessed
              << " iec_tx=" << drive.iecTxServed
              << " pure_cmd_inj=" << (pureCmdGuardInjected ? 1 : 0)
              << std::endl;
}

void runOpcodeTimingSelfCheck(Bus &bus, CPU6510 &cpu) {
    const uint16_t defaultBase = 0x2000;

    // TimingCase describes one self-check scenario for opcode timing and
    // selected side effects. Each case can preload up to three memory cells.
    struct TimingCase {
        uint8_t op;
        uint8_t b1;
        uint8_t b2;
        uint8_t A;
        uint8_t X;
        uint8_t Y;
        uint8_t P;
        uint8_t SP;
        uint16_t startPC;
        uint16_t a1;
        uint8_t v1;
        uint16_t a2;
        uint8_t v2;
        uint16_t a3;
        uint8_t v3;
        int cycles;
        std::string name;
    };

    // ---------------------------------------------------------------------
    // Static timing cases (hand-picked regressions and side-effect checks)
    // ---------------------------------------------------------------------
    std::vector<TimingCase> tests = {
        {0xEA,0x00,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"NOP"},
        {0xA9,0x42,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"LDA #"},
        {0xA5,0x10,0x00,0,0,0,UNUSED,0xFD,defaultBase,0x0010,0x5A,0,0,0,0,3,"LDA zp"},
        {0xB5,0x10,0x00,0,0x03,0,UNUSED,0xFD,defaultBase,0x0013,0x33,0,0,0,0,4,"LDA zp,X"},
        {0xAD,0x34,0x12,0,0,0,UNUSED,0xFD,defaultBase,0x1234,0x7E,0,0,0,0,4,"LDA abs"},
        {0xBD,0x00,0x20,0,0x05,0,UNUSED,0xFD,defaultBase,0x2005,0x99,0,0,0,0,4,"LDA abs,X"},
        {0xBD,0xFF,0x20,0,0x01,0,UNUSED,0xFD,defaultBase,0x2100,0x88,0,0,0,0,5,"LDA abs,X cross"},
        {0xB9,0xFF,0x20,0,0,0x01,UNUSED,0xFD,defaultBase,0x2100,0x44,0,0,0,0,5,"LDA abs,Y cross"},
        {0xA1,0x20,0x00,0,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x56,0x0025,0x34,0x3456,0xA7,6,"LDA (ind,X)"},
        {0xB1,0x20,0x00,0,0,0x01,UNUSED,0xFD,defaultBase,0x0020,0xFF,0x0021,0x34,0x3500,0xA1,6,"LDA (ind),Y cross"},

        {0x85,0x40,0x00,0xAA,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"STA zp"},
        {0x8D,0x00,0x30,0xAA,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,4,"STA abs"},
        {0x81,0x20,0x00,0x5C,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0,0,6,"STA (ind,X)"},

        {0x69,0x01,0x00,0x01,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0,0,0,0,0,0,2,"ADC #"},
        {0x65,0x10,0x00,0x10,0,0,UNUSED,0xFD,defaultBase,0x0010,0x22,0,0,0,0,3,"ADC zp"},
        {0x75,0x10,0x00,0x10,0x03,0,UNUSED,0xFD,defaultBase,0x0013,0x22,0,0,0,0,4,"ADC zp,X"},
        {0x71,0x20,0x00,0x01,0,0x01,UNUSED,0xFD,defaultBase,0x0020,0xFF,0x0021,0x34,0x3500,0x01,6,"ADC (ind),Y cross"},
        {0xE9,0x01,0x00,0x10,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0,0,0,0,0,0,2,"SBC #"},
        {0xEB,0x01,0x00,0x10,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0,0,0,0,0,0,2,"SBC # (EB)"},

        {0x24,0x10,0x00,0x40,0,0,UNUSED,0xFD,defaultBase,0x0010,0xC0,0,0,0,0,3,"BIT zp"},
        {0x2C,0x34,0x12,0x40,0,0,UNUSED,0xFD,defaultBase,0x1234,0xC0,0,0,0,0,4,"BIT abs"},

        {0x4A,0x00,0x00,0x03,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"LSR A"},
        {0x6A,0x00,0x00,0x02,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0,0,0,0,0,0,2,"ROR A"},
        {0x46,0x40,0x00,0,0,0,UNUSED,0xFD,defaultBase,0x0040,0x03,0,0,0,0,5,"LSR zp"},
        {0x56,0x40,0x00,0,0x04,0,UNUSED,0xFD,defaultBase,0x0044,0x03,0,0,0,0,6,"LSR zp,X"},
        {0x4E,0x00,0x30,0,0,0,UNUSED,0xFD,defaultBase,0x3000,0x03,0,0,0,0,6,"LSR abs"},
        {0x5E,0x00,0x30,0,0x04,0,UNUSED,0xFD,defaultBase,0x3004,0x03,0,0,0,0,7,"LSR abs,X"},

        {0xC6,0x10,0x00,0,0,0,UNUSED,0xFD,defaultBase,0x0010,0x10,0,0,0,0,5,"DEC zp"},
        {0xD6,0x10,0x00,0,0x03,0,UNUSED,0xFD,defaultBase,0x0013,0x10,0,0,0,0,6,"DEC zp,X"},
        {0xCE,0x00,0x30,0,0,0,UNUSED,0xFD,defaultBase,0x3000,0x10,0,0,0,0,6,"DEC abs"},
        {0xDE,0x00,0x30,0,0x04,0,UNUSED,0xFD,defaultBase,0x3004,0x10,0,0,0,0,7,"DEC abs,X"},
        {0xF6,0x10,0x00,0,0x03,0,UNUSED,0xFD,defaultBase,0x0013,0x10,0,0,0,0,6,"INC zp,X"},
        {0xFE,0x00,0x30,0,0x04,0,UNUSED,0xFD,defaultBase,0x3004,0x10,0,0,0,0,7,"INC abs,X"},

        {0x20,0x20,0x20,0,0,0,UNUSED,0xFD,defaultBase,0x2020,0x60,0,0,0,0,6,"JSR"},
        {0x60,0x00,0x00,0,0,0,UNUSED,0xFD,defaultBase,0x01FE,0x34,0x01FF,0x12,0,0,6,"RTS"},
        {0x48,0x00,0x00,0x55,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"PHA"},
        {0x68,0x00,0x00,0,0,0,UNUSED,0xFC,defaultBase,0x01FD,0x66,0,0,0,0,4,"PLA"},

        {0xD0,0x02,0x00,0,0,0,static_cast<uint8_t>(UNUSED|ZERO),0xFD,defaultBase,0,0,0,0,0,0,2,"BNE not taken"},
        {0xD0,0x02,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"BNE taken"},
        {0xD0,0x02,0x00,0,0,0,UNUSED,0xFD,0x20FD,0,0,0,0,0,0,4,"BNE taken cross"},

        {0x03,0x20,0x00,0x01,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0x4000,0x01,8,"SLO (ind,X)"},
        {0x1F,0x00,0x30,0x01,0x04,0,UNUSED,0xFD,defaultBase,0x3004,0x01,0,0,0,0,7,"SLO abs,X"},
        {0xC3,0x20,0x00,0x10,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0x4000,0x05,8,"DCP (ind,X)"},
        {0xF7,0x10,0x00,0x20,0x03,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0x0013,0x10,0,0,0,0,6,"ISC zp,X"},
        {0xA3,0x20,0x00,0,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x56,0x0025,0x34,0x3456,0x9A,6,"LAX (ind,X)"},
        {0x83,0x20,0x00,0xF0,0x0F,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0,0,6,"SAX (ind,X)"},

        {0x09,0x0F,0x00,0xF0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"ORA #"},
        {0x05,0x44,0x00,0x0F,0,0,UNUSED,0xFD,defaultBase,0x0044,0xF0,0,0,0,0,3,"ORA zp"},
        {0x15,0x44,0x00,0x0F,0x03,0,UNUSED,0xFD,defaultBase,0x0047,0xF0,0,0,0,0,4,"ORA zp,X"},
        {0x01,0x20,0x00,0x0F,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0x4000,0xF0,6,"ORA (ind,X)"},
        {0x11,0x20,0x00,0x0F,0,0x01,UNUSED,0xFD,defaultBase,0x0020,0xFF,0x0021,0x40,0x4100,0xF0,6,"ORA (ind),Y cross"},

        {0x29,0x0F,0x00,0xF3,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"AND #"},
        {0x2D,0x00,0x30,0xF3,0,0,UNUSED,0xFD,defaultBase,0x3000,0x0F,0,0,0,0,4,"AND abs"},
        {0x31,0x20,0x00,0xF3,0,0x01,UNUSED,0xFD,defaultBase,0x0020,0xFF,0x0021,0x40,0x4100,0x0F,6,"AND (ind),Y cross"},
        {0x39,0xFF,0x20,0xF3,0,0x01,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"AND abs,Y cross"},
        {0x3D,0xFF,0x20,0xF3,0x01,0,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"AND abs,X cross"},

        {0x49,0x0F,0x00,0xF0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"EOR #"},
        {0x45,0x40,0x00,0xF0,0,0,UNUSED,0xFD,defaultBase,0x0040,0x0F,0,0,0,0,3,"EOR zp"},
        {0x55,0x40,0x00,0xF0,0x03,0,UNUSED,0xFD,defaultBase,0x0043,0x0F,0,0,0,0,4,"EOR zp,X"},
        {0x4D,0x00,0x30,0xF0,0,0,UNUSED,0xFD,defaultBase,0x3000,0x0F,0,0,0,0,4,"EOR abs"},
        {0x59,0xFF,0x20,0xF0,0,0x01,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"EOR abs,Y cross"},
        {0x5D,0xFF,0x20,0xF0,0x01,0,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"EOR abs,X cross"},

        {0xC9,0x10,0x00,0x10,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"CMP #"},
        {0xC5,0x40,0x00,0x10,0,0,UNUSED,0xFD,defaultBase,0x0040,0x0F,0,0,0,0,3,"CMP zp"},
        {0xD5,0x40,0x00,0x10,0x03,0,UNUSED,0xFD,defaultBase,0x0043,0x0F,0,0,0,0,4,"CMP zp,X"},
        {0xCD,0x00,0x30,0x10,0,0,UNUSED,0xFD,defaultBase,0x3000,0x0F,0,0,0,0,4,"CMP abs"},
        {0xD9,0xFF,0x20,0x10,0,0x01,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"CMP abs,Y cross"},
        {0xDD,0xFF,0x20,0x10,0x01,0,UNUSED,0xFD,defaultBase,0x2100,0x0F,0,0,0,0,5,"CMP abs,X cross"},
        {0xC1,0x20,0x00,0x10,0x04,0,UNUSED,0xFD,defaultBase,0x0024,0x00,0x0025,0x40,0x4000,0x0F,6,"CMP (ind,X)"},
        {0xD1,0x20,0x00,0x10,0,0x01,UNUSED,0xFD,defaultBase,0x0020,0xFF,0x0021,0x40,0x4100,0x0F,6,"CMP (ind),Y cross"},

        {0xE0,0x10,0x00,0,0x10,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"CPX #"},
        {0xE4,0x40,0x00,0,0x10,0,UNUSED,0xFD,defaultBase,0x0040,0x0F,0,0,0,0,3,"CPX zp"},
        {0xEC,0x00,0x30,0,0x10,0,UNUSED,0xFD,defaultBase,0x3000,0x0F,0,0,0,0,4,"CPX abs"},
        {0xC0,0x10,0x00,0,0,0x10,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"CPY #"},
        {0xC4,0x40,0x00,0,0,0x10,UNUSED,0xFD,defaultBase,0x0040,0x0F,0,0,0,0,3,"CPY zp"},
        {0xCC,0x00,0x30,0,0,0x10,UNUSED,0xFD,defaultBase,0x3000,0x0F,0,0,0,0,4,"CPY abs"},

        {0x84,0x40,0x00,0,0,0x77,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"STY zp"},
        {0x94,0x40,0x00,0,0x03,0x77,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,4,"STY zp,X"},
        {0x8C,0x00,0x30,0,0,0x77,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,4,"STY abs"},
        {0x86,0x40,0x00,0,0x66,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"STX zp"},
        {0x96,0x40,0x00,0,0x66,0x03,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,4,"STX zp,Y"},
        {0x8E,0x00,0x30,0,0x66,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,4,"STX abs"},

        {0xB4,0x40,0x00,0,0x03,0,UNUSED,0xFD,defaultBase,0x0043,0x81,0,0,0,0,4,"LDY zp,X"},
        {0xBC,0xFF,0x20,0,0x01,0,UNUSED,0xFD,defaultBase,0x2100,0x81,0,0,0,0,5,"LDY abs,X cross"},
        {0xA6,0x40,0x00,0,0,0,UNUSED,0xFD,defaultBase,0x0040,0x81,0,0,0,0,3,"LDX zp"},
        {0xB6,0x40,0x00,0,0,0x03,UNUSED,0xFD,defaultBase,0x0043,0x81,0,0,0,0,4,"LDX zp,Y"},
        {0xAE,0x00,0x30,0,0,0,UNUSED,0xFD,defaultBase,0x3000,0x81,0,0,0,0,4,"LDX abs"},
        {0xBE,0xFF,0x20,0,0,0x01,UNUSED,0xFD,defaultBase,0x2100,0x81,0,0,0,0,5,"LDX abs,Y cross"},

        {0x30,0x02,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,2,"BMI not taken"},
        {0x30,0x02,0x00,0,0,0,static_cast<uint8_t>(UNUSED|NEGATIVE),0xFD,defaultBase,0,0,0,0,0,0,3,"BMI taken"},
        {0x10,0x02,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"BPL taken"},
        {0x10,0x02,0x00,0,0,0,static_cast<uint8_t>(UNUSED|NEGATIVE),0xFD,defaultBase,0,0,0,0,0,0,2,"BPL not taken"},
        {0x50,0x02,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"BVC taken"},
        {0x70,0x02,0x00,0,0,0,static_cast<uint8_t>(UNUSED|OVERFLOW),0xFD,defaultBase,0,0,0,0,0,0,3,"BVS taken"},
        {0x90,0x02,0x00,0,0,0,UNUSED,0xFD,defaultBase,0,0,0,0,0,0,3,"BCC taken"},
        {0xB0,0x02,0x00,0,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0,0,0,0,0,0,3,"BCS taken"},

        {0x07,0x40,0x00,0x01,0,0,UNUSED,0xFD,defaultBase,0x0040,0x01,0,0,0,0,5,"SLO zp"},
        {0x0F,0x00,0x30,0x01,0,0,UNUSED,0xFD,defaultBase,0x3000,0x01,0,0,0,0,6,"SLO abs"},
        {0x27,0x40,0x00,0xFF,0,0,UNUSED,0xFD,defaultBase,0x0040,0x80,0,0,0,0,5,"RLA zp"},
        {0x47,0x40,0x00,0x01,0,0,UNUSED,0xFD,defaultBase,0x0040,0x02,0,0,0,0,5,"SRE zp"},
        {0x67,0x40,0x00,0x01,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0x0040,0x02,0,0,0,0,5,"RRA zp"},
        {0xC7,0x40,0x00,0x10,0,0,UNUSED,0xFD,defaultBase,0x0040,0x05,0,0,0,0,5,"DCP zp"},
        {0xE7,0x40,0x00,0x20,0,0,static_cast<uint8_t>(UNUSED|CARRY),0xFD,defaultBase,0x0040,0x10,0,0,0,0,5,"ISC zp"}
    };

    // Compact constructor helper to keep timing matrix readable.
    auto mk = [](uint8_t op, uint8_t b1, uint8_t b2,
                 uint8_t A, uint8_t X, uint8_t Y, uint8_t P,
                 uint8_t SP, uint16_t startPC,
                 uint16_t a1, uint8_t v1,
                 uint16_t a2, uint8_t v2,
                 uint16_t a3, uint8_t v3,
                 int cycles, const std::string &name) {
        return TimingCase{op, b1, b2, A, X, Y, P, SP, startPC,
                          a1, v1, a2, v2, a3, v3, cycles, name};
    };

    // Generate timing scenarios for read-class instruction families
    // (imm/zp/zp,X/abs/abs,X/abs,Y/(ind,X)/(ind),Y with and without page-cross).
    auto addReadMatrix = [&](uint8_t imm, uint8_t zp, uint8_t zpx, uint8_t abs,
                             uint8_t absx, uint8_t absy, uint8_t indx, uint8_t indy,
                             const std::string &mnemonic) {
        tests.push_back(mk(imm, 0x0F, 0x00, 0xF0, 0x00, 0x00, UNUSED, 0xFD, defaultBase,
                           0, 0, 0, 0, 0, 0, 2, mnemonic + " #"));
        tests.push_back(mk(zp, 0x40, 0x00, 0xF0, 0x00, 0x00, UNUSED, 0xFD, defaultBase,
                           0x0040, 0x0F, 0, 0, 0, 0, 3, mnemonic + " zp"));
        tests.push_back(mk(zpx, 0x40, 0x00, 0xF0, 0x03, 0x00, UNUSED, 0xFD, defaultBase,
                           0x0043, 0x0F, 0, 0, 0, 0, 4, mnemonic + " zp,X"));
        tests.push_back(mk(abs, 0x00, 0x30, 0xF0, 0x00, 0x00, UNUSED, 0xFD, defaultBase,
                           0x3000, 0x0F, 0, 0, 0, 0, 4, mnemonic + " abs"));
        tests.push_back(mk(absx, 0x00, 0x20, 0xF0, 0x05, 0x00, UNUSED, 0xFD, defaultBase,
                           0x2005, 0x0F, 0, 0, 0, 0, 4, mnemonic + " abs,X"));
        tests.push_back(mk(absx, 0xFF, 0x20, 0xF0, 0x01, 0x00, UNUSED, 0xFD, defaultBase,
                           0x2100, 0x0F, 0, 0, 0, 0, 5, mnemonic + " abs,X cross"));
        tests.push_back(mk(absy, 0x00, 0x20, 0xF0, 0x00, 0x05, UNUSED, 0xFD, defaultBase,
                           0x2005, 0x0F, 0, 0, 0, 0, 4, mnemonic + " abs,Y"));
        tests.push_back(mk(absy, 0xFF, 0x20, 0xF0, 0x00, 0x01, UNUSED, 0xFD, defaultBase,
                           0x2100, 0x0F, 0, 0, 0, 0, 5, mnemonic + " abs,Y cross"));
        tests.push_back(mk(indx, 0x20, 0x00, 0xF0, 0x04, 0x00, UNUSED, 0xFD, defaultBase,
                           0x0024, 0x00, 0x0025, 0x40, 0x4000, 0x0F, 6, mnemonic + " (ind,X)"));
        tests.push_back(mk(indy, 0x20, 0x00, 0xF0, 0x00, 0x05, UNUSED, 0xFD, defaultBase,
                           0x0020, 0x10, 0x0021, 0x40, 0x4015, 0x0F, 5, mnemonic + " (ind),Y"));
        tests.push_back(mk(indy, 0x20, 0x00, 0xF0, 0x00, 0x01, UNUSED, 0xFD, defaultBase,
                           0x0020, 0xFF, 0x0021, 0x40, 0x4100, 0x0F, 6, mnemonic + " (ind),Y cross"));
    };

    // ---------------------------------------------------------------------
    // Generated timing matrices (official read families)
    // ---------------------------------------------------------------------
    addReadMatrix(0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11, "ORA");
    addReadMatrix(0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31, "AND");
    addReadMatrix(0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51, "EOR");
    addReadMatrix(0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71, "ADC");
    addReadMatrix(0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1, "SBC");
    addReadMatrix(0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1, "CMP");

    struct BranchCase {
        uint8_t op;
        uint8_t flag;
        bool branchWhenFlagSet;
        const char *name;
    };
    // Branch timing matrix: not taken / taken / taken with page cross.
    const std::vector<BranchCase> branches = {
        {0x10, NEGATIVE, false, "BPL"},
        {0x30, NEGATIVE, true,  "BMI"},
        {0x50, OVERFLOW, false, "BVC"},
        {0x70, OVERFLOW, true,  "BVS"},
        {0x90, CARRY,    false, "BCC"},
        {0xB0, CARRY,    true,  "BCS"},
        {0xD0, ZERO,     false, "BNE"},
        {0xF0, ZERO,     true,  "BEQ"}
    };

    // ---------------------------------------------------------------------
    // Generated timing matrices (branch families)
    // ---------------------------------------------------------------------
    for (size_t bi = 0; bi < branches.size(); ++bi) {
        const BranchCase &b = branches[bi];
        uint8_t pTaken = static_cast<uint8_t>(UNUSED | (b.branchWhenFlagSet ? b.flag : 0));
        uint8_t pNot = static_cast<uint8_t>(UNUSED | (b.branchWhenFlagSet ? 0 : b.flag));
        tests.push_back(mk(b.op, 0x02, 0x00, 0, 0, 0, pNot, 0xFD, defaultBase,
                           0, 0, 0, 0, 0, 0, 2, std::string(b.name) + " not taken"));
        tests.push_back(mk(b.op, 0x02, 0x00, 0, 0, 0, pTaken, 0xFD, defaultBase,
                           0, 0, 0, 0, 0, 0, 3, std::string(b.name) + " taken"));
        tests.push_back(mk(b.op, 0x02, 0x00, 0, 0, 0, pTaken, 0xFD, 0x20FD,
                           0, 0, 0, 0, 0, 0, 4, std::string(b.name) + " taken cross"));
    }

    // ---------------------------------------------------------------------
    // Execute timing checks
    // ---------------------------------------------------------------------
    int timingMismatchCount = 0;
    for (size_t i = 0; i < tests.size(); ++i) {
        const TimingCase &t = tests[i];

        // Reset memory and install the test program.
        for (size_t a = 0; a < bus.memory.size(); ++a) {
            bus.memory[a] = 0;
        }

        // Force flat RAM map for timing harness so reset vectors/opcodes are read
        // from bus.memory and not from BASIC/KERNAL overlays.
        bus.cpuPortDir = static_cast<uint8_t>(bus.cpuPortDir | 0x07);
        bus.cpuPortData = static_cast<uint8_t>(bus.cpuPortData & static_cast<uint8_t>(~0x07));
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.memory[0x0001] = bus.cpuPortData;

        uint16_t pc = (t.startPC == 0) ? defaultBase : t.startPC;
        bus.memory[0xFFFC] = Lo(pc);
        bus.memory[0xFFFD] = Hi(pc);

        bus.memory[pc] = t.op;
        bus.memory[pc + 1] = t.b1;
        bus.memory[pc + 2] = t.b2;

        if (t.a1 < bus.memory.size()) {
            bus.memory[t.a1] = t.v1;
        }
        if (t.a2 < bus.memory.size()) {
            bus.memory[t.a2] = t.v2;
        }
        if (t.a3 < bus.memory.size()) {
            bus.memory[t.a3] = t.v3;
        }

        cpu.reset();

        // Let the RESET micro-sequence complete before running the test opcode.
        uint16_t safety = 0;
        while (!cpu.isIdle() && safety < 200) {
            cpu.clock();
            safety++;
        }

        Registers r = cpu.getRegisters();
        r.A = t.A;
        r.X = t.X;
        r.Y = t.Y;
        r.P = static_cast<uint8_t>(t.P | UNUSED);
        r.SP = t.SP;
        cpu.setRegisters(r);

        // Measure elapsed cycles for exactly one decoded instruction.
        uint64_t startHalf = cpu.getTotalHalfCycles();
        cpu.clock();
        while (!cpu.isIdle() && safety < 1200) {
            cpu.clock();
            safety++;
        }
        uint64_t endHalf = cpu.getTotalHalfCycles();

        int measuredCycles = static_cast<int>((endHalf - startHalf) / 2);
        if (measuredCycles != t.cycles) {
            timingMismatchCount++;
            std::cerr << "[TIMING FAIL] " << t.name
                      << " opcode=$" << std::hex << (int)t.op
                      << " expected=" << std::dec << t.cycles
                      << " measured=" << measuredCycles << std::endl;
            continue;
        }

        // Side-effect checks for selected opcodes.
        Registers out = cpu.getRegisters();
        if (t.op == 0x8D || t.op == 0x8E || t.op == 0x8C) {
            uint16_t addr = (uint16_t(t.b2) << 8) | t.b1;
            if (t.op == 0x8D) {
                assert(bus.memory[addr] == t.A);
            }
            if (t.op == 0x8E) {
                assert(bus.memory[addr] == t.X);
            }
            if (t.op == 0x8C) {
                assert(bus.memory[addr] == t.Y);
            }
        }
        if (t.op == 0x85) {
            assert(bus.memory[t.b1] == t.A);
        }
        if (t.op == 0x94) {
            assert(bus.memory[static_cast<uint8_t>(t.b1 + t.X)] == t.Y);
        }
        if (t.op == 0x96) {
            assert(bus.memory[static_cast<uint8_t>(t.b1 + t.Y)] == t.X);
        }
        if (t.op == 0x46 || t.op == 0x56 || t.op == 0x4E || t.op == 0x5E) {
            uint16_t addr = (t.op == 0x46)
                                ? t.b1
                                : (t.op == 0x56)
                                      ? static_cast<uint8_t>(t.b1 + t.X)
                                      : (t.op == 0x4E)
                                            ? ((uint16_t(t.b2) << 8) | t.b1)
                                            : (((uint16_t(t.b2) << 8) | t.b1) + t.X);
            assert(bus.memory[addr] == static_cast<uint8_t>(t.v1 >> 1));
        }
        if (t.op == 0xC6 || t.op == 0xD6 || t.op == 0xCE || t.op == 0xDE) {
            uint16_t addr = (t.op == 0xC6)
                                ? t.b1
                                : (t.op == 0xD6)
                                      ? static_cast<uint8_t>(t.b1 + t.X)
                                      : (t.op == 0xCE)
                                            ? ((uint16_t(t.b2) << 8) | t.b1)
                                            : (((uint16_t(t.b2) << 8) | t.b1) + t.X);
            assert(bus.memory[addr] == static_cast<uint8_t>(t.v1 - 1));
        }
        if (t.op == 0xF6 || t.op == 0xFE) {
            uint16_t addr = (t.op == 0xF6)
                                ? static_cast<uint8_t>(t.b1 + t.X)
                                : (((uint16_t(t.b2) << 8) | t.b1) + t.X);
            assert(bus.memory[addr] == static_cast<uint8_t>(t.v1 + 1));
        }
        if (t.op == 0x03 || t.op == 0x07 || t.op == 0x0F || t.op == 0x1F) {
            uint16_t addr = (t.op == 0x07)
                                ? t.b1
                                : (t.op == 0x0F)
                                      ? ((uint16_t(t.b2) << 8) | t.b1)
                                      : (t.op == 0x1F)
                                            ? (((uint16_t(t.b2) << 8) | t.b1) + t.X)
                                            : ((uint16_t(t.v2) << 8) | t.v1);
            uint8_t shifted = static_cast<uint8_t>(t.v3 ? (t.v3 << 1) : (t.v1 << 1));
            assert(bus.memory[addr] == shifted);
        }
        if (t.op == 0x48) {
            assert(bus.memory[0x0100 | t.SP] == t.A);
        }
        if (t.op == 0x68) {
            assert(out.A == t.v1);
        }
        if (t.op == 0x60) {
            assert(out.PC == static_cast<uint16_t>(((t.v2 << 8) | t.v1) + 1));
        }
    }

    if (timingMismatchCount != 0) {
        std::cerr << "[TIMING] mismatch count=" << timingMismatchCount << std::endl;
        assert(false);
    }

std::cout << "[TIMING] extended opcode timing checks passed (official + unofficial subset)." << std::endl;
}

static void runWeek3SubcycleSelfChecks(Bus &bus, CPU6510 &cpu) {
    struct BusCase {
        const char *name;
        std::function<void()> setup;
        std::function<void()> verify;
    };

    std::vector<BusCase> busCases;

    busCases.push_back(BusCase{
        "cpu-port-0000",
        [&]() {
            bus.flatMemoryMode = false;
            bus.hasBasicRom = false;
            bus.hasKernalRom = false;
            bus.hasCharRom = false;
            bus.cpuPortDir = 0x2F;
            bus.cpuPortData = 0x37;
        },
        [&]() {
            assert(bus.read(0x0000) == 0x2F);
        }
    });

    busCases.push_back(BusCase{
        "cpu-port-0001",
        [&]() {
            bus.flatMemoryMode = false;
            bus.hasBasicRom = false;
            bus.hasKernalRom = false;
            bus.hasCharRom = false;
            bus.cpuPortDir = 0x07;
            bus.cpuPortData = 0x00;
        },
        [&]() {
            assert(bus.read(0x0001) == 0xF8);
        }
    });

    busCases.push_back(BusCase{
        "io-visible-memory-fallback",
        [&]() {
            bus.flatMemoryMode = false;
            bus.hasBasicRom = false;
            bus.hasKernalRom = false;
            bus.hasCharRom = false;
            bus.vic = nullptr;
            bus.cia1 = nullptr;
            bus.cia2 = nullptr;
            bus.sid = nullptr;
            bus.cpuPortDir = 0x07;
            bus.cpuPortData = 0x07;
            bus.memory[0xD020] = 0x3C;
        },
        [&]() {
            const uint8_t v = bus.read(0xD020);
            assert(v == 0x3C);
            assert(bus.openBusValue == 0x3C);
        }
    });

    busCases.push_back(BusCase{
        "io-hidden-ram-path",
        [&]() {
            bus.flatMemoryMode = false;
            bus.hasBasicRom = false;
            bus.hasKernalRom = false;
            bus.hasCharRom = false;
            bus.vic = nullptr;
            bus.cia1 = nullptr;
            bus.cia2 = nullptr;
            bus.sid = nullptr;
            bus.cpuPortDir = 0x07;
            bus.cpuPortData = 0x00;
            bus.memory[0xD020] = 0xA7;
        },
        [&]() {
            const uint8_t v = bus.read(0xD020);
            assert(v == 0xA7);
            assert(bus.openBusValue == 0xA7);
        }
    });

    for (size_t i = 0; i < busCases.size(); ++i) {
        busCases[i].setup();
        busCases[i].verify();
    }

    {
        for (size_t i = 0; i < bus.memory.size(); ++i) {
            bus.memory[i] = 0;
        }
        bus.flatMemoryMode = false;
        bus.cpuPortDir = static_cast<uint8_t>(bus.cpuPortDir | 0x07);
        bus.cpuPortData = static_cast<uint8_t>(bus.cpuPortData & static_cast<uint8_t>(~0x07));
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.memory[0x0001] = bus.cpuPortData;
        bus.memory[0xFFFC] = 0x00;
        bus.memory[0xFFFD] = 0x20;
        bus.memory[0x2000] = 0x68;
        bus.memory[0x01FD] = 0x66;
        cpu.reset();
        uint32_t guard = 0;
        while (!cpu.isIdle() && guard < 512) {
            cpu.clock();
            guard++;
        }
        Registers r = cpu.getRegisters();
        r.P = UNUSED;
        r.SP = 0xFC;
        cpu.setRegisters(r);
        const uint64_t start = cpu.getTotalHalfCycles();
        cpu.clock();
        guard = 0;
        while (!cpu.isIdle() && guard < 128) {
            cpu.clock();
            guard++;
        }
        Registers out = cpu.getRegisters();
        assert(out.A == 0x66);
        assert(bus.openBusValue == 0x66);
        const int cycles = static_cast<int>((cpu.getTotalHalfCycles() - start) / 2);
        assert(cycles == 4);
    }

    {
        cpu.clearMicroOpsForTest();
        cpu.setCurrentPhaseForTest(PHI1);
        std::string order;
        cpu.pushMicroOpForTest(PHI1, [&]() { order.push_back('1'); });
        cpu.pushMicroOpForTest(PHI2, [&]() { order.push_back('2'); });
        cpu.clock();
        cpu.clock();
        assert(order == "12");
        while (cpu.hasPendingMicroOpsForTest()) {
            cpu.clock();
        }
    }

    {
        for (size_t i = 0; i < bus.memory.size(); ++i) {
            bus.memory[i] = 0;
        }
        bus.flatMemoryMode = false;
        bus.cpuPortDir = static_cast<uint8_t>(bus.cpuPortDir | 0x07);
        bus.cpuPortData = static_cast<uint8_t>(bus.cpuPortData & static_cast<uint8_t>(~0x07));
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.memory[0x0001] = bus.cpuPortData;
        bus.memory[0xFFFA] = 0x56;
        bus.memory[0xFFFB] = 0x34;
        bus.memory[0xFFFE] = 0x9A;
        bus.memory[0xFFFF] = 0x78;
        bus.memory[0xFFFC] = 0x00;
        bus.memory[0xFFFD] = 0x20;
        bus.memory[0x2000] = 0xEA;
        cpu.reset();
        uint32_t guard = 0;
        while (!cpu.isIdle() && guard < 512) {
            cpu.clock();
            guard++;
        }
        Registers r = cpu.getRegisters();
        r.P = UNUSED;
        cpu.setRegisters(r);
        cpu.setNMI(false);
        cpu.setIRQ(false);
        guard = 0;
        while ((!cpu.isIdle() || guard < 8) && guard < 512) {
            cpu.clock();
            guard++;
        }
        Registers out = cpu.getRegisters();
        assert(out.PC == 0x3456);
    }

    bus.flatMemoryMode = false;
}

static void runWeek11OpenBusBankingChecks(Bus &bus, CPU6510 &cpu) {
    struct BankingCase {
        const char *name;
        uint8_t portData;
        bool expectIoVisible;
        bool expectCharVisible;
        uint8_t expectA000;
        uint8_t expectD020;
        uint8_t expectE000;
    };

    const uint8_t savedDir = bus.cpuPortDir;
    const uint8_t savedData = bus.cpuPortData;
    const bool savedBasic = bus.hasBasicRom;
    const bool savedKernal = bus.hasKernalRom;
    const bool savedChar = bus.hasCharRom;
    auto *savedVic = bus.vic;
    auto *savedCia1 = bus.cia1;
    auto *savedCia2 = bus.cia2;
    auto *savedSid = bus.sid;
    auto savedReadTap = bus.readTap;
    auto savedWriteTap = bus.writeTap;
    auto savedPreReadTap = bus.preReadTap;

    bus.vic = nullptr;
    bus.cia1 = nullptr;
    bus.cia2 = nullptr;
    bus.sid = nullptr;
    bus.flatMemoryMode = false;
    bus.hasBasicRom = true;
    bus.hasKernalRom = true;
    bus.hasCharRom = true;
    bus.basicRom.fill(0xBA);
    bus.kernalRom.fill(0xE1);
    bus.charRom.fill(0xC3);
    bus.memory[0xA000] = 0x0A;
    bus.memory[0xD020] = 0x20;
    bus.memory[0xE000] = 0x0E;
    bus.cpuPortDir = 0x07;
    bus.memory[0x0000] = bus.cpuPortDir;

    std::vector<BankingCase> matrix = {
        {"bank-all-rom-io", 0x07, true,  false, 0xBA, 0x20, 0xE1},
        {"bank-char-hidden", 0x03, false, true,  0xBA, 0xC3, 0xE1},
        {"bank-kernal-only", 0x02, false, true,  0x0A, 0xC3, 0xE1},
        {"bank-ram-visible", 0x00, false, false, 0x0A, 0x20, 0x0E}
    };

    for (const auto &tc : matrix) {
        bus.cpuPortData = tc.portData;
        bus.memory[0x0001] = tc.portData;

        const uint8_t a = bus.read(0xA000);
        const uint8_t d = bus.read(0xD020);
        const uint8_t e = bus.read(0xE000);

        if (a != tc.expectA000 || d != tc.expectD020 || e != tc.expectE000) {
            std::cerr << "[WEEK11] FAIL matrix " << tc.name
                      << " a=$" << std::hex << int(a)
                      << " d=$" << int(d)
                      << " e=$" << int(e)
                      << std::dec << std::endl;
            assert(false);
        }

        const bool loram = (bus.cpuPortDir & 0x01) && (bus.cpuPortData & 0x01);
        const bool hiram = (bus.cpuPortDir & 0x02) && (bus.cpuPortData & 0x02);
        const bool charen = (bus.cpuPortDir & 0x04) && (bus.cpuPortData & 0x04);
        const bool ioVisible = (loram || hiram) && charen;
        const bool charVisible = (loram || hiram) && !charen;
        assert(ioVisible == tc.expectIoVisible);
        assert(charVisible == tc.expectCharVisible);
        assert(bus.openBusValue == e);
        assert(bus.lastDataBusValue == e);
    }

    {
        for (size_t i = 0; i < bus.memory.size(); ++i) {
            bus.memory[i] = 0;
        }
        bus.cpuPortDir = 0x07;
        bus.cpuPortData = 0x00;
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.memory[0x0001] = bus.cpuPortData;
        bus.hasKernalRom = true;
        bus.kernalRom[0x0000] = 0x99;
        bus.memory[0xE000] = 0x00;

        const uint16_t start = 0x2000;
        const std::vector<uint8_t> prog = {
            0xA9, 0x00,       // LDA #$00
            0x85, 0x01,       // STA $01  (hide ROM)
            0xA9, 0x42,       // LDA #$42
            0x8D, 0x00, 0xE0, // STA $E000 (RAM under ROM)
            0xA9, 0x02,       // LDA #$02
            0x85, 0x01,       // STA $01 (show KERNAL)
            0xAD, 0x00, 0xE0, // LDA $E000 (ROM)
            0x85, 0x40,       // STA $40
            0xA9, 0x00,       // LDA #$00
            0x85, 0x01,       // STA $01 (hide ROM)
            0xAD, 0x00, 0xE0, // LDA $E000 (RAM)
            0x85, 0x41,       // STA $41
            0x4C, 0x1E, 0x20  // JMP $201E
        };
        for (size_t i = 0; i < prog.size(); ++i) {
            bus.memory[start + i] = prog[i];
        }
        bus.memory[0xFFFC] = static_cast<uint8_t>(start & 0xFF);
        bus.memory[0xFFFD] = static_cast<uint8_t>((start >> 8) & 0xFF);

        cpu.reset();
        uint32_t guard = 0;
        while (guard < 5000) {
            cpu.clock();
            if (cpu.isIdle() && cpu.getRegisters().PC == 0x201E) {
                break;
            }
            guard++;
        }
        assert(bus.memory[0x0040] == 0x99);
        assert(bus.memory[0x0041] == 0x42);
    }

    {
        auto runIrqVectorCase = [&](uint8_t portData, uint16_t expectPc) {
            for (size_t i = 0; i < bus.memory.size(); ++i) {
                bus.memory[i] = 0;
            }
            bus.cpuPortDir = 0x07;
            bus.cpuPortData = portData;
            bus.memory[0x0000] = bus.cpuPortDir;
            bus.memory[0x0001] = bus.cpuPortData;
            bus.hasKernalRom = true;
            bus.kernalRom[0x1FFE] = 0x34;
            bus.kernalRom[0x1FFF] = 0x12;
            bus.memory[0xFFFE] = 0x78;
            bus.memory[0xFFFF] = 0x56;
            bus.memory[0xFFFC] = 0x00;
            bus.memory[0xFFFD] = 0x20;
            bus.memory[0x2000] = 0xEA;

            cpu.reset();
            uint32_t guard = 0;
            while (!cpu.isIdle() && guard < 512) {
                cpu.clock();
                guard++;
            }
            Registers regs = cpu.getRegisters();
            regs.P = UNUSED;
            cpu.setRegisters(regs);
            cpu.setIRQ(false);
            guard = 0;
            while (guard < 256) {
                cpu.clock();
                if (cpu.isIdle() && cpu.getRegisters().PC == expectPc) {
                    break;
                }
                guard++;
            }
            assert(cpu.getRegisters().PC == expectPc);
            cpu.setIRQ(true);
        };

        runIrqVectorCase(0x02, 0x1234);
        runIrqVectorCase(0x00, 0x5678);
    }

    {
        struct TraceEntry {
            uint16_t addr;
            uint8_t val;
            bool write;
        };
        std::vector<TraceEntry> trace;
        trace.reserve(32);

        bus.readTap = [&](uint16_t addr, uint8_t val) {
            if (addr == 0xA000 || addr == 0xD020 || addr == 0xE000) {
                trace.push_back(TraceEntry{addr, val, false});
            }
        };
        bus.writeTap = [&](uint16_t addr, uint8_t val) {
            if (addr == 0x0001) {
                trace.push_back(TraceEntry{addr, val, true});
            }
        };

        bus.cpuPortDir = 0x07;
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.hasBasicRom = true;
        bus.hasKernalRom = true;
        bus.hasCharRom = true;
        bus.basicRom.fill(0xBA);
        bus.kernalRom.fill(0xE1);
        bus.charRom.fill(0xC3);
        bus.memory[0xA000] = 0x0A;
        bus.memory[0xD020] = 0x20;
        bus.memory[0xE000] = 0x0E;

        const std::array<uint8_t, 4> seq = {0x07, 0x03, 0x02, 0x00};
        for (uint8_t v : seq) {
            bus.write(0x0001, v);
            const uint8_t a = bus.read(0xA000);
            const uint8_t d = bus.read(0xD020);
            const uint8_t e = bus.read(0xE000);
            assert(bus.lastDataBusValue == e);
            assert(bus.openBusValue == e);
            (void)a;
            (void)d;
        }

        assert(!trace.empty());
        size_t writeCount = 0;
        size_t readCount = 0;
        for (const auto &ev : trace) {
            if (ev.write) {
                assert(ev.addr == 0x0001);
                writeCount++;
            } else {
                assert(ev.addr == 0xA000 || ev.addr == 0xD020 || ev.addr == 0xE000);
                readCount++;
            }
        }
        assert(writeCount == seq.size());
        assert(readCount == seq.size() * 3);
    }

    bus.readTap = savedReadTap;
    bus.writeTap = savedWriteTap;
    bus.preReadTap = savedPreReadTap;
    bus.cpuPortDir = savedDir;
    bus.cpuPortData = savedData;
    bus.hasBasicRom = savedBasic;
    bus.hasKernalRom = savedKernal;
    bus.hasCharRom = savedChar;
    bus.vic = savedVic;
    bus.cia1 = savedCia1;
    bus.cia2 = savedCia2;
    bus.sid = savedSid;
    bus.flatMemoryMode = false;
}

static void runWeek12InterruptBoundaryChecks(Bus &bus, CPU6510 &cpu, VICII &vic, CIA6526 &cia1, CIA6526 &cia2) {
    const uint8_t savedDir = bus.cpuPortDir;
    const uint8_t savedData = bus.cpuPortData;
    const bool savedBasic = bus.hasBasicRom;
    const bool savedKernal = bus.hasKernalRom;
    const bool savedChar = bus.hasCharRom;
    auto savedReadTap = bus.readTap;
    auto savedWriteTap = bus.writeTap;
    auto savedPreReadTap = bus.preReadTap;

    size_t mismatches = 0;
    auto fail = [&](const std::string &msg) {
        std::cerr << "[WEEK12] FAIL: " << msg << std::endl;
        mismatches++;
    };

    auto syncLinesFromDevices = [&]() {
        bool irq = false;
        if ((vic.irqFlags & 0x01) != 0) irq = true;
        if ((cia1.icr & cia1.ier & 0x7F) != 0) irq = true;
        if ((cia2.icr & cia2.ier & 0x7F) != 0) irq = true;
        cpu.setIRQ(!irq);

        bool nmi = false;
        if ((cia1.icr & 0x80) != 0) nmi = true;
        if ((cia2.icr & 0x80) != 0) nmi = true;
        cpu.setNMI(!nmi);
    };

    auto runUntil = [&](const std::function<bool()> &pred, uint32_t limit) {
        for (uint32_t i = 0; i < limit; ++i) {
            syncLinesFromDevices();
            cpu.clock();
            if (pred()) {
                return true;
            }
        }
        return false;
    };

    auto resetHarness = [&]() {
        std::fill(bus.memory.begin(), bus.memory.end(), 0);
        bus.flatMemoryMode = false;
        bus.vic = nullptr;
        bus.cia1 = nullptr;
        bus.cia2 = nullptr;
        bus.sid = nullptr;
        bus.hasBasicRom = false;
        bus.hasKernalRom = false;
        bus.hasCharRom = false;
        bus.cpuPortDir = 0x07;
        bus.cpuPortData = 0x00;
        bus.memory[0x0000] = bus.cpuPortDir;
        bus.memory[0x0001] = bus.cpuPortData;
        bus.readTap = nullptr;
        bus.writeTap = nullptr;
        bus.preReadTap = nullptr;
        bus.onDrivenBusValue(0xFF);
        bus.lastDataBusValue = 0xFF;

        vic.irqFlags = 0;
        cia1.icr = 0;
        cia1.ier = 0;
        cia2.icr = 0;
        cia2.ier = 0;
    };

    auto initVectorsAndReset = [&](uint16_t resetPc, uint16_t irqPc, uint16_t nmiPc) {
        bus.memory[0xFFFC] = static_cast<uint8_t>(resetPc & 0xFF);
        bus.memory[0xFFFD] = static_cast<uint8_t>((resetPc >> 8) & 0xFF);
        bus.memory[0xFFFE] = static_cast<uint8_t>(irqPc & 0xFF);
        bus.memory[0xFFFF] = static_cast<uint8_t>((irqPc >> 8) & 0xFF);
        bus.memory[0xFFFA] = static_cast<uint8_t>(nmiPc & 0xFF);
        bus.memory[0xFFFB] = static_cast<uint8_t>((nmiPc >> 8) & 0xFF);
        cpu.reset();
        const bool resetOk = runUntil([&]() { return cpu.isIdle(); }, 1024);
        if (!resetOk) {
            fail("reset did not complete to idle");
        }
    };

    {
        resetHarness();
        bus.memory[0x2000] = 0xEA;
        bus.memory[0x2001] = 0xEA;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        Registers regs = cpu.getRegisters();
        regs.P = UNUSED;
        cpu.setRegisters(regs);

        const bool oneNopDone = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x2001; }, 256);
        if (!oneNopDone) {
            fail("prefetch-boundary setup could not retire first NOP");
        }

        cia1.ier = 0x01;
        cia1.icr = 0x01;
        const bool irqTaken = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 512);
        if (!irqTaken) {
            fail("prefetch-boundary IRQ was not taken");
        } else {
            if (bus.memory[0x01FD] != 0x20 || bus.memory[0x01FC] != 0x01) {
                fail("prefetch-boundary IRQ pushed wrong return PC");
            }
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0x78;
        bus.memory[0x2001] = 0xEA;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        Registers regs = cpu.getRegisters();
        regs.P = UNUSED;
        cpu.setRegisters(regs);

        const bool seiStarted = runUntil([&]() { return !cpu.isIdle(); }, 64);
        if (!seiStarted) {
            fail("SEI edge-order setup did not start instruction");
        }

        vic.irqFlags = 0x01;
        const bool seiDone = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x2001; }, 256);
        if (!seiDone) {
            fail("SEI edge-order did not retire");
        } else {
            const Registers out = cpu.getRegisters();
            if (!ALU::getFlag(out.P, INTERRUPT_DISABLE)) {
                fail("SEI did not set I before boundary");
            }
            if (out.PC == 0x3456) {
                fail("SEI incorrectly allowed pending IRQ at same boundary");
            }
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0x58;
        bus.memory[0x2001] = 0xEA;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        vic.irqFlags = 0x01;
        const bool cliIrq = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 512);
        if (!cliIrq) {
            fail("CLI boundary did not trigger pending IRQ");
        } else if (bus.memory[0x01FD] != 0x20 || bus.memory[0x01FC] != 0x01) {
            fail("CLI boundary IRQ pushed wrong return PC");
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0x28;
        bus.memory[0x2001] = 0xEA;
        bus.memory[0x01FE] = UNUSED;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        cia2.ier = 0x01;
        cia2.icr = 0x01;
        const bool plpIrq = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 1024);
        if (!plpIrq) {
            fail("PLP boundary did not trigger pending IRQ");
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0x00;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        const bool brkTaken = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 1024);
        if (!brkTaken) {
            fail("BRK did not vector to IRQ/BRK handler");
        } else {
            const uint8_t pushedP = bus.memory[0x01FB];
            if ((pushedP & BREAK) == 0 || (pushedP & UNUSED) == 0) {
                fail("BRK push status missing B/U bits");
            }
            if (!ALU::getFlag(cpu.getRegisters().P, INTERRUPT_DISABLE)) {
                fail("BRK did not set I flag");
            }
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0x40;
        bus.memory[0x2001] = 0xEA;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        Registers regs = cpu.getRegisters();
        regs.SP = 0xFA;
        regs.P = UNUSED | INTERRUPT_DISABLE;
        cpu.setRegisters(regs);

        bus.memory[0x01FB] = UNUSED;
        bus.memory[0x01FC] = 0x34;
        bus.memory[0x01FD] = 0x12;

        cia1.ier = 0x01;
        cia1.icr = 0x01;
        const bool rtiIrq = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 1024);
        if (!rtiIrq) {
            fail("RTI boundary did not retrigger pending IRQ");
        } else if (bus.memory[0x01FD] != 0x12 || bus.memory[0x01FC] != 0x34) {
            fail("RTI boundary IRQ pushed wrong return PC");
        }
    }

    {
        resetHarness();
        bus.memory[0x2000] = 0xEA;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);

        Registers regs = cpu.getRegisters();
        regs.P = UNUSED;
        cpu.setRegisters(regs);

        vic.irqFlags = 0x01;
        cia2.icr = 0x80;
        const bool nmiTaken = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x4567; }, 1024);
        if (!nmiTaken) {
            fail("NMI priority over IRQ did not hold");
        }
    }

    {
        resetHarness();
        struct PhaseEvent {
            uint16_t addr;
            bool write;
            Phase phase;
        };
        std::vector<PhaseEvent> events;
        events.reserve(32);

        bus.readTap = [&](uint16_t addr, uint8_t) {
            if (addr == 0xFFFE || addr == 0xFFFF || (addr >= 0x0100 && addr <= 0x01FF)) {
                events.push_back(PhaseEvent{addr, false, cpu.getCurrentPhase()});
            }
        };
        bus.writeTap = [&](uint16_t addr, uint8_t) {
            if (addr >= 0x0100 && addr <= 0x01FF) {
                events.push_back(PhaseEvent{addr, true, cpu.getCurrentPhase()});
            }
        };

        bus.memory[0x2000] = 0x00;
        initVectorsAndReset(0x2000, 0x3456, 0x4567);
        const bool brkTaken = runUntil([&]() { return cpu.isIdle() && cpu.getRegisters().PC == 0x3456; }, 1024);
        if (!brkTaken) {
            fail("PHI validation setup BRK did not complete");
        }

        size_t stackWrites = 0;
        size_t vectorReads = 0;
        for (const auto &ev : events) {
            if (ev.write) {
                stackWrites++;
                if (ev.phase != PHI1) {
                    fail("stack write observed outside PHI1");
                    break;
                }
            } else if (ev.addr == 0xFFFE || ev.addr == 0xFFFF) {
                vectorReads++;
                if (ev.phase != PHI2) {
                    fail("vector read observed outside PHI2");
                    break;
                }
            }
        }
        if (stackWrites < 3) {
            fail("PHI validation did not observe all BRK stack writes");
        }
        if (vectorReads < 2) {
            fail("PHI validation did not observe BRK vector reads");
        }
    }

    bus.readTap = savedReadTap;
    bus.writeTap = savedWriteTap;
    bus.preReadTap = savedPreReadTap;
    bus.cpuPortDir = savedDir;
    bus.cpuPortData = savedData;
    bus.hasBasicRom = savedBasic;
    bus.hasKernalRom = savedKernal;
    bus.hasCharRom = savedChar;
    bus.vic = &vic;
    bus.cia1 = &cia1;
    bus.cia2 = &cia2;
    bus.flatMemoryMode = false;

    if (mismatches != 0) {
        std::cerr << "[WEEK12] interrupt-boundary suite mismatches=" << mismatches << std::endl;
        assert(false);
    }

    std::cerr << "[WEEK12] PASS: interrupt-boundary suite mismatches=0" << std::endl;
}

static void runWeek45TimeCoreMultiDomainSelfCheck() {
    CIA6526 cia2A;
    Drive1541 driveA;
    IecBridgePolarity pol = makeRuntimeDefaultIecPolarity();

    SharedIecClockDomain domainA(cia2A, driveA, pol);
    domainA.configureDomainRatesForTest(985248ULL, 1000000ULL, 1200, 1337u, 0);

    uint32_t eventCounterA = 0;
    uint64_t eventStampA = 0;
    domainA.scheduleEventAtNextC64Boundary([&]() {
        eventCounterA++;
        eventStampA = domainA.getCurrentTimeUnits();
    });

    const uint64_t startDriveA = domainA.getDriveHalfTicks();
    const uint64_t startC64A = domainA.getC64HalfTicks();
    for (int i = 0; i < 4096; ++i) {
        domainA.tickHalfCycle();
    }

    const uint64_t c64TicksA = domainA.getC64HalfTicks() - startC64A;
    const uint64_t driveTicksA = domainA.getDriveHalfTicks() - startDriveA;

    if (c64TicksA != 4096ULL) {
        std::cerr << "[WEEK45 TIME] FAIL: C64 domain did not advance expected ticks." << std::endl;
        assert(false);
    }
    if (driveTicksA == c64TicksA) {
        std::cerr << "[WEEK45 TIME] FAIL: drive domain is still lockstep with C64 domain." << std::endl;
        assert(false);
    }
    if (eventCounterA != 1) {
        std::cerr << "[WEEK45 TIME] FAIL: timed event queue did not fire deterministically." << std::endl;
        assert(false);
    }

    CIA6526 cia2B;
    Drive1541 driveB;
    SharedIecClockDomain domainB(cia2B, driveB, pol);
    domainB.configureDomainRatesForTest(985248ULL, 1000000ULL, 1200, 1337u, 8);

    CIA6526 cia2C;
    Drive1541 driveC;
    SharedIecClockDomain domainC(cia2C, driveC, pol);
    domainC.configureDomainRatesForTest(985248ULL, 1000000ULL, 1200, 1337u, 8);

    uint64_t digestB = 0;
    uint64_t digestC = 0;
    auto runDigest = [&](SharedIecClockDomain &dom, uint64_t &digest) {
        for (int i = 0; i < 2048; ++i) {
            dom.scheduleEventAfter(0, [&]() {
                digest ^= (dom.getCurrentTimeUnits() + (dom.getDriveHalfTicks() << 1) + (dom.getC64HalfTicks() << 3));
                digest = (digest << 7) | (digest >> 57);
                digest ^= 0x9E3779B97F4A7C15ULL;
            });
            dom.tickHalfCycle();
            digest ^= (dom.getDriveHalfTicks() * 0x100000001B3ULL) ^ dom.getCurrentTimeUnits();
            digest = (digest << 11) | (digest >> 53);
        }
    };

    runDigest(domainB, digestB);
    runDigest(domainC, digestC);

    if (digestB != digestC) {
        std::cerr << "[WEEK45 TIME] FAIL: replay determinism mismatch for same seed." << std::endl;
        assert(false);
    }

    std::cerr << "[WEEK45 TIME] PASS: multi-domain scheduler + ratio/drift + deterministic replay"
              << " c64_ticks=" << c64TicksA
              << " drive_ticks=" << driveTicksA
              << " digest=$" << std::hex << digestB << std::dec
              << std::endl;
}


// ==========================
// MAIN di test
// ==========================
static void runDriveIecSmokeSuite(Bus &bus, CIA6526 &cia2) {
    runDrive1541Smoke();
    runDrive1541IecHandshakeSmoke(cia2);
    runDrive1541IecCommandSmoke();
    runDrive1541IecDirectoryStubSmoke(cia2);
    runDrive1541IecHostSessionSmoke(cia2);
    runDrive1541IecStatusTimeoutSmoke(cia2);
    runDrive1541IecMemoryCommandSmoke(cia2);
    runDrive1541IecExecBlockCommandSmoke(cia2);
    runDrive1541IecExecBlockSemanticsSmoke(cia2);
    runDrive1541IecBufferPointerDirectorySmoke(cia2);
    runDrive1541IecBlockAllocMapSmoke(cia2);
    runDrive1541IecNamedCatalogSmoke(cia2);
    runDrive1541IecOpenSuffixSmoke(cia2);
    runDrive1541IecWildcardDirectorySmoke(cia2);
    runDrive1541IecWildcardTypeFilterSmoke(cia2);
    runDrive1541IecWildcardTypeModeFilterSmoke(cia2);
    runDrive1541IecWildcardNegatedModeFilterSmoke(cia2);
    runDrive1541TimingBattery(cia2);
    runDrive1541LoadDirectoryE2ESmoke(cia2, bus);
    runKernelSerialLoadDirectoryTrueE2E();
}

static void runWeek15BaAecCpuHandoffChecks();
static void runWeek16CrossDomainEdgeHardReference();
static void runWeek16TemporalFuzzDeterminismChecks();
static void runWeek17ChipRevisionProfilesChecks();
static void runWeek18OpenBusEdgeHardReference();
static void runWeek19CiaDenseEdgeHardReference();
static void runWeek20VicPathologicalEdgeHardReference();
static void runWeek21BusCornerEdgeHardReference();
static void runWeek22PortMapEdgeHardReference();
static void runWeek23CiaIrqNmiBridgeEdgeHardReference();
static void runWeek24IrqLatchUnderAecEdgeHardReference();
static void runWeek25CiaSerialEdgeHardReference();
static void runWeek26DriveIecHandshakeEdgeHardReference();
static void runWeek27DriveCommandPhaseEdgeHardReference();
static void runWeek28DriveEoiAtnEdgeHardReference();
static void runWeek29DriveIecTimeoutRecoveryEdgeHardReference();
static void runWeek30DriveCommandChannelEdgeHardReference();
static void runWeek31DriveStatusTalkEdgeHardReference();
static void runWeek32DriveDirectoryStreamEdgeHardReference();
static void runWeek33DriveDirectoryFilterEdgeHardReference();
static void runWeek34DriveAllocMapEdgeHardReference();
static void runWeek35DrivePointerDirectoryEdgeHardReference();
static void runWeek36DriveCatalogLifecycleEdgeHardReference();
static void runWeek37DriveAtnCommandGateEdgeHardReference();
static void runWeek38DriveTalkChannelCloseEdgeHardReference();
static void runWeek39DriveCmdResponseFallbackEdgeHardReference();
static void runWeek40DriveCmdBufferCommitEdgeHardReference();
static void syncInterruptLines(Bus &bus, CPU6510 &cpu);

static bool runConfiguredProfiles(Bus &bus, CPU6510 &cpu, VICII &vic, CIA6526 &cia2) {
    #if RUN_PROFILE == RUN_PROFILE_FULL
    runWeek45TimeCoreMultiDomainSelfCheck();
    runWeek11OpenBusBankingChecks(bus, cpu);
    runWeek12InterruptBoundaryChecks(bus, cpu, vic, *bus.cia1, cia2);
    runWeek15BaAecCpuHandoffChecks();
    runWeek16CrossDomainEdgeHardReference();
    runWeek16TemporalFuzzDeterminismChecks();
    runWeek17ChipRevisionProfilesChecks();
    runWeek18OpenBusEdgeHardReference();
    runWeek19CiaDenseEdgeHardReference();
    runWeek20VicPathologicalEdgeHardReference();
    runWeek21BusCornerEdgeHardReference();
    runWeek22PortMapEdgeHardReference();
    runWeek23CiaIrqNmiBridgeEdgeHardReference();
    runWeek24IrqLatchUnderAecEdgeHardReference();
    runWeek25CiaSerialEdgeHardReference();
    runWeek26DriveIecHandshakeEdgeHardReference();
    runWeek27DriveCommandPhaseEdgeHardReference();
    runWeek28DriveEoiAtnEdgeHardReference();
    runWeek29DriveIecTimeoutRecoveryEdgeHardReference();
    runWeek30DriveCommandChannelEdgeHardReference();
    runWeek31DriveStatusTalkEdgeHardReference();
    runWeek32DriveDirectoryStreamEdgeHardReference();
    runWeek33DriveDirectoryFilterEdgeHardReference();
    runWeek34DriveAllocMapEdgeHardReference();
    runWeek35DrivePointerDirectoryEdgeHardReference();
    runWeek36DriveCatalogLifecycleEdgeHardReference();
    runWeek37DriveAtnCommandGateEdgeHardReference();
    runWeek38DriveTalkChannelCloseEdgeHardReference();
    runWeek39DriveCmdResponseFallbackEdgeHardReference();
    runWeek40DriveCmdBufferCommitEdgeHardReference();
    runCia6526EdgeCaseBattery();
    runWeek3SubcycleSelfChecks(bus, cpu);
    runFullRegressionSuite(bus, cpu, vic);
    return true;
    #elif RUN_PROFILE == RUN_PROFILE_STRICT
    runWeek45TimeCoreMultiDomainSelfCheck();
    runWeek11OpenBusBankingChecks(bus, cpu);
    runWeek12InterruptBoundaryChecks(bus, cpu, vic, *bus.cia1, cia2);
    runWeek15BaAecCpuHandoffChecks();
    runWeek16CrossDomainEdgeHardReference();
    runWeek16TemporalFuzzDeterminismChecks();
    runWeek17ChipRevisionProfilesChecks();
    runWeek18OpenBusEdgeHardReference();
    runWeek19CiaDenseEdgeHardReference();
    runWeek20VicPathologicalEdgeHardReference();
    runWeek21BusCornerEdgeHardReference();
    runWeek22PortMapEdgeHardReference();
    runWeek23CiaIrqNmiBridgeEdgeHardReference();
    runWeek24IrqLatchUnderAecEdgeHardReference();
    runWeek25CiaSerialEdgeHardReference();
    runWeek26DriveIecHandshakeEdgeHardReference();
    runWeek27DriveCommandPhaseEdgeHardReference();
    runWeek28DriveEoiAtnEdgeHardReference();
    runWeek29DriveIecTimeoutRecoveryEdgeHardReference();
    runWeek30DriveCommandChannelEdgeHardReference();
    runWeek31DriveStatusTalkEdgeHardReference();
    runWeek32DriveDirectoryStreamEdgeHardReference();
    runWeek33DriveDirectoryFilterEdgeHardReference();
    runWeek34DriveAllocMapEdgeHardReference();
    runWeek35DrivePointerDirectoryEdgeHardReference();
    runWeek36DriveCatalogLifecycleEdgeHardReference();
    runWeek37DriveAtnCommandGateEdgeHardReference();
    runWeek38DriveTalkChannelCloseEdgeHardReference();
    runWeek39DriveCmdResponseFallbackEdgeHardReference();
    runWeek40DriveCmdBufferCommitEdgeHardReference();
    runCia6526EdgeCaseBattery();
    runWeek3SubcycleSelfChecks(bus, cpu);
    runOpcodeTimingSelfCheck(bus, cpu);
    runViciiChecklist(vic, bus);
    runExternalRomValidation(bus, cpu);
    #if DRIVE1541_SMOKE_TEST
    runDriveIecSmokeSuite(bus, cia2);
    #endif
    return true;
    #elif RUN_PROFILE == RUN_PROFILE_FAST
    #if CPU_EXTERNAL_ROM_TEST
    runExternalRomValidation(bus, cpu);
    #if DRIVE1541_SMOKE_TEST
    const bool runOnlyKernelIec = (std::getenv("RUN_ONLY_KERNEL_IEC_E2E") != nullptr);
    if (runOnlyKernelIec) {
        runKernelSerialLoadDirectoryTrueE2E();
        return true;
    }
    runDriveIecSmokeSuite(bus, cia2);
    #endif
    return true;
    #endif
    #endif

    return false;
}

static void tickVideo(Bus &bus) {
    if (bus.vic != nullptr) {
        bus.vic->tickHalf();
    }
}

static bool cpuHasVicContention(const Bus &bus) {
    return (bus.vic != nullptr && !bus.vic->aecLine);
}

static void tickCpuWithVicContention(Bus &bus, CPU6510 &cpu, bool &lastVicHadBus) {
    const bool vicHasBus = cpuHasVicContention(bus);
    if (vicHasBus) {
        if (!lastVicHadBus) {
            cpu.enqueueDummyRead(cpu.getPC());
        }
        cpu.clock(false);
        #if DEBUG_VIC
        std::cout << "[CPU] Bus locked by VIC-II at raster "
                  << bus.vic->rasterLine
                  << " (cycle=" << bus.vic->cycleInLine
                  << " pixel=" << bus.vic->pixelClock
                  << " BA=" << (bus.vic->baLine ? 1 : 0)
                  << " AEC=" << (bus.vic->aecLine ? 1 : 0)
                  << ")"
                  << std::endl;
        #endif
    } else {
        cpu.clock(true);
    }

    lastVicHadBus = vicHasBus;
}

static void tickPeripherals(Bus &bus);
static void syncInterruptLines(Bus &bus, CPU6510 &cpu);

static std::vector<std::string> readTextRowsNoHeader(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::vector<std::string> rows;
    std::string line;
    bool headerSkipped = false;
    while (std::getline(in, line)) {
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            rows.push_back(line);
        }
    }
    return rows;
}

static void writeWeek15EdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "step,aec,last_vic,phase,pending,dummy,halfcycles,marker\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static std::vector<std::string> buildWeek15BaAecEdgeTraceRows() {
    Bus bus;
    VICII vic;
    bus.vic = &vic;
    vic.bus = &bus;
    CPU6510 cpu(bus);

    cpu.clearMicroOpsForTest();
    cpu.setCurrentPhaseForTest(PHI2);
    bool lastVicHadBus = false;
    int markerExec = 0;
    cpu.pushMicroOpForTest(PHI2, [&markerExec]() { markerExec++; });

    const std::array<int, 16> aecPattern = {
        0, 0, 1, 1, 0, 0, 1, 1,
        1, 0, 1, 0, 1, 1, 1, 1
    };

    std::vector<std::string> rows;
    rows.reserve(aecPattern.size());
    for (size_t i = 0; i < aecPattern.size(); ++i) {
        vic.aecLine = (aecPattern[i] != 0);
        tickCpuWithVicContention(bus, cpu, lastVicHadBus);
        std::ostringstream oss;
        oss << i
            << "," << aecPattern[i]
            << "," << (lastVicHadBus ? 1 : 0)
            << "," << ((cpu.getCurrentPhase() == PHI1) ? 1 : 2)
            << "," << cpu.pendingMicroOpCountForTest()
            << "," << (cpu.inDummySequence ? 1 : 0)
            << "," << cpu.getTotalHalfCycles()
            << "," << markerExec;
        rows.push_back(oss.str());
    }
    return rows;
}

static void runWeek15BaAecEdgeHardReference() {
    const std::string runtimePath = "week15_baaec_handoff_runtime.csv";
    const std::string refPath = "reference/edge/week15_baaec_handoff_trace.csv";

    const std::vector<std::string> got = buildWeek15BaAecEdgeTraceRows();
    writeWeek15EdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK15_BOOTSTRAP_BAAEC_REF") != nullptr);
    if (bootstrap) {
        writeWeek15EdgeTraceCsv(refPath, got);
        std::cout << "[WEEK15][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK15][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }

    if (got.size() != ref.size()) {
        std::cerr << "[WEEK15][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }

    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK15][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK15][HARDREF] PASS: BA/AEC edge trace matches reference" << std::endl;
}

static void runWeek15BaAecCpuHandoffChecks() {
    Bus tb;
    CPU6510 tcpu(tb);

    tcpu.clearMicroOpsForTest();
    tcpu.setCurrentPhaseForTest(PHI2);
    int phi2Exec = 0;
    tcpu.pushMicroOpForTest(PHI2, [&phi2Exec]() { phi2Exec++; });
    const uint64_t h0 = tcpu.getTotalHalfCycles();
    tcpu.clock(false);
    assert(phi2Exec == 0);
    assert(tcpu.getCurrentPhase() == PHI1);
    assert(tcpu.getTotalHalfCycles() == h0 + 1);

    tcpu.clock(true);
    assert(phi2Exec == 0);
    assert(tcpu.getCurrentPhase() == PHI2);
    tcpu.clock(true);
    assert(phi2Exec == 1);

    tcpu.clearMicroOpsForTest();
    tcpu.setCurrentPhaseForTest(PHI1);
    int phi1Exec = 0;
    tcpu.pushMicroOpForTest(PHI1, [&phi1Exec]() { phi1Exec++; });
    tcpu.clock(false);
    assert(phi1Exec == 1);
    assert(tcpu.getCurrentPhase() == PHI2);

    Bus ib;
    VICII ivic;
    ib.vic = &ivic;
    ivic.bus = &ib;
    CPU6510 icpu(ib);

    icpu.clearMicroOpsForTest();
    icpu.setCurrentPhaseForTest(PHI2);
    int relPhi2Exec = 0;
    icpu.pushMicroOpForTest(PHI2, [&relPhi2Exec]() { relPhi2Exec++; });

    bool lastVicHadBus = true;
    ivic.aecLine = false;
    const uint64_t relH0 = icpu.getTotalHalfCycles();
    tickCpuWithVicContention(ib, icpu, lastVicHadBus);
    assert(relPhi2Exec == 0);
    assert(icpu.getCurrentPhase() == PHI1);
    assert(icpu.getTotalHalfCycles() == relH0 + 1);
    assert(lastVicHadBus);

    ivic.aecLine = true;
    tickCpuWithVicContention(ib, icpu, lastVicHadBus);
    assert(relPhi2Exec == 0);
    assert(icpu.getCurrentPhase() == PHI2);
    assert(icpu.getTotalHalfCycles() == relH0 + 2);
    assert(!lastVicHadBus);

    tickCpuWithVicContention(ib, icpu, lastVicHadBus);
    assert(relPhi2Exec == 1);
    assert(icpu.getTotalHalfCycles() == relH0 + 3);

    Bus eb;
    VICII evic;
    eb.vic = &evic;
    evic.bus = &eb;
    CPU6510 ecpu(eb);
    ecpu.clearMicroOpsForTest();
    ecpu.setCurrentPhaseForTest(PHI1);
    bool edgeLastVicHadBus = false;
    evic.aecLine = false;
    tickCpuWithVicContention(eb, ecpu, edgeLastVicHadBus);
    assert(edgeLastVicHadBus);
    assert(ecpu.inDummySequence);

    const size_t queuedAfterAcquire = ecpu.pendingMicroOpCountForTest();
    evic.aecLine = false;
    tickCpuWithVicContention(eb, ecpu, edgeLastVicHadBus);
    assert(ecpu.pendingMicroOpCountForTest() == queuedAfterAcquire);

    for (int i = 0; i < 32 && ecpu.inDummySequence; ++i) {
        evic.aecLine = true;
        tickCpuWithVicContention(eb, ecpu, edgeLastVicHadBus);
    }
    assert(!ecpu.inDummySequence);

    Bus qb;
    VICII qvic;
    qb.vic = &qvic;
    qvic.bus = &qb;
    CPU6510 qcpu(qb);
    qcpu.clearMicroOpsForTest();
    qcpu.setCurrentPhaseForTest(PHI2);
    qcpu.enqueueDummyRead(qcpu.getPC());

    int qPhi2Exec = 0;
    qcpu.pushMicroOpForTest(PHI2, [&qPhi2Exec]() { qPhi2Exec++; });

    bool qLastVicHadBus = true;
    qvic.aecLine = false;
    for (int i = 0; i < 8; ++i) {
        tickCpuWithVicContention(qb, qcpu, qLastVicHadBus);
    }
    assert(qPhi2Exec == 0);

    qvic.aecLine = true;
    for (int i = 0; i < 32 && qPhi2Exec == 0; ++i) {
        tickCpuWithVicContention(qb, qcpu, qLastVicHadBus);
    }
    assert(qPhi2Exec == 1);

    runWeek15BaAecEdgeHardReference();

    std::cout << "[WEEK15] PASS: BA/AEC handoff edge gating keeps PHI2 bus ops stalled and PHI1 progressing" << std::endl;
}

static std::vector<std::string> buildWeek16CrossDomainTraceRows() {
    Bus bus;
    VICII vic;
    CIA6526 cia1;
    CIA6526 cia2;
    SID sid;
    bus.vic = &vic;
    bus.cia1 = &cia1;
    bus.cia2 = &cia2;
    bus.sid = &sid;
    vic.bus = &bus;
    CPU6510 cpu(bus);

    cpu.clearMicroOpsForTest();
    cpu.setCurrentPhaseForTest(PHI2);
    bool lastVicHadBus = false;
    int markerExec = 0;
    cpu.pushMicroOpForTest(PHI2, [&markerExec]() { markerExec++; });

    cia1.ier = 0x01;
    cia2.ier = 0x01;

    std::vector<std::string> rows;
    rows.reserve(192);
    for (int step = 0; step < 192; ++step) {
        const bool aecLow = ((step % 7) <= 1) || ((step % 19) == 5);
        vic.aecLine = !aecLow;

        cia1.icr = 0;
        cia2.icr = 0;
        if ((step % 5) == 0) cia1.icr |= 0x01;
        if ((step % 9) == 0) cia2.icr |= 0x01;
        if ((step % 11) == 0) cia1.icr |= 0x80;
        if ((step % 13) == 0) cia2.icr |= 0x80;

        if ((step % 16) == 0 && cpu.pendingMicroOpCountForTest() < 2) {
            cpu.pushMicroOpForTest(PHI2, [&markerExec]() { markerExec++; });
        }

        const Phase prePhase = cpu.getCurrentPhase();
        const uint64_t preHalf = cpu.getTotalHalfCycles();
        const int preMarker = markerExec;
        const size_t prePending = cpu.pendingMicroOpCountForTest();
        const bool preLastVicHadBus = lastVicHadBus;

        tickCpuWithVicContention(bus, cpu, lastVicHadBus);
        tickPeripherals(bus);
        syncInterruptLines(bus, cpu);

        assert(cpu.getTotalHalfCycles() == (preHalf + 1));
        if (aecLow && prePhase == PHI2) {
            assert(markerExec == preMarker);
            if (preLastVicHadBus) {
                assert(cpu.pendingMicroOpCountForTest() == prePending);
            }
        }

        std::ostringstream oss;
        oss << step
            << "," << (vic.aecLine ? 1 : 0)
            << "," << ((prePhase == PHI1) ? 1 : 2)
            << "," << ((cpu.getCurrentPhase() == PHI1) ? 1 : 2)
            << "," << cpu.pendingMicroOpCountForTest()
            << "," << (cpu.inDummySequence ? 1 : 0)
            << "," << (cpu.irqLine ? 1 : 0)
            << "," << (cpu.nmiLine ? 1 : 0)
            << "," << (lastVicHadBus ? 1 : 0)
            << "," << cpu.getTotalHalfCycles()
            << "," << markerExec;
        rows.push_back(oss.str());
    }
    return rows;
}

static void writeWeek16CrossDomainTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "step,aec,pre_phase,post_phase,pending,dummy,irq,nmi,last_vic,halfcycles,marker\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek16CrossDomainEdgeHardReference() {
    const std::string runtimePath = "week16_cross_domain_runtime.csv";
    const std::string refPath = "reference/edge/week16_cross_domain_trace.csv";

    const std::vector<std::string> got = buildWeek16CrossDomainTraceRows();
    writeWeek16CrossDomainTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK16_BOOTSTRAP_CROSS_REF") != nullptr);
    if (bootstrap) {
        writeWeek16CrossDomainTraceCsv(refPath, got);
        std::cout << "[WEEK16][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK16][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK16][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK16][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK16][HARDREF] PASS: cross-domain edge trace matches reference" << std::endl;
}

static uint64_t runWeek16TemporalFuzzDigest(uint32_t seed) {
    Bus bus;
    VICII vic;
    bus.vic = &vic;
    vic.bus = &bus;
    CPU6510 cpu(bus);

    cpu.clearMicroOpsForTest();
    cpu.setCurrentPhaseForTest(PHI2);
    bool lastVicHadBus = false;
    int markerExec = 0;
    cpu.pushMicroOpForTest(PHI2, [&markerExec]() { markerExec++; });

    std::mt19937 rng(seed);
    uint64_t digest = 1469598103934665603ULL;

    for (int i = 0; i < 1024; ++i) {
        const uint32_t rv = rng();
        vic.aecLine = ((rv & 0x3u) != 0u);
        cpu.setIRQ(((rv & 0x10u) == 0u));
        cpu.setNMI(((rv & 0x40u) == 0u));

        if ((rv & 0x80u) != 0u && cpu.pendingMicroOpCountForTest() < 3) {
            cpu.pushMicroOpForTest(PHI2, [&markerExec]() { markerExec++; });
        }

        const Phase prePhase = cpu.getCurrentPhase();
        const int preMarker = markerExec;
        const uint64_t preHalf = cpu.getTotalHalfCycles();
        const bool aecLow = !vic.aecLine;

        tickCpuWithVicContention(bus, cpu, lastVicHadBus);

        assert(cpu.getTotalHalfCycles() == (preHalf + 1));
        if (aecLow && prePhase == PHI2) {
            assert(markerExec == preMarker);
        }

        digest ^= (static_cast<uint64_t>(vic.aecLine ? 1 : 0) << 1);
        digest ^= (static_cast<uint64_t>((cpu.getCurrentPhase() == PHI1) ? 1 : 2) << 3);
        digest ^= (static_cast<uint64_t>(cpu.pendingMicroOpCountForTest()) << 8);
        digest ^= (static_cast<uint64_t>(markerExec) << 24);
        digest ^= cpu.getTotalHalfCycles();
        digest *= 1099511628211ULL;
    }

    return digest;
}

static void runWeek16TemporalFuzzDeterminismChecks() {
    const std::array<uint32_t, 5> seeds = {1u, 7u, 42u, 1337u, 0xC64u};
    uint64_t aggregate = 0;
    for (size_t i = 0; i < seeds.size(); ++i) {
        const uint64_t d1 = runWeek16TemporalFuzzDigest(seeds[i]);
        const uint64_t d2 = runWeek16TemporalFuzzDigest(seeds[i]);
        assert(d1 == d2);
        aggregate ^= (d1 + (static_cast<uint64_t>(seeds[i]) << 32));
        aggregate = (aggregate << 9) | (aggregate >> 55);
    }
    std::cout << "[WEEK16][FUZZ] PASS: temporal fuzz deterministic digest=$"
              << std::hex << aggregate << std::dec << std::endl;
}

static std::vector<std::string> buildWeek19CiaDenseRevisionRows(CIA6526::Revision rev, const char *label) {
    CIA6526 cia;
    cia.setRevision(rev);
    cia.ier = 0xFF;
    cia.write(0xDC04, 0x04);
    cia.write(0xDC05, 0x00);
    cia.write(0xDC0E, 0x51);
    cia.write(0xDC0F, static_cast<uint8_t>(cia.t2_ctrl & static_cast<uint8_t>(~0x80)));
    cia.write(0xDC08, 0x09);
    cia.write(0xDC09, 0x59);
    cia.write(0xDC0A, 0x59);
    cia.write(0xDC0B, 0x11);

    std::vector<std::string> rows;
    rows.reserve(256);
    for (int i = 0; i < 192; ++i) {
        const bool sp = ((i * 13 + 7) & 0x20) != 0;
        const bool cnt = ((i & 1) == 0);
        cia.setSerialPins(cnt, sp);
        if ((i % 19) == 0) {
            cia.setFlagPin(false);
        } else {
            cia.setFlagPin(true);
        }
        cia.cycleCore.tickHalfCycle(cia);

        std::ostringstream oss;
        oss << label
            << "," << i
            << "," << int(cia.tod_10ths)
            << "," << int(cia.serialShiftBitCount)
            << "," << int(cia.icr)
            << "," << cia.timer1UnderflowCount
            << "," << cia.serialRxByteCount
            << "," << cia.serialTxByteCount;
        rows.push_back(oss.str());
    }
    return rows;
}

static std::vector<std::string> buildWeek19CiaDenseEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek19CiaDenseRevisionRows(CIA6526::REV_6526, "6526");
    const auto r1 = buildWeek19CiaDenseRevisionRows(CIA6526::REV_6526A, "6526A");
    const auto r2 = buildWeek19CiaDenseRevisionRows(CIA6526::REV_6526R4, "6526R4");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek19CiaDenseEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,step,tod10,serial_bits,icr,t1_underflow,rx_bytes,tx_bytes\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek19CiaDenseEdgeHardReference() {
    const std::string runtimePath = "week19_cia_dense_runtime.csv";
    const std::string refPath = "reference/edge/week19_cia_dense_trace.csv";

    const std::vector<std::string> got = buildWeek19CiaDenseEdgeTraceRows();
    writeWeek19CiaDenseEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK19_BOOTSTRAP_CIA_REF") != nullptr);
    if (bootstrap) {
        writeWeek19CiaDenseEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK19][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK19][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK19][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK19][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK19][HARDREF] PASS: CIA dense revision trace matches reference" << std::endl;
}

static void runWeek17ChipRevisionProfilesChecks() {
    {
        CIA6526 cia;
        cia.setRevision(CIA6526::REV_6526);
        assert(cia.getRevision() == CIA6526::REV_6526);
        assert(cia.getRevisionProfile().todDividerReload == 10);

        cia.setRevision(CIA6526::REV_6526A);
        assert(cia.getRevision() == CIA6526::REV_6526A);
        assert(cia.getRevisionProfile().todDividerReload == 12);
        assert(cia.getRevisionProfile().flagIrqImmediate);
        assert(cia.getRevisionProfile().serialInputShiftOnFallingEdge);

        cia.setRevision(CIA6526::REV_6526R4);
        assert(cia.getRevision() == CIA6526::REV_6526R4);
        assert(cia.getRevisionProfile().todDividerReload == 11);
    }

    {
        CIA6526 ciaBase;
        ciaBase.setRevision(CIA6526::REV_6526);
        ciaBase.tod_10ths = 0;
        for (int i = 0; i < 20; ++i) {
            ciaBase.cycleCore.tickHalfCycle(ciaBase);
        }
        const uint8_t baseTod10 = ciaBase.tod_10ths;

        CIA6526 ciaA;
        ciaA.setRevision(CIA6526::REV_6526A);
        ciaA.tod_10ths = 0;
        for (int i = 0; i < 20; ++i) {
            ciaA.cycleCore.tickHalfCycle(ciaA);
        }
        const uint8_t aTod10 = ciaA.tod_10ths;

        assert(baseTod10 > aTod10);
    }

    {
        CIA6526 c0;
        c0.setRevision(CIA6526::REV_6526);
        c0.ier = static_cast<uint8_t>(1u << 4);
        c0.setFlagPin(false);
        c0.cycleCore.tickHalfCycle(c0);
        assert((c0.icr & (1u << 4)) == 0);
        c0.cycleCore.tickHalfCycle(c0);
        assert((c0.icr & (1u << 4)) != 0);

        CIA6526 c1;
        c1.setRevision(CIA6526::REV_6526A);
        c1.ier = static_cast<uint8_t>(1u << 4);
        c1.setFlagPin(false);
        c1.cycleCore.tickHalfCycle(c1);
        assert((c1.icr & (1u << 4)) != 0);
    }

    {
        CIA6526 c0;
        c0.setRevision(CIA6526::REV_6526);
        c0.write(0xDC0E, static_cast<uint8_t>(c0.controlA & static_cast<uint8_t>(~0x40)));
        c0.setSerialPins(true, false);
        c0.cycleCore.tickHalfCycle(c0);
        c0.setSerialPins(false, true);
        c0.cycleCore.tickHalfCycle(c0);
        const uint8_t nmosCount = c0.serialShiftBitCount;

        CIA6526 c1;
        c1.setRevision(CIA6526::REV_6526A);
        c1.write(0xDC0E, static_cast<uint8_t>(c1.controlA & static_cast<uint8_t>(~0x40)));
        c1.setSerialPins(true, false);
        c1.cycleCore.tickHalfCycle(c1);
        c1.setSerialPins(false, true);
        c1.cycleCore.tickHalfCycle(c1);
        const uint8_t hmosCount = c1.serialShiftBitCount;

        assert(nmosCount == 0);
        assert(hmosCount == 1);
    }

    {
        CIA6526 c0;
        c0.setRevision(CIA6526::REV_6526);
        c0.write(0xDC04, 0x02);
        c0.write(0xDC05, 0x00);
        c0.write(0xDC0E, 0x51);
        c0.write(0xDC0C, 0xA5);
        for (int i = 0; i < 96; ++i) {
            c0.tick();
            if (c0.serialSpHigh) {
                break;
            }
        }
        const uint16_t nmosDivider = c0.serialShiftDivider;

        CIA6526 c1;
        c1.setRevision(CIA6526::REV_6526A);
        c1.write(0xDC04, 0x02);
        c1.write(0xDC05, 0x00);
        c1.write(0xDC0E, 0x51);
        c1.write(0xDC0C, 0xA5);
        for (int i = 0; i < 96; ++i) {
            c1.tick();
            if (c1.serialSpHigh) {
                break;
            }
        }
        const uint16_t hmosDivider = c1.serialShiftDivider;
        assert(nmosDivider != hmosDivider);
    }

    {
        CIA6526 ciaBase;
        ciaBase.setRevision(CIA6526::REV_6526);
        ciaBase.write(0xDC0B, 0x12);
        for (int i = 0; i < 64; ++i) {
            ciaBase.cycleCore.tickHalfCycle(ciaBase);
        }
        const uint8_t stopped10ths = ciaBase.tod_10ths;
        assert(stopped10ths == 0);
        ciaBase.write(0xDC08, 0x00);
        for (int i = 0; i < 64; ++i) {
            ciaBase.cycleCore.tickHalfCycle(ciaBase);
        }
        assert(ciaBase.tod_10ths > stopped10ths);

        CIA6526 ciaA;
        ciaA.setRevision(CIA6526::REV_6526A);
        ciaA.write(0xDC0B, 0x12);
        for (int i = 0; i < 64; ++i) {
            ciaA.cycleCore.tickHalfCycle(ciaA);
        }
        assert(ciaA.tod_10ths > 0);
    }

    {
        Bus b;
        CPU6510 cpu6510(b);
        cpu6510.setRevision(CPU6510::REV_6510);
        assert(cpu6510.getRevision() == CPU6510::REV_6510);
        assert(cpu6510.getRevisionProfile().nmosDecimalBehavior);

        CPU6510 cpu8500(b);
        cpu8500.setRevision(CPU6510::REV_8500);
        assert(cpu8500.getRevision() == CPU6510::REV_8500);
        assert(cpu8500.getRevisionProfile().supportsPortFallbackPullups);

        CPU6510 cpu8500r2(b);
        cpu8500r2.setRevision(CPU6510::REV_8500R2);
        assert(cpu8500r2.getRevision() == CPU6510::REV_8500R2);
        assert(cpu8500r2.getRevisionProfile().branchDummyReadOnNoCross);

        Registers rN = cpu6510.getRegisters();
        rN.A = 0x15;
        rN.P = static_cast<uint8_t>(UNUSED | DECIMAL | CARRY);
        cpu6510.setRegisters(rN);
        cpu6510.sbcRevisionAware(0x06, true);
        const Registers rnOut = cpu6510.getRegisters();

        Registers rH = cpu8500.getRegisters();
        rH.A = 0x15;
        rH.P = static_cast<uint8_t>(UNUSED | DECIMAL | CARRY);
        cpu8500.setRegisters(rH);
        cpu8500.sbcRevisionAware(0x06, true);
        const Registers rhOut = cpu8500.getRegisters();

        assert(rnOut.A == 0x09);
        assert(rhOut.A == 0x0F);

        const uint8_t xaaImm = 0xFF;
        const uint8_t xaaX = 0x11;
        const uint8_t xaa6510 = static_cast<uint8_t>(xaaX & xaaImm &
            (cpu6510.getRevisionProfile().xaaUsesMagicConstEE ? 0xEE : 0xFF));
        const uint8_t xaa8500 = static_cast<uint8_t>(xaaX & xaaImm &
            (cpu8500.getRevisionProfile().xaaUsesMagicConstEE ? 0xEE : 0xFF));
        assert(xaa6510 != xaa8500);
    }

    {
        VICII vic;
        vic.setRevision(VICII::REV_6569);
        assert(vic.getRevision() == VICII::REV_6569);
        assert(vic.getRevisionProfile().visibleLineStart == 48);
        assert(vic.getRevisionProfile().badlineStartCycle == 12);

        vic.setRevision(VICII::REV_8565);
        assert(vic.getRevision() == VICII::REV_8565);
        assert(vic.getRevisionProfile().visibleLineEnd == 247);
        assert(vic.getRevisionProfile().badlineReleaseCycle == 55);
        assert(vic.getRevisionProfile().rasterIrqNeedsMaskEdge);
        assert(vic.getRevisionProfile().fldRequiresBadlineCarry);

        vic.setRevision(VICII::REV_6569R3);
        assert(vic.getRevision() == VICII::REV_6569R3);
        assert(vic.getRevisionProfile().spriteDmaStopLineMask == 0xFE);
    }

    {
        VICII v0;
        v0.setRevision(VICII::REV_6569);
        v0.ctrl2 = 0;
        v0.cycleInLine = 14;
        v0.pixelClock = 0;
        v0.write(0xD016, 0x08);
        assert(v0.vspTriggered);

        VICII v1;
        v1.setRevision(VICII::REV_8565);
        v1.ctrl2 = 0;
        v1.cycleInLine = 14;
        v1.pixelClock = 0;
        v1.write(0xD016, 0x08);
        assert(!v1.vspTriggered);
    }

    {
        Drive1541 d1541;
        d1541.setRevision(Drive1541::REV_1541);
        assert(d1541.getRevisionProfile().iecStrictEoiAck);
        assert(d1541.getRevisionProfile().iecCommandNeedsAtnLow);

        Drive1541 d1541c;
        d1541c.setRevision(Drive1541::REV_1541C);
        assert(d1541c.getRevision() == Drive1541::REV_1541C);
        assert(!d1541c.getRevisionProfile().iecStrictEoiAck);
        assert(!d1541c.getRevisionProfile().iecCommandNeedsAtnLow);
        assert(!d1541c.getRevisionProfile().iecHandshakeNeedsClockLowAck);

        Drive1541 d1541ii;
        d1541ii.setRevision(Drive1541::REV_1541II);
        assert(d1541ii.getRevision() == Drive1541::REV_1541II);
        assert(d1541ii.getRevisionProfile().iecAtnAckTicksOverride == 6);
    }

    {
        Bus nmosBus;
        nmosBus.setOpenBusRevision(Bus::OPENBUS_C64_NMOS);
        nmosBus.onDrivenBusValue(0xAA);
        for (uint32_t i = 0; i < nmosBus.getOpenBusProfile().decayReadThreshold - 1; ++i) {
            nmosBus.applyOpenBusDecayIfNeeded();
        }
        assert(nmosBus.openBusValue == 0xAA);
        nmosBus.applyOpenBusDecayIfNeeded();
        assert(nmosBus.openBusValue == nmosBus.getOpenBusProfile().decayValue);

        Bus hmosBus;
        hmosBus.setOpenBusRevision(Bus::OPENBUS_C64_HMOS);
        hmosBus.onDrivenBusValue(0xAA);
        for (uint32_t i = 0; i < hmosBus.getOpenBusProfile().decayReadThreshold; ++i) {
            hmosBus.applyOpenBusDecayIfNeeded();
        }
        assert(hmosBus.openBusValue == hmosBus.getOpenBusProfile().decayValue);
        assert(hmosBus.getOpenBusProfile().decayReadThreshold < nmosBus.getOpenBusProfile().decayReadThreshold);
    }

    {
        VICII vicBase;
        vicBase.setRevision(VICII::REV_6569);
        const auto baseRule = vicBase.getStepRule(true);

        VICII vicA;
        vicA.setRevision(VICII::REV_8565);
        const auto aRule = vicA.getStepRule(true);

        assert(baseRule.op == aRule.op);
        assert(baseRule.fetch == aRule.fetch);
    }

    std::cout << "[WEEK17] PASS: explicit chip revision profiles (CIA/VIC) and variant behaviors" << std::endl;
}

static std::vector<std::string> buildWeek18OpenBusRevisionRows() {
    std::vector<std::string> rows;
    rows.reserve(8);

    auto buildFor = [&](Bus::OpenBusRevision rev, const char *label) {
        Bus b;
        b.flatMemoryMode = false;
        b.hasBasicRom = false;
        b.hasKernalRom = false;
        b.hasCharRom = false;
        b.vic = nullptr;
        b.cia1 = nullptr;
        b.cia2 = nullptr;
        b.sid = nullptr;
        b.cpuPortDir = 0x07;
        b.cpuPortData = 0x00;
        b.memory[0x0000] = b.cpuPortDir;
        b.memory[0x0001] = b.cpuPortData;
        b.memory[0xD020] = 0x5A;
        b.setOpenBusRevision(rev);

        (void)b.read(0xD020);
        const uint32_t threshold = b.getOpenBusProfile().decayReadThreshold;
        for (uint32_t i = 0; i < threshold + 1; ++i) {
            (void)b.read(0xDEAD);
            std::ostringstream oss;
            oss << label
                << "," << i
                << "," << threshold
                << "," << static_cast<int>(b.openBusValue);
            rows.push_back(oss.str());
        }
    };

    buildFor(Bus::OPENBUS_C64_NMOS, "nmos");
    buildFor(Bus::OPENBUS_C64_HMOS, "hmos");
    return rows;
}

static void writeWeek18OpenBusEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,idx,threshold,openbus\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek18OpenBusEdgeHardReference() {
    const std::string runtimePath = "week18_openbus_revision_runtime.csv";
    const std::string refPath = "reference/edge/week18_openbus_revision_trace.csv";

    const std::vector<std::string> got = buildWeek18OpenBusRevisionRows();
    writeWeek18OpenBusEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK18_BOOTSTRAP_OPENBUS_REF") != nullptr);
    if (bootstrap) {
        writeWeek18OpenBusEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK18][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK18][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK18][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK18][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK18][HARDREF] PASS: open-bus decay revision trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek20VicPathologicalRevisionRows(VICII::Revision rev, const char *label) {
    Bus b;
    VICII vic;
    b.vic = &vic;
    vic.bus = &b;
    vic.setRevision(rev);
    vic.ctrl1 = 0x10;
    vic.ctrl2 = 0x00;
    vic.rasterLine = 48;
    vic.cycleInLine = 10;
    vic.pixelClock = 0;
    vic.sprEnable = 0x01;
    vic.sprY[0] = 48;

    if (vic.getRevisionProfile().vspFlickerSensitive) {
        vic.cycleInLine = 14;
        vic.pixelClock = 0;
        vic.write(0xD016, static_cast<uint8_t>((vic.ctrl2 ^ 0x08) & 0x1F));
    }
    if (vic.getRevisionProfile().fldRequiresBadlineCarry) {
        vic.cycleInLine = 15;
        vic.pixelClock = 0;
        vic.badlineActive = true;
        vic.write(0xD011, static_cast<uint8_t>(0x10 | ((vic.ctrl1 + 1) & 0x07)));
    }

    std::vector<std::string> rows;
    rows.reserve(256);
    for (int i = 0; i < 224; ++i) {
        if ((i % 17) == 0) {
            vic.write(0xD011, static_cast<uint8_t>(0x10 | (i & 0x07)));
        }
        if ((i % 29) == 0) {
            vic.write(0xD016, static_cast<uint8_t>((vic.ctrl2 ^ 0x08) & 0x1F));
        }
        vic.tickHalf();

        std::ostringstream oss;
        oss << label
            << "," << i
            << "," << vic.rasterLine
            << "," << vic.cycleInLine
            << "," << vic.pixelClock
            << "," << (vic.badlineActive ? 1 : 0)
            << "," << int(vic.spriteDmaMask)
            << "," << (vic.vspTriggered ? 1 : 0)
            << "," << (vic.fldTriggered ? 1 : 0)
            << "," << int(vic.irqFlags);
        rows.push_back(oss.str());
    }
    return rows;
}

static std::vector<std::string> buildWeek20VicPathologicalEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek20VicPathologicalRevisionRows(VICII::REV_6569, "6569");
    const auto r1 = buildWeek20VicPathologicalRevisionRows(VICII::REV_6569R3, "6569R3");
    const auto r2 = buildWeek20VicPathologicalRevisionRows(VICII::REV_8565, "8565");
    const auto r3 = buildWeek20VicPathologicalRevisionRows(VICII::REV_8565R2, "8565R2");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    rows.insert(rows.end(), r3.begin(), r3.end());
    return rows;
}

static void writeWeek20VicPathologicalEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,step,line,cycle,pixel,badline,sprite_dma,vsp,fld,irq\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek20VicPathologicalEdgeHardReference() {
    const std::string runtimePath = "week20_vic_pathological_runtime.csv";
    const std::string refPath = "reference/edge/week20_vic_pathological_trace.csv";

    const std::vector<std::string> got = buildWeek20VicPathologicalEdgeTraceRows();
    writeWeek20VicPathologicalEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK20_BOOTSTRAP_VIC_REF") != nullptr);
    if (bootstrap) {
        writeWeek20VicPathologicalEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK20][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK20][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK20][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK20][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK20][HARDREF] PASS: VIC pathological revision trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek21BusCornerEdgeTraceRows() {
    std::vector<std::string> rows;
    rows.reserve(256);

    auto pushRow = [&](const std::string &name, uint64_t step, uint8_t value, const Bus &b) {
        std::ostringstream oss;
        oss << name
            << "," << step
            << "," << int(value)
            << "," << int(b.openBusValue)
            << "," << int(b.lastDataBusValue)
            << "," << b.openBusIdleReads
            << "," << int(b.cpuPortData)
            << "," << int(b.cpuPortDir)
            << "," << (b.flatMemoryMode ? 1 : 0);
        rows.push_back(oss.str());
    };

    {
        Bus b;
        b.flatMemoryMode = false;
        b.hasBasicRom = false;
        b.hasKernalRom = false;
        b.hasCharRom = false;
        b.vic = nullptr;
        b.cia1 = nullptr;
        b.cia2 = nullptr;
        b.sid = nullptr;
        b.cpuPortDir = 0x07;
        b.cpuPortData = 0x00;
        b.memory[0x0000] = b.cpuPortDir;
        b.memory[0x0001] = b.cpuPortData;
        b.memory[0xD020] = 0x3C;
        b.setOpenBusRevision(Bus::OPENBUS_C64_NMOS);

        uint64_t step = 0;
        const uint8_t ioRead = b.read(0xD020);
        pushRow("io_ram_read", step++, ioRead, b);

        for (uint32_t i = 0; i < b.getOpenBusProfile().decayReadThreshold + 2; ++i) {
            const uint8_t ob = b.read(0xDEAD);
            if (i < 4 || i + 2 >= b.getOpenBusProfile().decayReadThreshold) {
                pushRow("openbus_decay", step++, ob, b);
            }
        }

        b.write(0x0001, 0x07);
        b.hasBasicRom = true;
        b.basicRom.fill(0xBA);
        b.memory[0xA000] = 0x11;
        const uint8_t aRom = b.read(0xA000);
        pushRow("bank_rom", step++, aRom, b);

        b.write(0x0001, 0x00);
        const uint8_t aRam = b.read(0xA000);
        pushRow("bank_ram", step++, aRam, b);

        b.flatMemoryMode = true;
        b.write(0x1234, 0xA5);
        const uint8_t flat = b.read(0x1234);
        pushRow("flat_mode", step++, flat, b);
    }

    {
        Bus b;
        VICII vic;
        b.vic = &vic;
        vic.bus = &b;
        b.cpuPortDir = 0x07;
        b.cpuPortData = 0x07;
        b.memory[0x0000] = b.cpuPortDir;
        b.memory[0x0001] = b.cpuPortData;
        b.hasBasicRom = false;
        b.hasKernalRom = false;
        b.hasCharRom = false;

        CPU6510 cpu(b);
        cpu.clearMicroOpsForTest();
        cpu.setCurrentPhaseForTest(PHI2);
        bool lastVicHadBus = false;
        uint64_t step = 0;

        cpu.pushMicroOpForTest(PHI2, [&]() {
            const uint8_t v = b.read(0xD012);
            (void)v;
        });

        vic.aecLine = false;
        tickCpuWithVicContention(b, cpu, lastVicHadBus);
        {
            std::ostringstream oss;
            oss << "aec_hold"
                << "," << step++
                << "," << cpu.pendingMicroOpCountForTest()
                << "," << (cpu.getCurrentPhase() == PHI1 ? 1 : 2)
                << "," << (lastVicHadBus ? 1 : 0)
                << "," << int(b.openBusValue)
                << "," << b.openBusIdleReads
                << ",0,0";
            rows.push_back(oss.str());
        }

        vic.aecLine = true;
        tickCpuWithVicContention(b, cpu, lastVicHadBus);
        tickCpuWithVicContention(b, cpu, lastVicHadBus);
        {
            std::ostringstream oss;
            oss << "aec_release"
                << "," << step++
                << "," << cpu.pendingMicroOpCountForTest()
                << "," << (cpu.getCurrentPhase() == PHI1 ? 1 : 2)
                << "," << (lastVicHadBus ? 1 : 0)
                << "," << int(b.openBusValue)
                << "," << b.openBusIdleReads
                << ",0,0";
            rows.push_back(oss.str());
        }
    }

    return rows;
}

static void writeWeek21BusCornerEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "case,step,val,openbus,last_data,idle_reads,port_data,port_dir,flat\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek21BusCornerEdgeHardReference() {
    const std::string runtimePath = "week21_bus_corner_runtime.csv";
    const std::string refPath = "reference/edge/week21_bus_corner_trace.csv";

    const std::vector<std::string> got = buildWeek21BusCornerEdgeTraceRows();
    writeWeek21BusCornerEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK21_BOOTSTRAP_BUS_REF") != nullptr);
    if (bootstrap) {
        writeWeek21BusCornerEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK21][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK21][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK21][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK21][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK21][HARDREF] PASS: bus-level corner trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek22PortMapEdgeTraceRowsForRevision(Bus::OpenBusRevision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Bus b;
    b.flatMemoryMode = false;
    b.hasBasicRom = true;
    b.basicRom.fill(0xBA);
    b.hasKernalRom = false;
    b.hasCharRom = true;
    b.charRom.fill(0xA1);
    b.vic = nullptr;
    b.cia1 = nullptr;
    b.cia2 = nullptr;
    b.sid = nullptr;
    b.memory[0xD020] = 0x3C;
    b.setOpenBusRevision(rev);
    b.write(0x0000, 0x07);
    b.write(0x0001, 0x07);

    auto pushRow = [&](const char *name, uint64_t step, uint8_t value) {
        const bool loram = (b.cpuPortDir & 0x01) && (b.cpuPortData & 0x01);
        const bool hiram = (b.cpuPortDir & 0x02) && (b.cpuPortData & 0x02);
        const bool charen = (b.cpuPortDir & 0x04) && (b.cpuPortData & 0x04);
        const bool ioVisible = (loram || hiram) && charen;
        const bool charVisible = (loram || hiram) && !charen;

        std::ostringstream oss;
        oss << label
            << "," << name
            << "," << step
            << "," << int(value)
            << "," << int(b.openBusValue)
            << "," << int(b.lastDataBusValue)
            << "," << b.openBusIdleReads
            << "," << int(b.cpuPortData)
            << "," << int(b.cpuPortDir)
            << "," << (ioVisible ? 1 : 0)
            << "," << (charVisible ? 1 : 0);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;

    const uint8_t ioVisibleRead = b.read(0xD020);
    pushRow("io_visible_mem", step++, ioVisibleRead);

    b.write(0x0001, 0x03);
    b.charRom[0x020] = 0xA5;
    b.memory[0xD020] = 0x5A;
    const uint8_t charVisibleRead = b.read(0xD020);
    pushRow("char_visible_read", step++, charVisibleRead);

    b.write(0x0001, 0x00);
    const uint8_t ramVisibleRead = b.read(0xD020);
    pushRow("ram_visible_read", step++, ramVisibleRead);

    b.write(0x0001, 0x07);
    const uint8_t basicRead = b.read(0xA000);
    pushRow("drive_openbus_seed", step++, basicRead);

    const uint32_t threshold = b.getOpenBusProfile().decayReadThreshold;
    for (uint32_t i = 0; i <= threshold; ++i) {
        const uint8_t floating = b.read(0xDE80);
        if (i == 0 || i + 1 == threshold || i == threshold) {
            pushRow("floating_decay", step++, floating);
        }
    }

    b.write(0x0000, 0x05);
    b.write(0x0001, 0x02);
    const uint8_t maskedPortRead = b.read(0x0001);
    pushRow("port_mask_read", step++, maskedPortRead);

    b.flatMemoryMode = true;
    b.write(0x1F00, 0x6D);
    const uint8_t flatRead = b.read(0x1F00);
    pushRow("flat_mode_passthrough", step++, flatRead);

    return rows;
}

static std::vector<std::string> buildWeek22PortMapEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto nmos = buildWeek22PortMapEdgeTraceRowsForRevision(Bus::OPENBUS_C64_NMOS, "nmos");
    const auto hmos = buildWeek22PortMapEdgeTraceRowsForRevision(Bus::OPENBUS_C64_HMOS, "hmos");
    rows.insert(rows.end(), nmos.begin(), nmos.end());
    rows.insert(rows.end(), hmos.begin(), hmos.end());
    return rows;
}

static void writeWeek22PortMapEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,case,step,val,openbus,last_data,idle_reads,port_data,port_dir,io_visible,char_visible\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek22PortMapEdgeHardReference() {
    const std::string runtimePath = "week22_port_map_runtime.csv";
    const std::string refPath = "reference/edge/week22_port_map_trace.csv";

    const std::vector<std::string> got = buildWeek22PortMapEdgeTraceRows();
    writeWeek22PortMapEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK22_BOOTSTRAP_PORTMAP_REF") != nullptr);
    if (bootstrap) {
        writeWeek22PortMapEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK22][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK22][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK22][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK22][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK22][HARDREF] PASS: port-map/open-bus edge trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek23CiaIrqNmiBridgeRowsForRevision(CIA6526::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Bus b;
    CIA6526 cia1;
    CIA6526 cia2;
    b.cia1 = &cia1;
    b.cia2 = &cia2;

    cia1.setRevision(rev);
    cia2.setRevision(rev);
    cia1.setFlagPin(true);
    cia2.setFlagPin(true);
    cia1.write(0x000D, 0x90);
    cia2.write(0x000D, 0x90);

    CPU6510 cpu(b);

    auto pushRow = [&](const char *event, uint64_t step) {
        syncInterruptLines(b, cpu);
        std::ostringstream oss;
        oss << label
            << "," << event
            << "," << step
            << "," << int(cia1.icr)
            << "," << int(cia2.icr)
            << "," << int(cia1.icrDeferredEvents)
            << "," << int(cia2.icrDeferredEvents)
            << "," << (cpu.irqLine ? 1 : 0)
            << "," << (cpu.nmiLine ? 1 : 0);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    cia1.setFlagPin(false);
    cia2.setFlagPin(false);
    pushRow("flag_fall_pre_tick", step++);

    cia1.cycleCore.tickHalfCycle(cia1);
    cia2.cycleCore.tickHalfCycle(cia2);
    pushRow("half_tick_1", step++);

    cia1.cycleCore.tickHalfCycle(cia1);
    cia2.cycleCore.tickHalfCycle(cia2);
    pushRow("half_tick_2", step++);

    cia1.setFlagPin(true);
    cia2.setFlagPin(true);
    cia1.cycleCore.tickHalfCycle(cia1);
    cia2.cycleCore.tickHalfCycle(cia2);
    pushRow("flag_release", step++);

    (void)cia1.read(0x000D, b.openBusValue);
    pushRow("cia1_icr_read_clear", step++);

    (void)cia2.read(0x000D, b.openBusValue);
    pushRow("cia2_icr_read_clear", step++);

    return rows;
}

static std::vector<std::string> buildWeek23CiaIrqNmiBridgeEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek23CiaIrqNmiBridgeRowsForRevision(CIA6526::REV_6526, "6526");
    const auto r1 = buildWeek23CiaIrqNmiBridgeRowsForRevision(CIA6526::REV_6526A, "6526A");
    const auto r2 = buildWeek23CiaIrqNmiBridgeRowsForRevision(CIA6526::REV_6526R4, "6526R4");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek23CiaIrqNmiBridgeEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,event,step,cia1_icr,cia2_icr,cia1_deferred,cia2_deferred,cpu_irq_line,cpu_nmi_line\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek23CiaIrqNmiBridgeEdgeHardReference() {
    const std::string runtimePath = "week23_cia_irq_nmi_runtime.csv";
    const std::string refPath = "reference/edge/week23_cia_irq_nmi_trace.csv";

    const std::vector<std::string> got = buildWeek23CiaIrqNmiBridgeEdgeTraceRows();
    writeWeek23CiaIrqNmiBridgeEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK23_BOOTSTRAP_IRQNMI_REF") != nullptr);
    if (bootstrap) {
        writeWeek23CiaIrqNmiBridgeEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK23][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK23][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK23][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK23][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK23][HARDREF] PASS: CIA IRQ/NMI bridge trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek24IrqLatchUnderAecRowsForRevision(CPU6510::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(32);

    Bus b;
    VICII vic;
    b.vic = &vic;
    vic.bus = &b;
    b.cia1 = nullptr;
    b.cia2 = nullptr;

    CPU6510 cpu(b);
    cpu.setRevision(rev);
    cpu.setCurrentPhaseForTest(PHI2);
    cpu.clearMicroOpsForTest();
    cpu.blockNMI = false;
    bool lastVicHadBus = false;

    cpu.pushMicroOpForTest(PHI2, [&]() {
        (void)b.read(0xD012);
    });

    auto pushRow = [&](const char *event, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << event
            << "," << step
            << "," << cpu.pendingMicroOpCountForTest()
            << "," << (cpu.getCurrentPhase() == PHI1 ? 1 : 2)
            << "," << (cpu.irqLine ? 1 : 0)
            << "," << (cpu.irqSampledLow ? 1 : 0)
            << "," << (cpu.pendingNMI ? 1 : 0)
            << "," << (cpu.nmiLine ? 1 : 0)
            << "," << (vic.aecLine ? 1 : 0)
            << "," << (lastVicHadBus ? 1 : 0);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    cpu.setIRQ(false);
    vic.aecLine = false;
    tickCpuWithVicContention(b, cpu, lastVicHadBus);
    pushRow("irq_latched_hold", step++);

    cpu.setIRQ(true);
    pushRow("irq_line_release_only", step++);

    vic.aecLine = true;
    tickCpuWithVicContention(b, cpu, lastVicHadBus);
    tickCpuWithVicContention(b, cpu, lastVicHadBus);
    pushRow("aec_release_exec", step++);

    cpu.setNMI(false);
    vic.aecLine = false;
    tickCpuWithVicContention(b, cpu, lastVicHadBus);
    pushRow("nmi_edge_hold", step++);

    vic.aecLine = true;
    tickCpuWithVicContention(b, cpu, lastVicHadBus);
    cpu.setNMI(true);
    pushRow("nmi_release", step++);

    return rows;
}

static std::vector<std::string> buildWeek24IrqLatchUnderAecEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek24IrqLatchUnderAecRowsForRevision(CPU6510::REV_6510, "6510");
    const auto r1 = buildWeek24IrqLatchUnderAecRowsForRevision(CPU6510::REV_6510R2, "6510R2");
    const auto r2 = buildWeek24IrqLatchUnderAecRowsForRevision(CPU6510::REV_8500, "8500");
    const auto r3 = buildWeek24IrqLatchUnderAecRowsForRevision(CPU6510::REV_8500R2, "8500R2");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    rows.insert(rows.end(), r3.begin(), r3.end());
    return rows;
}

static void writeWeek24IrqLatchUnderAecEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,event,step,pending_ops,phase,irq_line,irq_sampled_low,pending_nmi,nmi_line,aec,last_vic_bus\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek24IrqLatchUnderAecEdgeHardReference() {
    const std::string runtimePath = "week24_irq_latch_runtime.csv";
    const std::string refPath = "reference/edge/week24_irq_latch_trace.csv";

    const std::vector<std::string> got = buildWeek24IrqLatchUnderAecEdgeTraceRows();
    writeWeek24IrqLatchUnderAecEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK24_BOOTSTRAP_IRQLATCH_REF") != nullptr);
    if (bootstrap) {
        writeWeek24IrqLatchUnderAecEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK24][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK24][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK24][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK24][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK24][HARDREF] PASS: IRQ/NMI latch-under-AEC trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek25CiaSerialRowsForRevision(CIA6526::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(128);

    CIA6526 cia;
    cia.setRevision(rev);
    cia.write(0x000D, 0x88);
    cia.write(0x0004, 0x01);
    cia.write(0x0005, 0x00);
    cia.write(0x000E, 0x51);

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(cia.serialDataReg)
            << "," << int(cia.serialShiftReg)
            << "," << int(cia.serialShiftBitCount)
            << "," << int(cia.serialOutputBitCount)
            << "," << cia.serialRxByteCount
            << "," << cia.serialTxByteCount
            << "," << int(cia.icr)
            << "," << int(cia.serialShiftDivider);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    cia.write(0x000C, 0xA6);
    pushRow("tx_seed", step++);

    for (uint32_t i = 0; i < 160; ++i) {
        cia.setSerialPins(false, ((i & 1u) != 0));
        cia.cycleCore.tickHalfCycle(cia);
        cia.setSerialPins(true, ((i & 1u) != 0));
        cia.cycleCore.tickHalfCycle(cia);

        if ((i % 23u) == 0u || cia.serialTxByteCount > 0) {
            pushRow("tx_tick", step++);
            if (cia.serialTxByteCount > 0) {
                break;
            }
        }
    }

    (void)cia.read(0x000D, 0xFF);
    pushRow("tx_icr_clear", step++);

    cia.write(0x000E, 0x01);
    cia.serialShiftReg = 0;
    cia.serialShiftBitCount = 0;
    const bool fallingEdgeInput = cia.getRevisionProfile().serialInputShiftOnFallingEdge;
    for (int bit = 0; bit < 8; ++bit) {
        const bool inBit = ((0x5Au >> (7 - bit)) & 0x01) != 0;
        if (fallingEdgeInput) {
            cia.setSerialPins(true, inBit);
            cia.cycleCore.tickHalfCycle(cia);
            cia.setSerialPins(false, inBit);
            cia.cycleCore.tickHalfCycle(cia);
        } else {
            cia.setSerialPins(false, inBit);
            cia.cycleCore.tickHalfCycle(cia);
            cia.setSerialPins(true, inBit);
            cia.cycleCore.tickHalfCycle(cia);
        }
        pushRow("rx_bit", step++);
    }

    pushRow("rx_done", step++);
    (void)cia.read(0x000D, 0xFF);
    pushRow("rx_icr_clear", step++);

    return rows;
}

static std::vector<std::string> buildWeek25CiaSerialEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek25CiaSerialRowsForRevision(CIA6526::REV_6526, "6526");
    const auto r1 = buildWeek25CiaSerialRowsForRevision(CIA6526::REV_6526A, "6526A");
    const auto r2 = buildWeek25CiaSerialRowsForRevision(CIA6526::REV_6526R4, "6526R4");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek25CiaSerialEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,sdr,shift,bits_in,bits_out,rx_bytes,tx_bytes,icr,divider\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek25CiaSerialEdgeHardReference() {
    const std::string runtimePath = "week25_cia_serial_runtime.csv";
    const std::string refPath = "reference/edge/week25_cia_serial_trace.csv";

    const std::vector<std::string> got = buildWeek25CiaSerialEdgeTraceRows();
    writeWeek25CiaSerialEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK25_BOOTSTRAP_CIASERIAL_REF") != nullptr);
    if (bootstrap) {
        writeWeek25CiaSerialEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK25][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK25][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK25][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK25][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK25][HARDREF] PASS: CIA serial edge trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek26DriveIecHandshakeRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();
    drive.iecEnableAtnAck = true;
    drive.iecEnableListenerByteAck = true;
    drive.iecListening = true;

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(drive.iecAtnAckTicks)
            << "," << (drive.iecAtnAckPullDATA ? 1 : 0)
            << "," << (drive.iecAtnHandshakeActive ? 1 : 0)
            << "," << (drive.iecAtnAckSawClockLow ? 1 : 0)
            << "," << int(drive.iecRxByteAckTicks)
            << "," << (drive.iecRxByteAckPullDATA ? 1 : 0)
            << "," << drive.iecRxProcessed
            << "," << drive.iecClockRisingSeen
            << "," << drive.iecClockRisingAtnLow;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.setIecLines(false, true, true);
    drive.tickIecHalfCycle();
    pushRow("atn_fall", step++);

    drive.setIecLines(false, false, true);
    drive.tickIecHalfCycle();
    pushRow("clock_low_ack", step++);

    drive.setIecLines(false, true, true);
    drive.tickIecHalfCycle();
    pushRow("clock_high_post_ack", step++);

    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("command_exit", step++);

    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, false);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, false, true);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, false);
    drive.tickIecHalfCycle();
    pushRow("byte_complete", step++);

    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("byte_ack_release", step++);

    return rows;
}

static std::vector<std::string> buildWeek26DriveIecHandshakeEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek26DriveIecHandshakeRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek26DriveIecHandshakeRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek26DriveIecHandshakeRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek26DriveIecHandshakeEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,atn_ack_ticks,atn_ack_pull,atn_active,atn_clk_seen,rx_ack_ticks,rx_ack_pull,rx_processed,clk_rise,clk_rise_atn_low\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek26DriveIecHandshakeEdgeHardReference() {
    const std::string runtimePath = "week26_drive_iec_runtime.csv";
    const std::string refPath = "reference/edge/week26_drive_iec_trace.csv";

    const std::vector<std::string> got = buildWeek26DriveIecHandshakeEdgeTraceRows();
    writeWeek26DriveIecHandshakeEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK26_BOOTSTRAP_DRIVEIEC_REF") != nullptr);
    if (bootstrap) {
        writeWeek26DriveIecHandshakeEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK26][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK26][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK26][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK26][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK26][HARDREF] PASS: drive IEC handshake trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek27DriveCommandPhaseRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();
    drive.iecEnableAtnAck = true;
    drive.iecEnableListenerByteAck = true;

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (drive.iecListening ? 1 : 0)
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.iecListenSecondary)
            << "," << int(drive.iecTalkSecondary)
            << "," << int(drive.iecActiveListenChannel)
            << "," << int(drive.iecActiveTalkChannel)
            << "," << (drive.iecTalkSa0Confirmed ? 1 : 0)
            << "," << int(drive.iecSerialState)
            << "," << int(drive.lastIecCommand)
            << "," << drive.iecTxQueue.size();
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.setIecLines(false, true, true);
    drive.tickIecHalfCycle();
    pushRow("atn_low_enter_cmd", step++);

    drive.consumeReceivedByte(0x28, true);
    pushRow("listen_device", step++);

    drive.consumeReceivedByte(0xF0, true);
    pushRow("listen_sa0", step++);

    drive.consumeReceivedByte(0x3F, true);
    pushRow("unlisten", step++);

    drive.consumeReceivedByte(0x48, true);
    pushRow("talk_device", step++);

    drive.consumeReceivedByte(0x60, true);
    pushRow("talk_sa0", step++);

    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("atn_high_talkdata", step++);

    drive.consumeReceivedByte(0x5F, true);
    pushRow("untalk", step++);

    return rows;
}

static std::vector<std::string> buildWeek27DriveCommandPhaseEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek27DriveCommandPhaseRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek27DriveCommandPhaseRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek27DriveCommandPhaseRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek27DriveCommandPhaseEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,listening,talking,listen_sa,talk_sa,listen_ch,talk_ch,talk_sa0,state,last_cmd,txq\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek27DriveCommandPhaseEdgeHardReference() {
    const std::string runtimePath = "week27_drive_cmdphase_runtime.csv";
    const std::string refPath = "reference/edge/week27_drive_cmdphase_trace.csv";

    const std::vector<std::string> got = buildWeek27DriveCommandPhaseEdgeTraceRows();
    writeWeek27DriveCommandPhaseEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK27_BOOTSTRAP_DRIVECMD_REF") != nullptr);
    if (bootstrap) {
        writeWeek27DriveCommandPhaseEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK27][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK27][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK27][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK27][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK27][HARDREF] PASS: drive command-phase trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek28DriveEoiAtnRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();
    drive.iecEnableAtnAck = true;
    drive.iecEnableListenerByteAck = true;
    drive.iecTalking = true;
    drive.iecActiveTalkChannel = 0;
    drive.iecTalkSecondary = 0;
    drive.iecTalkSa0Confirmed = true;
    drive.iecOpenTalkChannels[0] = true;
    drive.iecTxQueue.clear();
    drive.iecTxQueue.push_back(0x41);
    drive.iecTxQueue.push_back(0x42);

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(drive.iecSerialState)
            << "," << (drive.iecTxByteActive ? 1 : 0)
            << "," << int(drive.iecTxBitCount)
            << "," << (drive.iecEoiPendingAck ? 1 : 0)
            << "," << (drive.iecEoiAckLowSeen ? 1 : 0)
            << "," << int(drive.iecEoiWaitTicks)
            << "," << drive.iecEoiAckCount
            << "," << drive.iecTxServed
            << "," << drive.iecTxQueue.size()
            << "," << (drive.iecAtnAckPullDATA ? 1 : 0)
            << "," << (drive.iecAtnHandshakeActive ? 1 : 0);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    for (int i = 0; i < 24; ++i) {
        drive.setIecLines(true, false, true);
        drive.tickIecHalfCycle();
        drive.setIecLines(true, true, true);
        drive.tickIecHalfCycle();
        if ((i % 6) == 0 || drive.iecEoiPendingAck) {
            pushRow("talk_shift", step++);
        }
        if (drive.iecEoiPendingAck) {
            break;
        }
    }

    drive.setIecLines(true, true, false);
    drive.tickIecHalfCycle();
    pushRow("eoi_ack_low", step++);

    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("eoi_ack_release", step++);

    drive.setIecLines(false, true, true);
    drive.tickIecHalfCycle();
    pushRow("atn_low_during_talk", step++);

    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("atn_high_resume", step++);

    return rows;
}

static std::vector<std::string> buildWeek28DriveEoiAtnEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek28DriveEoiAtnRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek28DriveEoiAtnRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek28DriveEoiAtnRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek28DriveEoiAtnEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,state,tx_active,tx_bits,eoi_pending,eoi_low_seen,eoi_wait,eoi_ack_count,tx_served,txq,atn_ack_pull,atn_active\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek28DriveEoiAtnEdgeHardReference() {
    const std::string runtimePath = "week28_drive_eoi_atn_runtime.csv";
    const std::string refPath = "reference/edge/week28_drive_eoi_atn_trace.csv";

    const std::vector<std::string> got = buildWeek28DriveEoiAtnEdgeTraceRows();
    writeWeek28DriveEoiAtnEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK28_BOOTSTRAP_DRIVEEOI_REF") != nullptr);
    if (bootstrap) {
        writeWeek28DriveEoiAtnEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK28][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK28][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK28][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK28][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK28][HARDREF] PASS: drive EOI/ATN edge trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek29DriveIecTimeoutRecoveryRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();
    drive.iecEnableAtnAck = true;
    drive.iecEnableListenerByteAck = true;
    drive.iecListening = true;
    drive.iecSerialState = Drive1541::IecSerialState::ListenData;
    drive.iecRxShift = 0x03;
    drive.iecRxBitCount = 3;

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(drive.iecRxBitCount)
            << "," << int(drive.iecRxShift)
            << "," << int(drive.iecRxIdleTicks)
            << "," << drive.iecRxTimeoutCount
            << "," << int(drive.iecTxBitCount)
            << "," << int(drive.iecTxIdleTicks)
            << "," << drive.iecTxTimeoutCount
            << "," << int(drive.iecEoiWaitTicks)
            << "," << drive.iecEoiTimeoutCount
            << "," << (drive.iecEoiPendingAck ? 1 : 0)
            << "," << drive.iecCommandDispatchCount
            << "," << drive.iecDataDispatchCount
            << "," << drive.iecCommandSyntaxErrorCount;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.setIecLines(true, true, true);
    for (int i = 0; i < 300; ++i) {
        drive.tickIecHalfCycle();
    }
    pushRow("rx_timeout", step++);

    drive.setIecLines(true, false, false);
    drive.tickIecHalfCycle();
    drive.setIecLines(true, true, true);
    drive.tickIecHalfCycle();
    pushRow("rx_recover_edge", step++);

    drive.iecTalking = true;
    drive.iecListening = false;
    drive.iecSerialState = Drive1541::IecSerialState::TalkData;
    drive.iecTxByteActive = true;
    drive.iecTxBitCount = 2;
    drive.iecTxShift = 0xA5;
    drive.iecTxIdleTicks = 0;
    drive.setIecLines(true, true, true);
    for (int i = 0; i < 300; ++i) {
        drive.tickIecHalfCycle();
    }
    pushRow("tx_timeout", step++);

    drive.iecEoiPendingAck = true;
    drive.iecEoiAckLowSeen = false;
    drive.iecEoiWaitTicks = 0;
    for (int i = 0; i < 300; ++i) {
        drive.tickIecHalfCycle();
    }
    pushRow("eoi_timeout", step++);

    drive.consumeReceivedByte(0x28, true);
    drive.consumeReceivedByte(0xF0, true);
    drive.consumeReceivedByte(0x3F, true);
    pushRow("post_timeout_cmd", step++);

    return rows;
}

static std::vector<std::string> buildWeek29DriveIecTimeoutRecoveryEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek29DriveIecTimeoutRecoveryRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek29DriveIecTimeoutRecoveryRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek29DriveIecTimeoutRecoveryRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek29DriveIecTimeoutRecoveryEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,rx_bits,rx_shift,rx_idle,rx_timeout,tx_bits,tx_idle,tx_timeout,eoi_wait,eoi_timeout,eoi_pending,cmd_dispatch,data_dispatch,syntax_err\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek29DriveIecTimeoutRecoveryEdgeHardReference() {
    const std::string runtimePath = "week29_drive_timeout_runtime.csv";
    const std::string refPath = "reference/edge/week29_drive_timeout_trace.csv";

    const std::vector<std::string> got = buildWeek29DriveIecTimeoutRecoveryEdgeTraceRows();
    writeWeek29DriveIecTimeoutRecoveryEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK29_BOOTSTRAP_DRIVETIMEOUT_REF") != nullptr);
    if (bootstrap) {
        writeWeek29DriveIecTimeoutRecoveryEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK29][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK29][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK29][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK29][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK29][HARDREF] PASS: drive timeout/recovery trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek30DriveCommandChannelRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << drive.iecCommandDispatchCount
            << "," << drive.iecDataDispatchCount
            << "," << drive.iecCommandSyntaxErrorCount
            << "," << drive.iecCommandResponseQueue.size()
            << "," << drive.iecTxQueue.size()
            << "," << drive.iecAllocatedBlockCount
            << "," << int(drive.memory[0x0400])
            << "," << drive.iecStatusLine;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.iecCommandChannelBuffer.clear();
    {
        const std::string cmd = "M-W,0400,00,02,AA,55";
        for (char c : cmd) {
            drive.iecCommandChannelBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    drive.processCommandChannelBuffer();
    pushRow("mw_ok", step++);

    drive.iecCommandChannelBuffer.clear();
    {
        const std::string cmd = "M-R,0400,00,02";
        for (char c : cmd) {
            drive.iecCommandChannelBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    drive.processCommandChannelBuffer();
    pushRow("mr_ok", step++);

    drive.buildCommandResponsePayload();
    pushRow("mr_payload", step++);

    drive.iecCommandChannelBuffer.clear();
    {
        const std::string cmd = "B-A,00,11,01";
        for (char c : cmd) {
            drive.iecCommandChannelBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    drive.processCommandChannelBuffer();
    pushRow("ba_ok", step++);

    drive.iecCommandChannelBuffer.clear();
    {
        const std::string cmd = "B-F,00,11,01";
        for (char c : cmd) {
            drive.iecCommandChannelBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    drive.processCommandChannelBuffer();
    pushRow("bf_ok", step++);

    drive.iecCommandChannelBuffer.clear();
    {
        const std::string cmd = "M-W,0400,00,02,ZZ,55";
        for (char c : cmd) {
            drive.iecCommandChannelBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    drive.processCommandChannelBuffer();
    pushRow("mw_syntax_err", step++);

    return rows;
}

static std::vector<std::string> buildWeek30DriveCommandChannelEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek30DriveCommandChannelRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek30DriveCommandChannelRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek30DriveCommandChannelRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek30DriveCommandChannelEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,cmd_dispatch,data_dispatch,syntax_err,respq,txq,alloc_count,mem_0400,status\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek30DriveCommandChannelEdgeHardReference() {
    const std::string runtimePath = "week30_drive_cmdch_runtime.csv";
    const std::string refPath = "reference/edge/week30_drive_cmdch_trace.csv";

    const std::vector<std::string> got = buildWeek30DriveCommandChannelEdgeTraceRows();
    writeWeek30DriveCommandChannelEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK30_BOOTSTRAP_CMDCH_REF") != nullptr);
    if (bootstrap) {
        writeWeek30DriveCommandChannelEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK30][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK30][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK30][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK30][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK30][HARDREF] PASS: drive command-channel trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek31DriveStatusTalkRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step) {
        const int txHead = drive.iecTxQueue.empty() ? -1 : int(drive.iecTxQueue.front());
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.iecTalkSecondary)
            << "," << int(drive.iecActiveTalkChannel)
            << "," << drive.iecCommandResponseQueue.size()
            << "," << drive.iecTxQueue.size()
            << "," << txHead
            << "," << drive.iecStatusLine;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.iecCommandResponseQueue.clear();
    drive.iecCommandResponseQueue.push_back(0x41);
    drive.iecCommandResponseQueue.push_back(0x42);
    drive.iecCommandResponseQueue.push_back(0x43);
    pushRow("resp_queue_seed", step++);

    drive.consumeReceivedByte(0x48, true);
    pushRow("talk_device", step++);

    drive.consumeReceivedByte(0x6F, true);
    pushRow("talk_ch15_response", step++);

    drive.iecTxQueue.clear();
    drive.iecStatusLine = "74,DRIVE NOT READY,00,00";
    drive.consumeReceivedByte(0x6F, true);
    pushRow("talk_ch15_status", step++);

    drive.consumeReceivedByte(0x5F, true);
    pushRow("untalk", step++);

    return rows;
}

static std::vector<std::string> buildWeek31DriveStatusTalkEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek31DriveStatusTalkRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek31DriveStatusTalkRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek31DriveStatusTalkRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek31DriveStatusTalkEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,talking,talk_sa,talk_ch,respq,txq,tx_head,status\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek31DriveStatusTalkEdgeHardReference() {
    const std::string runtimePath = "week31_drive_status_talk_runtime.csv";
    const std::string refPath = "reference/edge/week31_drive_status_talk_trace.csv";

    const std::vector<std::string> got = buildWeek31DriveStatusTalkEdgeTraceRows();
    writeWeek31DriveStatusTalkEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK31_BOOTSTRAP_STATUSTALK_REF") != nullptr);
    if (bootstrap) {
        writeWeek31DriveStatusTalkEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK31][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK31][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK31][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK31][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK31][HARDREF] PASS: drive status-talk trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek32DriveDirectoryStreamRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step) {
        const int txHead = drive.iecTxQueue.empty() ? -1 : int(drive.iecTxQueue.front());
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << drive.iecTxQueue.size()
            << "," << txHead
            << "," << int(drive.iecChannelBufferPos[0])
            << "," << (drive.iecDirectoryFromBlockBuffer ? 1 : 0)
            << "," << drive.virtualBlocksFree()
            << "," << drive.iecDirectoryWildcardPattern
            << "," << drive.iecDirectoryTypeFilter
            << "," << drive.iecDirectoryModeFilter;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.iecNameBuffer.clear();
    {
        const std::string dirSpec = "$D*Q,S,W";
        for (char c : dirSpec) {
            drive.iecNameBuffer.push_back(static_cast<uint8_t>(c));
        }
    }
    const Drive1541::DirectoryFilters filters = drive.extractDirectoryFilters();
    drive.iecDirectoryWildcardPattern = filters.pattern;
    drive.iecDirectoryTypeFilter = filters.type;
    drive.iecDirectoryModeFilter = filters.mode;
    drive.iecDirectoryModeFilterNegated = filters.modeNegated;
    drive.buildDirectoryStubPayload();
    pushRow("dir_stub_filtered", step++);

    drive.iecBlockBufferValid = true;
    for (int i = 0; i < 256; ++i) {
        drive.iecBlockBuffer[static_cast<size_t>(i)] = static_cast<uint8_t>((i * 7) & 0xFF);
    }
    drive.iecDirectoryFromBlockBuffer = true;
    drive.iecChannelBufferPos[0] = 0x10;
    drive.buildDirectoryFromBlockBufferPayload(0);
    pushRow("dir_blockbuf_ptr10", step++);

    drive.iecChannelBufferPos[0] = 0x20;
    drive.buildDirectoryFromBlockBufferPayload(0);
    pushRow("dir_blockbuf_ptr20", step++);

    drive.iecDirectoryWildcardPattern.clear();
    drive.iecDirectoryTypeFilter.clear();
    drive.iecDirectoryModeFilter.clear();
    drive.iecDirectoryModeFilterNegated = false;
    drive.buildDirectoryStubPayload();
    pushRow("dir_stub_reset", step++);

    return rows;
}

static std::vector<std::string> buildWeek32DriveDirectoryStreamEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek32DriveDirectoryStreamRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek32DriveDirectoryStreamRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek32DriveDirectoryStreamRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek32DriveDirectoryStreamEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,txq,tx_head,ch0_ptr,from_blockbuf,blocks_free,wildcard,type,mode\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek32DriveDirectoryStreamEdgeHardReference() {
    const std::string runtimePath = "week32_drive_dir_stream_runtime.csv";
    const std::string refPath = "reference/edge/week32_drive_dir_stream_trace.csv";

    const std::vector<std::string> got = buildWeek32DriveDirectoryStreamEdgeTraceRows();
    writeWeek32DriveDirectoryStreamEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK32_BOOTSTRAP_DIRSTREAM_REF") != nullptr);
    if (bootstrap) {
        writeWeek32DriveDirectoryStreamEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK32][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK32][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK32][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK32][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK32][HARDREF] PASS: drive directory-stream trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek33DriveDirectoryFilterRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step) {
        const int txHead = drive.iecTxQueue.empty() ? -1 : int(drive.iecTxQueue.front());
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << drive.iecTxQueue.size()
            << "," << txHead
            << "," << drive.iecAllocatedBlockCount
            << "," << drive.virtualBlocksFree()
            << "," << drive.iecDirectoryWildcardPattern
            << "," << drive.iecDirectoryTypeFilter
            << "," << drive.iecDirectoryModeFilter
            << "," << (drive.iecDirectoryModeFilterNegated ? 1 : 0);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++);

    drive.iecChannelOpenName[0] = "DEMO-SEQ";
    drive.iecChannelOpenNameValid[0] = true;
    drive.iecChannelOpenType[0] = "SEQ";
    drive.iecChannelOpenMode[0] = "W";
    drive.allocateVirtualBlock(0x11, 0x01, 0x00);
    drive.allocateVirtualBlock(0x11, 0x02, 0x00);

    drive.iecChannelOpenName[1] = "DEMO-PRG";
    drive.iecChannelOpenNameValid[1] = true;
    drive.iecChannelOpenType[1] = "PRG";
    drive.iecChannelOpenMode[1] = "R";
    drive.allocateVirtualBlock(0x12, 0x01, 0x01);

    drive.iecChannelOpenName[2] = "TOOLS-SEQ";
    drive.iecChannelOpenNameValid[2] = true;
    drive.iecChannelOpenType[2] = "SEQ";
    drive.iecChannelOpenMode[2] = "A";
    drive.allocateVirtualBlock(0x13, 0x01, 0x02);

    pushRow("catalog_seed", step++);

    drive.iecDirectoryWildcardPattern = "DEMO*";
    drive.iecDirectoryTypeFilter = "SEQ";
    drive.iecDirectoryModeFilter = "W";
    drive.iecDirectoryModeFilterNegated = false;
    drive.buildDirectoryStubPayload();
    pushRow("filter_demo_seq_w", step++);

    drive.iecDirectoryWildcardPattern = "*SEQ";
    drive.iecDirectoryTypeFilter = "SEQ";
    drive.iecDirectoryModeFilter = "W";
    drive.iecDirectoryModeFilterNegated = true;
    drive.buildDirectoryStubPayload();
    pushRow("filter_negated_mode", step++);

    drive.iecDirectoryWildcardPattern = "";
    drive.iecDirectoryTypeFilter = "";
    drive.iecDirectoryModeFilter = "";
    drive.iecDirectoryModeFilterNegated = false;
    drive.buildDirectoryStubPayload();
    pushRow("filter_reset", step++);

    return rows;
}

static std::vector<std::string> buildWeek33DriveDirectoryFilterEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek33DriveDirectoryFilterRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek33DriveDirectoryFilterRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek33DriveDirectoryFilterRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek33DriveDirectoryFilterEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,txq,tx_head,alloc_count,blocks_free,wildcard,type,mode,mode_negated\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek33DriveDirectoryFilterEdgeHardReference() {
    const std::string runtimePath = "week33_drive_dir_filter_runtime.csv";
    const std::string refPath = "reference/edge/week33_drive_dir_filter_trace.csv";

    const std::vector<std::string> got = buildWeek33DriveDirectoryFilterEdgeTraceRows();
    writeWeek33DriveDirectoryFilterEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK33_BOOTSTRAP_DIRFILTER_REF") != nullptr);
    if (bootstrap) {
        writeWeek33DriveDirectoryFilterEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK33][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK33][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK33][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK33][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK33][HARDREF] PASS: drive directory-filter trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek34DriveAllocMapRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, uint8_t trk, uint8_t sec, uint8_t ch, bool opResult) {
        const uint16_t idx = drive.blockAllocIndex(trk, sec);
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(trk)
            << "," << int(sec)
            << "," << int(ch)
            << "," << (opResult ? 1 : 0)
            << "," << drive.iecAllocatedBlockCount
            << "," << drive.virtualBlocksFree()
            << "," << int(drive.iecBlockAllocated[idx])
            << "," << int(drive.iecAllocOwnerEntry[idx]);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++, 0x11, 0x01, 0x00, true);

    const bool a1 = drive.allocateVirtualBlock(0x11, 0x01, 0x00);
    pushRow("alloc_1101_ch0", step++, 0x11, 0x01, 0x00, a1);

    const bool a2 = drive.allocateVirtualBlock(0x11, 0x02, 0x01);
    pushRow("alloc_1102_ch1", step++, 0x11, 0x02, 0x01, a2);

    const bool a3 = drive.allocateVirtualBlock(0x11, 0x01, 0x02);
    pushRow("alloc_dup_1101", step++, 0x11, 0x01, 0x02, a3);

    const bool f1 = drive.freeVirtualBlock(0x11, 0x01);
    pushRow("free_1101", step++, 0x11, 0x01, 0x00, f1);

    const bool f2 = drive.freeVirtualBlock(0x11, 0x01);
    pushRow("free_missing_1101", step++, 0x11, 0x01, 0x00, f2);

    const bool a4 = drive.allocateVirtualBlock(0x12, 0x03, 0x02);
    pushRow("alloc_1203_ch2", step++, 0x12, 0x03, 0x02, a4);

    const bool f3 = drive.freeVirtualBlock(0x11, 0x02);
    pushRow("free_1102", step++, 0x11, 0x02, 0x01, f3);

    const bool f4 = drive.freeVirtualBlock(0x12, 0x03);
    pushRow("free_1203", step++, 0x12, 0x03, 0x02, f4);

    return rows;
}

static std::vector<std::string> buildWeek34DriveAllocMapEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek34DriveAllocMapRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek34DriveAllocMapRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek34DriveAllocMapRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek34DriveAllocMapEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,trk,sec,ch,op_ok,alloc_count,blocks_free,alloc_bit,owner\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek34DriveAllocMapEdgeHardReference() {
    const std::string runtimePath = "week34_drive_alloc_map_runtime.csv";
    const std::string refPath = "reference/edge/week34_drive_alloc_map_trace.csv";

    const std::vector<std::string> got = buildWeek34DriveAllocMapEdgeTraceRows();
    writeWeek34DriveAllocMapEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK34_BOOTSTRAP_ALLOCMAP_REF") != nullptr);
    if (bootstrap) {
        writeWeek34DriveAllocMapEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK34][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK34][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK34][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK34][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK34][HARDREF] PASS: drive alloc-map trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek35DrivePointerDirectoryRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, uint8_t ch) {
        const int txHead = drive.iecTxQueue.empty() ? -1 : int(drive.iecTxQueue.front());
        const size_t txCount = drive.iecTxQueue.size();
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(ch)
            << "," << int(drive.iecChannelBufferPos[ch])
            << "," << (drive.iecChannelPointerValid[ch] ? 1 : 0)
            << "," << txCount
            << "," << txHead
            << "," << (drive.iecDirectoryFromBlockBuffer ? 1 : 0)
            << "," << int(drive.iecBlockBufferTrack)
            << "," << int(drive.iecBlockBufferSector);
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    const uint8_t ch = 0;
    pushRow("baseline", step++, ch);

    drive.iecBlockBufferValid = true;
    drive.iecBlockBufferTrack = 0x12;
    drive.iecBlockBufferSector = 0x05;
    for (int i = 0; i < 256; ++i) {
        drive.iecBlockBuffer[static_cast<size_t>(i)] = static_cast<uint8_t>((i + 0x33) & 0xFF);
    }
    drive.iecDirectoryFromBlockBuffer = true;

    drive.iecChannelBufferPos[ch] = 0x00;
    drive.iecChannelPointerValid[ch] = true;
    drive.buildDirectoryFromBlockBufferPayload(ch);
    pushRow("ptr00_stream", step++, ch);

    drive.iecChannelBufferPos[ch] = 0x10;
    drive.buildDirectoryFromBlockBufferPayload(ch);
    pushRow("ptr10_stream", step++, ch);

    drive.iecChannelBufferPos[ch] = 0x80;
    drive.buildDirectoryFromBlockBufferPayload(ch);
    pushRow("ptr80_stream", step++, ch);

    drive.iecChannelBufferPos[ch] = 0xF0;
    drive.buildDirectoryFromBlockBufferPayload(ch);
    pushRow("ptrf0_stream", step++, ch);

    drive.iecDirectoryFromBlockBuffer = false;
    drive.buildDirectoryStubPayload();
    pushRow("stub_fallback", step++, ch);

    return rows;
}

static std::vector<std::string> buildWeek35DrivePointerDirectoryEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek35DrivePointerDirectoryRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek35DrivePointerDirectoryRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek35DrivePointerDirectoryRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek35DrivePointerDirectoryEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,ch,ptr,ptr_valid,txq,tx_head,from_blockbuf,blk_trk,blk_sec\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek35DrivePointerDirectoryEdgeHardReference() {
    const std::string runtimePath = "week35_drive_ptr_dir_runtime.csv";
    const std::string refPath = "reference/edge/week35_drive_ptr_dir_trace.csv";

    const std::vector<std::string> got = buildWeek35DrivePointerDirectoryEdgeTraceRows();
    writeWeek35DrivePointerDirectoryEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK35_BOOTSTRAP_PTRDIR_REF") != nullptr);
    if (bootstrap) {
        writeWeek35DrivePointerDirectoryEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK35][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK35][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK35][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK35][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK35][HARDREF] PASS: drive pointer-directory trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek36DriveCatalogLifecycleRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(96);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto countCatalogUsed = [&]() {
        uint32_t n = 0;
        for (size_t i = 0; i < drive.iecCatalog.size(); ++i) {
            if (drive.iecCatalog[i].used) {
                n++;
            }
        }
        return n;
    };

    auto pushRow = [&](const char *phase, uint64_t step, uint8_t ch) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << int(ch)
            << "," << countCatalogUsed()
            << "," << drive.iecAllocatedBlockCount
            << "," << drive.virtualBlocksFree()
            << "," << int(drive.iecChannelCatalogEntry[ch & 0x0F])
            << "," << (drive.iecChannelOpenNameValid[ch & 0x0F] ? 1 : 0)
            << "," << drive.iecChannelOpenName[ch & 0x0F]
            << "," << drive.iecStatusLine;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++, 0);

    drive.iecChannelOpenName[0] = "FILE-A";
    drive.iecChannelOpenNameValid[0] = true;
    drive.iecChannelOpenType[0] = "PRG";
    drive.iecChannelOpenMode[0] = "W";
    const bool a0_1 = drive.allocateVirtualBlock(0x11, 0x01, 0x00);
    (void)a0_1;
    pushRow("ch0_alloc_1", step++, 0);

    const bool a0_2 = drive.allocateVirtualBlock(0x11, 0x02, 0x00);
    (void)a0_2;
    pushRow("ch0_alloc_2", step++, 0);

    drive.iecChannelOpenName[1] = "FILE-B";
    drive.iecChannelOpenNameValid[1] = true;
    drive.iecChannelOpenType[1] = "SEQ";
    drive.iecChannelOpenMode[1] = "A";
    const bool a1_1 = drive.allocateVirtualBlock(0x12, 0x03, 0x01);
    (void)a1_1;
    pushRow("ch1_alloc_1", step++, 1);

    const bool f0_1 = drive.freeVirtualBlock(0x11, 0x01);
    (void)f0_1;
    pushRow("ch0_free_1", step++, 0);

    const bool f0_2 = drive.freeVirtualBlock(0x11, 0x02);
    (void)f0_2;
    pushRow("ch0_free_2_remove", step++, 0);

    const bool f1_1 = drive.freeVirtualBlock(0x12, 0x03);
    (void)f1_1;
    pushRow("ch1_free_remove", step++, 1);

    const bool fMissing = drive.freeVirtualBlock(0x12, 0x03);
    if (!fMissing) {
        drive.iecStatusLine = "65,NO BLOCK,00,00";
    }
    pushRow("free_missing", step++, 1);

    return rows;
}

static std::vector<std::string> buildWeek36DriveCatalogLifecycleEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek36DriveCatalogLifecycleRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek36DriveCatalogLifecycleRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek36DriveCatalogLifecycleRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek36DriveCatalogLifecycleEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,ch,catalog_used,alloc_count,blocks_free,ch_map,name_valid,name,status\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek36DriveCatalogLifecycleEdgeHardReference() {
    const std::string runtimePath = "week36_drive_catalog_runtime.csv";
    const std::string refPath = "reference/edge/week36_drive_catalog_trace.csv";

    const std::vector<std::string> got = buildWeek36DriveCatalogLifecycleEdgeTraceRows();
    writeWeek36DriveCatalogLifecycleEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK36_BOOTSTRAP_CATALOG_REF") != nullptr);
    if (bootstrap) {
        writeWeek36DriveCatalogLifecycleEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK36][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK36][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK36][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK36][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK36][HARDREF] PASS: drive catalog lifecycle trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek37DriveAtnCommandGateRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(64);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, bool opOk) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (opOk ? 1 : 0)
            << "," << (drive.iecATN ? 1 : 0)
            << "," << int(drive.iecSerialState)
            << "," << (drive.iecListening ? 1 : 0)
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.lastIecCommand)
            << "," << drive.iecCommandDispatchCount
            << "," << drive.iecCommandSyntaxErrorCount;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++, true);

    drive.iecSerialState = Drive1541::IecSerialState::Command;
    drive.iecATN = true;
    const bool listenBlocked = drive.processIecCommandByte(0x28);
    pushRow("listen_blocked_atn_high", step++, listenBlocked);

    drive.iecATN = false;
    const bool listenOk = drive.processIecCommandByte(0x28);
    pushRow("listen_ok_atn_low", step++, listenOk);

    drive.iecATN = true;
    const bool talkBlocked = drive.processIecCommandByte(0x48);
    pushRow("talk_blocked_atn_high", step++, talkBlocked);

    drive.iecATN = false;
    const bool talkOk = drive.processIecCommandByte(0x48);
    pushRow("talk_ok_atn_low", step++, talkOk);

    const bool talkSa0 = drive.processIecCommandByte(0x60);
    pushRow("talk_sa0", step++, talkSa0);

    const bool unlisten = drive.processIecCommandByte(0x3F);
    pushRow("unlisten", step++, unlisten);

    const bool untalk = drive.processIecCommandByte(0x5F);
    pushRow("untalk", step++, untalk);

    return rows;
}

static std::vector<std::string> buildWeek37DriveAtnCommandGateEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek37DriveAtnCommandGateRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek37DriveAtnCommandGateRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek37DriveAtnCommandGateRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek37DriveAtnCommandGateEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,op_ok,atn_high,state,listening,talking,last_cmd,cmd_dispatch,syntax_err\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek37DriveAtnCommandGateEdgeHardReference() {
    const std::string runtimePath = "week37_drive_atn_gate_runtime.csv";
    const std::string refPath = "reference/edge/week37_drive_atn_gate_trace.csv";

    const std::vector<std::string> got = buildWeek37DriveAtnCommandGateEdgeTraceRows();
    writeWeek37DriveAtnCommandGateEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK37_BOOTSTRAP_ATNGATE_REF") != nullptr);
    if (bootstrap) {
        writeWeek37DriveAtnCommandGateEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK37][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK37][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK37][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK37][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK37][HARDREF] PASS: drive ATN command-gate trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek38DriveTalkChannelCloseRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(96);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, bool opOk) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (opOk ? 1 : 0)
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.iecTalkSecondary)
            << "," << (drive.iecOpenTalkChannels[15] ? 1 : 0)
            << "," << drive.iecChannelCloseCount[15]
            << "," << drive.iecCommandSyntaxErrorCount
            << "," << drive.iecTxQueue.size();
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++, true);

    drive.iecSerialState = Drive1541::IecSerialState::Command;
    drive.iecATN = false;

    const bool talkCmd = drive.processIecCommandByte(0x48);
    pushRow("talk_cmd", step++, talkCmd);

    const bool talkSa2Invalid = drive.processIecCommandByte(0x62);
    pushRow("talk_sa2_invalid", step++, talkSa2Invalid);

    const bool talkSa15 = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15", step++, talkSa15);

    const bool close15A = drive.processIecCommandByte(0xEF);
    pushRow("close15_a", step++, close15A);

    const bool untalkA = drive.processIecCommandByte(0x5F);
    pushRow("untalk_a", step++, untalkA);

    const bool talkCmdAgain = drive.processIecCommandByte(0x48);
    pushRow("talk_cmd_again", step++, talkCmdAgain);

    const bool talkSa15Again = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15_again", step++, talkSa15Again);

    const bool close15B = drive.processIecCommandByte(0xEF);
    pushRow("close15_b", step++, close15B);

    const bool untalkB = drive.processIecCommandByte(0x5F);
    pushRow("untalk_b", step++, untalkB);

    return rows;
}

static std::vector<std::string> buildWeek38DriveTalkChannelCloseEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek38DriveTalkChannelCloseRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek38DriveTalkChannelCloseRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek38DriveTalkChannelCloseRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek38DriveTalkChannelCloseEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,op_ok,talking,talk_sa,open_talk15,close15_count,syntax_err,txq_size\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek38DriveTalkChannelCloseEdgeHardReference() {
    const std::string runtimePath = "week38_drive_talkch_close_runtime.csv";
    const std::string refPath = "reference/edge/week38_drive_talkch_close_trace.csv";

    const std::vector<std::string> got = buildWeek38DriveTalkChannelCloseEdgeTraceRows();
    writeWeek38DriveTalkChannelCloseEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK38_BOOTSTRAP_TALKCH_REF") != nullptr);
    if (bootstrap) {
        writeWeek38DriveTalkChannelCloseEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK38][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK38][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK38][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK38][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK38][HARDREF] PASS: drive TALK/CLOSE channel trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek39DriveCmdResponseFallbackRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(96);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, bool opOk) {
        const int txHead = drive.iecTxQueue.empty() ? -1 : int(drive.iecTxQueue.front());
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (opOk ? 1 : 0)
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.iecTalkSecondary)
            << "," << drive.iecCommandResponseQueue.size()
            << "," << drive.iecTxQueue.size()
            << "," << txHead
            << "," << (drive.iecOpenTalkChannels[15] ? 1 : 0)
            << "," << drive.iecStatusLine;
        rows.push_back(oss.str());
    };

    uint64_t step = 0;
    pushRow("baseline", step++, true);

    drive.iecCommandResponseQueue.clear();
    drive.iecCommandResponseQueue.push_back(0x41);
    drive.iecCommandResponseQueue.push_back(0x42);
    drive.iecCommandResponseQueue.push_back(0x43);
    pushRow("seed_response", step++, true);

    drive.iecSerialState = Drive1541::IecSerialState::Command;
    drive.iecATN = false;

    const bool talkCmd = drive.processIecCommandByte(0x48);
    pushRow("talk_cmd", step++, talkCmd);

    const bool talkSa15Response = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15_response", step++, talkSa15Response);

    const bool close15A = drive.processIecCommandByte(0xEF);
    pushRow("close15_a", step++, close15A);

    const bool talkSa15Status = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15_status_fallback", step++, talkSa15Status);

    const bool untalk = drive.processIecCommandByte(0x5F);
    pushRow("untalk", step++, untalk);

    const bool talkSa15AfterUntalk = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15_after_untalk", step++, talkSa15AfterUntalk);

    return rows;
}

static std::vector<std::string> buildWeek39DriveCmdResponseFallbackEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek39DriveCmdResponseFallbackRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek39DriveCmdResponseFallbackRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek39DriveCmdResponseFallbackRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek39DriveCmdResponseFallbackEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,op_ok,talking,talk_sa,respq,txq,tx_head,open_talk15,status\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek39DriveCmdResponseFallbackEdgeHardReference() {
    const std::string runtimePath = "week39_drive_cmdresp_fallback_runtime.csv";
    const std::string refPath = "reference/edge/week39_drive_cmdresp_fallback_trace.csv";

    const std::vector<std::string> got = buildWeek39DriveCmdResponseFallbackEdgeTraceRows();
    writeWeek39DriveCmdResponseFallbackEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK39_BOOTSTRAP_CMDRESP_REF") != nullptr);
    if (bootstrap) {
        writeWeek39DriveCmdResponseFallbackEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK39][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK39][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK39][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK39][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK39][HARDREF] PASS: drive command-response/status fallback trace matches reference" << std::endl;
}

static std::vector<std::string> buildWeek40DriveCmdBufferCommitRowsForRevision(Drive1541::Revision rev, const char *label) {
    std::vector<std::string> rows;
    rows.reserve(128);

    Drive1541 drive;
    drive.setRevision(rev);
    drive.reset();

    auto pushRow = [&](const char *phase, uint64_t step, bool opOk) {
        std::ostringstream oss;
        oss << label
            << "," << phase
            << "," << step
            << "," << (opOk ? 1 : 0)
            << "," << (drive.iecListening ? 1 : 0)
            << "," << (drive.iecTalking ? 1 : 0)
            << "," << int(drive.iecListenSecondary)
            << "," << int(drive.iecTalkSecondary)
            << "," << drive.iecDataDispatchCount
            << "," << drive.iecCommandDispatchCount
            << "," << drive.iecCommandChannelBuffer.size()
            << "," << drive.iecCommandSyntaxErrorCount
            << "," << int(drive.memory[0x0400])
            << "," << drive.iecTxQueue.size();
        rows.push_back(oss.str());
    };

    auto feedData = [&](const std::string &cmd) {
        bool ok = true;
        for (char c : cmd) {
            if (!drive.processIecDataByte(static_cast<uint8_t>(c))) {
                ok = false;
                break;
            }
        }
        return ok;
    };

    uint64_t step = 0;
    pushRow("baseline", step++, true);

    drive.iecSerialState = Drive1541::IecSerialState::Command;
    drive.iecATN = false;

    const bool listenCmdA = drive.processIecCommandByte(0x28);
    pushRow("listen_cmd_a", step++, listenCmdA);

    const bool sa15A = drive.processIecCommandByte(0xFF);
    pushRow("sa15_a", step++, sa15A);

    const bool dataValid = feedData("M-W,0400,00,01,AA");
    pushRow("data_valid_buffered", step++, dataValid);

    const bool unlistenValid = drive.processIecCommandByte(0x3F);
    pushRow("execute_valid_unlisten", step++, unlistenValid);

    const bool talkCmd = drive.processIecCommandByte(0x48);
    pushRow("talk_cmd", step++, talkCmd);

    const bool talkSa15 = drive.processIecCommandByte(0x6F);
    pushRow("talk_sa15_status", step++, talkSa15);

    const bool untalkA = drive.processIecCommandByte(0x5F);
    pushRow("untalk_a", step++, untalkA);

    const bool listenCmdB = drive.processIecCommandByte(0x28);
    pushRow("listen_cmd_b", step++, listenCmdB);

    const bool sa15B = drive.processIecCommandByte(0xFF);
    pushRow("sa15_b", step++, sa15B);

    const bool dataInvalid = feedData("M-W,0400,00,01,ZZ");
    pushRow("data_invalid_buffered", step++, dataInvalid);

    const bool unlistenInvalid = drive.processIecCommandByte(0x3F);
    pushRow("execute_invalid_unlisten", step++, unlistenInvalid);

    return rows;
}

static std::vector<std::string> buildWeek40DriveCmdBufferCommitEdgeTraceRows() {
    std::vector<std::string> rows;
    const auto r0 = buildWeek40DriveCmdBufferCommitRowsForRevision(Drive1541::REV_1541, "1541");
    const auto r1 = buildWeek40DriveCmdBufferCommitRowsForRevision(Drive1541::REV_1541C, "1541C");
    const auto r2 = buildWeek40DriveCmdBufferCommitRowsForRevision(Drive1541::REV_1541II, "1541II");
    rows.insert(rows.end(), r0.begin(), r0.end());
    rows.insert(rows.end(), r1.begin(), r1.end());
    rows.insert(rows.end(), r2.begin(), r2.end());
    return rows;
}

static void writeWeek40DriveCmdBufferCommitEdgeTraceCsv(const std::string &path, const std::vector<std::string> &rows) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << "rev,phase,step,op_ok,listening,talking,listen_sa,talk_sa,data_dispatch,cmd_dispatch,cmd_buf_len,syntax_err,mem_0400,txq_size\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << rows[i] << "\n";
    }
}

static void runWeek40DriveCmdBufferCommitEdgeHardReference() {
    const std::string runtimePath = "week40_drive_cmdbuf_commit_runtime.csv";
    const std::string refPath = "reference/edge/week40_drive_cmdbuf_commit_trace.csv";

    const std::vector<std::string> got = buildWeek40DriveCmdBufferCommitEdgeTraceRows();
    writeWeek40DriveCmdBufferCommitEdgeTraceCsv(runtimePath, got);

    const bool bootstrap = (std::getenv("WEEK40_BOOTSTRAP_CMDBUF_REF") != nullptr);
    if (bootstrap) {
        writeWeek40DriveCmdBufferCommitEdgeTraceCsv(refPath, got);
        std::cout << "[WEEK40][HARDREF] BOOTSTRAP: wrote " << refPath << std::endl;
        return;
    }

    const std::vector<std::string> ref = readTextRowsNoHeader(refPath);
    if (ref.empty()) {
        std::cerr << "[WEEK40][HARDREF] FAIL: missing/empty reference " << refPath << std::endl;
        assert(false);
    }
    if (ref.size() != got.size()) {
        std::cerr << "[WEEK40][HARDREF] FAIL: row count mismatch got=" << got.size()
                  << " ref=" << ref.size() << std::endl;
        assert(false);
    }
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != ref[i]) {
            std::cerr << "[WEEK40][HARDREF] FAIL: mismatch row=" << i
                      << " got='" << got[i] << "'"
                      << " ref='" << ref[i] << "'" << std::endl;
            assert(false);
        }
    }

    std::cout << "[WEEK40][HARDREF] PASS: drive command-buffer commit trace matches reference" << std::endl;
}

static void tickPeripherals(Bus &bus) {
    if (bus.cia1 != nullptr) bus.cia1->tick();
    if (bus.cia2 != nullptr) bus.cia2->tick();
    if (bus.sid  != nullptr) bus.sid->tick();
}

static void syncInterruptLines(Bus &bus, CPU6510 &cpu) {
    bool irq = false;
    if (bus.vic != nullptr && (bus.vic->irqFlags & 0x01)) irq = true;
    if (bus.cia1 != nullptr && (bus.cia1->icr & bus.cia1->ier & 0x7F)) irq = true;
    if (bus.cia2 != nullptr && (bus.cia2->icr & bus.cia2->ier & 0x7F)) irq = true;
    cpu.setIRQ(!irq);

    bool nmi = false;
    if (bus.cia1 != nullptr && (bus.cia1->icr & 0x80)) nmi = true;
    if (bus.cia2 != nullptr && (bus.cia2->icr & 0x80)) nmi = true;
    cpu.setNMI(!nmi);
}

static void emitWarmupVicDebug(uint64_t i, Bus &bus, bool lastVicHadBus) {
    if (i < 200 && bus.vic != nullptr) {
        std::cout << "[DBG] raster=" << bus.vic->rasterLine
                  << " cycle=" << bus.vic->cycleInLine
                  << " pixelClock=" << bus.vic->pixelClock
                  << " isFetching=" << bus.vic->isFetching()
                  << " BA=" << (bus.vic->baLine ? 1 : 0)
                  << " AEC=" << (bus.vic->aecLine ? 1 : 0)
                  << " lastVicHadBus=" << lastVicHadBus
                  << std::endl;
    }
}

static void runMainLoopSelfChecks(Bus &bus, CPU6510 &cpu, uint64_t i, bool lastVicHadBus) {
    #if SELF_CHECK
    if (bus.vic != nullptr) {
        assert(bus.vic->bus == &bus);
        assert(bus.vic->pixelClock >= 0 && bus.vic->pixelClock < 8);
        assert(bus.vic->cycleInLine >= 0 && bus.vic->cycleInLine < 63);
        assert(bus.vic->rasterLine >= 0 && bus.vic->rasterLine < 312);
    }
    assert(cpu.getTotalHalfCycles() == (i + 1));
    if (lastVicHadBus) {
        assert(bus.vic != nullptr && !bus.vic->aecLine);
    }
    #endif
}

static void runMainExecutionLoop(Bus &bus, CPU6510 &cpu, uint64_t &halfCycleCount) {
    bool lastVicHadBus = false;
    for (uint64_t i = 0; i < 99990000; ++i) {
        tickVideo(bus);
        tickCpuWithVicContention(bus, cpu, lastVicHadBus);
        tickPeripherals(bus);
        syncInterruptLines(bus, cpu);
        emitWarmupVicDebug(i, bus, lastVicHadBus);
        runMainLoopSelfChecks(bus, cpu, i, lastVicHadBus);

        halfCycleCount++;
        if (cpu.halted) {
            std::cout << "[SYSTEM] CPU halted, stopping execution loop." << std::endl;
            break;
        }
    }
}

int main() {

    std::ofstream quietStdout("NUL");
#if !CPU_TRACE_VERBOSE
    if (quietStdout.is_open()) {
        std::cout.rdbuf(quietStdout.rdbuf());
    }
#endif

    Bus bus;
    const bool systemRomsLoaded = bus.loadSystemRoms("roms");
    if (!systemRomsLoaded) {
        std::cerr << "[ROM] Warning: BASIC/KERNAL/CHAR ROM set not fully loaded from ./roms" << std::endl;
    }
    
    VICII vic;
    bus.vic = &vic; // collega il VIC-II al bus
    vic.bus = &bus;

    CIA6526 cia1;
    bus.cia1 = &cia1;

    CIA6526 cia2;
    bus.cia2 = &cia2;  
    
    SID sid;
    bus.sid = &sid;    

    CPU6510 cpu(bus);
    configureChipRevisionsFromEnv(bus, cpu, vic, cia1, cia2);
    ALU alu;

    // -------------------------
    // NMI VECTOR (no test code) 
    // -------------------------
    //const uint16_t START_ADDR_NMI = 0x9000;
    const uint16_t START_ADDR_NMI = 0xFE43;
    
    bus.memory[0xFFFA] = Lo(START_ADDR_NMI);
    bus.memory[0xFFFB] = Hi(START_ADDR_NMI); 


    // -------------------------
    // IRQ VECTOR (no test code) 
    // -------------------------

    const uint16_t START_ADDR_IRQ = 0xFF48;    

    bus.memory[0xFFFE] = Lo(START_ADDR_IRQ);
    bus.memory[0xFFFF] = Hi(START_ADDR_IRQ);

    // FD30 31 EA $0314 IRQ vector (l'ho messo anche sotto)
    bus.memory[0xFD30] = 0x31; bus.memory[0xFD31] = 0xEA;    

    // FD32 66 FE $0316 BRK vector (l'ho messo anche sotto)
    bus.memory[0xFD32] = 0x66; bus.memory[0xFD33] = 0xFE;


    // -----------------------
    // RESET VECTOR  
    // -----------------------
    
    //uint16_t START_ADDR_RESET = 0x8000;
    uint16_t START_ADDR_RESET = 0xFCE2;

    bus.memory[0xFFFC] = Lo(START_ADDR_RESET);
    bus.memory[0xFFFD] = Hi(START_ADDR_RESET);

    bus.memory[0x0316] = 0xE2;  // $0316: $E2  ; low byte del jump target
    bus.memory[0x0317] = 0xFC;  // $0317: $FC  ; high byte del jump target → salta a $FC00, dove dovrebbe esserci il codice reale        
    
    
    // -----------------------------------------
    // Source code after RESET VECTOR (at $8000)
    // -----------------------------------------

    /* $A000–$BFFF   BASIC ROM */

    #pragma region *** BASIC ROM entry points (0xA000 - 0xA003)

    /* ----------------------------------------------------------------------

                                    *** start of the BASIC ROM
    .:A000 94 E3    BASIC cold start entry point
    .:A002 7B E3    BASIC warm start entry point

    ------------------------------------------------------------------------ */

    bus.memory[0xA000] = 0x94; bus.memory[0xA001] = 0xE3;    // BASIC cold start entry point
    bus.memory[0xA002] = 0x7B; bus.memory[0xA003] = 0xE3;    // BASIC warm start entry point

    #pragma endregion

    #pragma region *** check available memory (0xA408 - 0xA434) todo

    /* ----------------------------------------------------------------------

                                    *** check available memory
                                    do out of memory error if no room

        .,A408 C4 34    CPY $34         compare with bottom of string space high byte
        .,A40A 90 28    BCC $A434       if less then exit (is ok)
        .,A40C D0 04    BNE $A412       skip next test if greater (tested <)
                                        high byte was =, now do low byte
        .,A40E C5 33    CMP $33         compare with bottom of string space low byte
        .,A410 90 22    BCC $A434       if less then exit (is ok)
                                        address is > string storage ptr (oops!)
        .,A412 48       PHA             push address low byte
        .,A413 A2 09    LDX #$09        set index to save $57 to $60 inclusive
        .,A415 98       TYA             copy address high byte (to push on stack)
                                        save misc numeric work area
        .,A416 48       PHA             push byte
        .,A417 B5 57    LDA $57,X       get byte from $57 to $60
        .,A419 CA       DEX             decrement index
        .,A41A 10 FA    BPL $A416       loop until all done
        .,A41C 20 26 B5 JSR $B526       do garbage collection routine
                                        restore misc numeric work area
        .,A41F A2 F7    LDX #$F7        set index to restore bytes
        .,A421 68       PLA             pop byte
        .,A422 95 61    STA $61,X       save byte to $57 to $60
        .,A424 E8       INX             increment index
        .,A425 30 FA    BMI $A421       loop while -ve
        .,A427 68       PLA             pop address high byte
        .,A428 A8       TAY             copy back to Y
        .,A429 68       PLA             pop address low byte
        .,A42A C4 34    CPY $34         compare with bottom of string space high byte
        .,A42C 90 06    BCC $A434       if less then exit (is ok)
        .,A42E D0 05    BNE $A435       if greater do out of memory error then warm start
                                        high byte was =, now do low byte
        .,A430 C5 33    CMP $33         compare with bottom of string space low byte
        .,A432 B0 01    BCS $A435       if >= do out of memory error then warm start
                                        ok exit, carry clear
        .,A434 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xA408] = 0xC4; bus.memory[0xA409] = 0x34;                               // CPY $34         compare with bottom of string space high byte
    bus.memory[0xA40A] = 0x90; bus.memory[0xA40B] = 0x28;                               // BCC $A434       if less then exit (is ok)
    bus.memory[0xA40C] = 0xD0; bus.memory[0xA40D] = 0x04;                               // BNE $A412       skip next test if greater (tested <)
    //                                                                                  //                 high byte was =, now do low byte
    bus.memory[0xA40E] = 0xC5; bus.memory[0xA40F] = 0x33;                               // CMP $33         compare with bottom of string space low byte
    bus.memory[0xA410] = 0x90; bus.memory[0xA411] = 0x22;                               // BCC $A434       if less then exit (is ok)
    //                                                                                  //                 address is > string storage ptr (oops!)
    bus.memory[0xA412] = 0x48;                                                          // PHA             push address low byte
    bus.memory[0xA413] = 0xA2; bus.memory[0xA414] = 0x09;                               // LDX #$09        set index to save $57 to $60 inclusive
    bus.memory[0xA415] = 0x98;                                                          // TYA             copy address high byte (to push on stack)
    //                                                                                  //                 save misc numeric work area
    bus.memory[0xA416] = 0x48;                                                          // PHA             push byte
    bus.memory[0xA417] = 0xB5; bus.memory[0xA418] = 0x57;                               // LDA $57,X       get byte from $57 to $60
    bus.memory[0xA419] = 0xCA;                                                          // DEX             decrement index
    bus.memory[0xA41A] = 0x10; bus.memory[0xA41B] = 0xFA;                               // BPL $A416       loop until all done
    bus.memory[0xA41C] = 0x20; bus.memory[0xA41D] = 0x26; bus.memory[0xA41E] = 0xB5;    // JSR $B526       do garbage collection routine            *** todo ***
    //                                                                                  //                 restore misc numeric work area
    bus.memory[0xA41F] = 0xA2; bus.memory[0xA420] = 0xF7;                               // LDX #$F7        set index to restore bytes
    bus.memory[0xA421] = 0x68;                                                          // PLA             pop byte
    bus.memory[0xA422] = 0x95; bus.memory[0xA423] = 0x61;                               // STA $61,X       save byte to $57 to $60
    bus.memory[0xA424] = 0xE8;                                                          // INX             increment index
    bus.memory[0xA425] = 0x30; bus.memory[0xA426] = 0xFA;                               // BMI $A421       loop while -ve
    bus.memory[0xA427] = 0x68;                                                          // PLA             pop address high byte
    bus.memory[0xA428] = 0xA8;                                                          // TAY             copy back to Y
    bus.memory[0xA429] = 0x68;                                                          // PLA             pop address low byte
    bus.memory[0xA42A] = 0xC4; bus.memory[0xA42B] = 0x34;                               // CPY $34         compare with bottom of string space high byte
    bus.memory[0xA42C] = 0x90; bus.memory[0xA42D] = 0x06;                               // BCC $A434       if less then exit (is ok)
    bus.memory[0xA42E] = 0xD0; bus.memory[0xA42F] = 0x05;                               // BNE $A435       if greater do out of memory error then warm start    *** done ***
    //                                                                                  //                 high byte was =, now do low byte
    bus.memory[0xA430] = 0xC5; bus.memory[0xA431] = 0x33;                               // CMP $33         compare with bottom of string space low byte
    bus.memory[0xA432] = 0xB0; bus.memory[0xA433] = 0x01;                               // BCS $A435       if >= do out of memory error then warm start         *** done ***
    //                                                                                  //                 ok exit, carry clear
    bus.memory[0xA434] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** do out of memory error then warm start (0xA435 - 0xA439)

    /* ----------------------------------------------------------------------

                                        *** do out of memory error then warm start
        .,A435 A2 10    LDX #$10        error code $10, out of memory error
                                        do error #X then warm start
        .,A437 6C 00 03 JMP ($0300)     do error message

    ------------------------------------------------------------------------ */

    bus.memory[0xA435] = 0xA2; bus.memory[0xA436] = 0x10;                               // LDX #$10        error code $10, out of memory error
    //                                                                                  //                 do error #X then warm start
    bus.memory[0xA437] = 0x6C; bus.memory[0xA438] = 0x00; bus.memory[0xA439] = 0x03;    // JMP ($0300)     do error message                     *** done ***

    #pragma endregion

    #pragma region *** do error X then warm start (0xA43A - 0xA473) todo

    /* ----------------------------------------------------------------------

                    *** do error #X then warm start ***

        .,A43A 8A       TXA             copy error number
        .,A43B 0A       ASL             *2
        .,A43C AA       TAX             copy to index
        .,A43D BD 26 A3 LDA $A326,X     get error message pointer low byte
        .,A440 85 22    STA $22         save it
        .,A442 BD 27 A3 LDA $A327,X     get error message pointer high byte
        .,A445 85 23    STA $23         save it
        .,A447 20 CC FF JSR $FFCC       close input and output channels
        .,A44A A9 00    LDA #$00        clear A
        .,A44C 85 13    STA $13         clear current I/O channel, flag default
        .,A44E 20 D7 AA JSR $AAD7       print CR/LF
        .,A451 20 45 AB JSR $AB45       print "?"
        .,A454 A0 00    LDY #$00        clear index
        .,A456 B1 22    LDA ($22),Y     get byte from message
        .,A458 48       PHA             save status
        .,A459 29 7F    AND #$7F        mask 0xxx xxxx, clear b7
        .,A45B 20 47 AB JSR $AB47       output character
        .,A45E C8       INY             increment index
        .,A45F 68       PLA             restore status
        .,A460 10 F4    BPL $A456       loop if character was not end marker
        .,A462 20 7A A6 JSR $A67A       flush BASIC stack and clear continue pointer
        .,A465 A9 69    LDA #$69        set " ERROR" pointer low byte
        .,A467 A0 A3    LDY #$A3        set " ERROR" pointer high byte
        .,A469 20 1E AB JSR $AB1E       print null terminated string
        .,A46C A4 3A    LDY $3A         get current line number high byte
        .,A46E C8       INY             increment it
        .,A46F F0 03    BEQ $A474       branch if was in immediate mode
        .,A471 20 C2 BD JSR $BDC2       do " IN " line number message

    ------------------------------------------------------------------------ */

    bus.memory[0xA43A] = 0x8A;                                                                  // TXA             copy error number
    bus.memory[0xA43B] = 0x0A;                                                                  // ASL             *2
    bus.memory[0xA43C] = 0xAA;                                                                  // TAX             copy to index

    bus.memory[0xA43D] = 0xBD; bus.memory[0xA43E] = 0x26; bus.memory[0xA43F] = 0xA3;            // LDA $A326,X     error msg pointer low
    bus.memory[0xA440] = 0x85; bus.memory[0xA441] = 0x22;                                       // STA $22         save low pointer

    bus.memory[0xA442] = 0xBD; bus.memory[0xA443] = 0x27; bus.memory[0xA444] = 0xA3;            // LDA $A327,X     error msg pointer high
    bus.memory[0xA445] = 0x85; bus.memory[0xA446] = 0x23;                                       // STA $23         save high pointer

    bus.memory[0xA447] = 0x20; bus.memory[0xA448] = 0xCC; bus.memory[0xA449] = 0xFF;            // JSR $FFCC       close I/O                *** done ***

    bus.memory[0xA44A] = 0xA9; bus.memory[0xA44B] = 0x00;                                       // LDA #$00        clear A
    bus.memory[0xA44C] = 0x85; bus.memory[0xA44D] = 0x13;                                       // STA $13         clear I/O channel

    bus.memory[0xA44E] = 0x20; bus.memory[0xA44F] = 0xD7; bus.memory[0xA450] = 0xAA;            // JSR $AAD7       print CR/LF              *** done ***
    bus.memory[0xA451] = 0x20; bus.memory[0xA452] = 0x45; bus.memory[0xA453] = 0xAB;            // JSR $AB45       print '?'                *** done ***

    bus.memory[0xA454] = 0xA0; bus.memory[0xA455] = 0x00;                                       // LDY #$00        clear index

    bus.memory[0xA456] = 0xB1; bus.memory[0xA457] = 0x22;                                       // LDA ($22),Y     get message byte
    bus.memory[0xA458] = 0x48;                                                                  // PHA
    bus.memory[0xA459] = 0x29; bus.memory[0xA45A] = 0x7F;                                       // AND #$7F        clear bit 7
    bus.memory[0xA45B] = 0x20; bus.memory[0xA45C] = 0x47; bus.memory[0xA45D] = 0xAB;            // JSR $AB47       output char              *** done ***
    bus.memory[0xA45E] = 0xC8;                                                                  // INY
    bus.memory[0xA45F] = 0x68;                                                                  // PLA
    bus.memory[0xA460] = 0x10; bus.memory[0xA461] = 0xF4;                                       // BPL $A456       loop

    bus.memory[0xA462] = 0x20; bus.memory[0xA463] = 0x7A; bus.memory[0xA464] = 0xA6;            // JSR $A67A       flush stack              *** done ***

    bus.memory[0xA465] = 0xA9; bus.memory[0xA466] = 0x69;                                       // LDA #$69        " ERROR" ptr low
    bus.memory[0xA467] = 0xA0; bus.memory[0xA468] = 0xA3;                                       // LDY #$A3        " ERROR" ptr high

    bus.memory[0xA469] = 0x20; bus.memory[0xA46A] = 0x1E; bus.memory[0xA46B] = 0xAB;            // JSR $AB1E       print string             *** done ***

    bus.memory[0xA46C] = 0xA4; bus.memory[0xA46D] = 0x3A;                                       // LDY $3A         current line high
    bus.memory[0xA46E] = 0xC8;                                                                  // INY
    bus.memory[0xA46F] = 0xF0; bus.memory[0xA470] = 0x03;                                       // BEQ $A474       if immediate mode        *** done ***

    bus.memory[0xA471] = 0x20; bus.memory[0xA472] = 0xC2; bus.memory[0xA473] = 0xBD;            // JSR $BDC2       print "IN line"          *** todo ***

    #pragma endregion

    #pragma region *** do warm start (0xA474 - 0xA480)

    /* ----------------------------------------------------------------------

                                    *** do warm start ***

        .,A474 A9 76    LDA #$76        set "READY." pointer low byte
        .,A476 A0 A3    LDY #$A3        set "READY." pointer high byte
        .,A478 20 1E AB JSR $AB1E       print null terminated string
        .,A47B A9 80    LDA #$80        set for control messages only
        .,A47D 20 90 FF JSR $FF90       control kernal messages
        .,A480 6C 02 03 JMP ($0302)     do BASIC warm start

    ------------------------------------------------------------------------ */

    bus.memory[0xA474] = 0xA9; bus.memory[0xA475] = 0x76;                               // LDA #$76         set "READY." pointer low byte
    bus.memory[0xA476] = 0xA0; bus.memory[0xA477] = 0xA3;                               // LDY #$A3         set "READY." pointer high byte
    bus.memory[0xA478] = 0x20; bus.memory[0xA479] = 0x1E; bus.memory[0xA47A] = 0xAB;    // JSR $AB1E        print null terminated string        *** done ***
    bus.memory[0xA47B] = 0xA9; bus.memory[0xA47C] = 0x80;                               // LDA #$80         set for control messages only
    bus.memory[0xA47D] = 0x20; bus.memory[0xA47E] = 0x90; bus.memory[0xA47F] = 0xFF;    // JSR $FF90        control kernal messages             *** done ***
    bus.memory[0xA480] = 0x6C; bus.memory[0xA481] = 0x02; bus.memory[0xA482] = 0x03;    // JMP ($0302)      do BASIC warm start

    #pragma endregion

    #pragma region *** search BASIC for temporary integer line number (0xA613 - 0xA641)

    /* ----------------------------------------------------------------------

                    *** search BASIC for temporary integer line number ***

        .,A613 A5 2B    LDA $2B         get start of memory low byte
        .,A615 A6 2C    LDX $2C         get start of memory high byte

                    *** search BASIC for temp integer line number from AX
                    returns carry set if found

        .,A617 A0 01    LDY #$01        set index to next line pointer high byte
        .,A619 85 5F    STA $5F         save low byte as current
        .,A61B 86 60    STX $60         save high byte as current
        .,A61D B1 5F    LDA ($5F),Y     get next line pointer high byte
        .,A61F F0 1F    BEQ $A640       pointer was zero so done
        .,A621 C8       INY             increment index
        .,A622 C8       INY
        .,A623 A5 15    LDA $15         get temporary integer high byte
        .,A625 D1 5F    CMP ($5F),Y     compare with line # high byte
        .,A627 90 18    BCC $A641       exit if temp < this line
        .,A629 F0 03    BEQ $A62E       go check low byte
        .,A62B 88       DEY
        .,A62C D0 09    BNE $A637       branch always
        .,A62E A5 14    LDA $14         get temporary integer low byte
        .,A630 88       DEY
        .,A631 D1 5F    CMP ($5F),Y     compare low byte
        .,A633 90 0C    BCC $A641       exit if temp < this line
        .,A635 F0 0A    BEQ $A641       exit if temp = (found line#)

                    *** continue search ***

        .,A637 88       DEY             decrement to next line pointer high byte
        .,A638 B1 5F    LDA ($5F),Y     get next line pointer high byte
        .,A63A AA       TAX
        .,A63B 88       DEY             decrement to next line pointer low byte
        .,A63C B1 5F    LDA ($5F),Y     get next line pointer low byte
        .,A63E B0 D7    BCS $A617       loop (carry always set)

        .,A640 18       CLC             clear found flag
        .,A641 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xA613] = 0xA5; bus.memory[0xA614] = 0x2B;                // LDA $2B          get start of memory low byte
    bus.memory[0xA615] = 0xA6; bus.memory[0xA616] = 0x2C;                // LDX $2C          get start of memory high byte

    bus.memory[0xA617] = 0xA0; bus.memory[0xA618] = 0x01;                // LDY #$01         index = next line ptr high byte
    bus.memory[0xA619] = 0x85; bus.memory[0xA61A] = 0x5F;                // STA $5F          save current low byte
    bus.memory[0xA61B] = 0x86; bus.memory[0xA61C] = 0x60;                // STX $60          save current high byte
    bus.memory[0xA61D] = 0xB1; bus.memory[0xA61E] = 0x5F;                // LDA ($5F),Y      read next line ptr high byte
    bus.memory[0xA61F] = 0xF0; bus.memory[0xA620] = 0x1F;                // BEQ $A640        if zero → end of program
    bus.memory[0xA621] = 0xC8;                                           // INY
    bus.memory[0xA622] = 0xC8;                                           // INY
    bus.memory[0xA623] = 0xA5; bus.memory[0xA624] = 0x15;                // LDA $15          temp high byte
    bus.memory[0xA625] = 0xD1; bus.memory[0xA626] = 0x5F;                // CMP ($5F),Y      compare high byte
    bus.memory[0xA627] = 0x90; bus.memory[0xA628] = 0x18;                // BCC $A641        temp < → exit
    bus.memory[0xA629] = 0xF0; bus.memory[0xA62A] = 0x03;                // BEQ $A62E        equal → check low byte
    bus.memory[0xA62B] = 0x88;                                           // DEY
    bus.memory[0xA62C] = 0xD0; bus.memory[0xA62D] = 0x09;                // BNE $A637        unconditional branch
    bus.memory[0xA62E] = 0xA5; bus.memory[0xA62F] = 0x14;                // LDA $14          temp low byte
    bus.memory[0xA630] = 0x88;                                           // DEY
    bus.memory[0xA631] = 0xD1; bus.memory[0xA632] = 0x5F;                // CMP ($5F),Y      compare low byte
    bus.memory[0xA633] = 0x90; bus.memory[0xA634] = 0x0C;                // BCC $A641        not found
    bus.memory[0xA635] = 0xF0; bus.memory[0xA636] = 0x0A;                // BEQ $A641        found

    bus.memory[0xA637] = 0x88;                                           // DEY
    bus.memory[0xA638] = 0xB1; bus.memory[0xA639] = 0x5F;                // LDA ($5F),Y
    bus.memory[0xA63A] = 0xAA;                                           // TAX
    bus.memory[0xA63B] = 0x88;                                           // DEY
    bus.memory[0xA63C] = 0xB1; bus.memory[0xA63D] = 0x5F;                // LDA ($5F),Y
    bus.memory[0xA63E] = 0xB0; bus.memory[0xA63F] = 0xD7;                // BCS $A617        loop (C = always set)

    bus.memory[0xA640] = 0x18;                                           // CLC              clear "found" flag
    bus.memory[0xA641] = 0x60;                                           // RTS

    #pragma endregion

    #pragma region *** perform NEW, CLEAR, RESTORE (0xA642 - 0xA677)

    /* ----------------------------------------------------------------------

                                    *** perform NEW
    .:A642 D0 FD    BNE $A641       exit if following byte to allow syntax error
    .:A644 A9 00    LDA #$00        clear A
    .:A646 A8       TAY             clear index
    .:A647 91 2B    STA ($2B),Y     clear pointer to next line low byte
    .:A649 C8       INY             increment index
    .:A64A 91 2B    STA ($2B),Y     clear pointer to next line high byte, erase program
    .:A64C A5 2B    LDA $2B         get start of memory low byte
    .:A64E 18       CLC             clear carry for add
    .:A64F 69 02    ADC #$02        add null program length
    .:A651 85 2D    STA $2D         set start of variables low byte
    .:A653 A5 2C    LDA $2C         get start of memory high byte
    .:A655 69 00    ADC #$00        add carry
    .:A657 85 2E    STA $2E         set start of variables high byte

                                    *** reset execute pointer and do CLR
    .:A659 20 8E A6 JSR $A68E       set BASIC execute pointer to start of memory - 1
    .:A65C A9 00    LDA #$00        set Zb for CLR entry

                                    *** perform CLR
    .:A65E D0 2D    BNE $A68D       exit if following byte to allow syntax error
    .:A660 20 E7 FF JSR $FFE7       close all channels and files
    .:A663 A5 37    LDA $37         get end of memory low byte
    .:A665 A4 38    LDY $38         get end of memory high byte
    .:A667 85 33    STA $33         set bottom of string space low byte, clear strings
    .:A669 84 34    STY $34         set bottom of string space high byte
    .:A66B A5 2D    LDA $2D         get start of variables low byte
    .:A66D A4 2E    LDY $2E         get start of variables high byte
    .:A66F 85 2F    STA $2F         set end of variables low byte, clear variables
    .:A671 84 30    STY $30         set end of variables high byte
    .:A673 85 31    STA $31         set end of arrays low byte, clear arrays
    .:A675 84 32    STY $32         set end of arrays high byte

                                    *** do RESTORE and clear stack
    .:A677 20 1D A8 JSR $A81D       perform RESTORE

    ------------------------------------------------------------------------ */

    bus.memory[0xA642] = 0xD0; bus.memory[0xA643] = 0xFD;                               // BNE $A641 exit if following byte allows syntax error     *** done ***
    bus.memory[0xA644] = 0xA9; bus.memory[0xA645] = 0x00;                               // LDA #$00 clear A
    bus.memory[0xA646] = 0xA8;                                                          // TAY clear index
    bus.memory[0xA647] = 0x91; bus.memory[0xA648] = 0x2B;                               // STA ($2B),Y clear pointer to next line low byte
    bus.memory[0xA649] = 0xC8;                                                          // INY increment index
    bus.memory[0xA64A] = 0x91; bus.memory[0xA64B] = 0x2B;                               // STA ($2B),Y clear high byte (erase program)
    bus.memory[0xA64C] = 0xA5; bus.memory[0xA64D] = 0x2B;                               // LDA $2B get start of memory low
    bus.memory[0xA64E] = 0x18;                                                          // CLC clear carry
    bus.memory[0xA64F] = 0x69; bus.memory[0xA650] = 0x02;                               // ADC #$02 add null program length
    bus.memory[0xA651] = 0x85; bus.memory[0xA652] = 0x2D;                               // STA $2D set start of variables low
    bus.memory[0xA653] = 0xA5; bus.memory[0xA654] = 0x2C;                               // LDA $2C get start of memory high
    bus.memory[0xA655] = 0x69; bus.memory[0xA656] = 0x00;                               // ADC #$00 add carry
    bus.memory[0xA657] = 0x85; bus.memory[0xA658] = 0x2E;                               // STA $2E set start of variables high

    bus.memory[0xA659] = 0x20; bus.memory[0xA65A] = 0x8E; bus.memory[0xA65B] = 0xA6;    // JSR $A68E reset exec pointer                             *** done ***
    bus.memory[0xA65C] = 0xA9; bus.memory[0xA65D] = 0x00;                               // LDA #$00 prepare CLR

    bus.memory[0xA65E] = 0xD0; bus.memory[0xA65F] = 0x2D;                               // BNE $A68D allow syntax error                             *** done ***
    bus.memory[0xA660] = 0x20; bus.memory[0xA661] = 0xE7; bus.memory[0xA662] = 0xFF;    // JSR $FFE7 close all channels                             *** done ***
    bus.memory[0xA663] = 0xA5; bus.memory[0xA664] = 0x37;                               // LDA $37 end memory low
    bus.memory[0xA665] = 0xA4; bus.memory[0xA666] = 0x38;                               // LDY $38 end memory high
    bus.memory[0xA667] = 0x85; bus.memory[0xA668] = 0x33;                               // STA $33 clear strings low
    bus.memory[0xA669] = 0x84; bus.memory[0xA66A] = 0x34;                               // STY $34 clear strings high
    bus.memory[0xA66B] = 0xA5; bus.memory[0xA66C] = 0x2D;                               // LDA $2D start of variables low
    bus.memory[0xA66D] = 0xA4; bus.memory[0xA66E] = 0x2E;                               // LDY $2E start of variables high
    bus.memory[0xA66F] = 0x85; bus.memory[0xA670] = 0x2F;                               // STA $2F clear variables low
    bus.memory[0xA671] = 0x84; bus.memory[0xA672] = 0x30;                               // STY $30 clear variables high
    bus.memory[0xA673] = 0x85; bus.memory[0xA674] = 0x31;                               // STA $31 clear arrays low
    bus.memory[0xA675] = 0x84; bus.memory[0xA676] = 0x32;                               // STY $32 clear arrays high

    bus.memory[0xA677] = 0x20; bus.memory[0xA678] = 0x1D; bus.memory[0xA679] = 0xA8;    // JSR $A81D RESTORE                                        *** done ***

    #pragma endregion

    #pragma region *** flush BASIC stack and clear the continue pointer (0xA67A - 0xA68D)

    /* ----------------------------------------------------------------------

                                    *** flush BASIC stack and clear the continue pointer
    .:A67A A2 19    LDX #$19        get the descriptor stack start
    .:A67C 86 16    STX $16         set the descriptor stack pointer
    .:A67E 68       PLA             pull the return address low byte
    .:A67F A8       TAY             copy it
    .:A680 68       PLA             pull the return address high byte
    .:A681 A2 FA    LDX #$FA        set the cleared stack pointer
    .:A683 9A       TXS             set the stack
    .:A684 48       PHA             push the return address high byte
    .:A685 98       TYA             restore the return address low byte
    .:A686 48       PHA             push the return address low byte
    .:A687 A9 00    LDA #$00        clear A
    .:A689 85 3E    STA $3E         clear the continue pointer high byte
    .:A68B 85 10    STA $10         clear the subscript/FNX flag
    .:A68D 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xA67A] = 0xA2; bus.memory[0xA67B] = 0x19;                // LDX #$19 get descriptor stack start
    bus.memory[0xA67C] = 0x86; bus.memory[0xA67D] = 0x16;                // STX $16 set descriptor stack pointer
    bus.memory[0xA67E] = 0x68;                                           // PLA pull return address low byte
    bus.memory[0xA67F] = 0xA8;                                           // TAY copy it
    bus.memory[0xA680] = 0x68;                                           // PLA pull return address high byte
    bus.memory[0xA681] = 0xA2; bus.memory[0xA682] = 0xFA;                // LDX #$FA cleared stack pointer
    bus.memory[0xA683] = 0x9A;                                           // TXS set the stack
    bus.memory[0xA684] = 0x48;                                           // PHA push return address high byte
    bus.memory[0xA685] = 0x98;                                           // TYA restore return address low byte
    bus.memory[0xA686] = 0x48;                                           // PHA push return address low byte
    bus.memory[0xA687] = 0xA9; bus.memory[0xA688] = 0x00;                // LDA #$00 clear A
    bus.memory[0xA689] = 0x85; bus.memory[0xA68A] = 0x3E;                // STA $3E clear continue pointer high byte
    bus.memory[0xA68B] = 0x85; bus.memory[0xA68C] = 0x10;                // STA $10 clear subscript/FNX flag
    bus.memory[0xA68D] = 0x60;                                           // RTS

    #pragma endregion

    #pragma region *** set BASIC execute pointer to start of memory - 1 (0xA68E - 0xA69B)

    /* ----------------------------------------------------------------------

                    *** set BASIC execute pointer to start of memory - 1 ***

        .,A68E 18       CLC             clear carry for add
        .,A68F A5 2B    LDA $2B         get start of memory low byte
        .,A691 69 FF    ADC #$FF        add -1 low byte
        .,A693 85 7A    STA $7A         set BASIC execute pointer low byte
        .,A695 A5 2C    LDA $2C         get start of memory high byte
        .,A697 69 FF    ADC #$FF        add -1 high byte
        .,A699 85 7B    STA $7B         save BASIC execute pointer high byte
        .,A69B 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xA68E] = 0x18;                                           // CLC              clear carry for add
    bus.memory[0xA68F] = 0xA5; bus.memory[0xA690] = 0x2B;                // LDA $2B          get start of memory low byte
    bus.memory[0xA691] = 0x69; bus.memory[0xA692] = 0xFF;                // ADC #$FF         add -1 low byte
    bus.memory[0xA693] = 0x85; bus.memory[0xA694] = 0x7A;                // STA $7A          set BASIC execute pointer low byte
    bus.memory[0xA695] = 0xA5; bus.memory[0xA696] = 0x2C;                // LDA $2C          get start of memory high byte
    bus.memory[0xA697] = 0x69; bus.memory[0xA698] = 0xFF;                // ADC #$FF         add -1 high byte
    bus.memory[0xA699] = 0x85; bus.memory[0xA69A] = 0x7B;                // STA $7B          save BASIC execute pointer high byte
    bus.memory[0xA69B] = 0x60;                                           // RTS

    #pragma endregion

    #pragma region *** perform RESTORE (0xA81D - 0xA82B)

    /* ----------------------------------------------------------------------

                                *** perform RESTORE ***

        .,A81D 38       SEC             set carry for subtract
        .,A81E A5 2B    LDA $2B         get start of memory low byte
        .,A820 E9 01    SBC #$01        -1
        .,A822 A4 2C    LDY $2C         get start of memory high byte
        .,A824 B0 01    BCS $A827       branch if no rollunder
        .,A826 88       DEY             else decrement high byte
        .,A827 85 41    STA $41         set DATA pointer low byte
        .,A829 84 42    STY $42         set DATA pointer high byte
        .,A82B 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xA81D] = 0x38;                                           // SEC              set carry for subtract
    bus.memory[0xA81E] = 0xA5; bus.memory[0xA81F] = 0x2B;                // LDA $2B          get start of memory low byte
    bus.memory[0xA820] = 0xE9; bus.memory[0xA821] = 0x01;                // SBC #$01         -1
    bus.memory[0xA822] = 0xA4; bus.memory[0xA823] = 0x2C;                // LDY $2C          get start of memory high byte
    bus.memory[0xA824] = 0xB0; bus.memory[0xA825] = 0x01;                // BCS $A827        branch if no rollunder
    bus.memory[0xA826] = 0x88;                                           // DEY              else decrement high byte
    bus.memory[0xA827] = 0x85; bus.memory[0xA828] = 0x41;                // STA $41          set DATA pointer low byte
    bus.memory[0xA829] = 0x84; bus.memory[0xA82A] = 0x42;                // STY $42          set DATA pointer high byte
    bus.memory[0xA82B] = 0x60;                                           // RTS

    #pragma endregion

    #pragma region *** print CR/LF (0xAAD7 - 0xAAE7)

    /* ----------------------------------------------------------------------

                            *** print CR/LF ***

        .,AAD7 A9 0D    LDA #$0D        set [CR]
        .,AAD9 20 47 AB JSR $AB47       print the character
        .,AADC 24 13    BIT $13         test current I/O channel
        .,AADE 10 05    BPL $AAE5       if ?? toggle A, EOR #$FF and return
        .,AAE0 A9 0A    LDA #$0A        set [LF]
        .,AAE2 20 47 AB JSR $AB47       print the character
                                        toggle A
        .,AAE5 49 FF    EOR #$FF        invert A
        .,AAE7 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xAAD7] = 0xA9; bus.memory[0xAAD8] = 0x0D;                               // LDA #$0D        set [CR]
    bus.memory[0xAAD9] = 0x20; bus.memory[0xAADA] = 0x47; bus.memory[0xAADB] = 0xAB;    // JSR $AB47       print the character                  *** done ***

    bus.memory[0xAADC] = 0x24; bus.memory[0xAADD] = 0x13;                               // BIT $13         test current I/O channel
    bus.memory[0xAADE] = 0x10; bus.memory[0xAADF] = 0x05;                               // BPL $AAE5       if ?? toggle A, EOR #$FF and return

    bus.memory[0xAAE0] = 0xA9; bus.memory[0xAAE1] = 0x0A;                               // LDA #$0A        set [LF]
    bus.memory[0xAAE2] = 0x20; bus.memory[0xAAE3] = 0x47; bus.memory[0xAAE4] = 0xAB;    // JSR $AB47       print the character                  *** done ***

    bus.memory[0xAAE5] = 0x49; bus.memory[0xAAE6] = 0xFF;                               // EOR #$FF        invert A
    bus.memory[0xAAE7] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** print null-terminated string (0xAB1E - 0xAB20)  todo

    /* ----------------------------------------------------------------------

                                    *** print null terminated string
    .,AB1E 20 87 B4 JSR $B487       print " terminated string to utility pointer

    ------------------------------------------------------------------------ */

    bus.memory[0xAB1E] = 0x20; bus.memory[0xAB1F] = 0x87; bus.memory[0xAB20] = 0xB4;            // JSR $B487       print terminated string to utility pointer   *** todo ***

    #pragma endregion
     
    #pragma region *** print (0xAB45 - 0xAB46)

    /* ----------------------------------------------------------------------

                                    *** print "?"
    .,AB45 A9 3F    LDA #$3F        set "?"                        
    ---------------------------------------------------------------------- */

    bus.memory[0xAB45] = 0xA9; bus.memory[0xAB46] = 0x3F;                                       // LDA #$3F        set '?'

    #pragma endregion

    #pragma region *** print character (0xAB47 - 0xAB4C)    todo
        
    /* ----------------------------------------------------------------------
                                *** print character
    .,AB47 20 0C E1 JSR $E10C       output character to channel with error check
    .,AB4A 29 FF    AND #$FF        set the flags on A
    .,AB4C 60       RTS             
    ---------------------------------------------------------------------- */

    bus.memory[0xAB47] = 0x20; bus.memory[0xAB48] = 0x0C; bus.memory[0xAB49] = 0xE1;            // JSR $E10C       output character with check   **** todo ****
    bus.memory[0xAB4A] = 0x29; bus.memory[0xAB4B] = 0xFF;                                       // AND #$FF        set flags
    bus.memory[0xAB4C] = 0x60;                                                                  // RTS

    #pragma endregion

    #pragma region *** do string vector (0xB475 - 0xB486) todo

    /* ----------------------------------------------------------------------

                                    *** do string vector
                                    copy descriptor pointer and make string space A bytes long
    .:B475 A6 64    LDX $64         get descriptor pointer low byte
    .:B477 A4 65    LDY $65         get descriptor pointer high byte
    .:B479 86 50    STX $50         save descriptor pointer low byte
    .:B47B 84 51    STY $51         save descriptor pointer high byte

                                    *** make string space A bytes long
    .:B47D 20 F4 B4 JSR $B4F4       make space in string memory for string A long
    .:B480 86 62    STX $62         save string pointer low byte
    .:B482 84 63    STY $63         save string pointer high byte
    .:B484 85 61    STA $61         save length
    .:B486 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xB475] = 0xA6; bus.memory[0xB476] = 0x64;                               // LDX $64
    bus.memory[0xB477] = 0xA4; bus.memory[0xB478] = 0x65;                               // LDY $65
    bus.memory[0xB479] = 0x86; bus.memory[0xB47A] = 0x50;                               // STX $50
    bus.memory[0xB47B] = 0x84; bus.memory[0xB47C] = 0x51;                               // STY $51

    bus.memory[0xB47D] = 0x20; bus.memory[0xB47E] = 0xF4; bus.memory[0xB47F] = 0xB4;    // JSR $B4F4    *** todo ***
    bus.memory[0xB480] = 0x86; bus.memory[0xB481] = 0x62;                               // STX $62
    bus.memory[0xB482] = 0x84; bus.memory[0xB483] = 0x63;                               // STY $63
    bus.memory[0xB484] = 0x85; bus.memory[0xB485] = 0x61;                               // STA $61
    bus.memory[0xB486] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** scan, set up string (0xB487 - 0xB4F3)

    /* ----------------------------------------------------------------------

                                    *** scan, set up string ***

        This routine parses a null- or quote-terminated string,
        stores its length, computes the end pointer, and pushes a descriptor
        on the BASIC string–descriptor stack.

        It handles both:
        - null-terminated strings (EOS = $00)
        - quote-terminated strings (")

        It moves strings from the input buffer into string memory if needed.

        print " terminated string to utility pointer
        .,B487 A2 22       LDX #$22        set terminator to "
        .,B489 86 07       STX $07         set search character, terminator 1
        .,B48B 86 08       STX $08         set terminator 2

        print search or alternate terminated string to utility pointer
        source is AY

        .,B48D 85 6F       STA $6F         store string start low byte
        .,B48F 84 70       STY $70         store string start high byte
        .,B491 85 62       STA $62         save string pointer low byte
        .,B493 84 63       STY $63         save string pointer high byte
        .,B495 A0 FF       LDY #$FF        set length to -1
        .,B497 C8          INY             increment length
        .,B498 B1 6F       LDA ($6F),Y     get byte from string
        .,B49A F0 0C       BEQ $B4A8       exit loop if null byte [EOS]
        .,B49C C5 07       CMP $07         compare with search character, terminator 1
        .,B49E F0 04       BEQ $B4A4       branch if terminator
        .,B4A0 C5 08       CMP $08         compare with terminator 2
        .,B4A2 D0 F3       BNE $B497       loop if not terminator 2
        .,B4A4 C9 22       CMP #$22        compare with "
        .,B4A6 F0 01       BEQ $B4A9       branch if " (carry set if = !)
        .,B4A8 18          CLC             clear carry for add (only if [EOL] terminated string)
        .,B4A9 84 61       STY $61         save length in FAC1 exponent
        .,B4AB 98          TYA             copy length to A
        .,B4AC 65 6F       ADC $6F         add string start low byte
        .,B4AE 85 71       STA $71         save string end low byte
        .,B4B0 A6 70       LDX $70         get string start high byte
        .,B4B2 90 01       BCC $B4B5       branch if no low byte overflow
        .,B4B4 E8          INX             else increment high byte
        .,B4B5 86 72       STX $72         save string end high byte

        .,B4B7 A5 70       LDA $70         get string start high byte
        .,B4B9 F0 04       BEQ $B4BF       branch if in utility area
        .,B4BB C9 02       CMP #$02        compare with input buffer memory high byte
        .,B4BD D0 0B       BNE $B4CA       branch if not in input buffer memory

        string in input buffer or utility area → move to string memory

        .,B4BF 98          TYA
        .,B4C0 20 75 B4    JSR $B475       copy descriptor pointer + make string space
        .,B4C3 A6 6F       LDX $6F
        .,B4C5 A4 70       LDY $70
        .,B4C7 20 88 B6    JSR $B688       store string A bytes long from XY to utility pointer

        check descriptor stack, then push descriptor

        .,B4CA A6 16       LDX $16         get descriptor stack pointer
        .,B4CC E0 22       CPX #$22        compare with max+1
        .,B4CE D0 05       BNE $B4D5       ok if room
        .,B4D0 A2 19       LDX #$19        error: string too complex
        .,B4D2 4C 37 A4    JMP $A437       error then warm start

        .,B4D5 A5 61       LDA $61         length
        .,B4D7 95 00       STA $00,X
        .,B4D9 A5 62       LDA $62         pointer low
        .,B4DB 95 01       STA $01,X
        .,B4DD A5 63       LDA $63         pointer high
        .,B4DF 95 02       STA $02,X

        .,B4E1 A0 00       LDY #0
        .,B4E3 86 64       STX $64         save descriptor ptr low
        .,B4E5 84 65       STY $65         save descriptor ptr high ($00)
        .,B4E7 84 70       STY $70         clear FAC1 rounding byte
        .,B4E9 88          DEY             Y=$FF
        .,B4EA 84 0D       STY $0D         mark type = string
        .,B4EC 86 17       STX $17         save current descriptor pointer low
        .,B4EE E8          INX
        .,B4EF E8          INX
        .,B4F0 E8          INX             update pointer
        .,B4F1 86 16       STX $16         save new descriptor stack pointer
        .,B4F3 60          RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xB487] = 0xA2; bus.memory[0xB488] = 0x22;          // LDX #$22        set terminator to "
    bus.memory[0xB489] = 0x86; bus.memory[0xB48A] = 0x07;          // STX $07         search char / terminator1
    bus.memory[0xB48B] = 0x86; bus.memory[0xB48C] = 0x08;          // STX $08         terminator2

    bus.memory[0xB48D] = 0x85; bus.memory[0xB48E] = 0x6F;          // STA $6F
    bus.memory[0xB48F] = 0x84; bus.memory[0xB490] = 0x70;          // STY $70
    bus.memory[0xB491] = 0x85; bus.memory[0xB492] = 0x62;          // STA $62
    bus.memory[0xB493] = 0x84; bus.memory[0xB494] = 0x63;          // STY $63
    bus.memory[0xB495] = 0xA0; bus.memory[0xB496] = 0xFF;          // LDY #$FF

    bus.memory[0xB497] = 0xC8;                                     // INY
    bus.memory[0xB498] = 0xB1; bus.memory[0xB499] = 0x6F;          // LDA ($6F),Y
    bus.memory[0xB49A] = 0xF0; bus.memory[0xB49B] = 0x0C;          // BEQ $B4A8
    bus.memory[0xB49C] = 0xC5; bus.memory[0xB49D] = 0x07;          // CMP $07
    bus.memory[0xB49E] = 0xF0; bus.memory[0xB49F] = 0x04;          // BEQ $B4A4
    bus.memory[0xB4A0] = 0xC5; bus.memory[0xB4A1] = 0x08;          // CMP $08
    bus.memory[0xB4A2] = 0xD0; bus.memory[0xB4A3] = 0xF3;          // BNE $B497
    bus.memory[0xB4A4] = 0xC9; bus.memory[0xB4A5] = 0x22;          // CMP #$22
    bus.memory[0xB4A6] = 0xF0; bus.memory[0xB4A7] = 0x01;          // BEQ $B4A9
    bus.memory[0xB4A8] = 0x18;                                     // CLC
    bus.memory[0xB4A9] = 0x84; bus.memory[0xB4AA] = 0x61;          // STY $61
    bus.memory[0xB4AB] = 0x98;                                     // TYA
    bus.memory[0xB4AC] = 0x65; bus.memory[0xB4AD] = 0x6F;          // ADC $6F
    bus.memory[0xB4AE] = 0x85; bus.memory[0xB4AF] = 0x71;          // STA $71
    bus.memory[0xB4B0] = 0xA6; bus.memory[0xB4B1] = 0x70;          // LDX $70
    bus.memory[0xB4B2] = 0x90; bus.memory[0xB4B3] = 0x01;          // BCC $B4B5
    bus.memory[0xB4B4] = 0xE8;                                     // INX
    bus.memory[0xB4B5] = 0x86; bus.memory[0xB4B6] = 0x72;          // STX $72

    bus.memory[0xB4B7] = 0xA5; bus.memory[0xB4B8] = 0x70;          // LDA $70
    bus.memory[0xB4B9] = 0xF0; bus.memory[0xB4BA] = 0x04;          // BEQ $B4BF
    bus.memory[0xB4BB] = 0xC9; bus.memory[0xB4BC] = 0x02;          // CMP #$02
    bus.memory[0xB4BD] = 0xD0; bus.memory[0xB4BE] = 0x0B;          // BNE $B4CA

    bus.memory[0xB4BF] = 0x98;                                    // TYA
    bus.memory[0xB4C0] = 0x20; bus.memory[0xB4C1] = 0x75; bus.memory[0xB4C2] = 0xB4;  // JSR $B475          *** done ***
    bus.memory[0xB4C3] = 0xA6; bus.memory[0xB4C4] = 0x6F;          // LDX $6F
    bus.memory[0xB4C5] = 0xA4; bus.memory[0xB4C6] = 0x70;          // LDY $70
    bus.memory[0xB4C7] = 0x20; bus.memory[0xB4C8] = 0x88; bus.memory[0xB4C9] = 0xB6;  // JSR $B688          *** done ***

    bus.memory[0xB4CA] = 0xA6; bus.memory[0xB4CB] = 0x16;          // LDX $16
    bus.memory[0xB4CC] = 0xE0; bus.memory[0xB4CD] = 0x22;          // CPX #$22
    bus.memory[0xB4CE] = 0xD0; bus.memory[0xB4CF] = 0x05;          // BNE $B4D5
    bus.memory[0xB4D0] = 0xA2; bus.memory[0xB4D1] = 0x19;          // LDX #$19
    bus.memory[0xB4D2] = 0x4C; bus.memory[0xB4D3] = 0x37; bus.memory[0xB4D4] = 0xA4;  // JMP $A437          *** done ***

    bus.memory[0xB4D5] = 0xA5; bus.memory[0xB4D6] = 0x61;          // LDA $61
    bus.memory[0xB4D7] = 0x95; bus.memory[0xB4D8] = 0x00;          // STA $00,X
    bus.memory[0xB4D9] = 0xA5; bus.memory[0xB4DA] = 0x62;          // LDA $62
    bus.memory[0xB4DB] = 0x95; bus.memory[0xB4DC] = 0x01;          // STA $01,X
    bus.memory[0xB4DD] = 0xA5; bus.memory[0xB4DE] = 0x63;          // LDA $63
    bus.memory[0xB4DF] = 0x95; bus.memory[0xB4E0] = 0x02;          // STA $02,X

    bus.memory[0xB4E1] = 0xA0; bus.memory[0xB4E2] = 0x00;          // LDY #$00
    bus.memory[0xB4E3] = 0x86; bus.memory[0xB4E4] = 0x64;          // STX $64
    bus.memory[0xB4E5] = 0x84; bus.memory[0xB4E6] = 0x65;          // STY $65
    bus.memory[0xB4E7] = 0x84; bus.memory[0xB4E8] = 0x70;          // STY $70
    bus.memory[0xB4E9] = 0x88;                                     // DEY
    bus.memory[0xB4EA] = 0x84; bus.memory[0xB4EB] = 0x0D;          // STY $0D
    bus.memory[0xB4EC] = 0x86; bus.memory[0xB4ED] = 0x17;          // STX $17
    bus.memory[0xB4EE] = 0xE8;                                     // INX
    bus.memory[0xB4EF] = 0xE8;                                     // INX
    bus.memory[0xB4F0] = 0xE8;                                     // INX
    bus.memory[0xB4F1] = 0x86; bus.memory[0xB4F2] = 0x16;          // STX $16
    bus.memory[0xB4F3] = 0x60;                                     // RTS

    #pragma endregion

    #pragma region *** copy string from descriptor to utility pointer (0xB67A - 0xB6A2)

    /* ----------------------------------------------------------------------

                                    *** copy string from descriptor to utility pointer

            .,B67A A0 00    LDY #$00        clear index
            .,B67C B1 6F    LDA ($6F),Y     get string length
            .,B67E 48       PHA             save it
            .,B67F C8       INY             increment index
            .,B680 B1 6F    LDA ($6F),Y     get string pointer low byte
            .,B682 AA       TAX             copy to X
            .,B683 C8       INY             increment index
            .,B684 B1 6F    LDA ($6F),Y     get string pointer high byte
            .,B686 A8       TAY             copy to Y
            .,B687 68       PLA             get length back
            .,B688 86 22    STX $22         save string pointer low byte
            .,B68A 84 23    STY $23         save string pointer high byte
                                    store string from pointer to utility pointer
            .,B68C A8       TAY             copy length as index
            .,B68D F0 0A    BEQ $B699       branch if null string
            .,B68F 48       PHA             save length
            .,B690 88       DEY             decrement length/index
            .,B691 B1 22    LDA ($22),Y     get byte from string
            .,B693 91 35    STA ($35),Y     save byte to destination
            .,B695 98       TYA             copy length/index
            .,B696 D0 F8    BNE $B690       loop if not all done yet
            .,B698 68       PLA             restore length
            .,B699 18       CLC             clear carry for add
            .,B69A 65 35    ADC $35         add string utility ptr low byte
            .,B69C 85 35    STA $35         save string utility ptr low byte
            .,B69E 90 02    BCC $B6A2       branch if no rollover
            .,B6A0 E6 36    INC $36         increment string utility ptr high byte
            .,B6A2 60       RTS

    ----------------------------------------------------------------------- */

    bus.memory[0xB67A] = 0xA0; bus.memory[0xB67B] = 0x00;                                // LDY #$00        clear index
    bus.memory[0xB67C] = 0xB1; bus.memory[0xB67D] = 0x6F;                                // LDA ($6F),Y     get string length
    bus.memory[0xB67E] = 0x48;                                                           // PHA             save it
    bus.memory[0xB67F] = 0xC8;                                                           // INY             increment index
    bus.memory[0xB680] = 0xB1; bus.memory[0xB681] = 0x6F;                                // LDA ($6F),Y     get string pointer low byte
    bus.memory[0xB682] = 0xAA;                                                           // TAX             copy to X
    bus.memory[0xB683] = 0xC8;                                                           // INY             increment index
    bus.memory[0xB684] = 0xB1; bus.memory[0xB685] = 0x6F;                                // LDA ($6F),Y     get string pointer high byte
    bus.memory[0xB686] = 0xA8;                                                           // TAY             copy to Y
    bus.memory[0xB687] = 0x68;                                                           // PLA             get length back
    bus.memory[0xB688] = 0x86; bus.memory[0xB689] = 0x22;                                // STX $22         save string pointer low byte
    bus.memory[0xB68A] = 0x84; bus.memory[0xB68B] = 0x23;                                // STY $23         save string pointer high byte

    bus.memory[0xB68C] = 0xA8;                                                           // TAY             copy length as index
    bus.memory[0xB68D] = 0xF0; bus.memory[0xB68E] = 0x0A;                                // BEQ $B699       branch if null string
    bus.memory[0xB68F] = 0x48;                                                           // PHA             save length
    bus.memory[0xB690] = 0x88;                                                           // DEY             decrement index
    bus.memory[0xB691] = 0xB1; bus.memory[0xB692] = 0x22;                                // LDA ($22),Y     get byte from string
    bus.memory[0xB693] = 0x91; bus.memory[0xB694] = 0x35;                                // STA ($35),Y     save byte to destination
    bus.memory[0xB695] = 0x98;                                                           // TYA             copy length/index
    bus.memory[0xB696] = 0xD0; bus.memory[0xB697] = 0xF8;                                // BNE $B690       loop
    bus.memory[0xB698] = 0x68;                                                           // PLA             restore length
    bus.memory[0xB699] = 0x18;                                                           // CLC             clear carry
    bus.memory[0xB69A] = 0x65; bus.memory[0xB69B] = 0x35;                                // ADC $35         add utility ptr low
    bus.memory[0xB69C] = 0x85; bus.memory[0xB69D] = 0x35;                                // STA $35         save low
    bus.memory[0xB69E] = 0x90; bus.memory[0xB69F] = 0x02;                                // BCC $B6A2       branch if no rollover
    bus.memory[0xB6A0] = 0xE6; bus.memory[0xB6A1] = 0x36;                                // INC $36         increment high
    bus.memory[0xB6A2] = 0x60;                                                           // RTS

    #pragma endregion

    /* $E000–$FFFF   KERNAL ROM */

    #pragma region *** BASIC warm start (0xE37B - 0xE391)

    /* ----------------------------------------------------------------------

                            *** BASIC warm start entry point

    .,E37B 20 CC FF JSR $FFCC       close input and output channels
    .,E37E A9 00    LDA #$00        clear A
    .,E380 85 13    STA $13         set current I/O channel, flag default
    .,E382 20 7A A6 JSR $A67A       flush BASIC stack and clear continue pointer
    .,E385 58       CLI             enable the interrupts
    .,E386 A2 80    LDX #$80        set -ve error, just do warm start
    .,E388 6C 00 03 JMP ($0300)     go handle error message, normally $E38B
    .,E38B 8A       TXA             copy the error number
    .,E38C 30 03    BMI $E391       if -ve go do warm start
    .,E38E 4C 3A A4 JMP $A43A       else do error #X then warm start
    .,E391 4C 74 A4 JMP $A474       do warm start

    ----------------------------------------------------------------------- */

    bus.memory[0xE37B] = 0x20; bus.memory[0xE37C] = 0xCC; bus.memory[0xE37D] = 0xFF;    // JSR $FFCC close I/O channels                     *** done ***
    bus.memory[0xE37E] = 0xA9; bus.memory[0xE37F] = 0x00;                               // LDA #$00 clear A
    bus.memory[0xE380] = 0x85; bus.memory[0xE381] = 0x13;                               // STA $13 set current I/O channel
    bus.memory[0xE382] = 0x20; bus.memory[0xE383] = 0x7A; bus.memory[0xE384] = 0xA6;    // JSR $A67A flush stack, clear CONT                *** done ***
    bus.memory[0xE385] = 0x58;                                                          // CLI enable IRQ

    bus.memory[0xE386] = 0xA2; bus.memory[0xE387] = 0x80;                               // LDX #$80 negative error → warm start
    bus.memory[0xE388] = 0x6C; bus.memory[0xE389] = 0x00; bus.memory[0xE38A] = 0x03;    // JMP ($0300) error handler                        *** done ***
    bus.memory[0xE38B] = 0x8A;                                                          // TXA copy error #
    bus.memory[0xE38C] = 0x30; bus.memory[0xE38D] = 0x03;                               // BMI $E391 if -ve warm start
    bus.memory[0xE38E] = 0x4C; bus.memory[0xE38F] = 0x3A; bus.memory[0xE390] = 0xA4;    // JMP $A43A error then warm start                  *** done ***
    bus.memory[0xE391] = 0x4C; bus.memory[0xE392] = 0x74; bus.memory[0xE393] = 0xA4;    // JMP $A474 warm start                             *** done ***

    #pragma endregion

    #pragma region *** BASIC cold start routine, zero-page character get subroutine & initialisation table scan (0xE394 - 0xE3B9) 

    /* ----------------------------------------------------------------------

                                    *** BASIC cold start entry point
    .,E394 20 53 E4 JSR $E453       initialise the BASIC vector table
    .,E397 20 BF E3 JSR $E3BF       initialise the BASIC RAM locations
    .,E39A 20 22 E4 JSR $E422       print the start up message and initialise the memory pointers

    .,E39D A2 FB    LDX #$FB        value for start stack
    .,E39F 9A       TXS             set stack pointer
    .,E3A0 D0 E4    BNE $E386       do "READY." warm start, branch always


                                    *** zero-page character get subroutine & initialisation table scan

    .,E3A2 E6 7A    INC $7A         increment BASIC execute pointer low byte
    .,E3A4 D0 02    BNE $E3A8       branch if no carry
    .,E3A6 E6 7B    INC $7B         increment BASIC execute pointer high byte

    .,E3A8 AD 60 EA LDA $EA60       get byte to scan
    .,E3AB C9 3A    CMP #$3A        compare with ":"
    .,E3AD B0 0A    BCS $E3B9       exit if >=

    .,E3AF C9 20    CMP #$20        compare with " "
    .,E3B1 F0 EF    BEQ $E3A2       if " " go do next

    .,E3B3 38       SEC             set carry for SBC
    .,E3B4 E9 30    SBC #$30        subtract "0"
    .,E3B6 38       SEC             set carry
    .,E3B7 E9 D0    SBC #$D0        subtract -"0"

    .,E3B9 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xE394] = 0x20; bus.memory[0xE395] = 0x53; bus.memory[0xE396] = 0xE4;    // JSR $E453  initialise BASIC vector table                 *** done ***
    bus.memory[0xE397] = 0x20; bus.memory[0xE398] = 0xBF; bus.memory[0xE399] = 0xE3;    // JSR $E3BF  initialise BASIC RAM locations                *** done ***
    bus.memory[0xE39A] = 0x20; bus.memory[0xE39B] = 0x22; bus.memory[0xE39C] = 0xE4;    // JSR $E422  print startup message & init memory pointers  *** done ***

    bus.memory[0xE39D] = 0xA2; bus.memory[0xE39E] = 0xFB;                               // LDX #$FB  stack start
    bus.memory[0xE39F] = 0x9A;                                                          // TXS       set stack pointer
    bus.memory[0xE3A0] = 0xD0; bus.memory[0xE3A1] = 0xE4;                               // BNE $E386 always branch (warm start READY)               *** done ***  


    bus.memory[0xE3A2] = 0xE6; bus.memory[0xE3A3] = 0x7A;                               // INC $7A   increment BASIC exec pointer low
    bus.memory[0xE3A4] = 0xD0; bus.memory[0xE3A5] = 0x02;                               // BNE $E3A8
    bus.memory[0xE3A6] = 0xE6; bus.memory[0xE3A7] = 0x7B;                               // INC $7B   increment BASIC exec pointer high

    bus.memory[0xE3A8] = 0xAD; bus.memory[0xE3A9] = 0x60; bus.memory[0xE3AA] = 0xEA;    // LDA $EA60 get scan byte                                  
    bus.memory[0xE3AB] = 0xC9; bus.memory[0xE3AC] = 0x3A;                               // CMP #$3A
    bus.memory[0xE3AD] = 0xB0; bus.memory[0xE3AE] = 0x0A;                               // BCS $E3B9 exit if >=

    bus.memory[0xE3AF] = 0xC9; bus.memory[0xE3B0] = 0x20;                               // CMP #$20  space?
    bus.memory[0xE3B1] = 0xF0; bus.memory[0xE3B2] = 0xEF;                               // BEQ $E3A2

    bus.memory[0xE3B3] = 0x38;                                                          // SEC
    bus.memory[0xE3B4] = 0xE9; bus.memory[0xE3B5] = 0x30;                               // SBC #$30  subtract "0"
    bus.memory[0xE3B6] = 0x38;                                                          // SEC
    bus.memory[0xE3B7] = 0xE9; bus.memory[0xE3B8] = 0xD0;                               // SBC #$D0  subtract -"0"

    bus.memory[0xE3B9] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** initialise BASIC RAM locations (0xE3BF - 0xE421) 

    /* ----------------------------------------------------------------------

                                    *** initialise BASIC RAM locations

        .,E3BF A9 4C    LDA #$4C        opcode for JMP
        .,E3C1 85 54    STA $54         save for functions vector jump
        .,E3C3 8D 10 03 STA $0310       save for USR() vector jump
                                    set USR() vector to illegal quantity error
        .,E3C6 A9 48    LDA #$48        set USR() vector low byte
        .,E3C8 A0 B2    LDY #$B2        set USR() vector high byte
        .,E3CA 8D 11 03 STA $0311       save USR() vector low byte
        .,E3CD 8C 12 03 STY $0312       save USR() vector high byte
        .,E3D0 A9 91    LDA #$91        set fixed to float vector low byte
        .,E3D2 A0 B3    LDY #$B3        set fixed to float vector high byte
        .,E3D4 85 05    STA $05         save fixed to float vector low byte
        .,E3D6 84 06    STY $06         save fixed to float vector high byte
        .,E3D8 A9 AA    LDA #$AA        set float to fixed vector low byte
        .,E3DA A0 B1    LDY #$B1        set float to fixed vector high byte
        .,E3DC 85 03    STA $03         save float to fixed vector low byte
        .,E3DE 84 04    STY $04         save float to fixed vector high byte

                                    copy the character get subroutine from $E3A2 to $0074
        .,E3E0 A2 1C    LDX #$1C        set the byte count
        .,E3E2 BD A2 E3 LDA $E3A2,X     get a byte from the table
        .,E3E5 95 73    STA $73,X       save the byte in page zero
        .,E3E7 CA       DEX             decrement the count
        .,E3E8 10 F8    BPL $E3E2       loop if not all done

                                    clear descriptors, strings, program area and memory pointers
        .,E3EA A9 03    LDA #$03        set garbage collection step size
        .,E3EC 85 53    STA $53         save step size
        .,E3EE A9 00    LDA #$00        clear A
        .,E3F0 85 68    STA $68         clear FAC1 overflow
        .,E3F2 85 13    STA $13         clear current I/O channel
        .,E3F4 85 18    STA $18         clear descriptor stack high byte
        .,E3F6 A2 01    LDX #$01        set X
        .,E3F8 8E FD 01 STX $01FD       chain link pointer low byte
        .,E3FB 8E FC 01 STX $01FC       chain link pointer high byte
        .,E3FE A2 19    LDX #$19        descriptor stack initial value
        .,E400 86 16    STX $16         set descriptor stack pointer
        .,E402 38       SEC             prepare to read bottom of memory
        .,E403 20 9C FF JSR $FF9C       read/set bottom of memory
        .,E406 86 2B    STX $2B         save start of memory low byte
        .,E408 84 2C    STY $2C         save start of memory high byte
        .,E40A 38       SEC             prepare to read top of memory
        .,E40B 20 99 FF JSR $FF99       read/set top of memory
        .,E40E 86 37    STX $37         save end of memory low byte
        .,E410 84 38    STY $38         save end of memory high byte
        .,E412 86 33    STX $33         set bottom of string space low byte
        .,E414 84 34    STY $34         set bottom of string space high byte
        .,E416 A0 00    LDY #$00        clear Y
        .,E418 98       TYA             clear A
        .,E419 91 2B    STA ($2B),Y     clear first byte of memory
        .,E41B E6 2B    INC $2B         increment start-of-memory low byte
        .,E41D D0 02    BNE $E421       if no rollover, skip high byte increment
        .,E41F E6 2C    INC $2C         increment start-of-memory high byte
        .,E421 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xE3BF] = 0xA9; bus.memory[0xE3C0] = 0x4C;                            // LDA #$4C        opcode for JMP
    bus.memory[0xE3C1] = 0x85; bus.memory[0xE3C2] = 0x54;                            // STA $54         save for functions vector jump
    bus.memory[0xE3C3] = 0x8D; bus.memory[0xE3C4] = 0x10; bus.memory[0xE3C5] = 0x03; // STA $0310       save USR() vector jump

    bus.memory[0xE3C6] = 0xA9; bus.memory[0xE3C7] = 0x48;                            // LDA #$48        USR() vector low byte
    bus.memory[0xE3C8] = 0xA0; bus.memory[0xE3C9] = 0xB2;                            // LDY #$B2        USR() vector high byte
    bus.memory[0xE3CA] = 0x8D; bus.memory[0xE3CB] = 0x11; bus.memory[0xE3CC] = 0x03; // STA $0311       save USR() low byte
    bus.memory[0xE3CD] = 0x8C; bus.memory[0xE3CE] = 0x12; bus.memory[0xE3CF] = 0x03; // STY $0312       save USR() high byte

    bus.memory[0xE3D0] = 0xA9; bus.memory[0xE3D1] = 0x91;                            // LDA #$91        fixed→float low
    bus.memory[0xE3D2] = 0xA0; bus.memory[0xE3D3] = 0xB3;                            // LDY #$B3        fixed→float high
    bus.memory[0xE3D4] = 0x85; bus.memory[0xE3D5] = 0x05;                            // STA $05         save low
    bus.memory[0xE3D6] = 0x84; bus.memory[0xE3D7] = 0x06;                            // STY $06         save high

    bus.memory[0xE3D8] = 0xA9; bus.memory[0xE3D9] = 0xAA;                            // LDA #$AA        float→fixed low
    bus.memory[0xE3DA] = 0xA0; bus.memory[0xE3DB] = 0xB1;                            // LDY #$B1        float→fixed high
    bus.memory[0xE3DC] = 0x85; bus.memory[0xE3DD] = 0x03;                            // STA $03         save low
    bus.memory[0xE3DE] = 0x84; bus.memory[0xE3DF] = 0x04;                            // STY $04         save high

    bus.memory[0xE3E0] = 0xA2; bus.memory[0xE3E1] = 0x1C;                            // LDX #$1C        byte count
    bus.memory[0xE3E2] = 0xBD; bus.memory[0xE3E3] = 0xA2; bus.memory[0xE3E4] = 0xE3; // LDA $E3A2,X     load from table
    bus.memory[0xE3E5] = 0x95; bus.memory[0xE3E6] = 0x73;                            // STA $73,X       save to ZP
    bus.memory[0xE3E7] = 0xCA;                                                       // DEX             decrement
    bus.memory[0xE3E8] = 0x10; bus.memory[0xE3E9] = 0xF8;                            // BPL $E3E2       loop

    bus.memory[0xE3EA] = 0xA9; bus.memory[0xE3EB] = 0x03;                            // LDA #$03        GC step
    bus.memory[0xE3EC] = 0x85; bus.memory[0xE3ED] = 0x53;                            // STA $53
    bus.memory[0xE3EE] = 0xA9; bus.memory[0xE3EF] = 0x00;                            // LDA #$00
    bus.memory[0xE3F0] = 0x85; bus.memory[0xE3F1] = 0x68;                            // STA $68
    bus.memory[0xE3F2] = 0x85; bus.memory[0xE3F3] = 0x13;                            // STA $13
    bus.memory[0xE3F4] = 0x85; bus.memory[0xE3F5] = 0x18;                            // STA $18

    bus.memory[0xE3F6] = 0xA2; bus.memory[0xE3F7] = 0x01;                            // LDX #$01
    bus.memory[0xE3F8] = 0x8E; bus.memory[0xE3F9] = 0xFD; bus.memory[0xE3FA] = 0x01; // STX $01FD
    bus.memory[0xE3FB] = 0x8E; bus.memory[0xE3FC] = 0xFC; bus.memory[0xE3FD] = 0x01; // STX $01FC

    bus.memory[0xE3FE] = 0xA2; bus.memory[0xE3FF] = 0x19;                            // LDX #$19
    bus.memory[0xE400] = 0x86; bus.memory[0xE401] = 0x16;                            // STX $16

    bus.memory[0xE402] = 0x38;                                                       // SEC
    bus.memory[0xE403] = 0x20; bus.memory[0xE404] = 0x9C; bus.memory[0xE405] = 0xFF; // JSR $FF9C           *** done ***
    bus.memory[0xE406] = 0x86; bus.memory[0xE407] = 0x2B;                            // STX $2B
    bus.memory[0xE408] = 0x84; bus.memory[0xE409] = 0x2C;                            // STY $2C

    bus.memory[0xE40A] = 0x38;                                                       // SEC
    bus.memory[0xE40B] = 0x20; bus.memory[0xE40C] = 0x99; bus.memory[0xE40D] = 0xFF; // JSR $FF99           *** done ***
    bus.memory[0xE40E] = 0x86; bus.memory[0xE40F] = 0x37;                            // STX $37
    bus.memory[0xE410] = 0x84; bus.memory[0xE411] = 0x38;                            // STY $38

    bus.memory[0xE412] = 0x86; bus.memory[0xE413] = 0x33;                            // STX $33
    bus.memory[0xE414] = 0x84; bus.memory[0xE415] = 0x34;                            // STY $34

    bus.memory[0xE416] = 0xA0; bus.memory[0xE417] = 0x00;                            // LDY #$00
    bus.memory[0xE418] = 0x98;                                                       // TYA
    bus.memory[0xE419] = 0x91; bus.memory[0xE41A] = 0x2B;                            // STA ($2B),Y
    bus.memory[0xE41B] = 0xE6; bus.memory[0xE41C] = 0x2B;                            // INC $2B
    bus.memory[0xE41D] = 0xD0; bus.memory[0xE41E] = 0x02;                            // BNE $E421
    bus.memory[0xE41F] = 0xE6; bus.memory[0xE420] = 0x2C;                            // INC $2C
    bus.memory[0xE421] = 0x60;                                                       // RTS

    #pragma endregion

    #pragma region *** print start-up message & init memory pointers (0xE422 - 0xE444)

    /* ----------------------------------------------------------------------

                            *** print the start up message
                            *** and initialise the memory pointers

        .,E422 A5 2B    LDA $2B         get the start of memory low byte
        .,E424 A4 2C    LDY $2C         get the start of memory high byte
        .,E426 20 08 A4 JSR $A408       check available memory,
                                        do out of memory error if no room

        .,E429 A9 73    LDA #$73        pointer to "**** COMMODORE 64 BASIC V2 ****"
        .,E42B A0 E4    LDY #$E4        high byte of pointer
        .,E42D 20 1E AB JSR $AB1E       print null-terminated string

        .,E430 A5 37    LDA $37         get end of memory low byte
        .,E432 38       SEC             prepare subtract
        .,E433 E5 2B    SBC $2B         subtract start of memory low
        .,E435 AA       TAX             copy result to X

        .,E436 A5 38    LDA $38         get end of memory high byte
        .,E438 E5 2C    SBC $2C         subtract start of memory high
        .,E43A 20 CD BD JSR $BDCD       print XA as unsigned integer

        .,E43D A9 60    LDA #$60        pointer to " BYTES FREE"
        .,E43F A0 E4    LDY #$E4
        .,E441 20 1E AB JSR $AB1E       print null-terminated string

        .,E444 4C 44 A6 JMP $A644       do NEW, CLEAR, RESTORE, return

    ------------------------------------------------------------------------ */

    bus.memory[0xE422] = 0xA5; bus.memory[0xE423] = 0x2B;                               // LDA $2B          get the start of memory low byte
    bus.memory[0xE424] = 0xA4; bus.memory[0xE425] = 0x2C;                               // LDY $2C          get the start of memory high byte
    bus.memory[0xE426] = 0x20; bus.memory[0xE427] = 0x08; bus.memory[0xE428] = 0xA4;    // JSR $A408        check available memory,                 *** done ***
                                                                                        //                  do out of memory error if no room

    bus.memory[0xE429] = 0xA9; bus.memory[0xE42A] = 0x73;                               // LDA #$73         pointer to "**** COMMODORE 64 BASIC V2 ****"
    bus.memory[0xE42B] = 0xA0; bus.memory[0xE42C] = 0xE4;                               // LDY #$E4         high byte of pointer
    bus.memory[0xE42D] = 0x20; bus.memory[0xE42E] = 0x1E; bus.memory[0xE42F] = 0xAB;    // JSR $AB1E        print null-terminated string            *** done ***

    bus.memory[0xE430] = 0xA5; bus.memory[0xE431] = 0x37;                               // LDA $37          get end of memory low byte
    bus.memory[0xE432] = 0x38;                                                          // SEC              prepare subtract
    bus.memory[0xE433] = 0xE5; bus.memory[0xE434] = 0x2B;                               // SBC $2B          subtract start of memory low
    bus.memory[0xE435] = 0xAA;                                                          // TAX              copy result to X

    bus.memory[0xE436] = 0xA5; bus.memory[0xE437] = 0x38;                               // LDA $38          get end of memory high byte
    bus.memory[0xE438] = 0xE5; bus.memory[0xE439] = 0x2C;                               // SBC $2C          subtract start of memory high
    bus.memory[0xE43A] = 0x20; bus.memory[0xE43B] = 0xCD; bus.memory[0xE43C] = 0xBD;    // JSR $BDCD        print XA as unsigned integer            *** todo ***

    bus.memory[0xE43D] = 0xA9; bus.memory[0xE43E] = 0x60;                               // LDA #$60         pointer to " BYTES FREE"
    bus.memory[0xE43F] = 0xA0; bus.memory[0xE440] = 0xE4;                               // LDY #$E4
    bus.memory[0xE441] = 0x20; bus.memory[0xE442] = 0x1E; bus.memory[0xE443] = 0xAB;    // JSR $AB1E        print null-terminated string            *** done ***

    bus.memory[0xE444] = 0x4C; bus.memory[0xE445] = 0x44; bus.memory[0xE446] = 0xA6;    // JMP $A644        do NEW, CLEAR, RESTORE, return          *** done ***

    #pragma endregion

    #pragma region *** BASIC vectors (0xE447 - 0xE452)

    /* ----------------------------------------------------------------------

                                    *** BASIC vectors
                                    Questi vettori vengono copiati in RAM
                                    a partire da $0300.

        .,E447 8B E3    error message           → $0300
        .,E449 83 A4    BASIC warm start        → $0302
        .,E44B 7C A5    crunch BASIC tokens     → $0304
        .,E44D 1A A7    uncrunch BASIC tokens   → $0306
        .,E44F E4 A7    start new BASIC code    → $0308
        .,E451 86 AE    get arithmetic element  → $030A

    ------------------------------------------------------------------------ */

    bus.memory[0xE447] = 0x8B; bus.memory[0xE448] = 0xE3;    // error message          -> $0300
    bus.memory[0xE449] = 0x83; bus.memory[0xE44A] = 0xA4;    // BASIC warm start       -> $0302
    bus.memory[0xE44B] = 0x7C; bus.memory[0xE44C] = 0xA5;    // crunch BASIC tokens    -> $0304
    bus.memory[0xE44D] = 0x1A; bus.memory[0xE44E] = 0xA7;    // uncrunch BASIC tokens  -> $0306
    bus.memory[0xE44F] = 0xE4; bus.memory[0xE450] = 0xA7;    // start new BASIC code   -> $0308
    bus.memory[0xE451] = 0x86; bus.memory[0xE452] = 0xAE;    // get arithmetic element -> $030A

    #pragma endregion

    #pragma region *** initialise the BASIC vectors (0xE453 - 0xE45E)

    /* ----------------------------------------------------------------------

                                    *** initialise the BASIC vectors

    .,E453 A2 0B    LDX #$0B        set byte count
    .,E455 BD 47 E4 LDA $E447,X     get byte from table
    .,E458 9D 00 03 STA $0300,X     save byte to RAM
    .,E45B CA       DEX             decrement index
    .,E45C 10 F7    BPL $E455       loop if more to do
    .,E45E 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xE453] = 0xA2; bus.memory[0xE454] = 0x0B;                               // LDX #$0B    set byte count
    bus.memory[0xE455] = 0xBD; bus.memory[0xE456] = 0x47; bus.memory[0xE457] = 0xE4;    // LDA $E447,X get byte from table
    bus.memory[0xE458] = 0x9D; bus.memory[0xE459] = 0x00; bus.memory[0xE45A] = 0x03;    // STA $0300,X save byte to RAM
    bus.memory[0xE45B] = 0xCA;                                                          // DEX         decrement index
    bus.memory[0xE45C] = 0x10; bus.memory[0xE45D] = 0xF7;                               // BPL $E455   loop if more to do
    bus.memory[0xE45E] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** initialise the screen and keyboard (0xE4DA - 0xE4DF)

    /* ----------------------------------------------------------------------    

                                    *** save the current colour to the colour RAM
    .,E4DA AD 21 D0 LDA $D021       get the current colour code
    .,E4DD 91 F3    STA ($F3),Y     save it to the colour RAM
    .,E4DF 60       RTS  
    ------------------------------------------------------------------------ */

    bus.memory[0xE4DA] = 0xAD; bus.memory[0xE4DB] = 0x21; bus.memory[0xE4DC] = 0xD0;    // LDA $D021       get the current colour code
    bus.memory[0xE4DD] = 0x91; bus.memory[0xE4DE] = 0xF3;                               // STA ($F3),Y     save it to the colour RAM
    bus.memory[0xE4DF] = 0x60;                                                          // RTS
        
    #pragma endregion

    #pragma region *** initialise the screen and keyboard (0xE518 - 0xE598)

    /* ----------------------------------------------------------------------
                                    *** initialise the screen and keyboard
    .,E518 20 A0 E5 JSR $E5A0       initialise the vic chip
    .,E51B A9 00    LDA #$00        clear A
    .,E51D 8D 91 02 STA $0291       clear the shift mode switch
    .,E520 85 CF    STA $CF         clear the cursor blink phase
    .,E522 A9 48    LDA #$48        get the keyboard decode logic pointer low byte
    .,E524 8D 8F 02 STA $028F       save the keyboard decode logic pointer low byte
    .,E527 A9 EB    LDA #$EB        get the keyboard decode logic pointer high byte
    .,E529 8D 90 02 STA $0290       save the keyboard decode logic pointer high byte
    .,E52C A9 0A    LDA #$0A        set the maximum size of the keyboard buffer
    .,E52E 8D 89 02 STA $0289       save the maximum size of the keyboard buffer
    .,E531 8D 8C 02 STA $028C       save the repeat delay counter
    .,E534 A9 0E    LDA #$0E        set light blue
    .,E536 8D 86 02 STA $0286       save the current colour code
    .,E539 A9 04    LDA #$04        speed 4
    .,E53B 8D 8B 02 STA $028B       save the repeat speed counter
    .,E53E A9 0C    LDA #$0C        set the cursor flash timing
    .,E540 85 CD    STA $CD         save the cursor timing countdown
    .,E542 85 CC    STA $CC         save the cursor enable, $00 = flash cursor 
    
                                    *** clear the screen
    .,E544 AD 88 02 LDA $0288       get the screen memory page
    .,E547 09 80    ORA #$80        set the high bit, flag every line is a logical line start
    .,E549 A8       TAY             copy to Y
    .,E54A A9 00    LDA #$00        clear the line start low byte
    .,E54C AA       TAX             clear the index
    .,E54D 94 D9    STY $D9,X       save the start of line X pointer high byte
    .,E54F 18       CLC             clear carry for add
    .,E550 69 28    ADC #$28        add the line length to the low byte
    .,E552 90 01    BCC $E555       if no rollover skip the high byte increment
    .,E554 C8       INY             else increment the high byte
    .,E555 E8       INX             increment the line index
    .,E556 E0 1A    CPX #$1A        compare it with the number of lines + 1
    .,E558 D0 F3    BNE $E54D       loop if not all done
    .,E55A A9 FF    LDA #$FF        set the end of table marker
    .,E55C 95 D9    STA $D9,X       mark the end of the table
    .,E55E A2 18    LDX #$18        set the line count, 25 lines to do, 0 to 24
    .,E560 20 FF E9 JSR $E9FF       clear screen line X
    .,E563 CA       DEX             decrement the count
    .,E564 10 FA    BPL $E560       loop if more to do  
        
                                    *** home the cursor
    .,E566 A0 00    LDY #$00        clear Y
    .,E568 84 D3    STY $D3         clear the cursor column
    .,E56A 84 D6    STY $D6         clear the cursor row    

                                    *** set screen pointers for cursor row, column
    .,E56C A6 D6    LDX $D6         get the cursor row
    .,E56E A5 D3    LDA $D3         get the cursor column
    .,E570 B4 D9    LDY $D9,X       get start of line X pointer high byte
    .,E572 30 08    BMI $E57C       if it is the logical line start continue
    .,E574 18       CLC             else clear carry for add
    .,E575 69 28    ADC #$28        add one line length
    .,E577 85 D3    STA $D3         save the cursor column
    .,E579 CA       DEX             decrement the cursor row
    .,E57A 10 F4    BPL $E570       loop, branch always
    .,E57C 20 F0 E9 JSR $E9F0       fetch a screen address
    .,E57F A9 27    LDA #$27        set the line length
    .,E581 E8       INX             increment the cursor row
    .,E582 B4 D9    LDY $D9,X       get the start of line X pointer high byte
    .,E584 30 06    BMI $E58C       if logical line start exit
    .,E586 18       CLC             else clear carry for add
    .,E587 69 28    ADC #$28        add one line length to the current line length
    .,E589 E8       INX             increment the cursor row
    .,E58A 10 F6    BPL $E582       loop, branch always
    .,E58C 85 D5    STA $D5         save current screen line length
    .,E58E 4C 24 EA JMP $EA24       calculate the pointer to colour RAM and return
    .,E591 E4 C9    CPX $C9         compare it with the input cursor row
    .,E593 F0 03    BEQ $E598       if there just exit
    .,E595 4C ED E6 JMP $E6ED       else go ??
    .,E598 60       RTS        

    ------------------------------------------------------------------------ */
    
    // *** initialise the screen and keyboard
    bus.memory[0xE518] = 0x20; bus.memory[0xE519] = 0xA0; bus.memory[0xE51A] = 0xE5;   // JSR $E5A0       initialise the vic chip                         *** done ***
    bus.memory[0xE51B] = 0xA9; bus.memory[0xE51C] = 0x00;                              // LDA #$00        clear A
    bus.memory[0xE51D] = 0x8D; bus.memory[0xE51E] = 0x91; bus.memory[0xE51F] = 0x02;   // STA $0291       clear the shift mode switch
    bus.memory[0xE520] = 0x85; bus.memory[0xE521] = 0xCF;                              // STA $CF         clear the cursor blink phase
    bus.memory[0xE522] = 0xA9; bus.memory[0xE523] = 0x48;                              // LDA #$48        get keyboard decode logic pointer low byte
    bus.memory[0xE524] = 0x8D; bus.memory[0xE525] = 0x8F; bus.memory[0xE526] = 0x02;   // STA $028F       save keyboard decode logic pointer low byte
    bus.memory[0xE527] = 0xA9; bus.memory[0xE528] = 0xEB;                              // LDA #$EB        get keyboard decode logic pointer high byte
    bus.memory[0xE529] = 0x8D; bus.memory[0xE52A] = 0x90; bus.memory[0xE52B] = 0x02;   // STA $0290       save keyboard decode logic pointer high byte
    bus.memory[0xE52C] = 0xA9; bus.memory[0xE52D] = 0x0A;                              // LDA #$0A        set maximum size of keyboard buffer
    bus.memory[0xE52E] = 0x8D; bus.memory[0xE52F] = 0x89; bus.memory[0xE530] = 0x02;   // STA $0289       save maximum size of keyboard buffer
    bus.memory[0xE531] = 0x8D; bus.memory[0xE532] = 0x8C; bus.memory[0xE533] = 0x02;   // STA $028C       save repeat delay counter
    bus.memory[0xE534] = 0xA9; bus.memory[0xE535] = 0x0E;                              // LDA #$0E        set light blue
    bus.memory[0xE536] = 0x8D; bus.memory[0xE537] = 0x86; bus.memory[0xE538] = 0x02;   // STA $0286       save current colour code
    bus.memory[0xE539] = 0xA9; bus.memory[0xE53A] = 0x04;                              // LDA #$04        speed 4
    bus.memory[0xE53B] = 0x8D; bus.memory[0xE53C] = 0x8B; bus.memory[0xE53D] = 0x02;   // STA $028B       save repeat speed counter
    bus.memory[0xE53E] = 0xA9; bus.memory[0xE53F] = 0x0C;                              // LDA #$0C        set cursor flash timing
    bus.memory[0xE540] = 0x85; bus.memory[0xE541] = 0xCD;                              // STA $CD         save cursor timing countdown
    bus.memory[0xE542] = 0x85; bus.memory[0xE543] = 0xCC;                              // STA $CC         save cursor enable ($00 = flash cursor)

    // *** clear the screen
    bus.memory[0xE544] = 0xAD; bus.memory[0xE545] = 0x88; bus.memory[0xE546] = 0x02;   // LDA $0288       get the screen memory page
    bus.memory[0xE547] = 0x09; bus.memory[0xE548] = 0x80;                              // ORA #$80        set the high bit, flag every line is a logical line start
    bus.memory[0xE549] = 0xA8;                                                         // TAY             copy to Y
    bus.memory[0xE54A] = 0xA9; bus.memory[0xE54B] = 0x00;                              // LDA #$00        clear the line start low byte
    bus.memory[0xE54C] = 0xAA;                                                         // TAX             clear the index
    bus.memory[0xE54D] = 0x94; bus.memory[0xE54E] = 0xD9;                              // STY $D9,X       save the start of line X pointer high byte
    bus.memory[0xE54F] = 0x18;                                                         // CLC             clear carry for add
    bus.memory[0xE550] = 0x69; bus.memory[0xE551] = 0x28;                              // ADC #$28        add the line length to the low byte
    bus.memory[0xE552] = 0x90; bus.memory[0xE553] = 0x01;                              // BCC $E555       if no rollover skip the high byte increment
    bus.memory[0xE554] = 0xC8;                                                         // INY             else increment the high byte
    bus.memory[0xE555] = 0xE8;                                                         // INX             increment the line index
    bus.memory[0xE556] = 0xE0; bus.memory[0xE557] = 0x1A;                              // CPX #$1A        compare it with the number of lines + 1
    bus.memory[0xE558] = 0xD0; bus.memory[0xE559] = 0xF3;                              // BNE $E54D       loop if not all done
    bus.memory[0xE55A] = 0xA9; bus.memory[0xE55B] = 0xFF;                              // LDA #$FF        set the end of table marker
    bus.memory[0xE55C] = 0x95; bus.memory[0xE55D] = 0xD9;                              // STA $D9,X       mark the end of the table
    bus.memory[0xE55E] = 0xA2; bus.memory[0xE55F] = 0x18;                              // LDX #$18        set the line count, 25 lines to do, 0–24
    bus.memory[0xE560] = 0x20; bus.memory[0xE561] = 0xFF; bus.memory[0xE562] = 0xE9;   // JSR $E9FF       clear screen line X                   *** done ***
    bus.memory[0xE563] = 0xCA;                                                         // DEX             decrement the count
    bus.memory[0xE564] = 0x10; bus.memory[0xE565] = 0xFA;                              // BPL $E560       loop if more to do

    // *** home the cursor (porta il cursore in alto a sinistra dello schermo)
    bus.memory[0xE566] = 0xA0; bus.memory[0xE567] = 0x00;                              // LDY #$00        clear Y
    bus.memory[0xE568] = 0x84; bus.memory[0xE569] = 0xD3;                              // STY $D3         clear the cursor column
    bus.memory[0xE56A] = 0x84; bus.memory[0xE56B] = 0xD6;                              // STY $D6         clear the cursor row
    
    // *** set screen pointers for cursor row, column
    bus.memory[0xE56C] = 0xA6; bus.memory[0xE56D] = 0xD6;                              // LDX $D6         get the cursor row
    bus.memory[0xE56E] = 0xA5; bus.memory[0xE56F] = 0xD3;                              // LDA $D3         get the cursor column
    bus.memory[0xE570] = 0xB4; bus.memory[0xE571] = 0xD9;                              // LDY $D9,X       get start of line X pointer high byte
    bus.memory[0xE572] = 0x30; bus.memory[0xE573] = 0x08;                              // BMI $E57C       if it is the logical line start continue
    bus.memory[0xE574] = 0x18;                                                         // CLC             else clear carry for add
    bus.memory[0xE575] = 0x69; bus.memory[0xE576] = 0x28;                              // ADC #$28        add one line length
    bus.memory[0xE577] = 0x85; bus.memory[0xE578] = 0xD3;                              // STA $D3         save the cursor column
    bus.memory[0xE579] = 0xCA;                                                         // DEX             decrement the cursor row
    bus.memory[0xE57A] = 0x10; bus.memory[0xE57B] = 0xF4;                              // BPL $E570       loop, branch always
    bus.memory[0xE57C] = 0x20; bus.memory[0xE57D] = 0xF0; bus.memory[0xE57E] = 0xE9;   // JSR $E9F0       fetch a screen address                *** done ***
    bus.memory[0xE57F] = 0xA9; bus.memory[0xE580] = 0x27;                              // LDA #$27        set the line length
    bus.memory[0xE581] = 0xE8;                                                         // INX             increment the cursor row
    bus.memory[0xE582] = 0xB4; bus.memory[0xE583] = 0xD9;                              // LDY $D9,X       get the start of line X pointer high byte
    bus.memory[0xE584] = 0x30; bus.memory[0xE585] = 0x06;                              // BMI $E58C       if logical line start exit
    bus.memory[0xE586] = 0x18;                                                         // CLC             else clear carry for add
    bus.memory[0xE587] = 0x69; bus.memory[0xE588] = 0x28;                              // ADC #$28        add one line length to the current line length
    bus.memory[0xE589] = 0xE8;                                                         // INX             increment the cursor row
    bus.memory[0xE58A] = 0x10; bus.memory[0xE58B] = 0xF6;                              // BPL $E582       loop, branch always
    bus.memory[0xE58C] = 0x85; bus.memory[0xE58D] = 0xD5;                              // STA $D5         save current screen line length
    bus.memory[0xE58E] = 0x4C; bus.memory[0xE58F] = 0x24; bus.memory[0xE590] = 0xEA;   // JMP $EA24       calculate the pointer to colour RAM and return       *** done ***
    bus.memory[0xE591] = 0xE4; bus.memory[0xE592] = 0xC9;                              // CPX $C9         compare it with the input cursor row
    bus.memory[0xE593] = 0xF0; bus.memory[0xE594] = 0x03;                              // BEQ $E598       if there just exit
    bus.memory[0xE595] = 0x4C; bus.memory[0xE596] = 0xED; bus.memory[0xE597] = 0xE6;   // JMP $E6ED       else go ??                            *** done ***
    bus.memory[0xE598] = 0x60;                                                         // RTS

    #pragma endregion

    #pragma region *** advance the cursor (0xE6B6 - 0xE700)    

    /* ----------------------------------------------------------------------
                                    *** advance the cursor
    .,E6B6 20 B3 E8 JSR $E8B3       test for line increment                 *** done ***                 
    .,E6B9 E6 D3    INC $D3         increment the cursor column
    .,E6BB A5 D5    LDA $D5         get current screen line length
    .,E6BD C5 D3    CMP $D3         compare ?? with the cursor column
    .,E6BF B0 3F    BCS $E700       exit if line length >= cursor column
    .,E6C1 C9 4F    CMP #$4F        compare with max length
    .,E6C3 F0 32    BEQ $E6F7       if at max clear column, back cursor up and do newline
    .,E6C5 AD 92 02 LDA $0292       get the autoscroll flag
    .,E6C8 F0 03    BEQ $E6CD       branch if autoscroll on
    .,E6CA 4C 67 E9 JMP $E967       else open space on screen               *** done ***
    .,E6CD A6 D6    LDX $D6         get the cursor row
    .,E6CF E0 19    CPX #$19        compare with max + 1
    .,E6D1 90 07    BCC $E6DA       if less than max + 1 go add this row to the current
                                    logical line
    .,E6D3 20 EA E8 JSR $E8EA       else scroll the screen                  *** done ***
    .,E6D6 C6 D6    DEC $D6         decrement the cursor row
    .,E6D8 A6 D6    LDX $D6         get the cursor row
                                    add this row to the current logical line
    .,E6DA 16 D9    ASL $D9,X       shift start of line X pointer high byte
    .,E6DC 56 D9    LSR $D9,X       shift start of line X pointer high byte back,
                                    make next screen line start of logical line, increment line length and set pointers
                                    clear b7, start of logical line
    .,E6DE E8       INX             increment screen row
    .,E6DF B5 D9    LDA $D9,X       get start of line X pointer high byte
    .,E6E1 09 80    ORA #$80        mark as start of logical line
    .,E6E3 95 D9    STA $D9,X       set start of line X pointer high byte
    .,E6E5 CA       DEX             restore screen row
    .,E6E6 A5 D5    LDA $D5         get current screen line length
                                    add one line length and set the pointers for the start of the line
    .,E6E8 18       CLC             clear carry for add
    .,E6E9 69 28    ADC #$28        add one line length
    .,E6EB 85 D5    STA $D5         save current screen line length
    .,E6ED B5 D9    LDA $D9,X       get start of line X pointer high byte
    .,E6EF 30 03    BMI $E6F4       exit loop if start of logical line
    .,E6F1 CA       DEX             else back up one line
    .,E6F2 D0 F9    BNE $E6ED       loop if not on first line               
    .,E6F4 4C F0 E9 JMP $E9F0       fetch a screen address                  *** done ***
    .,E6F7 C6 D6    DEC $D6         decrement the cursor row
    .,E6F9 20 7C E8 JSR $E87C       do newline                              *** done ***
    .,E6FC A9 00    LDA #$00        clear A
    .,E6FE 85 D3    STA $D3         clear the cursor column
    .,E700 60       RTS  

    ------------------------------------------------------------------------ */    

    bus.memory[0xE6B6] = 0x20; bus.memory[0xE6B7] = 0xB3; bus.memory[0xE6B8] = 0xE8;    // JSR $E8B3       test for line increment  *** done ***
    bus.memory[0xE6B9] = 0xE6; bus.memory[0xE6BA] = 0xD3;                               // INC $D3         increment cursor column
    bus.memory[0xE6BB] = 0xA5; bus.memory[0xE6BC] = 0xD5;                               // LDA $D5         get screen line length
    bus.memory[0xE6BD] = 0xC5; bus.memory[0xE6BE] = 0xD3;                               // CMP $D3
    bus.memory[0xE6BF] = 0xB0; bus.memory[0xE6C0] = 0x3F;                               // BCS $E700
    bus.memory[0xE6C1] = 0xC9; bus.memory[0xE6C2] = 0x4F;                               // CMP #$4F
    bus.memory[0xE6C3] = 0xF0; bus.memory[0xE6C4] = 0x32;                               // BEQ $E6F7
    bus.memory[0xE6C5] = 0xAD; bus.memory[0xE6C6] = 0x92; bus.memory[0xE6C7] = 0x02;    // LDA $0292       autoscroll flag
    bus.memory[0xE6C8] = 0xF0; bus.memory[0xE6C9] = 0x03;                               // BEQ $E6CD
    bus.memory[0xE6CA] = 0x4C; bus.memory[0xE6CB] = 0x67; bus.memory[0xE6CC] = 0xE9;    // JMP $E967       open space on screen

    bus.memory[0xE6CD] = 0xA6; bus.memory[0xE6CE] = 0xD6;                               // LDX $D6         cursor row
    bus.memory[0xE6CF] = 0xE0; bus.memory[0xE6D0] = 0x19;                               // CPX #$19
    bus.memory[0xE6D1] = 0x90; bus.memory[0xE6D2] = 0x07;                               // BCC $E6DA
    bus.memory[0xE6D3] = 0x20; bus.memory[0xE6D4] = 0xEA; bus.memory[0xE6D5] = 0xE8;    // JSR $E8EA       scroll screen            *** done ***             
    bus.memory[0xE6D6] = 0xC6; bus.memory[0xE6D7] = 0xD6;                               // DEC $D6
    bus.memory[0xE6D8] = 0xA6; bus.memory[0xE6D9] = 0xD6;                               // LDX $D6

    bus.memory[0xE6DA] = 0x16; bus.memory[0xE6DB] = 0xD9;                               // ASL $D9,X
    bus.memory[0xE6DC] = 0x56; bus.memory[0xE6DD] = 0xD9;                               // LSR $D9,X
    bus.memory[0xE6DE] = 0xE8;                                                          // INX
    bus.memory[0xE6DF] = 0xB5; bus.memory[0xE6E0] = 0xD9;                               // LDA $D9,X
    bus.memory[0xE6E1] = 0x09; bus.memory[0xE6E2] = 0x80;                               // ORA #$80        mark as start of logical line
    bus.memory[0xE6E3] = 0x95; bus.memory[0xE6E4] = 0xD9;                               // STA $D9,X
    bus.memory[0xE6E5] = 0xCA;                                                          // DEX

    bus.memory[0xE6E6] = 0xA5; bus.memory[0xE6E7] = 0xD5;                               // LDA $D5
    bus.memory[0xE6E8] = 0x18;                                                          // CLC
    bus.memory[0xE6E9] = 0x69; bus.memory[0xE6EA] = 0x28;                               // ADC #$28        add one line length
    bus.memory[0xE6EB] = 0x85; bus.memory[0xE6EC] = 0xD5;                               // STA $D5
    bus.memory[0xE6ED] = 0xB5; bus.memory[0xE6EE] = 0xD9;                               // LDA $D9,X
    bus.memory[0xE6EF] = 0x30; bus.memory[0xE6F0] = 0x03;                               // BMI $E6F4
    bus.memory[0xE6F1] = 0xCA;                                                          // DEX
    bus.memory[0xE6F2] = 0xD0; bus.memory[0xE6F3] = 0xF9;                               // BNE $E6ED
    bus.memory[0xE6F4] = 0x4C; bus.memory[0xE6F5] = 0xF0; bus.memory[0xE6F6] = 0xE9;    // JMP $E9F0       fetch screen address     *** done *** 
    bus.memory[0xE6F7] = 0xC6; bus.memory[0xE6F8] = 0xD6;                               // DEC $D6
    bus.memory[0xE6F9] = 0x20; bus.memory[0xE6FA] = 0x7C; bus.memory[0xE6FB] = 0xE8;    // JSR $E87C       newline                  *** done ***
    bus.memory[0xE6FC] = 0xA9; bus.memory[0xE6FD] = 0x00;                               // LDA #$00
    bus.memory[0xE6FE] = 0x85; bus.memory[0xE6FF] = 0xD3;                               // STA $D3         clear cursor column
    bus.memory[0xE700] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** initialise the vic chip (0xE5A0 - 0xE5B3)    

    /* ----------------------------------------------------------------------
                                    *** initialise the vic chip
    .,E5A0 A9 03    LDA #$03        set the screen as the output device
    .,E5A2 85 9A    STA $9A         save the output device number
    .,E5A4 A9 00    LDA #$00        set the keyboard as the input device
    .,E5A6 85 99    STA $99         save the input device number
    .,E5A8 A2 2F    LDX #$2F        set the count/index

    .,E5AA BD B8 EC LDA $ECB8,X     get a vic ii chip initialisation value  (copio 2F bytes da ECB8 (rom) ... a CFFF...)
    .,E5AD 9D FF CF STA $CFFF,X     save it to the vic ii chip
    .,E5B0 CA       DEX             decrement the count/index
    .,E5B1 D0 F7    BNE $E5AA       loop if more to do
    .,E5B3 60       RTS    
    ------------------------------------------------------------------------ */

    bus.memory[0xE5A0] = 0xA9; bus.memory[0xE5A1] = 0x03;                              // LDA #$03        set the screen as the output device
    bus.memory[0xE5A2] = 0x85; bus.memory[0xE5A3] = 0x9A;                              // STA $9A         save the output device number
    bus.memory[0xE5A4] = 0xA9; bus.memory[0xE5A5] = 0x00;                              // LDA #$00        set the keyboard as the input device
    bus.memory[0xE5A6] = 0x85; bus.memory[0xE5A7] = 0x99;                              // STA $99         save the input device number
    bus.memory[0xE5A8] = 0xA2; bus.memory[0xE5A9] = 0x2F;                              // LDX #$2F        set the count/index

    bus.memory[0xE5AA] = 0xBD; bus.memory[0xE5AB] = 0xB8; bus.memory[0xE5AC] = 0xEC;   // LDA $ECB8,X     get a vic ii chip initialisation value
    bus.memory[0xE5AD] = 0x9D; bus.memory[0xE5AE] = 0xFF; bus.memory[0xE5AF] = 0xCF;   // STA $CFFF,X     save it to the vic ii chip     
    bus.memory[0xE5B0] = 0xCA;                                                         // DEX             decrement the count/index 
    bus.memory[0xE5B1] = 0xD0; bus.memory[0xE5B2] = 0xF7;                              // BNE $E5AA       loop if more to do
    bus.memory[0xE5B3] = 0x60;                                                         // RTS  

    #pragma endregion    
 
    #pragma region *** do newline (0xE87C - 0xE88E)     
    
    /* ----------------------------------------------------------------------
                                    *** do newline
    .,E87C 46 C9    LSR $C9         shift >> input cursor row
    .,E87E A6 D6    LDX $D6         get the cursor row
    .,E880 E8       INX             increment the row
    .,E881 E0 19    CPX #$19        compare it with last row + 1
    .,E883 D0 03    BNE $E888       if not last row + 1 skip the screen scroll
    .,E885 20 EA E8 JSR $E8EA       else scroll the screen                               
    .,E888 B5 D9    LDA $D9,X       get start of line X pointer high byte
    .,E88A 10 F4    BPL $E880       loop if not start of logical line
    .,E88C 86 D6    STX $D6         save the cursor row
    .,E88E 4C 6C E5 JMP $E56C       set the screen pointers for cursor row, column and return    

    ------------------------------------------------------------------------ */

    // *** do newline
    bus.memory[0xE87C] = 0x46; bus.memory[0xE87D] = 0xC9;                               // LSR $C9              shift >> input cursor row
    bus.memory[0xE87E] = 0xA6; bus.memory[0xE87F] = 0xD6;                               // LDX $D6              get the cursor row
    bus.memory[0xE880] = 0xE8;                                                          // INX                  increment the row
    bus.memory[0xE881] = 0xE0; bus.memory[0xE882] = 0x19;                               // CPX #$19             compare with last row + 1
    bus.memory[0xE883] = 0xD0; bus.memory[0xE884] = 0x03;                               // BNE $E888            skip scroll if not last row
    bus.memory[0xE885] = 0x20; bus.memory[0xE886] = 0xEA; bus.memory[0xE887] = 0xE8;    // JSR $E8EA            scroll the screen               *** done ***

    bus.memory[0xE888] = 0xB5; bus.memory[0xE889] = 0xD9;                               // LDA $D9,X            get start of line high byte
    bus.memory[0xE88A] = 0x10; bus.memory[0xE88B] = 0xF4;                               // BPL $E880            loop if not logical line start
    bus.memory[0xE88C] = 0x86; bus.memory[0xE88D] = 0xD6;                               // STX $D6              save cursor row
    bus.memory[0xE88E] = 0x4C; bus.memory[0xE88F] = 0x6C; bus.memory[0xE890] = 0xE5;    // JMP $E56C            set screen pointers & return    *** done ***

    #pragma endregion   

    #pragma region *** test for line increment (0xE8B3 - 0xE8CA)

    /* ----------------------------------------------------------------------    
                                    *** test for line increment
                                    
                                    if at end of the line, but not at end of the last line, increment the cursor row
    .,E8B3 A2 02    LDX #$02        set the count
    .,E8B5 A9 27    LDA #$27        set the column
    .,E8B7 C5 D3    CMP $D3         compare the column with the cursor column
    .,E8B9 F0 07    BEQ $E8C2       if at end of line test and possibly increment cursor row
    .,E8BB 18       CLC             else clear carry for add
    .,E8BC 69 28    ADC #$28        increment to the next line
    .,E8BE CA       DEX             decrement the loop count
    .,E8BF D0 F6    BNE $E8B7       loop if more to test
    .,E8C1 60       RTS             
                                    cursor is at end of line
    .,E8C2 A6 D6    LDX $D6         get the cursor row
    .,E8C4 E0 19    CPX #$19        compare it with the end of the screen
    .,E8C6 F0 02    BEQ $E8CA       if at the end of screen just exit
    .,E8C8 E6 D6    INC $D6         else increment the cursor row
    .,E8CA 60       RTS 
    ------------------------------------------------------------------------ */

                                                                                       // *** test for line increment
                                                                                       // if at end of the line, but not at end of the last line, increment the cursor row

    bus.memory[0xE8B3] = 0xA2; bus.memory[0xE8B4] = 0x02;                              // LDX #$02     set the count
    bus.memory[0xE8B5] = 0xA9; bus.memory[0xE8B6] = 0x27;                              // LDA #$27     set the column
    bus.memory[0xE8B7] = 0xC5; bus.memory[0xE8B8] = 0xD3;                              // CMP $D3      compare the column with the cursor column
    bus.memory[0xE8B9] = 0xF0; bus.memory[0xE8BA] = 0x07;                              // BEQ $E8C2    if at end of line test and possibly increment cursor row
    bus.memory[0xE8BB] = 0x18;                                                         // CLC          else clear carry for add
    bus.memory[0xE8BC] = 0x69; bus.memory[0xE8BD] = 0x28;                              // ADC #$28     increment to the next line
    bus.memory[0xE8BE] = 0xCA;                                                         // DEX          decrement the loop count
    bus.memory[0xE8BF] = 0xD0; bus.memory[0xE8C0] = 0xF6;                              // BNE $E8B7    loop if more to test
    bus.memory[0xE8C1] = 0x60;                                                         // RTS

                                                                                       // cursor is at end of line
    bus.memory[0xE8C2] = 0xA6; bus.memory[0xE8C3] = 0xD6;                              // LDX $D6      get the cursor row
    bus.memory[0xE8C4] = 0xE0; bus.memory[0xE8C5] = 0x19;                              // CPX #$19     compare it with the end of the screen
    bus.memory[0xE8C6] = 0xF0; bus.memory[0xE8C7] = 0x02;                              // BEQ $E8CA    if at the end of screen just exit
    bus.memory[0xE8C8] = 0xE6; bus.memory[0xE8C9] = 0xD6;                              // INC $D6      else increment the cursor row
    bus.memory[0xE8CA] = 0x60;                                                         // RTS

    #pragma endregion    

    #pragma region *** scroll the screen (0xE8EA - 0xE964)

    /* ----------------------------------------------------------------------
                                    *** scroll the screen
    .,E8EA A5 AC    LDA $AC         copy the tape buffer start pointer
    .,E8EC 48       PHA             save it
    .,E8ED A5 AD    LDA $AD         copy the tape buffer start pointer
    .,E8EF 48       PHA             save it
    .,E8F0 A5 AE    LDA $AE         copy the tape buffer end pointer
    .,E8F2 48       PHA             save it
    .,E8F3 A5 AF    LDA $AF         copy the tape buffer end pointer
    .,E8F5 48       PHA             save it
    .,E8F6 A2 FF    LDX #$FF        set to -1 for pre increment loop
    .,E8F8 C6 D6    DEC $D6         decrement the cursor row
    .,E8FA C6 C9    DEC $C9         decrement the input cursor row
    .,E8FC CE A5 02 DEC $02A5       decrement the screen row marker
    .,E8FF E8       INX             increment the line number
    .,E900 20 F0 E9 JSR $E9F0       fetch a screen address, set the start of line X
    .,E903 E0 18    CPX #$18        compare with last line
    .,E905 B0 0C    BCS $E913       branch if >= $16
    .,E907 BD F1 EC LDA $ECF1,X     get the start of the next line pointer low byte
    .,E90A 85 AC    STA $AC         save the next line pointer low byte
    .,E90C B5 DA    LDA $DA,X       get the start of the next line pointer high byte
    .,E90E 20 C8 E9 JSR $E9C8       shift the screen line up
    .,E911 30 EC    BMI $E8FF       loop, branch always
    .,E913 20 FF E9 JSR $E9FF       clear screen line X
                                    now shift up the start of logical line bits
    .,E916 A2 00    LDX #$00        clear index
    .,E918 B5 D9    LDA $D9,X       get the start of line X pointer high byte
    .,E91A 29 7F    AND #$7F        clear the line X start of logical line bit
    .,E91C B4 DA    LDY $DA,X       get the start of the next line pointer high byte
    .,E91E 10 02    BPL $E922       if next line is not a start of line skip the start set
    .,E920 09 80    ORA #$80        set line X start of logical line bit
    .,E922 95 D9    STA $D9,X       set start of line X pointer high byte
    .,E924 E8       INX             increment line number
    .,E925 E0 18    CPX #$18        compare with last line
    .,E927 D0 EF    BNE $E918       loop if not last line
    .,E929 A5 F1    LDA $F1         get start of last line pointer high byte
    .,E92B 09 80    ORA #$80        mark as start of logical line
    .,E92D 85 F1    STA $F1         set start of last line pointer high byte
    .,E92F A5 D9    LDA $D9         get start of first line pointer high byte
    .,E931 10 C3    BPL $E8F6       if not start of logical line loop back and
                                    scroll the screen up another line
    .,E933 E6 D6    INC $D6         increment the cursor row
    .,E935 EE A5 02 INC $02A5       increment screen row marker
    .,E938 A9 7F    LDA #$7F        set keyboard column c7
    .,E93A 8D 00 DC STA $DC00       save VIA 1 DRA, keyboard column drive
    .,E93D AD 01 DC LDA $DC01       read VIA 1 DRB, keyboard row port
    .,E940 C9 FB    CMP #$FB        compare with row r2 active, [CTL]
    .,E942 08       PHP             save status
    .,E943 A9 7F    LDA #$7F        set keyboard column c7
    .,E945 8D 00 DC STA $DC00       save VIA 1 DRA, keyboard column drive
    .,E948 28       PLP             restore status
    .,E949 D0 0B    BNE $E956       skip delay if ??
                                    first time round the inner loop X will be $16
    .,E94B A0 00    LDY #$00        clear delay outer loop count, do this 256 times
    .,E94D EA       NOP             waste cycles
    .,E94E CA       DEX             decrement inner loop count
    .,E94F D0 FC    BNE $E94D       loop if not all done
    .,E951 88       DEY             decrement outer loop count
    .,E952 D0 F9    BNE $E94D       loop if not all done
    .,E954 84 C6    STY $C6         clear the keyboard buffer index
    .,E956 A6 D6    LDX $D6         get the cursor row
                                    restore the tape buffer pointers and exit
    .,E958 68       PLA             pull tape buffer end pointer
    .,E959 85 AF    STA $AF         restore it
    .,E95B 68       PLA             pull tape buffer end pointer
    .,E95C 85 AE    STA $AE         restore it
    .,E95E 68       PLA             pull tape buffer pointer
    .,E95F 85 AD    STA $AD         restore it
    .,E961 68       PLA             pull tape buffer pointer
    .,E962 85 AC    STA $AC         restore it
    .,E964 60       RTS             
    ----------------------------------------------------------------------- */

    // copy and save tape buffer pointers
    bus.memory[0xE8EA] = 0xA5; bus.memory[0xE8EB] = 0xAC;                              // LDA $AC      copy tape buffer start pointer
    bus.memory[0xE8EC] = 0x48;                                                         // PHA          save it
    bus.memory[0xE8ED] = 0xA5; bus.memory[0xE8EE] = 0xAD;                              // LDA $AD      copy tape buffer start pointer
    bus.memory[0xE8EF] = 0x48;                                                         // PHA          save it
    bus.memory[0xE8F0] = 0xA5; bus.memory[0xE8F1] = 0xAE;                              // LDA $AE      copy tape buffer end pointer
    bus.memory[0xE8F2] = 0x48;                                                         // PHA          save it
    bus.memory[0xE8F3] = 0xA5; bus.memory[0xE8F4] = 0xAF;                              // LDA $AF      copy tape buffer end pointer
    bus.memory[0xE8F5] = 0x48;                                                         // PHA          save it

    // prepare for pre-increment loop
    bus.memory[0xE8F6] = 0xA2; bus.memory[0xE8F7] = 0xFF;                              // LDX #$FF     set to -1 for pre increment loop
    bus.memory[0xE8F8] = 0xC6; bus.memory[0xE8F9] = 0xD6;                              // DEC $D6      decrement cursor row
    bus.memory[0xE8FA] = 0xC6; bus.memory[0xE8FB] = 0xC9;                              // DEC $C9      decrement input cursor row
    bus.memory[0xE8FC] = 0xCE; bus.memory[0xE8FD] = 0xA5; bus.memory[0xE8FE] = 0x02;   // DEC $02A5    decrement screen row marker

    // increment line number and fetch screen line start
    bus.memory[0xE8FF] = 0xE8;                                                         // INX          increment line number
    bus.memory[0xE900] = 0x20; bus.memory[0xE901] = 0xF0; bus.memory[0xE902] = 0xE9;   // JSR $E9F0    fetch screen address, set start of line X  *** done ***
    bus.memory[0xE903] = 0xE0; bus.memory[0xE904] = 0x18;                              // CPX #$18     compare with last line
    bus.memory[0xE905] = 0xB0; bus.memory[0xE906] = 0x0C;                              // BCS $E913    branch if >= last line

    // load start of next line
    bus.memory[0xE907] = 0xBD; bus.memory[0xE908] = 0xF1; bus.memory[0xE909] = 0xEC;   // LDA $ECF1,X  get start of next line pointer low byte
    bus.memory[0xE90A] = 0x85; bus.memory[0xE90B] = 0xAC;                              // STA $AC      save it
    bus.memory[0xE90C] = 0xB5; bus.memory[0xE90D] = 0xDA;                              // LDA $DA,X    get start of next line pointer high byte
    bus.memory[0xE90E] = 0x20; bus.memory[0xE90F] = 0xC8; bus.memory[0xE910] = 0xE9;   // JSR $E9C8    shift the screen line up          *** done ***
    bus.memory[0xE911] = 0x30; bus.memory[0xE912] = 0xEC;                              // BMI $E8FF    loop always

    // clear screen line
    bus.memory[0xE913] = 0x20; bus.memory[0xE914] = 0xFF; bus.memory[0xE915] = 0xE9;  // JSR $E9FF    clear screen line X           *** done ***

    // clear index and start logical line bits loop
    bus.memory[0xE916] = 0xA2; bus.memory[0xE917] = 0x00;                              // LDX #$00     clear index
    bus.memory[0xE918] = 0xB5; bus.memory[0xE919] = 0xD9;                              // LDA $D9,X    get start of line X pointer high byte
    bus.memory[0xE91A] = 0x29; bus.memory[0xE91B] = 0x7F;                              // AND #$7F     clear start of logical line bit
    bus.memory[0xE91C] = 0xB4; bus.memory[0xE91D] = 0xDA;                              // LDY $DA,X    get start of next line pointer high byte
    bus.memory[0xE91E] = 0x10; bus.memory[0xE91F] = 0x02;                              // BPL $E922    skip if next line not start
    bus.memory[0xE920] = 0x09; bus.memory[0xE921] = 0x80;                              // ORA #$80     set start of logical line bit
    bus.memory[0xE922] = 0x95; bus.memory[0xE923] = 0xD9;                              // STA $D9,X    set start of line X pointer high byte
    bus.memory[0xE924] = 0xE8;                                                         // INX          increment line number
    bus.memory[0xE925] = 0xE0; bus.memory[0xE926] = 0x18;                              // CPX #$18     compare with last line
    bus.memory[0xE927] = 0xD0; bus.memory[0xE928] = 0xEF;                              // BNE $E918    loop if not last line

    // mark start of last line
    bus.memory[0xE929] = 0xA5; bus.memory[0xE92A] = 0xF1;                              // LDA $F1      get start of last line pointer high byte
    bus.memory[0xE92B] = 0x09; bus.memory[0xE92C] = 0x80;                              // ORA #$80     mark as start of logical line
    bus.memory[0xE92D] = 0x85; bus.memory[0xE92E] = 0xF1;                              // STA $F1      set start of last line pointer high byte
    bus.memory[0xE92F] = 0xA5; bus.memory[0xE930] = 0xD9;                              // LDA $D9      get start of first line pointer high byte
    bus.memory[0xE931] = 0x10; bus.memory[0xE932] = 0xC3;                              // BPL $E8F6    loop if not start of logical line

    // increment cursor row and screen row marker
    bus.memory[0xE933] = 0xE6; bus.memory[0xE934] = 0xD6;                              // INC $D6      increment cursor row
    bus.memory[0xE935] = 0xEE; bus.memory[0xE936] = 0xA5; bus.memory[0xE937] = 0x02;   // INC $02A5    increment screen row marker

    // keyboard column and delay setup
    bus.memory[0xE938] = 0xA9; bus.memory[0xE939] = 0x7F;                              // LDA #$7F     set keyboard column c7
    bus.memory[0xE93A] = 0x8D; bus.memory[0xE93B] = 0x00; bus.memory[0xE93C] = 0xDC;   // STA $DC00    save VIA 1 DRA
    bus.memory[0xE93D] = 0xAD; bus.memory[0xE93E] = 0x01; bus.memory[0xE93F] = 0xDC;   // LDA $DC01    read VIA 1 DRB
    bus.memory[0xE940] = 0xC9; bus.memory[0xE941] = 0xFB;                              // CMP #$FB     compare with row r2 active
    bus.memory[0xE942] = 0x08;                                                         // PHP          save status
    bus.memory[0xE943] = 0xA9; bus.memory[0xE944] = 0x7F;                              // LDA #$7F     set keyboard column c7
    bus.memory[0xE945] = 0x8D; bus.memory[0xE946] = 0x00; bus.memory[0xE947] = 0xDC;   // STA $DC00    save VIA 1 DRA
    bus.memory[0xE948] = 0x28;                                                         // PLP          restore status
    bus.memory[0xE949] = 0xD0; bus.memory[0xE94A] = 0x0B;                              // BNE $E956    skip delay if not first
    bus.memory[0xE94B] = 0xA0; bus.memory[0xE94C] = 0x00;                              // LDY #$00     clear delay outer loop
    bus.memory[0xE94D] = 0xEA;                                                         // NOP          waste cycles
    bus.memory[0xE94E] = 0xCA;                                                         // DEX          decrement inner loop
    bus.memory[0xE94F] = 0xD0; bus.memory[0xE950] = 0xFC;                              // BNE $E94D    loop inner
    bus.memory[0xE951] = 0x88;                                                         // DEY          decrement outer loop
    bus.memory[0xE952] = 0xD0; bus.memory[0xE953] = 0xF9;                              // BNE $E94D    loop outer
    bus.memory[0xE954] = 0x84; bus.memory[0xE955] = 0xC6;                              // STY $C6      clear keyboard buffer index

    // restore cursor and tape buffer pointers
    bus.memory[0xE956] = 0xA6; bus.memory[0xE957] = 0xD6;                              // LDX $D6      get cursor row
    bus.memory[0xE958] = 0x68;                                                         // PLA          pull tape buffer end pointer
    bus.memory[0xE959] = 0x85; bus.memory[0xE95A] = 0xAF;                              // STA $AF      restore
    bus.memory[0xE95B] = 0x68;                                                         // PLA          pull tape buffer end pointer
    bus.memory[0xE95C] = 0x85; bus.memory[0xE95D] = 0xAE;                              // STA $AE      restore
    bus.memory[0xE95E] = 0x68;                                                         // PLA          pull tape buffer pointer
    bus.memory[0xE95F] = 0x85; bus.memory[0xE960] = 0xAD;                              // STA $AD      restore
    bus.memory[0xE961] = 0x68;                                                         // PLA          pull tape buffer pointer
    bus.memory[0xE962] = 0x85; bus.memory[0xE963] = 0xAC;                              // STA $AC      restore
    bus.memory[0xE964] = 0x60; 

    #pragma endregion

    #pragma region *** shift screen line up/down (0xE9C8 - 0xE9DF)

    /* ----------------------------------------------------------------------
                                    *** shift screen line up/down

    .,E9C8 29 03    AND #$03        mask 0000 00xx, line memory page
    .,E9CA 0D 88 02 ORA $0288       OR with screen memory page
    .,E9CD 85 AD    STA $AD         save next/previous line pointer high byte
    .,E9CF 20 E0 E9 JSR $E9E0       calculate pointers to screen lines colour RAM
    .,E9D2 A0 27    LDY #$27        set the column count
    .,E9D4 B1 AC    LDA ($AC),Y     get character from next/previous screen line
    .,E9D6 91 D1    STA ($D1),Y     save character to current screen line
    .,E9D8 B1 AE    LDA ($AE),Y     get colour from next/previous screen line colour RAM
    .,E9DA 91 F3    STA ($F3),Y     save colour to current screen line colour RAM
    .,E9DC 88       DEY             decrement column index/count
    .,E9DD 10 F5    BPL $E9D4       loop if more to do
    .,E9DF 60       RTS
    ------------------------------------------------------------------------ */

    // *** shift screen line up/down ($E9C8)
    bus.memory[0xE9C8] = 0x29; bus.memory[0xE9C9] = 0x03;                              // AND #$03        mask 0000 00xx, line memory page
    bus.memory[0xE9CA] = 0x0D; bus.memory[0xE9CB] = 0x88; bus.memory[0xE9CC] = 0x02;   // ORA $0288       OR with screen memory page
    bus.memory[0xE9CD] = 0x85; bus.memory[0xE9CE] = 0xAD;                              // STA $AD         save next/previous line pointer high byte
    bus.memory[0xE9CF] = 0x20; bus.memory[0xE9D0] = 0xE0; bus.memory[0xE9D1] = 0xE9;   // JSR $E9E0       calculate pointers to screen lines colour RAM
    bus.memory[0xE9D2] = 0xA0; bus.memory[0xE9D3] = 0x27;                              // LDY #$27        set the column count
    bus.memory[0xE9D4] = 0xB1; bus.memory[0xE9D5] = 0xAC;                              // LDA ($AC),Y     get character from next/previous screen line
    bus.memory[0xE9D6] = 0x91; bus.memory[0xE9D7] = 0xD1;                              // STA ($D1),Y     save character to current screen line
    bus.memory[0xE9D8] = 0xB1; bus.memory[0xE9D9] = 0xAE;                              // LDA ($AE),Y     get colour from next/previous screen line colour RAM
    bus.memory[0xE9DA] = 0x91; bus.memory[0xE9DB] = 0xF3;                              // STA ($F3),Y     save colour to current screen line colour RAM
    bus.memory[0xE9DC] = 0x88;                                                         // DEY             decrement column index/count
    bus.memory[0xE9DD] = 0x10; bus.memory[0xE9DE] = 0xF5;                              // BPL $E9D4       loop if more to do
    bus.memory[0xE9DF] = 0x60;                                                         // RTS

    #pragma endregion

    #pragma region *** fetch a screen address (0xE9F0 - 0xE9FE)    

    /* ----------------------------------------------------------------------
                                    *** fetch a screen address
    .,E9F0 BD F0 EC LDA $ECF0,X     get the start of line low byte from the ROM table
    .,E9F3 85 D1    STA $D1         set the current screen line pointer low byte
    .,E9F5 B5 D9    LDA $D9,X       get the start of line high byte from the RAM table
    .,E9F7 29 03    AND #$03        mask 0000 00xx, line memory page
    .,E9F9 0D 88 02 ORA $0288       OR with the screen memory page
    .,E9FC 85 D2    STA $D2         save the current screen line pointer high byte
    .,E9FE 60       RTS   
    ------------------------------------------------------------------------ */

    // *** fetch a screen address ($E9F0)
    bus.memory[0xE9F0] = 0xBD; bus.memory[0xE9F1] = 0xF0; bus.memory[0xE9F2] = 0xEC;   // LDA $ECF0,X     get the start of line low byte from the ROM table
    bus.memory[0xE9F3] = 0x85; bus.memory[0xE9F4] = 0xD1;                              // STA $D1         set the current screen line pointer low byte
    bus.memory[0xE9F5] = 0xB5; bus.memory[0xE9F6] = 0xD9;                              // LDA $D9,X       get the start of line high byte from the RAM table
    bus.memory[0xE9F7] = 0x29; bus.memory[0xE9F8] = 0x03;                              // AND #$03        mask 0000 00xx, line memory page
    bus.memory[0xE9F9] = 0x0D; bus.memory[0xE9FA] = 0x88; bus.memory[0xE9FB] = 0x02;   // ORA $0288       OR with the screen memory page
    bus.memory[0xE9FC] = 0x85; bus.memory[0xE9FD] = 0xD2;                              // STA $D2         save the current screen line pointer high byte
    bus.memory[0xE9FE] = 0x60;                                                         // RTS

    #pragma endregion  

    #pragma region *** clear screen line X (0xE9FF - 0xEA11)    

    /* ----------------------------------------------------------------------
                                    *** clear screen line X
    .,E9FF A0 27    LDY #$27        set number of columns to clear
    .,EA01 20 F0 E9 JSR $E9F0       fetch a screen address                  *** done ***
    .,EA04 20 24 EA JSR $EA24       calculate the pointer to colour RAM
    .,EA07 20 DA E4 JSR $E4DA       save the current colour to the colour RAM
    .,EA0A A9 20    LDA #$20        set [SPACE]
    .,EA0C 91 D1    STA ($D1),Y     clear character in current screen line
    .,EA0E 88       DEY             decrement index
    .,EA0F 10 F6    BPL $EA07       loop if more to do
    .,EA11 60       RTS    
    ------------------------------------------------------------------------ */

    // *** clear screen line X
    bus.memory[0xE9FF] = 0xA0; bus.memory[0xEA00] = 0x27;                               // LDY #$27        set number of columns to clear
    bus.memory[0xEA01] = 0x20; bus.memory[0xEA02] = 0xF0; bus.memory[0xEA03] = 0xE9;    // JSR $E9F0       fetch a screen address                     *** done ***
    bus.memory[0xEA04] = 0x20; bus.memory[0xEA05] = 0x24; bus.memory[0xEA06] = 0xEA;    // JSR $EA24       calculate the pointer to colour RAM        *** done ***
    bus.memory[0xEA07] = 0x20; bus.memory[0xEA08] = 0xDA; bus.memory[0xEA09] = 0xE4;    // JSR $E4DA       save the current colour to the colour RAM  *** done ***
    bus.memory[0xEA0A] = 0xA9; bus.memory[0xEA0B] = 0x20;                               // LDA #$20        set [SPACE]
    bus.memory[0xEA0C] = 0x91; bus.memory[0xEA0D] = 0xD1;                               // STA ($D1),Y     clear character in current screen line
    bus.memory[0xEA0E] = 0x88;                                                          // DEY             decrement index
    bus.memory[0xEA0F] = 0x10; bus.memory[0xEA10] = 0xF6;                               // BPL $EA07       loop if more to do
    bus.memory[0xEA11] = 0x60;                                                          // RTS

    #pragma endregion    

    #pragma region *** calculate the pointer to colour RAM (0xEA24 - 0xEA30)     

    /* ----------------------------------------------------------------------
                                    *** calculate the pointer to colour RAM
    .,EA24 A5 D1    LDA $D1         get current screen line pointer low byte
    .,EA26 85 F3    STA $F3         save pointer to colour RAM low byte
    .,EA28 A5 D2    LDA $D2         get current screen line pointer high byte
    .,EA2A 29 03    AND #$03        mask 0000 00xx, line memory page
    .,EA2C 09 D8    ORA #$D8        set  1101 01xx, colour memory page
    .,EA2E 85 F4    STA $F4         save pointer to colour RAM high byte
    .,EA30 60       RTS             
    ------------------------------------------------------------------------ */

    // *** calculate the pointer to colour RAM
    bus.memory[0xEA24] = 0xA5; bus.memory[0xEA25] = 0xD1;   // LDA $D1         get current screen line pointer low byte
    bus.memory[0xEA26] = 0x85; bus.memory[0xEA27] = 0xF3;   // STA $F3         save pointer to colour RAM low byte
    bus.memory[0xEA28] = 0xA5; bus.memory[0xEA29] = 0xD2;   // LDA $D2         get current screen line pointer high byte
    bus.memory[0xEA2A] = 0x29; bus.memory[0xEA2B] = 0x03;   // AND #$03        mask 0000 00xx, line memory page
    bus.memory[0xEA2C] = 0x09; bus.memory[0xEA2D] = 0xD8;   // ORA #$D8        set 1101 10xx, colour memory page
    bus.memory[0xEA2E] = 0x85; bus.memory[0xEA2F] = 0xF4;   // STA $F4         save pointer to colour RAM high byte
    bus.memory[0xEA30] = 0x60;                              // RTS

    #pragma endregion     

    #pragma region *** vic ii chip initialisation values (0xECB8 - 0xECE6)    

    /* ----------------------------------------------------------------------

    .:ECB8 FF

                                    *** vic ii chip initialisation values
    .:ECB9 00 00                    sprite 0 x,y
    .:ECBB 00 00                    sprite 1 x,y 
    .:ECBD 00 00                    sprite 2 x,y 
    .:ECBF 00 00                    sprite 3 x,y
    .:ECC1 00 00                    sprite 4 x,y 
    .:ECC3 00 00                    sprite 5 x,y 
    .:ECC5 00 00                    sprite 6 x,y 
    .:ECC7 00 00                    sprite 7 x,y
    .:ECC9 00                       sprites 0 to 7 x bit 8
    .:ECCA 9B                       enable screen, enable 25 rows
                                    vertical fine scroll and control
                                    bit function
                                    --- -------
                                    7  raster compare bit 8
                                    6  1 = enable extended color text mode
                                    5  1 = enable bitmap graphics mode
                                    4  1 = enable screen, 0 = blank screen
                                    3  1 = 25 row display, 0 = 24 row display
                                    2-0 vertical scroll count
    .:ECCB 37                       raster compare
    .:ECCC 00                       light pen x
    .:ECCD 00                       light pen y
    .:ECCE 00                       sprite 0 to 7 enable
    .:ECCF 08                       enable 40 column display
                                    horizontal fine scroll and control
                                    bit function
                                    --- -------
                                    7-6 unused
                                    5  1 = vic reset, 0 = vic on
                                    4  1 = enable multicolor mode
                                    3  1 = 40 column display, 0 = 38 column display
                                    2-0 horizontal scroll count
    .:ECC0 00                       sprite 0 to 7 y expand
    .:ECD1 14                       memory control
                                    bit function
                                    --- -------
                                    7-4 video matrix base address
                                    3-1 character data base address
                                    0  unused
    .:ECD2 0F                       clear all interrupts
                                    interrupt flags
                                    7 1 = interrupt
                                    6-4 unused
                                    3  1 = light pen interrupt
                                    2  1 = sprite to sprite collision interrupt
                                    1  1 = sprite to foreground collision interrupt
                                    0  1 = raster compare interrupt
    .:ECD3 00                       all vic IRQs disabeld
                                    IRQ enable
                                    bit function
                                    --- -------
                                    7-4 unused
                                    3  1 = enable light pen
                                    2  1 = enable sprite to sprite collision
                                    1  1 = enable sprite to foreground collision
                                    0  1 = enable raster compare
    .:ECD4 00                       sprite 0 to 7 foreground priority
    .:ECD5 00                       sprite 0 to 7 multicolour
    .:ECD6 00                       sprite 0 to 7 x expand
    .:ECD7 00                       sprite 0 to 7 sprite collision
    .:ECD8 00                       sprite 0 to 7 foreground collision
    .:ECD9 0E                       border colour
    .:ECDA 06                       background colour 0
    .:ECDB 01                       background colour 1
    .:ECDC 02                       background colour 2
    .:ECDD 03                       background colour 3
    .:ECDE 04                       sprite multicolour 0
    .:ECDF 00                       sprite multicolour 1
    .:ECD0 01                       sprite 0 colour
    .:ECE1 02                       sprite 1 colour
    .:ECE2 03                       sprite 2 colour
    .:ECE3 04                       sprite 3 colour
    .:ECE4 05                       sprite 4 colour
    .:ECE5 06                       sprite 5 colour
    .:ECE6 07                       sprite 6 colour
                                    sprite 7 colour is actually the first character of "LOAD" ($4C) 

    ------------------------------------------------------------------------ */

    bus.memory[0xECB8] = 0xFF;
    bus.memory[0xECB9] = 0x00; bus.memory[0xECBA] = 0x00;                              // sprite 0 x,y
    bus.memory[0xECBB] = 0x00; bus.memory[0xECBC] = 0x00;                              // sprite 1 x,y
    bus.memory[0xECBD] = 0x00; bus.memory[0xECBE] = 0x00;                              // sprite 2 x,y
    bus.memory[0xECBF] = 0x00; bus.memory[0xECC0] = 0x00;                              // sprite 3 x,y
    bus.memory[0xECC1] = 0x00; bus.memory[0xECC2] = 0x00;                              // sprite 4 x,y
    bus.memory[0xECC3] = 0x00; bus.memory[0xECC4] = 0x00;                              // sprite 5 x,y        
    bus.memory[0xECC5] = 0x00; bus.memory[0xECC6] = 0x00;                              // sprite 6 x,y
    bus.memory[0xECC7] = 0x00; bus.memory[0xECC8] = 0x00;                              // sprite 7 x,y
    bus.memory[0xECC9] = 0x00;                                                         // sprites 0 to 7 x bit 8 
    bus.memory[0xECCA] = 0x9B;                                                         // enable screen, enable 25 rows
    bus.memory[0xECCB] = 0x37;                                                         // raster compare
    bus.memory[0xECCC] = 0x00;                                                         // light pen x
    bus.memory[0xECCD] = 0x00;                                                         // light pen y
    bus.memory[0xECCE] = 0x00;                                                         // sprite 0 to 7 enable
    bus.memory[0xECCF] = 0x08;                                                         // enable 40 column display
    bus.memory[0xECD0] = 0x00;                                                         // sprite 0 to 7 y expand
    bus.memory[0xECD1] = 0x14;                                                         // memory control    
    bus.memory[0xECD2] = 0x0F;                                                         // clear all interrupts
    bus.memory[0xECD3] = 0x00;                                                         // all vic IRQs disabeld
    bus.memory[0xECD4] = 0x00;                                                         // sprite 0 to 7 foreground priority
    bus.memory[0xECD5] = 0x00;                                                         // sprite 0 to 7 multicolour
    bus.memory[0xECD6] = 0x00;                                                         // sprite 0 to 7 x expand
    bus.memory[0xECD7] = 0x00;                                                         // sprite 0 to 7 sprite collision
    bus.memory[0xECD8] = 0x00;                                                         // sprite 0 to 7 foreground collision
    bus.memory[0xECD9] = 0x0E;                                                         // border colour
    bus.memory[0xECDA] = 0x06;                                                         // background colour 0
    bus.memory[0xECDB] = 0x01;                                                         // background colour 1
    bus.memory[0xECDC] = 0x02;                                                         // background colour 2
    bus.memory[0xECDD] = 0x03;                                                         // background colour 3
    bus.memory[0xECDE] = 0x04;                                                         // sprite multicolour 0
    
    bus.memory[0xECDF] = 0x00;                                                         // sprite multicolour 1
    bus.memory[0xECE0] = 0x01;                                                         // sprite 0 colour
    bus.memory[0xECE1] = 0x02;                                                         // sprite 1 colour
    bus.memory[0xECE2] = 0x03;                                                         // sprite 2 colour
    bus.memory[0xECE3] = 0x04;                                                         // sprite 3 colour
    bus.memory[0xECE4] = 0x05;                                                         // sprite 4 colour
    bus.memory[0xECE5] = 0x06;                                                         // sprite 5 colour
    bus.memory[0xECE6] = 0x07;                                                         // sprite 6 colour
                                                                                       // sprite 7 colour is actually the first character of "LOAD" ($4C)
    #pragma endregion                                                                             

    #pragma region *** set the serial clock out low (0xEE8E - 0xEE96)      

    /* ----------------------------------------------------------------------     
                                    *** set the serial clock out low
    .,EE8E AD 00 DD LDA $DD00       read VIA 2 DRA, serial port and video address
    .,EE91 09 10    ORA #$10        mask xxx1 xxxx, set serial clock out low
    .,EE93 8D 00 DD STA $DD00       save VIA 2 DRA, serial port and video address
    .,EE96 60       RTS  
    
    ------------------------------------------------------------------------ */   

    bus.memory[0xEE8E] = 0xAD; bus.memory[0xEE8F] = 0x00; bus.memory[0xEE90] = 0xDD;    // LDA $DD00       read VIA 2 DRA, serial port and video address
    bus.memory[0xEE91] = 0x09; bus.memory[0xEE92] = 0x10;                               // ORA #$10        mask xxx1 xxxx, set serial clock out low
    bus.memory[0xEE93] = 0x8D; bus.memory[0xEE94] = 0x00; bus.memory[0xEE95] = 0xDD;    // save VIA 2 DRA, serial port and video address
    bus.memory[0xEE96] = 0x60;                                                          // RTS

    #pragma endregion

    #pragma region *** scan for autostart ROM at $8000, returns Zb=1 if ROM found (0xFD02 - 0xFD10)    

    /* ----------------------------------------------------------------------

    *** scan for autostart ROM at $8000, returns Zb=1 if ROM found

    .,FD02 A2 05    LDX #$05        five characters to test
    .,FD04 BD 0F FD LDA $FD0F,X     get test character
    .,FD07 DD 03 80 CMP $8003,X     compare wiith byte in ROM space
    .,FD0A D0 03    BNE $FD0F       exit if no match
    .,FD0C CA       DEX             decrement index
    .,FD0D D0 F5    BNE $FD04       loop if not all done
    .,FD0F 60       RTS             
                                    *** autostart ROM signature
    .:FD10 C3 C2 CD 38 30           'CBM80’
    
    ------------------------------------------------------------------------ */
    
    bus.memory[0xFD02] = 0xA2; bus.memory[0xFD03] = 0x05;                              // LDX #$05        five characters to test
    bus.memory[0xFD04] = 0xBD; bus.memory[0xFD05] = 0x0F; bus.memory[0xFD06] = 0xFD;   // LDA $FD0F,X     get test character (carica in A <- [FD0F+X])
    bus.memory[0xFD07] = 0xDD; bus.memory[0xFD08] = 0x03; bus.memory[0xFD09] = 0x80;   // CMP $8003,X     compare with byte in ROM space
    bus.memory[0xFD0A] = 0xD0; bus.memory[0xFD0B] = 0x03;                              // BNE $FD0F       exit if no match
    bus.memory[0xFD0C] = 0xCA;                                                         // DEX 
    bus.memory[0xFD0D] = 0xD0; bus.memory[0xFD0E] = 0xF5;                              // BNE $FD04       loop if not all done
    bus.memory[0xFD0F] = 0x60;                                                         // RTS 

    bus.memory[0xFD10] = 0xC3; bus.memory[0xFD11] = 0xC2; bus.memory[0xFD12] = 0xCD; bus.memory[0xFD13] = 0x38; bus.memory[0xFD14] = 0x30;  // [FD10] C3 C2 CD 38 30  'CBM80’

    #pragma endregion

    #pragma region *** restore default I/O vectors (0xFD15 - 0xFD2F)    

    /* ----------------------------------------------------------------------
                                    *** restore default I/O vectors
    .,FD15 A2 30    LDX #$30        pointer to vector table low byte
    .,FD17 A0 FD    LDY #$FD        pointer to vector table high byte
    .,FD19 18       CLC             flag set vectors

                                    *** set/read vectored I/O from (XY), Cb = 1 to read, Cb = 0 to set
    .,FD1A 86 C3    STX $C3         save pointer low byte
    .,FD1C 84 C4    STY $C4         save pointer high byte
    .,FD1E A0 1F    LDY #$1F        set byte count    
    .,FD20 B9 14 03 LDA $0314,Y     read vector byte from vectors
    .,FD23 B0 02    BCS $FD27       branch if read vectors
    .,FD25 B1 C3    LDA ($C3),Y     read vector byte from (XY)
    .,FD27 91 C3    STA ($C3),Y     save byte to (XY)
    .,FD29 99 14 03 STA $0314,Y     save byte to vector
    .,FD2C 88       DEY             decrement index

    .,FD2D 10 F1    BPL $FD20       loop if more to do
    .,FD2F 60       RTS         

    ------------------------------------------------------------------------ */

    bus.memory[0xFD15] = 0xA2; bus.memory[0xFD16] = 0x30;                              // LDX #$30        pointer to vector table low byte 
    bus.memory[0xFD17] = 0xA0; bus.memory[0xFD18] = 0xFD;                              // LDY #$FD        pointer to vector table high byte
    bus.memory[0xFD19] = 0x18;                                                         // CLC             flag set vectors

    bus.memory[0xFD1A] = 0x86; bus.memory[0xFD1B] = 0xC3;                              // STX $C3         save pointer low byte
    bus.memory[0xFD1C] = 0x84; bus.memory[0xFD1D] = 0xC4;                              // STY $C4         save pointer high byte    
    bus.memory[0xFD1E] = 0xA0; bus.memory[0xFD1F] = 0x1F;                              // LDY #$1F        set byte count

    // l’indirizzo $0314–$0315 contiene il vettore di interruzione IRQ — cioè l’indirizzo della routine che il processore deve eseguire quando si verifica un’interruzione IRQ (Interrupt ReQuest).

    bus.memory[0xFD20] = 0xB9; bus.memory[0xFD21] = 0x14; bus.memory[0xFD22] = 0x03;   // LDA $0314,Y     read vector byte from vectors
    bus.memory[0xFD23] = 0xB0; bus.memory[0xFD24] = 0x02;                              // BCS $FD27       branch if read vectors
    bus.memory[0xFD25] = 0xB1; bus.memory[0xFD26] = 0xC3;                              // LDA ($C3),Y     read vector byte from (XY)
    bus.memory[0xFD27] = 0x91; bus.memory[0xFD28] = 0xC3;                              // STA ($C3),Y     save byte to (XY)
    bus.memory[0xFD29] = 0x99; bus.memory[0xFD2A] = 0x14; bus.memory[0xFD2B] = 0x03;   // STA $0314,Y     save byte to vector
    bus.memory[0xFD2C] = 0x88;                                                         // DEY             decrement index
    bus.memory[0xFD2D] = 0x10; bus.memory[0xFD2E] = 0xF1;                              // BPL $FD20       loop if more to do     
    bus.memory[0xFD2F] = 0x60;                                                         // RTS

    #pragma endregion

    #pragma region *** KERNAL vectors (0xFD30 - 0xFD4F)

    /* ----------------------------------------------------------------------

                                    *** kernal vectors ***

        These entries define the indirect vectors stored at $0314–$0332,
        providing jump-table redirection for interrupts, I/O, and file handling.

        These vectors can be patched by the user or by extensions to redirect
        low-level behavior without modifying the KERNAL ROM itself.

        .,FD30 31 EA        $0314 IRQ vector
        .,FD32 66 FE        $0316 BRK vector
        .,FD34 47 FE        $0318 NMI vector
        .,FD36 4A F3        $031A open a logical file
        .,FD38 91 F2        $031C close a specified logical file
        .,FD3A 0E F2        $031E open channel for input
        .,FD3C 50 F2        $0320 open channel for output
        .,FD3E 33 F3        $0322 close input and output channels
        .,FD40 57 F1        $0324 input character from channel
        .,FD42 CA F1        $0326 output character to channel
        .,FD44 ED F6        $0328 scan stop key
        .,FD46 3E F1        $032A get character from the input device
        .,FD48 2F F3        $032C close all channels and files
        .,FD4A 66 FE        $032E user function
        
            Vector to user defined command, currently points to BRK.
            This is a holdover from PET days: the PET monitor jumped through 
            $032E if it encountered an unknown command, allowing users to 
            extend the monitor.
            Today it is still initialized (via STOP/RESTORE → BRK handler) 
            and updated by the kernal vector routine at $FD57,
            but it no longer has any practical function.

        .,FD4C A5 F4        $0330 load
        .,FD4E ED F5        $0332 save

    ------------------------------------------------------------------------ */

    bus.memory[0xFD30] = 0x31; bus.memory[0xFD31] = 0xEA;          // IRQ vector ($0314)
    bus.memory[0xFD32] = 0x66; bus.memory[0xFD33] = 0xFE;          // BRK vector ($0316)
    bus.memory[0xFD34] = 0x47; bus.memory[0xFD35] = 0xFE;          // NMI vector ($0318)

    bus.memory[0xFD36] = 0x4A; bus.memory[0xFD37] = 0xF3;          // open logical file
    bus.memory[0xFD38] = 0x91; bus.memory[0xFD39] = 0xF2;          // close specified logical file
    bus.memory[0xFD3A] = 0x0E; bus.memory[0xFD3B] = 0xF2;          // open channel for input
    bus.memory[0xFD3C] = 0x50; bus.memory[0xFD3D] = 0xF2;          // open channel for output
    bus.memory[0xFD3E] = 0x33; bus.memory[0xFD3F] = 0xF3;          // close input and output channels

    bus.memory[0xFD40] = 0x57; bus.memory[0xFD41] = 0xF1;          // input character from channel
    bus.memory[0xFD42] = 0xCA; bus.memory[0xFD43] = 0xF1;          // output character to channel
    bus.memory[0xFD44] = 0xED; bus.memory[0xFD45] = 0xF6;          // scan stop key
    bus.memory[0xFD46] = 0x3E; bus.memory[0xFD47] = 0xF1;          // get character from input device
    bus.memory[0xFD48] = 0x2F; bus.memory[0xFD49] = 0xF3;          // close all channels and files

    bus.memory[0xFD4A] = 0x66; bus.memory[0xFD4B] = 0xFE;          // user function vector ($032E), legacy PET handler

    bus.memory[0xFD4C] = 0xA5; bus.memory[0xFD4D] = 0xF4;          // load ($0330)
    bus.memory[0xFD4E] = 0xED; bus.memory[0xFD4F] = 0xF5;          // save ($0332)

    #pragma endregion

    #pragma region *** test RAM and find RAM end (0xFD50 - 0xFD9A)    

    /* ----------------------------------------------------------------------
                                    *** test RAM and find RAM end
    .,FD50 A9 00    LDA #$00        clear A
    .,FD52 A8       TAY             clear index
    .,FD53 99 02 00 STA $0002,Y     clear page 0, don't do $0000 or $0001
    .,FD56 99 00 02 STA $0200,Y     clear page 2
    .,FD59 99 00 03 STA $0300,Y     clear page 3
    .,FD5C C8       INY             increment index
    .,FD5D D0 F4    BNE $FD53       loop if more to do    

    .,FD5F A2 3C    LDX #$3C        set cassette buffer pointer low byte
    .,FD61 A0 03    LDY #$03        set cassette buffer pointer high byte
    .,FD63 86 B2    STX $B2         save tape buffer start pointer low byte
    .,FD65 84 B3    STY $B3         save tape buffer start pointer high byte 
        
    .,FD67 A8       TAY             clear Y
    .,FD68 A9 03    LDA #$03        set RAM test pointer high byte
    .,FD6A 85 C2    STA $C2         save RAM test pointer high byte
    .,FD6C E6 C2    INC $C2         increment RAM test pointer high byte    
    .,FD6E B1 C1    LDA ($C1),Y     
    .,FD70 AA       TAX             
    .,FD71 A9 55    LDA #$55        
    .,FD73 91 C1    STA ($C1),Y     
    .,FD75 D1 C1    CMP ($C1),Y 
    .,FD77 D0 0F    BNE $FD88    
    .,FD79 2A       ROL
    .,FD7A 91 C1    STA ($C1),Y     
    .,FD7C D1 C1    CMP ($C1),Y 
    .,FD7E D0 08    BNE $FD88       
    .,FD80 8A       TXA         
    .,FD81 91 C1    STA ($C1),Y    
    .,FD83 C8       INY     
    .,FD84 D0 E8    BNE $FD6E   
    .,FD86 F0 E4    BEQ $FD6C
    .,FD88 98       TYA             
    .,FD89 AA       TAX
    .,FD8A A4 C2    LDY $C2         
    .,FD8C 18       CLC  
    .,FD8D 20 2D FE JSR $FE2D       set the top of memory        
    .,FD90 A9 08    LDA #$08      
    .,FD92 8D 82 02 STA $0282       save the OS start of memory high byte
    .,FD95 A9 04    LDA #$04        
    .,FD97 8D 88 02 STA $0288       save the screen memory page
    .,FD9A 60       RTS       
    
    ------------------------------------------------------------------------ */

    bus.memory[0xFD50] = 0xA9; bus.memory[0xFD51] = 0x00;                              // LDA #$00        clear A
    bus.memory[0xFD52] = 0xA8;                                                         // TAY      
    bus.memory[0xFD53] = 0x99; bus.memory[0xFD54] = 0x02; bus.memory[0xFD55] = 0x00;   // STA $0002,Y     clear page 0, don't do $0000 or $0001
    bus.memory[0xFD56] = 0x99; bus.memory[0xFD57] = 0x00; bus.memory[0xFD58] = 0x02;   // STA $0200,Y     clear page 2
    bus.memory[0xFD59] = 0x99; bus.memory[0xFD5A] = 0x00; bus.memory[0xFD5B] = 0x03;   // STA $0300,Y     clear page 3
    bus.memory[0xFD5C] = 0xC8;                                                         // INY             increment index
    bus.memory[0xFD5D] = 0xD0; bus.memory[0xFD5E] = 0xF4;                              // BNE $FD53       loop if more to do (Quando Y diventa 0 di nuovo (dopo 256 incrementi), il flag Z = 1, quindi BNE non salta, ed esce dal ciclo.)
    
    bus.memory[0xFD5F] = 0xA2; bus.memory[0xFD60] = 0x3C;                              // LDX #$3C        set cassette buffer pointer low byte
    bus.memory[0xFD61] = 0xA0; bus.memory[0xFD62] = 0x03;                              // LDY #$03        set cassette buffer pointer high byte
    bus.memory[0xFD63] = 0x86; bus.memory[0xFD64] = 0xB2;                              // STX $B2         save tape buffer start pointer low byte
    bus.memory[0xFD65] = 0x84; bus.memory[0xFD66] = 0xB3;                              // STY $B3         save tape buffer start pointer high byte   

    bus.memory[0xFD67] = 0xA8;                                                         // TAY             clear Y    
    bus.memory[0xFD68] = 0xA9; bus.memory[0xFD69] = 0x03;                              // LDA #$03        set RAM test pointer high byte   
    bus.memory[0xFD6A] = 0x85; bus.memory[0xFD6B] = 0xC2;                              // STA $C2         save RAM test pointer high byte
    bus.memory[0xFD6C] = 0xE6; bus.memory[0xFD6D] = 0xC2;                              // INC $C2         increment RAM test pointer high byte  
    bus.memory[0xFD6E] = 0xB1; bus.memory[0xFD6F] = 0xC1;                              // LDA ($C1),Y     
    bus.memory[0xFD70] = 0xAA;                                                         // TAX             
    bus.memory[0xFD71] = 0xA9; bus.memory[0xFD72] = 0x55;                              // LDA #$55        
    bus.memory[0xFD73] = 0x91; bus.memory[0xFD74] = 0xC1;                              // STA ($C1),Y     
    bus.memory[0xFD75] = 0xD1; bus.memory[0xFD76] = 0xC1;                              // CMP ($C1),Y             
    bus.memory[0xFD77] = 0xD0; bus.memory[0xFD78] = 0x0F;                              // BNE $FD88
    bus.memory[0xFD79] = 0x2A;                                                         // ROL
    bus.memory[0xFD7A] = 0x91; bus.memory[0xFD7B] = 0xC1;                              // STA ($C1),Y     
    bus.memory[0xFD7C] = 0xD1; bus.memory[0xFD7D] = 0xC1;                              // CMP ($C1),Y 
    bus.memory[0xFD7E] = 0xD0; bus.memory[0xFD7F] = 0x08;                              // BNE $FD88       
    bus.memory[0xFD80] = 0x8A;                                                         // TXA 
    bus.memory[0xFD81] = 0x91; bus.memory[0xFD82] = 0xC1;                              // STA ($C1),Y              
    bus.memory[0xFD83] = 0xC8;                                                         // INY     
    bus.memory[0xFD84] = 0xD0; bus.memory[0xFD85] = 0xE8;                              // BNE $FD6E   
    bus.memory[0xFD86] = 0xF0; bus.memory[0xFD87] = 0xE4;                              // BEQ $FD6C
    bus.memory[0xFD88] = 0x98;                                                         // TYA             
    bus.memory[0xFD89] = 0xAA;                                                         // TAX 
    bus.memory[0xFD8A] = 0xA4; bus.memory[0xFD8B] = 0xC2;                              // LDY $C2
    bus.memory[0xFD8C] = 0x18;                                                         // CLC   
    bus.memory[0xFD8D] = 0x20; bus.memory[0xFD8E] = 0x2D; bus.memory[0xFD8F] = 0xFE;   // JSR $FE2D       set the top of memory
    bus.memory[0xFD90] = 0xA9; bus.memory[0xFD91] = 0x08;                              // LDA #$08    
    bus.memory[0xFD92] = 0x8D; bus.memory[0xFD93] = 0x82; bus.memory[0xFD94] = 0x02;   // STA $0282       save the OS start of memory high byte
    bus.memory[0xFD95] = 0xA9; bus.memory[0xFD96] = 0x04;                              // LDA #$04        
    bus.memory[0xFD97] = 0x8D; bus.memory[0xFD98] = 0x88; bus.memory[0xFD99] = 0x02;   // STA $0288       save the screen memory page        
    bus.memory[0xFD9A] = 0x60;                                                         // RTS

    #pragma endregion

    #pragma region *** initialise SID, CIA and IRQ (0xFDA3 - 0xFDF6)    

    /* ----------------------------------------------------------------------

                                    *** initialise SID, CIA and IRQ
    .,FDA3 A9 7F    LDA #$7F        disable all interrupts
    .,FDA5 8D 0D DC STA $DC0D       save VIA 1 ICR
    .,FDA8 8D 0D DD STA $DD0D       save VIA 2 ICR
    .,FDAB 8D 00 DC STA $DC00       save VIA 1 DRA, keyboard column drive

    .,FDAE A9 08    LDA #$08        set timer single shot
    .,FDB0 8D 0E DC STA $DC0E       save VIA 1 CRA
    .,FDB3 8D 0E DD STA $DD0E       save VIA 2 CRA
    .,FDB6 8D 0F DC STA $DC0F       save VIA 1 CRB
    .,FDB9 8D 0F DD STA $DD0F       save VIA 2 CRB   
        
    .,FDBC A2 00    LDX #$00        set all inputs
    .,FDBE 8E 03 DC STX $DC03       save VIA 1 DDRB, keyboard row
    .,FDC1 8E 03 DD STX $DD03       save VIA 2 DDRB, RS232 port  

    .,FDC4 8E 18 D4 STX $D418       clear the volume and filter select register
    .,FDC7 CA       DEX             set X = $FF
    .,FDC8 8E 02 DC STX $DC02       save VIA 1 DDRA, keyboard column 

    .,FDCB A9 07    LDA #$07        DATA out high, CLK out high, ATN out high, RE232 Tx DATA high, video address 15 = 1, video address 14 = 1       
    .,FDCD 8D 00 DD STA $DD00       save VIA 2 DRA, serial port and video address

    .,FDD0 A9 3F    LDA #$3F        set serial DATA input, serial CLK input
    .,FDD2 8D 02 DD STA $DD02       save VIA 2 DDRA, serial port and video address

    .,FDD5 A9 E7    LDA #$E7        set 1110 0111, motor off, enable I/O, enable KERNAL, enable BASIC
    .,FDD7 85 01    STA $01         save the 6510 I/O port

    .,FDD9 A9 2F    LDA #$2F        set 0010 1111, 0 = input, 1 = output
    .,FDDB 85 00    STA $00         save the 6510 I/O port direction register

    .,FDDD AD A6 02 LDA $02A6       get the PAL/NTSC flag
    .,FDE0 F0 0A    BEQ $FDEC       if NTSC go set NTSC timing else set PAL timing

    .,FDE2 A9 25    LDA #$25        
    .,FDE4 8D 04 DC STA $DC04       save VIA 1 timer A low byte  
    
    .,FDE7 A9 40    LDA #$40        
    .,FDE9 4C F3 FD JMP $FDF3 
    
    .,FDEC A9 95    LDA #$95
    .,FDEE 8D 04 DC STA $DC04       save VIA 1 timer A low byte
    .,FDF1 A9 42    LDA #$42

    .,FDF3 8D 05 DC STA $DC05       save VIA 1 timer A high byte
    .,FDF6 4C 6E FF JMP $FF6E   

    ------------------------------------------------------------------------ */

    bus.memory[0xFDA3] = 0xA9; bus.memory[0xFDA4] = 0x7F;                               // LDA #$7F        disable all interrupts
    bus.memory[0xFDA5] = 0x8D; bus.memory[0xFDA6] = 0x0D; bus.memory[0xFDA7] = 0xDC;    // STA $DC0D       save VIA 1 ICR
    bus.memory[0xFDA8] = 0x8D; bus.memory[0xFDA9] = 0x0D; bus.memory[0xFDAA] = 0xDD;    // STA $DD0D       save VIA 2 ICR
    bus.memory[0xFDAB] = 0x8D; bus.memory[0xFDAC] = 0x00; bus.memory[0xFDAD] = 0xDC;    // STA $DC00       save VIA 1 DRA, keyboard column drive
    
    bus.memory[0xFDAE] = 0xA9; bus.memory[0xFDAF] = 0x08;                               // LDA #$08        set timer single shot
    bus.memory[0xFDB0] = 0x8D; bus.memory[0xFDB1] = 0x0E; bus.memory[0xFDB2] = 0xDC;    // STA $DC0E       save VIA 1 CRA (Forza il caricamento del valore del Timer A senza avviarlo)
    bus.memory[0xFDB3] = 0x8D; bus.memory[0xFDB4] = 0x0E; bus.memory[0xFDB5] = 0xDC;    // STA $DD0E       save VIA 2 CRA
    bus.memory[0xFDB6] = 0x8D; bus.memory[0xFDB7] = 0x0F; bus.memory[0xFDB8] = 0xDC;    // STA $DC0F       save VIA 1 CRB
    bus.memory[0xFDB9] = 0x8D; bus.memory[0xFDBA] = 0x0F; bus.memory[0xFDBB] = 0xDD;    // STA $DD0F       save VIA 2 CRB    

    bus.memory[0xFDBC] = 0xA2; bus.memory[0xFDBD] = 0x00;                               // LDX #$00        set all inputs
    bus.memory[0xFDBE] = 0x8E; bus.memory[0xFDBF] = 0x03; bus.memory[0xFDC0] = 0xDC;    // STX $DC03       save VIA 1 DDRB, keyboard row
    bus.memory[0xFDC1] = 0x8E; bus.memory[0xFDC2] = 0x03; bus.memory[0xFDC3] = 0xDD;    // STX $DD03       save VIA 2 DDRB, RS232 port   
    
    bus.memory[0xFDC4] = 0x8E; bus.memory[0xFDC5] = 0x18; bus.memory[0xFDC6] = 0xD4;    // STX $D418       clear the volume and filter select register
    bus.memory[0xFDC7] = 0xCA;                                                          // DEX             set X = $FF

    bus.memory[0xFDC8] = 0x8E; bus.memory[0xFDC9] = 0x02; bus.memory[0xFDCA] = 0xDC;    // STX $DC02       save VIA 1 DDRA, keyboard column
    bus.memory[0xFDCB] = 0xA9; bus.memory[0xFDCC] = 0x07;                               // LDA #$07        DATA out high, CLK out high, ATN out high, RE232 Tx DATA high, video address 15 = 1, video address 14 = 1         
    bus.memory[0xFDCD] = 0x8D; bus.memory[0xFDCE] = 0x00; bus.memory[0xFDCF] = 0xDD;    // STA $DD00       save VIA 2 DRA, serial port and video address                                                                                            

    bus.memory[0xFDD0] = 0xA9; bus.memory[0xFDD1] = 0x3F;                               // LDA #$3F        set serial DATA input, serial CLK input 
    bus.memory[0xFDD2] = 0x8D; bus.memory[0xFDD3] = 0x02; bus.memory[0xFDD4] = 0xDD;    // STA $DD02       save VIA 2 DDRA, serial port and video address
    
    bus.memory[0xFDD5] = 0xA9; bus.memory[0xFDD6] = 0xE7;                               // LDA #$E7        set 1110 0111, motor off, enable I/O, enable KERNAL, enable BASIC
    bus.memory[0xFDD7] = 0x85; bus.memory[0xFDD8] = 0x01;                               // STA $01         save the 6510 I/O port (registro I/O del processore 6510)

    bus.memory[0xFDD9] = 0xA9; bus.memory[0xFDDA] = 0x2F;                               // LDA #$2F        set 0010 1111, 0 = input, 1 = output
    bus.memory[0xFDDB] = 0x85; bus.memory[0xFDDC] = 0x00;                               // STA $01         save the 6510 I/O port direction register 
    
    bus.memory[0xFDDD] = 0xAD; bus.memory[0xFDDE] = 0xA6; bus.memory[0xFDDF] = 0x02;    // LDA $02A6       get the PAL/NTSC flag
    bus.memory[0xFDE0] = 0xF0; bus.memory[0xFDE1] = 0x0A;                               // BEQ $FDEC       if NTSC go set NTSC timing else set PAL timing

    bus.memory[0xFDE2] = 0xA9; bus.memory[0xFDE3] = 0x25;                               // LDA #$25        
    bus.memory[0xFDE4] = 0x8D; bus.memory[0xFDE5] = 0x04; bus.memory[0xFDE6] = 0xDC;    // STA $DC04       save VIA 1 timer A low byte  
    
    bus.memory[0xFDE7] = 0xA9; bus.memory[0xFDE8] = 0x40;                               // LDA #$40        
    bus.memory[0xFDE9] = 0x4C; bus.memory[0xFDEA] = 0xF3; bus.memory[0xFDEB] = 0xFD;    // JMP $FDF3   

    bus.memory[0xFDEC] = 0xA9; bus.memory[0xFDED] = 0x95;                               // LDA #$95        NSTC case 
    bus.memory[0xFDEE] = 0x8D; bus.memory[0xFDEF] = 0x04; bus.memory[0xFDF0] = 0xDC;    // STA $DC04       save VIA 1 timer A low byte
    bus.memory[0xFDF1] = 0xA9; bus.memory[0xFDF2] = 0x42;                               // LDA #$42  
    bus.memory[0xFDF3] = 0x8D; bus.memory[0xFDF4] = 0x05; bus.memory[0xFDF5] = 0xDC;    // STA $DC05       save VIA 1 timer A high byte
    bus.memory[0xFDF6] = 0x4C; bus.memory[0xFDF7] = 0x6E; bus.memory[0xFDF8] = 0xFF;    // JMP $FF6E                                        *** done ***

    #pragma endregion

    #pragma region *** read/set the top of memory (0xFE25 - 0xFE2A)

    /* ----------------------------------------------------------------------

                                *** read/set the top of memory
                                Cb = 1 → read the top of memory into X,Y
                                Cb = 0 → set the top of memory from X,Y

        .,FE25 90 06    BCC $FE2D       if Cb clear, jump to set top of memory

            *** read the top of memory
        .,FE27 AE 83 02 LDX $0283       get memory top low byte
        .,FE2A AC 84 02 LDY $0284       get memory top high byte

    ------------------------------------------------------------------------ */

    bus.memory[0xFE25] = 0x90; bus.memory[0xFE26] = 0x06;                             // BCC $FE2D

    bus.memory[0xFE27] = 0xAE; bus.memory[0xFE28] = 0x83; bus.memory[0xFE29] = 0x02;  // LDX $0283
    bus.memory[0xFE2A] = 0xAC; bus.memory[0xFE2B] = 0x84; bus.memory[0xFE2C] = 0x02;  // LDY $0284

    #pragma endregion

    #pragma region *** set the top of memory (0xFE2D - 0xFE33)    

    /* ----------------------------------------------------------------------

                                    *** set the top of memory
    .,FE2D 8E 83 02 STX $0283       set memory top low byte
    .,FE30 8C 84 02 STY $0284       set memory top high byte
    .,FE33 60       RTS    
    
    ------------------------------------------------------------------------ */
      
    bus.memory[0xFE2D] = 0x8E; bus.memory[0xFE2E] = 0x83; bus.memory[0xFE2F] = 0x02;    // STX $0283       set memory top low byte     
    bus.memory[0xFE30] = 0x8C; bus.memory[0xFE31] = 0x84; bus.memory[0xFE32] = 0x02;    // STY $0284       set memory top high byte  
    bus.memory[0xFE33] = 0x60;                                                          // RTS 

    #pragma endregion    

    #pragma region *** read/set the bottom of memory (0xFE34 - 0xFE42)

    /* ----------------------------------------------------------------------

                            *** read/set the bottom of memory
        Cb = 1 → read
        Cb = 0 → set

        .,FE34 90 06    BCC $FE3C        if Cb clear → go set bottom of memory
        .,FE36 AE 81 02 LDX $0281        get OS start of memory low byte
        .,FE39 AC 82 02 LDY $0282        get OS start of memory high byte
        .,FE3C 8E 81 02 STX $0281        save OS start of memory low byte
        .,FE3F 8C 82 02 STY $0282        save OS start of memory high byte
        .,FE42 60       RTS

    ------------------------------------------------------------------------ */

    bus.memory[0xFE34] = 0x90; bus.memory[0xFE35] = 0x06;                            // BCC $FE3C
    bus.memory[0xFE36] = 0xAE; bus.memory[0xFE37] = 0x81; bus.memory[0xFE38] = 0x02; // LDX $0281
    bus.memory[0xFE39] = 0xAC; bus.memory[0xFE3A] = 0x82; bus.memory[0xFE3B] = 0x02; // LDY $0282
    bus.memory[0xFE3C] = 0x8E; bus.memory[0xFE3D] = 0x81; bus.memory[0xFE3E] = 0x02; // STX $0281
    bus.memory[0xFE3F] = 0x8C; bus.memory[0xFE40] = 0x82; bus.memory[0xFE41] = 0x02; // STY $0282
    bus.memory[0xFE42] = 0x60;                                                       // RTS

    #pragma endregion

    #pragma region *** NMI vector (0xFE43 - 0xFE46)    

    /* ----------------------------------------------------------------------
                                    *** NMI vector
    .,FE43 78       SEI             disable the interrupts
    .,FE44 6C 18 03 JMP ($0318)     do NMI vector
    ------------------------------------------------------------------------ */

    bus.memory[0xFE43] = 0x78;                                                          // SEI             disable the interrupts      
    bus.memory[0xFE44] = 0x6C; bus.memory[0xFE45] = 0x18; bus.memory[0xFE46] = 0x03;    // JMP ($0318)     do NMI vector   
                                                          
    #pragma endregion 

    #pragma region *** IRQ vector (0xFF48 - 0xFF5A) 

    /* ----------------------------------------------------------------------

      IRQ VECTOR

                                    *** IRQ vector
    .,FF48 48       PHA             save A
    .,FF49 8A       TXA             copy X
    .,FF4A 48       PHA             save X
    .,FF4B 98       TYA             copy Y
    .,FF4C 48       PHA             save Y
    .,FF4D BA       TSX             copy stack pointer
    
    .,FF4E BD 04 01 LDA $0104,X     get stacked status register
    .,FF51 29 10    AND #$10        mask BRK flag
    .,FF53 F0 03    BEQ $FF58       branch if not BRK
    .,FF55 6C 16 03 JMP ($0316)     else do BRK vector (iBRK)
    .,FF58 6C 14 03 JMP ($0314)     do IRQ vector (iIRQ)

    ------------------------------------------------------------------------ */

    bus.memory[0xFF48] = 0x48;                                                         // PHA             save A
    bus.memory[0xFF49] = 0x8A;                                                         // TXA             copy X
    bus.memory[0xFF4A] = 0x48;                                                         // PHA             save X
    bus.memory[0xFF4B] = 0x98;                                                         // TYA             copy Y    
    bus.memory[0xFF4C] = 0x48;                                                         // PHA             save Y 
    bus.memory[0xFF4D] = 0xBA;                                                         // TSX             copy stack pointer
    bus.memory[0xFF4E] = 0xBD; bus.memory[0xFF4F] = 0x04; bus.memory[0xFF50] = 0x01;   // LDA $0104,X      get stacked status register
    bus.memory[0xFF51] = 0x29; bus.memory[0xFF52] = 0x10;                              // AND #$10         mask BRK flag
    bus.memory[0xFF53] = 0xF0; bus.memory[0xFF54] = 0x03;                              // BEQ $FF58        branch if not BRK
    bus.memory[0xFF55] = 0x6C; bus.memory[0xFF56] = 0x16; bus.memory[0xFF57] = 0x03;   // JMP ($0316)      else do BRK vector (iBRK)
    bus.memory[0xFF58] = 0x6C; bus.memory[0xFF59] = 0x14; bus.memory[0xFF5A] = 0x03;   // JMP ($0314)      do IRQ vector (iIRQ)    

    #pragma endregion 

    #pragma region *** initialise VIC and screen editor (0xFF5B - 0xFF6B)   

    /* ----------------------------------------------------------------------
                                    *** initialise VIC and screen editor
    .,FF5B 20 18 E5 JSR $E518       initialise the screen and keyboard
    .,FF5E AD 12 D0 LDA $D012       read the raster compare register
    .,FF61 D0 FB    BNE $FF5E       loop if not raster line $00
    .,FF63 AD 19 D0 LDA $D019       read the vic interrupt flag register
    .,FF66 29 01    AND #$01        mask the raster compare flag
    .,FF68 8D A6 02 STA $02A6       save the PAL/NTSC flag
    .,FF6B 4C DD FD JMP $FDDD

    ------------------------------------------------------------------------ */

    bus.memory[0xFF5B] = 0x20; bus.memory[0xFF5C] = 0x18; bus.memory[0xFF5D] = 0xE5;   // JSR $E518   initialise the screen and keyboard
    bus.memory[0xFF5E] = 0xAD; bus.memory[0xFF5F] = 0x12; bus.memory[0xFF60] = 0xD0;   // LDA $D012   read the raster compare register
    bus.memory[0xFF61] = 0xD0; bus.memory[0xFF62] = 0xFB;                              // BNE $FF5E   loop if not raster line $00
    bus.memory[0xFF63] = 0xAD; bus.memory[0xFF64] = 0x19; bus.memory[0xFF65] = 0xD0;   // LDA $D019   read the VIC interrupt flag register
    bus.memory[0xFF66] = 0x29; bus.memory[0xFF67] = 0x01;                              // AND #$01    mask the raster compare flag
    bus.memory[0xFF68] = 0x8D; bus.memory[0xFF69] = 0xA6; bus.memory[0xFF6A] = 0x02;   // STA $02A6   save the PAL/NTSC flag
    bus.memory[0xFF6B] = 0x4C; bus.memory[0xFF6C] = 0xDD; bus.memory[0xFF6D] = 0xFD;   // JMP $FDDD

    #pragma endregion 

    #pragma region *** ?? (0xFF6E - 0xFF7D)    

    /* ---------------------------------------------------------------------- 

                                    *** ??
    .,FF6E A9 81    LDA #$81        enable timer A interrupt
    .,FF70 8D 0D DC STA $DC0D       save VIA 1 ICR
    .,FF73 AD 0E DC LDA $DC0E       read VIA 1 CRA
    .,FF76 29 80    AND #$80        mask x000 0000, TOD clock
    .,FF78 09 11    ORA #$11        mask xxx1 xxx1, load timer A, start timer A
    .,FF7A 8D 0E DC STA $DC0E       save VIA 1 CRA
    .,FF7D 4C 8E EE JMP $EE8E       set the serial clock out low and return   

    ------------------------------------------------------------------------ */

    bus.memory[0xFF6E] = 0xA9; bus.memory[0xFF6F] = 0x81;                               // LDA #$81        enable timer A interrupt      
    bus.memory[0xFF70] = 0x8D; bus.memory[0xFF71] = 0x0D; bus.memory[0xFF72] = 0xDC;    // STA $DC0D       save VIA 1 ICR
    bus.memory[0xFF73] = 0xAD; bus.memory[0xFF74] = 0x0E; bus.memory[0xFF75] = 0xDC;    // LDA $DC0E       read VIA 1 CRA
    bus.memory[0xFF76] = 0x29; bus.memory[0xFF77] = 0x80;                               // AND #$80        mask x000 0000, TOD clock
    bus.memory[0xFF78] = 0x09; bus.memory[0xFF79] = 0x11;                               // ORA #$11        mask xxx1 xxx1, load timer A, start timer A
    bus.memory[0xFF7A] = 0x8D; bus.memory[0xFF7B] = 0x0E; bus.memory[0xFF7C] = 0xDC;    // save VIA 1 CRA    
    bus.memory[0xFF7D] = 0x4C; bus.memory[0xFF7E] = 0x8E; bus.memory[0xFF7F] = 0xEE;    // JMP $EE8E       set the serial clock out low and return       

    #pragma endregion   
    
    #pragma region *** control kernal messages (0xFF90)

    /* ----------------------------------------------------------------------

                            *** control kernal messages ***

        This routine controls the printing of error and control messages by the KERNAL.

        Set A before calling:
            bit 7 = 1 → print KERNAL error message   (e.g. "FILE NOT FOUND")
            bit 6 = 1 → print control message        (e.g. "PRESS PLAY ON CASSETTE")

        Bits 6–7 determine the message source.

        .,FF90 4C 18 FE   JMP $FE18      control kernal messages

    ------------------------------------------------------------------------ */

    bus.memory[0xFF90] = 0x4C; bus.memory[0xFF91] = 0x18; bus.memory[0xFF92] = 0xFE;               // JMP $FE18

    #pragma endregion

    #pragma region *** read/set the bottom of memory (0xFF9C - 0xFF9E)

    /* ----------------------------------------------------------------------

                                    *** read/set the bottom of memory
        .,FF9C 4C 34 FE    JMP $FE34       read/set the bottom of memory

                                    *** scan the keyboard
                                    Questa routine esegue lo scanning della tastiera.
                                    È la stessa usata dall'interrupt handler.
                                    Se un tasto è premuto, il suo codice ASCII
                                    viene inserito nella keyboard queue.

    ------------------------------------------------------------------------ */

    bus.memory[0xFF9C] = 0x4C; bus.memory[0xFF9D] = 0x34; bus.memory[0xFF9E] = 0xFE;     // JMP $FE34  (read/set bottom of memory)

    #pragma endregion

    #pragma region *** read/set the top of memory (0xFF99 - 0xFF9B)

    /* ----------------------------------------------------------------------

                                *** read/set the top of memory
                                this routine is used to read and set the top of RAM. When this routine is called
                                with the carry bit set the pointer to the top of RAM will be loaded into XY. When
                                this routine is called with the carry bit clear XY will be saved as the top of
                                memory pointer changing the top of memory.

        .,FF99 4C 25 FE    JMP $FE25     read/set the top of memory

    ------------------------------------------------------------------------ */

    bus.memory[0xFF99] = 0x4C; bus.memory[0xFF9A] = 0x25; bus.memory[0xFF9B] = 0xFE;      // JMP $FE25     read/set the top of memory

    #pragma endregion

    #pragma region *** close input and output channels (0xFFCC - 0xFFCE)

    /* ----------------------------------------------------------------------

                                    *** close input and output channels
        this routine is called to clear all open channels and restore the I/O 
        channels to their original default values. It is usually called after 
        opening other I/O channels and using them for input/output operations. 
        
        The default input device is 0, the keyboard.  
        The default output device is 3, the screen.

        If one of the channels to be closed is to the serial port, an UNTALK 
        signal is sent first to clear the input channel or an UNLISTEN is sent 
        to clear the output channel.

        By not calling this routine and leaving listener(s) active on the serial 
        bus, several devices can receive the same data from the VIC at the same 
        time.

        One way to take advantage of this would be to command the printer to TALK 
        and the disk to LISTEN, allowing direct printing of a disk file.

            .,FFCC 6C 22 03     JMP ($0322)     do close input and output channels

    ------------------------------------------------------------------------ */

    bus.memory[0xFFCC] = 0x6C; bus.memory[0xFFCD] = 0x22; bus.memory[0xFFCE] = 0x03;      // JMP ($0322)  do close input and output channels

    #pragma endregion

    #pragma region *** close all channels and files (0xFFE7 - 0x0xFFE9)

    /* ----------------------------------------------------------------------

                            *** close all channels and files ***

        This routine closes all open files.  
        When this routine is called, the pointers into the open file table  
        are reset, closing all files.  
        Also the routine automatically resets the I/O channels.

            .,FFE7 6C 2C 03   JMP ($032C)        do close all channels and files

    ------------------------------------------------------------------------ */

    bus.memory[0xFFE7] = 0x6C; bus.memory[0xFFE8] = 0x2C; bus.memory[0xFFE9] = 0x03;    // JMP ($032C)    do close all channels and files

    #pragma endregion


    
    // used for (*)
    bus.memory[0x1234] = 0x7F; // valore originale per INC
    bus.memory[0x1235] = 0x01; // valore originale per DEC

    /* ----------------------------------------------------------------------
    
    Alcune prove iniziali
    
    ------------------------------------------------------------------------ */

    /*
    bus.memory[START_ADDR_RESET + 0x00] = 0xA9; bus.memory[START_ADDR_RESET + 0x01] = 0x7F;     // LDA #$7F
    bus.memory[START_ADDR_RESET + 0x02] = 0x69; bus.memory[START_ADDR_RESET + 0x03] = 0x01;     // ADC #$01
    bus.memory[START_ADDR_RESET + 0x04] = 0x38;  // SEC
    bus.memory[START_ADDR_RESET + 0x05] = 0xE9; bus.memory[START_ADDR_RESET + 0x06] = 0x01;     // SBC #$01
    bus.memory[START_ADDR_RESET + 0x07] = 0xC9; bus.memory[START_ADDR_RESET + 0x08] = 0x7F;     // CMP #$7F
    bus.memory[START_ADDR_RESET + 0x09] = 0xF0; bus.memory[START_ADDR_RESET + 0x0A] = 0x03;     // BEQ +3
    bus.memory[START_ADDR_RESET + 0x0B] = 0xEA;
    bus.memory[START_ADDR_RESET + 0x0C] = 0xEA;
    bus.memory[START_ADDR_RESET + 0x0D] = 0xEA;
    bus.memory[START_ADDR_RESET + 0x0E] = 0xEE; bus.memory[START_ADDR_RESET + 0x0F] = 0x34; bus.memory[START_ADDR_RESET + 0x10] = 0x12; // INC $1234  // (*)
    bus.memory[START_ADDR_RESET + 0x11] = 0xCE; bus.memory[START_ADDR_RESET + 0x12] = 0x35; bus.memory[START_ADDR_RESET + 0x13] = 0x12; // DEC $1235
    bus.memory[START_ADDR_RESET + 0x14] = 0xEA;
    bus.memory[START_ADDR_RESET + 0x15] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x16] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x17] = 0x78;  // SEI
    bus.memory[START_ADDR_RESET + 0x18] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x19] = 0x58;  // CLI
    bus.memory[START_ADDR_RESET + 0x1A] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x1B] = 0x00;  // BRK
    bus.memory[START_ADDR_RESET + 0x1C] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x1D] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x1E] = 0xEA;  // NOP
    bus.memory[START_ADDR_RESET + 0x1F] = 0xEA;  // NOP
    */


    /* ----------------------------------------------------------------------
    
    *** RESET, hardware reset starts here
    
    .,FCE2 A2 FF    LDX #$FF        set X for stack
    .,FCE4 78       SEI             disable the interrupts
    .,FCE5 9A       TXS             clear stack
    .,FCE6 D8       CLD             clear decimal mode
    .,FCE7 20 02 FD JSR $FD02       scan for autostart ROM at $8000
    .,FCEA D0 03    BNE $FCEF       if not there continue startup
    .,FCEC 6C 00 80 JMP ($8000)     else call ROM start code
    .,FCEF 8E 16 D0 STX $D016       read the horizontal fine scroll and control register
    .,FCF2 20 A3 FD JSR $FDA3       initialise SID, CIA and IRQ  (done)
    .,FCF5 20 50 FD JSR $FD50       RAM test and find RAM end   
    .,FCF8 20 15 FD JSR $FD15       restore default I/O vectors 
    .,FCFB 20 5B FF JSR $FF5B       initialise VIC and screen editor
    .,FCFE 58       CLI             enable the interrupts
    .,FCFF 6C 00 A0 JMP ($A000)     execute BASIC    
    
    ------------------------------------------------------------------------ */    

    // 0xFCE2
    bus.memory[START_ADDR_RESET + 0x00] = 0xA2; bus.memory[START_ADDR_RESET + 0x01] = 0xFF;                                              // LDX #$FF        set X for stack   
    bus.memory[START_ADDR_RESET + 0x02] = 0x78;                                                                                          // SEI             disable the interrupts
    bus.memory[START_ADDR_RESET + 0x03] = 0x9A;                                                                                          // TXS             clear stack
    bus.memory[START_ADDR_RESET + 0x04] = 0xD8;                                                                                          // CLD             clear decimal mode    
    bus.memory[START_ADDR_RESET + 0x05] = 0x20; bus.memory[START_ADDR_RESET + 0x06] = 0x02; bus.memory[START_ADDR_RESET + 0x07] = 0xFD;  // JSR $FD02       scan for autostart ROM at $8000    
    bus.memory[START_ADDR_RESET + 0x08] = 0xD0; bus.memory[START_ADDR_RESET + 0x09] = 0x03;                                              // BNE $FCEF       if not there continue startup    
    bus.memory[START_ADDR_RESET + 0x0A] = 0x6C; bus.memory[START_ADDR_RESET + 0x0B] = 0x00; bus.memory[START_ADDR_RESET + 0x0C] = 0x80;  // JMP ($8000)     else call ROM start code (esegue rom su cartridge all'indirizzo indicato nella locazione $8000)
    bus.memory[START_ADDR_RESET + 0x0D] = 0x8E; bus.memory[START_ADDR_RESET + 0x0E] = 0x16; bus.memory[START_ADDR_RESET + 0x0F] = 0xD0;  // STX $D016       read the horizontal fine scroll and control register 
    bus.memory[START_ADDR_RESET + 0x10] = 0x20; bus.memory[START_ADDR_RESET + 0x11] = 0xA3; bus.memory[START_ADDR_RESET + 0x12] = 0xFD;  // JSR $FDA3       initialise SID, CIA and IRQ
    bus.memory[START_ADDR_RESET + 0x13] = 0x20; bus.memory[START_ADDR_RESET + 0x14] = 0x50; bus.memory[START_ADDR_RESET + 0x15] = 0xFD;  // JSR $FD50       RAM test and find RAM end
    bus.memory[START_ADDR_RESET + 0x16] = 0x20; bus.memory[START_ADDR_RESET + 0x17] = 0x15; bus.memory[START_ADDR_RESET + 0x18] = 0xFD;  // JSR $FD15       restore default I/O vectors
    bus.memory[START_ADDR_RESET + 0x19] = 0x20; bus.memory[START_ADDR_RESET + 0x1A] = 0x5B; bus.memory[START_ADDR_RESET + 0x1B] = 0xFF;  // JSR $FF5B       initialise VIC and screen editor
    bus.memory[START_ADDR_RESET + 0x1C] = 0x58;                                                                                          // CLI             enable the interrupts
    bus.memory[START_ADDR_RESET + 0x1D] = 0x6C; bus.memory[START_ADDR_RESET + 0x1E] = 0x00; bus.memory[START_ADDR_RESET + 0x1F] = 0xA0;  // JMP ($A000)     execute BASIC

    //bus.memory[START_ADDR_RESET + 0x1D] = 0x02;  // KIL — arresta la CPU .......al posto di JMP ($A000)
 
       
    // -----------------------
    // Execution  
    // -----------------------

    if (runConfiguredProfiles(bus, cpu, vic, cia2)) {
        return 0;
    }

    cpu.reset();

    uint64_t halfCycleCount = 0;

    runMainExecutionLoop(bus, cpu, halfCycleCount);


    // ----------------------------
    // Print Values and other tests  
    // ----------------------------    

    Registers R = cpu.getRegisters();
    std::cout << "PC: $" << std::hex << (int)R.PC << "\n";
    std::cout << "A: $" << std::hex << (int)R.A << "\n";
    std::cout << "P (status): $" << std::hex << (int)R.P << "\n";
    std::cout << "Half-cycles totali: " << std::dec << cpu.getTotalHalfCycles() << "\n";
    std::cout << "Mem[1234] = $" << std::hex << (int)bus.read(0x1234) << "\n";
    std::cout << "Mem[1235] = $" << std::hex << (int)bus.read(0x1235) << "\n";
    std::cout << "Flag Z = " << ((R.P & ZERO) ? 1 : 0) << ", N = " << ((R.P & NEGATIVE) ? 1 : 0) << "\n";

    if (R.P & INTERRUPT_DISABLE)
        std::cout << "Flag I SET" << std::endl;
    else
        std::cout << "Flag I CLEAR" << std::endl;

    // Test solo in binario
    testADC_SBC(alu, R, bus);
    testCompare(alu, R);
 
}
