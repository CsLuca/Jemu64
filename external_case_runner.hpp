#pragma once

#include <unordered_map>

struct ExternalCaseLastEvent {
    uint64_t halfCycle = 0;
    uint16_t pc = 0;
    uint16_t addr = 0;
    bool isWrite = false;
    uint8_t data = 0;
};

struct ExternalCaseRunReport {
    std::string caseName;
    bool ok = false;
    uint64_t cycle = 0;
    uint16_t pc = 0;
    uint16_t addr = 0;
    bool isWrite = false;
    uint8_t data = 0;
};

static ExternalCaseRunReport &externalCaseLastRunReport() {
    static ExternalCaseRunReport report;
    return report;
}

static void resetBusForExternalCase(Bus &bus) {
    for (size_t i = 0; i < bus.memory.size(); ++i) {
        bus.memory[i] = 0;
    }

    bus.flatMemoryMode = true;

    // External CPU tests expect flat RAM view; disable BASIC/KERNAL/CHAR overlays.
    bus.cpuPortDir = static_cast<uint8_t>(bus.cpuPortDir | 0x07);
    bus.cpuPortData = static_cast<uint8_t>(bus.cpuPortData & ~0x07);
    bus.memory[0x0000] = bus.cpuPortDir;
    bus.memory[0x0001] = bus.cpuPortData;
}

static bool loadExternalCaseProgram(Bus &bus, CPU6510 &cpu, const ExternalRomCase &tc,
                                    uint16_t &effectiveLoadAddress,
                                    uint16_t &effectiveResetVector,
                                    bool &treatAsPrg,
                                    std::streambuf *savedCoutBuf) {
    std::ifstream rom(tc.romPath, std::ios::binary);
    if (!rom.is_open()) {
        if (savedCoutBuf != nullptr) {
            std::cout.rdbuf(savedCoutBuf);
        }
        if (tc.optional) {
            std::cerr << "[EXT] SKIP optional (missing ROM): " << tc.name
                      << " path=" << tc.romPath << std::endl;
            return true;
        }
        std::cerr << "[EXT] Cannot open ROM: " << tc.romPath << std::endl;
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(rom)), std::istreambuf_iterator<char>());

    const bool isPrgByExt = endsWithInsensitive(tc.romPath, ".prg");
    const std::string fmt = toLowerAscii(tc.format);
    treatAsPrg = (fmt == "prg") || (fmt == "auto" && isPrgByExt);

    size_t payloadOffset = 0;
    effectiveLoadAddress = tc.loadAddress;

    if (treatAsPrg) {
        if (data.size() < 2) {
            if (savedCoutBuf != nullptr) {
                std::cout.rdbuf(savedCoutBuf);
            }
            std::cerr << "[EXT] Invalid PRG (too small): " << tc.romPath << std::endl;
            return false;
        }
        const uint16_t prgHeaderLoad = static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
        if (effectiveLoadAddress == 0) {
            effectiveLoadAddress = prgHeaderLoad;
        }
        payloadOffset = 2;
    }

    for (size_t i = payloadOffset; i < data.size(); ++i) {
        const uint32_t addr = static_cast<uint32_t>(effectiveLoadAddress) + static_cast<uint32_t>(i - payloadOffset);
        if (addr >= 0x10000) break;
        bus.memory[addr] = data[i];
    }

    effectiveResetVector = tc.resetVector;
    if (tc.startPC != 0) {
        effectiveResetVector = tc.startPC;
    } else if (effectiveResetVector == 0) {
        effectiveResetVector = effectiveLoadAddress;
    }

    bus.memory[0xFFFC] = Lo(effectiveResetVector);
    bus.memory[0xFFFD] = Hi(effectiveResetVector);

    cpu.reset();
    uint32_t guard = 0;
    while (!cpu.isIdle() && guard < 512) {
        cpu.clock();
        guard++;
    }
    return true;
}

static std::streambuf *silenceStdoutToNull(std::ofstream &nullOut) {
    std::streambuf *savedCoutBuf = nullptr;
    if (nullOut.is_open()) {
        savedCoutBuf = std::cout.rdbuf(nullOut.rdbuf());
    }
    return savedCoutBuf;
}

static void restoreStdout(std::streambuf *savedCoutBuf) {
    if (savedCoutBuf != nullptr) {
        std::cout.rdbuf(savedCoutBuf);
    }
}

static std::ofstream openExternalCaseTrace(const ExternalRomCase &tc, std::string &traceName) {
    traceName = sanitizeFileName(tc.name) + ".trace.csv";
    std::ofstream trace(traceName, std::ios::binary);
    if (tc.traceEnabled && trace.is_open()) {
        trace << "half,pc,a,x,y,p,sp,addr,rw,data\n";
    }
    return trace;
}

static void runExternalCaseTraceLoop(CPU6510 &cpu,
                                     Bus &bus,
                                     const ExternalRomCase &tc,
                                     std::ofstream &trace,
                                     bool &hitPassPC,
                                     bool &hitFailPC,
                                     bool &halted,
                                     uint32_t &uniquePCCount,
                                     uint32_t &requirePCHitCount,
                                     Registers &finalRegs,
                                     ExternalCaseLastEvent &lastEvent) {
    std::array<uint8_t, 65536> visitedPC{};
    uniquePCCount = 0;
    requirePCHitCount = 0;
    hitPassPC = false;
    hitFailPC = false;
    halted = false;

    for (uint32_t i = 0; i < tc.maxHalfCycles; ++i) {
        cpu.clock();
        Registers r = cpu.getRegisters();
        lastEvent.halfCycle = cpu.getTotalHalfCycles();
        lastEvent.pc = r.PC;
        lastEvent.addr = bus.addressBus;
        lastEvent.isWrite = bus.lastIsWrite;
        lastEvent.data = bus.lastDataBusValue;
        if (visitedPC[r.PC] == 0) {
            visitedPC[r.PC] = 1;
            uniquePCCount++;
        }
        if (tc.requirePC != 0 && r.PC == tc.requirePC) {
            requirePCHitCount++;
        }
        if (tc.traceEnabled && trace.is_open()) {
            trace << cpu.getTotalHalfCycles() << ","
                  << std::hex << r.PC << ","
                  << (int)r.A << ","
                  << (int)r.X << ","
                  << (int)r.Y << ","
                  << (int)r.P << ","
                  << (int)r.SP << ","
                  << bus.addressBus << ","
                  << (bus.lastIsWrite ? "W" : "R") << ","
                  << (int)bus.lastDataBusValue << "\n";
        }
        if (tc.passPC != 0 && r.PC == tc.passPC) {
            hitPassPC = true;
            finalRegs = r;
            return;
        }
        if (tc.failPC != 0 && r.PC == tc.failPC) {
            hitFailPC = true;
            finalRegs = r;
            return;
        }
        if (cpu.halted) {
            halted = true;
            finalRegs = r;
            return;
        }
    }

    finalRegs = cpu.getRegisters();
}

static bool evaluateExternalCaseResult(const ExternalRomCase &tc,
                                       const Bus &bus,
                                       bool hitPassPC,
                                       bool hitFailPC,
                                       bool halted,
                                       uint32_t uniquePCCount,
                                       uint32_t requirePCHitCount) {
    bool ok = false;
    if (tc.passPC != 0) {
        ok = hitPassPC && !hitFailPC;
    } else {
        ok = !halted && !hitFailPC;
    }
    if (tc.requirePC != 0) {
        const uint32_t requiredHits = (tc.requirePCHits == 0) ? 1u : tc.requirePCHits;
        ok = ok && (requirePCHitCount >= requiredHits);
    }
    if (tc.minUniquePC != 0) {
        ok = ok && (uniquePCCount >= tc.minUniquePC);
    }
    if (tc.hasExpectedMem) {
        ok = ok && (bus.memory[tc.expectMemAddr] == tc.expectMemValue);
    }
    return ok;
}

static std::ostringstream buildExpectedMemInfo(const ExternalRomCase &tc, const Bus &bus) {
    std::ostringstream expectMemInfo;
    if (tc.hasExpectedMem) {
        expectMemInfo << " expect_mem[$" << std::hex << tc.expectMemAddr
                      << "]=$" << (int)bus.memory[tc.expectMemAddr]
                      << " (want $" << (int)tc.expectMemValue << ")";
    }
    return expectMemInfo;
}

static std::vector<std::string> readTraceRows(const std::string &path) {
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

static bool runReferenceTraceDiff(const ExternalRomCase &tc,
                                  const std::string &traceName,
                                  uint32_t &mismatchCount,
                                  std::string &reason) {
    mismatchCount = 0;
    reason.clear();
    if (tc.referenceTracePath.empty()) {
        return true;
    }

    std::string runtimeTracePath = traceName;
    {
        std::ifstream probe(runtimeTracePath, std::ios::binary);
        if (!probe.is_open()) {
            // Fallback to current working directory relative path.
            runtimeTracePath = std::string("./") + traceName;
        }
    }
    const std::vector<std::string> got = readTraceRows(runtimeTracePath);
    const std::vector<std::string> ref = readTraceRows(tc.referenceTracePath);

    if (ref.empty()) {
        reason = std::string("reference trace missing/empty path=") + tc.referenceTracePath;
        return false;
    }
    if (got.empty()) {
        reason = std::string("runtime trace missing/empty path=") + traceName;
        return false;
    }

    auto extractPcField = [](const std::string &row) -> std::string {
        // CSV format: half,pc,a,x,y,p,sp,addr,rw,data
        // return the 2nd column (pc), or full row if malformed.
        size_t c1 = row.find(',');
        if (c1 == std::string::npos) return row;
        size_t c2 = row.find(',', c1 + 1);
        if (c2 == std::string::npos) return row.substr(c1 + 1);
        return row.substr(c1 + 1, c2 - (c1 + 1));
    };

    const bool pcOnly = (tc.referenceMode == "pc_only");

    if (pcOnly) {
        std::unordered_map<std::string, uint64_t> gotFreq;
        std::unordered_map<std::string, uint64_t> refFreq;
        for (size_t i = 0; i < got.size(); ++i) {
            gotFreq[extractPcField(got[i])]++;
        }
        for (size_t i = 0; i < ref.size(); ++i) {
            refFreq[extractPcField(ref[i])]++;
        }

        for (auto it = gotFreq.begin(); it != gotFreq.end(); ++it) {
            const auto jt = refFreq.find(it->first);
            const uint64_t rv = (jt == refFreq.end()) ? 0ULL : jt->second;
            if (it->second > rv) {
                mismatchCount += static_cast<uint32_t>(it->second - rv);
            } else {
                mismatchCount += static_cast<uint32_t>(rv - it->second);
            }
            if (jt != refFreq.end()) {
                refFreq.erase(jt);
            }
        }
        for (auto it = refFreq.begin(); it != refFreq.end(); ++it) {
            mismatchCount += static_cast<uint32_t>(it->second);
        }

        const bool okPc = (mismatchCount <= tc.maxTraceMismatches);
        if (!okPc) {
            std::ostringstream oss;
            oss << "pc_only freq diff mismatches=" << mismatchCount
                << " limit=" << tc.maxTraceMismatches
                << " ref_rows=" << ref.size()
                << " got_rows=" << got.size();
            reason = oss.str();
        }
        return okPc;
    }

    const size_t n = std::min(got.size(), ref.size());
    for (size_t i = 0; i < n; ++i) {
        if (got[i] != ref[i]) {
            mismatchCount++;
        }
    }
    if (got.size() != ref.size()) {
        mismatchCount += static_cast<uint32_t>((got.size() > ref.size()) ? (got.size() - ref.size()) : (ref.size() - got.size()));
    }

    const bool ok = (mismatchCount <= tc.maxTraceMismatches);
    if (!ok) {
        std::ostringstream oss;
        oss << "trace diff mismatches=" << mismatchCount
            << " limit=" << tc.maxTraceMismatches
            << " ref_rows=" << ref.size()
            << " got_rows=" << got.size();
        reason = oss.str();
    }
    return ok;
}

static void logExternalCaseResult(const ExternalRomCase &tc,
                                  bool treatAsPrg,
                                  uint16_t effectiveLoadAddress,
                                  uint16_t effectiveResetVector,
                                  const Registers &r,
                                  uint32_t requirePCHitCount,
                                  uint32_t uniquePCCount,
                                  uint32_t traceMismatchCount,
                                  bool traceDiffEnabled,
                                  const std::ostringstream &expectMemInfo,
                                  const std::string &traceName,
                                  bool ok,
                                  const ExternalCaseLastEvent &lastEvent) {
    std::cerr << "[EXT] " << tc.name
              << " fmt=" << (treatAsPrg ? "prg" : "bin")
              << " load=$" << std::hex << effectiveLoadAddress
              << " reset=$" << effectiveResetVector
              << " pc=$" << std::hex << r.PC
              << " pass_pc=$" << tc.passPC
              << " fail_pc=$" << tc.failPC
              << " require_pc=$" << tc.requirePC
              << " require_pc_hits=" << std::dec << requirePCHitCount
              << "/" << ((tc.requirePC != 0 && tc.requirePCHits == 0) ? 1u : tc.requirePCHits)
              << " unique_pc=" << uniquePCCount
              << " ref_diff=" << (traceDiffEnabled ? "on" : "off")
              << " ref_mismatch=" << traceMismatchCount
              << expectMemInfo.str()
              << " trace=" << (tc.traceEnabled ? traceName : std::string("off"))
              << " result=" << (ok ? "PASS" : "FAIL")
              << std::endl;

    if (!ok) {
        std::cerr << "[EXT][FAIL-DUMP] case=" << tc.name
                  << " cycle=" << std::dec << lastEvent.halfCycle
                  << " pc=$" << std::hex << lastEvent.pc
                  << " addr=$" << lastEvent.addr
                  << " rw=" << (lastEvent.isWrite ? "W" : "R")
                  << " data=$" << static_cast<int>(lastEvent.data)
                  << std::endl;
    }

    ExternalCaseRunReport &report = externalCaseLastRunReport();
    report.caseName = tc.name;
    report.ok = ok;
    report.cycle = lastEvent.halfCycle;
    report.pc = lastEvent.pc;
    report.addr = lastEvent.addr;
    report.isWrite = lastEvent.isWrite;
    report.data = lastEvent.data;
}
