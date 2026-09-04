#pragma once

struct ExternalRomCase {
    std::string name;
    std::string romPath;
    std::string format = "auto"; // auto, bin, prg
    uint16_t loadAddress = 0;
    uint16_t resetVector = 0;
    uint16_t startPC = 0;
    uint32_t maxHalfCycles = 0;
    uint16_t passPC = 0;
    uint16_t failPC = 0;
    uint16_t requirePC = 0;
    uint32_t requirePCHits = 0;
    uint32_t minUniquePC = 0;
    uint16_t expectMemAddr = 0;
    uint8_t expectMemValue = 0;
    bool hasExpectedMem = false;
    bool traceEnabled = true;
    bool optional = false;
    std::string referenceTracePath;
    std::string referenceMode = "full"; // full, pc_only
    uint32_t maxTraceMismatches = 0;
    bool referenceOptional = false;
    std::string envCpuRevision;
    std::string envVicRevision;
    std::string envCiaRevision;
    std::string envOpenBusRevision;
    std::string envDriveRevision;
};
