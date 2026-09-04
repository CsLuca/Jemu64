#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "ibus.hpp"

// ==========================
// VIC-II con open-bus e letture inerti
// ==========================

class VICII {
public:

    enum Revision : uint8_t {
        REV_6569 = 0,
        REV_8565 = 1,
        REV_6569R3 = 2,
        REV_8565R2 = 3
    };

    struct RevisionProfile {
        Revision revision = REV_6569;
        uint8_t visibleLineStart = 48;
        uint8_t visibleLineEnd = 247;
        uint8_t badlineStartCycle = 12;
        uint8_t badlineReleaseCycle = 55;
        uint8_t borderOpenCycle = 15;
        uint8_t borderCloseCycle = 55;
        bool rasterIrqNeedsMaskEdge = false;
        bool spriteDmaRequiresDisplayEnable = false;
        bool vspFlickerSensitive = false;
        bool fldRequiresBadlineCarry = false;
        uint8_t spriteDmaStopLineMask = 0xFF;
    };

    static constexpr RevisionProfile makeRevisionProfile(Revision rev) {
        return (rev == REV_8565)
            ? RevisionProfile{REV_8565, 48, 247, 12, 55, 15, 55, true, true, false, true, 0xFE}
            : (rev == REV_6569R3)
                ? RevisionProfile{REV_6569R3, 48, 247, 12, 55, 15, 55, false, false, true, false, 0xFE}
                : (rev == REV_8565R2)
                    ? RevisionProfile{REV_8565R2, 48, 247, 12, 55, 15, 55, true, true, false, true, 0xFE}
            : RevisionProfile{REV_6569, 48, 247, 12, 55, 15, 55, false, false, true, false, 0xFF};
    }

    Revision revision = REV_6569;
    RevisionProfile revisionProfile = makeRevisionProfile(REV_6569);

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

    enum VicHalfPhase : uint8_t {
        VIC_PHI1 = 0,
        VIC_PHI2 = 1
    };

    enum VicBusOp : uint8_t {
        VIC_OP_REFRESH = 0,
        VIC_OP_VIDEO = 1,
        VIC_OP_SPRITE = 2
    };

    enum VicFetchAction : uint8_t {
        VIC_FETCH_NONE = 0,
        VIC_FETCH_VIDEO_MEMPTR_SCREEN,
        VIC_FETCH_VIDEO_COLOR,
        VIC_FETCH_VIDEO_PATTERN,
        VIC_FETCH_REFRESH,
        VIC_FETCH_SPRITE_PTR,
        VIC_FETCH_SPRITE_DATA0,
        VIC_FETCH_SPRITE_DATA1,
        VIC_FETCH_SPRITE_DATA2
    };

    enum VicTickAction : uint8_t {
        VIC_TICK_ACTION_NONE = 0,
        VIC_TICK_ACTION_BADLINE_LATCH = 1 << 0,
        VIC_TICK_ACTION_SPRITE_DMA_UPDATE = 1 << 1,
        VIC_TICK_ACTION_BADLINE_RELEASE = 1 << 2
    };

    IBus* bus = nullptr;   // puntatore al bus condiviso

    // Registri VIC-II
    uint8_t sprX[8] = {0};
    uint8_t sprY[8] = {0};
    uint8_t ctrl1 = 0x00;    // $D011
    uint8_t raster = 0x00;   // $D012
    uint8_t lightpenX = 0x00;// $D013
    uint8_t lightpenY = 0x00;// $D014
    uint8_t sprEnable = 0x00;// $D015
    uint8_t ctrl2 = 0x00;    // $D016
    uint8_t sprExpandY = 0x00;
    uint8_t memPtr = 0x00;
    uint8_t irqFlags = 0x00; // $D019
    uint8_t irqMask = 0x00;  // $D01A

    uint8_t internalBusLatch = 0xFF;

    // Stato interno raster
    int cycleInLine = 0;     // 0..62 (PAL)
    int rasterLine = 0;      // 0..311 (PAL)

    // Stato IRQ VIC-II
    bool rasterIRQPending = false;  // serve a evitare retrigger nella stessa linea
    bool rasterIrqMaskEdgeArmed = false;
    bool vspTriggered = false;
    bool fldTriggered = false;

    bool busLocked = false;

    uint8_t charFetchBuffer[40] = {0}; // pattern dei caratteri per la riga corrente
    uint8_t colorFetchBuffer[40] = {0}; // colori associati
    uint8_t pixelRowBuffer[320]; // 40 caratteri * 8 pixel per carattere

    uint8_t frameBuffer[312][320];

    struct VicCharSlot {
        uint8_t charCode;
        uint8_t colorCode;
        uint8_t charPattern;
    };

    VicCharSlot prefetch[3];
    int prefetchIndex = 0;  // quale slot aggiornare
    int prefetchHead = 0;   // points to oldest valid slot
    int prefetchCount = 0;  // how many valid in pipeline (0..3)

    // Registro temporaneo di costruzione (C/H/P)
    VicCharSlot prefetchTmp = {0, 0, 0};

    // shift-register state (carattere in corso di output)
    uint8_t shiftPattern = 0;
    uint8_t shiftColor = 0;
    int      shiftBitsRemaining = 0; // 0..8

    // finezza temporale: subcycle di pixel dentro il ciclo VIC
    int pixelClock = 0;     // 0..7 (pixel clock all'interno del ciclo VIC)

    bool cpuSuspended = false;
    bool baLine = true;
    bool aecLine = true;
    bool baNext = true;
    bool aecNext = true;
    VicHalfPhase halfPhase = VIC_PHI1;
    uint64_t halfTickCounter = 0;

    bool badlineActive = false;
    uint8_t spriteDmaMask = 0;
    uint8_t spritePointerLatch[8] = {0};
    uint8_t spriteDataLatch[8] = {0};
    uint8_t spriteDataBytes[8][3] = {{0}};
    uint16_t vc = 0;
    uint16_t vcBase = 0;
    uint8_t rc = 0;

    uint16_t pendingReadAddr[4] = {0, 0, 0, 0};
    uint8_t pendingReadCount = 0;
    char pendingVicOp = 'R';
    VicFetchAction pendingFetchAction = VIC_FETCH_NONE;
    uint8_t pendingSprite = 0xFF;
    int pendingFetchIndex = -1;
    int pendingRow = -1;
    uint8_t refreshCounter6 = 0;

    uint64_t frameHashCurrent = 1469598103934665603ULL;
    uint64_t frameHashLast = 0;
    bool frameHashValid = false;

    struct RasterEvent {
        uint16_t line = 0;
        uint8_t cycle = 0;
        uint8_t pixel = 0;
        uint8_t type = 0;
        uint8_t data = 0;
    };

    static constexpr uint8_t EVENT_BADLINE_ON = 1;
    static constexpr uint8_t EVENT_BADLINE_OFF = 2;
    static constexpr uint8_t EVENT_SPR_DMA_ON = 3;
    static constexpr uint8_t EVENT_SPR_DMA_OFF = 4;
    static constexpr uint8_t EVENT_RASTER_IRQ = 5;

    std::array<RasterEvent, 4096> rasterEvents = {};
    size_t rasterEventCount = 0;

    VicCharSlot stagedPrefetch[40];
    bool stagedValid[40];

    struct VicStepRule {
        VicBusOp op;
        bool busWindow;
        VicFetchAction fetch;
        uint8_t tickActions;
        uint8_t predicateMask;
        uint8_t fetchPlan;
        uint8_t commitPlan;
        uint8_t addrFormula0;
        uint8_t addrFormula1;
    };

    enum VicPredicateMask : uint8_t {
        VIC_PRED_NONE = 0,
        VIC_PRED_REQUIRE_SPRITE_DMA = 1 << 0,
        VIC_PRED_REQUIRE_BADLINE = 1 << 1,
        VIC_PRED_REQUIRE_BUSLOCKED = 1 << 2
    };

    enum VicFetchPlan : uint8_t {
        VIC_PLAN_NONE = 0,
        VIC_PLAN_VIDEO_MEMPTR_SCREEN,
        VIC_PLAN_VIDEO_COLOR,
        VIC_PLAN_VIDEO_PATTERN,
        VIC_PLAN_REFRESH,
        VIC_PLAN_SPRITE_PTR,
        VIC_PLAN_SPRITE_DATA0,
        VIC_PLAN_SPRITE_DATA1,
        VIC_PLAN_SPRITE_DATA2
    };

    enum VicCommitPlan : uint8_t {
        VIC_COMMIT_NONE = 0,
        VIC_COMMIT_VIDEO_MEMPTR_SCREEN,
        VIC_COMMIT_VIDEO_COLOR,
        VIC_COMMIT_VIDEO_PATTERN,
        VIC_COMMIT_REFRESH,
        VIC_COMMIT_SPRITE_PTR,
        VIC_COMMIT_SPRITE_DATA0,
        VIC_COMMIT_SPRITE_DATA1,
        VIC_COMMIT_SPRITE_DATA2
    };

    enum VicAddrFormula : uint8_t {
        VIC_ADDR_NONE = 0,
        VIC_ADDR_VIDEO_MEMPTR,
        VIC_ADDR_VIDEO_SCREEN,
        VIC_ADDR_VIDEO_COLOR,
        VIC_ADDR_VIDEO_PATTERN,
        VIC_ADDR_REFRESH,
        VIC_ADDR_SPRITE_PTR,
        VIC_ADDR_SPRITE_DATA0,
        VIC_ADDR_SPRITE_DATA1,
        VIC_ADDR_SPRITE_DATA2
    };

    using FetchPlanHandler = void (VICII::*)();
    using CommitPlanHandler = void (VICII::*)();

    static constexpr VicFetchAction kFetchByOpAndPixel[3][8] = {
        {VIC_FETCH_REFRESH, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE},
        {VIC_FETCH_VIDEO_MEMPTR_SCREEN, VIC_FETCH_VIDEO_COLOR, VIC_FETCH_VIDEO_PATTERN, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE},
        {VIC_FETCH_SPRITE_PTR, VIC_FETCH_SPRITE_DATA0, VIC_FETCH_SPRITE_DATA1, VIC_FETCH_SPRITE_DATA2, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE, VIC_FETCH_NONE}
    };

    static constexpr uint8_t kBusWindowByOpAndPixel[3][8] = {
        {0,0,0,0,0,0,0,0},
        {1,1,1,0,0,0,0,0},
        {1,1,1,1,0,0,0,0}
    };

    static constexpr uint8_t kPredicateMaskByOp[3] = {
        VIC_PRED_NONE,
        VIC_PRED_NONE,
        VIC_PRED_REQUIRE_SPRITE_DMA
    };

    // ====================================
    // Costruttore: inizializza stati
    // ====================================
    VICII() {
        for (int i = 0; i < 40; ++i) {
            stagedValid[i] = false;
            stagedPrefetch[i] = {0, 0, 0};
        }
        memset(frameBuffer, 0, sizeof(frameBuffer));
        memset(pixelRowBuffer, 0, sizeof(pixelRowBuffer));
    }

    // =========================================================
    // Verifica se il VIC-II sta occupando il bus per fetch
    // =========================================================
    bool isFetching() const {
        if (cycleInLine < 0 || cycleInLine >= 63) return false;
        if (pixelClock < 0 || pixelClock >= 8) return false;

        const VicBusOp op = plaOpForCycle(cycleInLine);
        if (op == VIC_OP_SPRITE && spriteDmaMask == 0) {
            return false;
        }
        return kBusWindowByOpAndPixel[static_cast<int>(op)][pixelClock] != 0;
    }

    void updateBaAecForCurrentSubstep(const VicStepRule &rule) {
        const bool locked = busLocked && (cycleInLine >= 12 && cycleInLine <= 54);
        const bool vicOwnsBus = rule.busWindow || locked;
        baNext = !vicOwnsBus;
        aecNext = !vicOwnsBus;
    }

    static constexpr size_t plaIndex(VicHalfPhase phase, int cycle, int pixel) {
        return static_cast<size_t>((phase == VIC_PHI1 ? 0 : 1) * 63 * 8 + cycle * 8 + pixel);
    }

    static constexpr VicBusOp plaOpForCycle(int cycle) {
        return (cycle >= 8 && cycle <= 14) || (cycle >= 57 && cycle <= 62)
            ? VIC_OP_SPRITE
            : ((cycle >= 15 && cycle <= 54) ? VIC_OP_VIDEO : VIC_OP_REFRESH);
    }

    static constexpr VicFetchAction plaFetchFor(VicBusOp op, VicHalfPhase phase, int pixel) {
        if (phase != VIC_PHI1) {
            return VIC_FETCH_NONE;
        }
        if (pixel < 0 || pixel >= 8) {
            return VIC_FETCH_NONE;
        }
        return kFetchByOpAndPixel[static_cast<int>(op)][pixel];
    }

    static constexpr bool plaBusWindowFor(VicBusOp op, VicHalfPhase phase, int pixel) {
        if (phase != VIC_PHI2 || pixel < 0 || pixel >= 8) {
            return false;
        }
        return kBusWindowByOpAndPixel[static_cast<int>(op)][pixel] != 0;
    }

    static constexpr uint8_t plaTickActionsBase(VicHalfPhase phase, int cycle, int pixel) {
        return (pixel == 0 && phase == VIC_PHI1 && cycle == 12) ? VIC_TICK_ACTION_BADLINE_LATCH :
               (pixel == 0 && phase == VIC_PHI1 && cycle == 55) ? VIC_TICK_ACTION_SPRITE_DMA_UPDATE :
               (pixel == 0 && phase == VIC_PHI2 && cycle == 55) ? VIC_TICK_ACTION_BADLINE_RELEASE :
               VIC_TICK_ACTION_NONE;
    }

    uint8_t plaTickActionsWithRevision(VicHalfPhase phase, int cycle, int pixel) const {
        const int latchCycle = static_cast<int>(revisionProfile.badlineStartCycle);
        const int releaseCycle = static_cast<int>(revisionProfile.badlineReleaseCycle);
        return (pixel == 0 && phase == VIC_PHI1 && cycle == latchCycle) ? VIC_TICK_ACTION_BADLINE_LATCH :
               (pixel == 0 && phase == VIC_PHI1 && cycle == 55) ? VIC_TICK_ACTION_SPRITE_DMA_UPDATE :
               (pixel == 0 && phase == VIC_PHI2 && cycle == releaseCycle) ? VIC_TICK_ACTION_BADLINE_RELEASE :
               VIC_TICK_ACTION_NONE;
    }

    static constexpr uint8_t plaTickPredicateMask(uint8_t tickActions) {
        uint8_t m = VIC_PRED_NONE;
        if ((tickActions & VIC_TICK_ACTION_BADLINE_LATCH) != 0) {
            m = static_cast<uint8_t>(m | VIC_PRED_REQUIRE_BADLINE);
        }
        if ((tickActions & VIC_TICK_ACTION_BADLINE_RELEASE) != 0) {
            m = static_cast<uint8_t>(m | VIC_PRED_REQUIRE_BUSLOCKED);
        }
        return m;
    }

    static constexpr VicFetchPlan plaFetchPlanFromAction(VicFetchAction f) {
        return (f == VIC_FETCH_VIDEO_MEMPTR_SCREEN) ? VIC_PLAN_VIDEO_MEMPTR_SCREEN :
               (f == VIC_FETCH_VIDEO_COLOR) ? VIC_PLAN_VIDEO_COLOR :
               (f == VIC_FETCH_VIDEO_PATTERN) ? VIC_PLAN_VIDEO_PATTERN :
               (f == VIC_FETCH_REFRESH) ? VIC_PLAN_REFRESH :
               (f == VIC_FETCH_SPRITE_PTR) ? VIC_PLAN_SPRITE_PTR :
               (f == VIC_FETCH_SPRITE_DATA0) ? VIC_PLAN_SPRITE_DATA0 :
               (f == VIC_FETCH_SPRITE_DATA1) ? VIC_PLAN_SPRITE_DATA1 :
               (f == VIC_FETCH_SPRITE_DATA2) ? VIC_PLAN_SPRITE_DATA2 :
               VIC_PLAN_NONE;
    }

    static constexpr VicCommitPlan plaCommitPlanFromAction(VicFetchAction f) {
        return (f == VIC_FETCH_VIDEO_MEMPTR_SCREEN) ? VIC_COMMIT_VIDEO_MEMPTR_SCREEN :
               (f == VIC_FETCH_VIDEO_COLOR) ? VIC_COMMIT_VIDEO_COLOR :
               (f == VIC_FETCH_VIDEO_PATTERN) ? VIC_COMMIT_VIDEO_PATTERN :
               (f == VIC_FETCH_REFRESH) ? VIC_COMMIT_REFRESH :
               (f == VIC_FETCH_SPRITE_PTR) ? VIC_COMMIT_SPRITE_PTR :
               (f == VIC_FETCH_SPRITE_DATA0) ? VIC_COMMIT_SPRITE_DATA0 :
               (f == VIC_FETCH_SPRITE_DATA1) ? VIC_COMMIT_SPRITE_DATA1 :
               (f == VIC_FETCH_SPRITE_DATA2) ? VIC_COMMIT_SPRITE_DATA2 :
               VIC_COMMIT_NONE;
    }

    static constexpr VicAddrFormula plaAddrFormula0FromPlan(VicFetchPlan p) {
        return (p == VIC_PLAN_VIDEO_MEMPTR_SCREEN) ? VIC_ADDR_VIDEO_MEMPTR :
               (p == VIC_PLAN_VIDEO_COLOR) ? VIC_ADDR_VIDEO_COLOR :
               (p == VIC_PLAN_VIDEO_PATTERN) ? VIC_ADDR_VIDEO_PATTERN :
               (p == VIC_PLAN_REFRESH) ? VIC_ADDR_REFRESH :
               (p == VIC_PLAN_SPRITE_PTR) ? VIC_ADDR_SPRITE_PTR :
               (p == VIC_PLAN_SPRITE_DATA0) ? VIC_ADDR_SPRITE_DATA0 :
               (p == VIC_PLAN_SPRITE_DATA1) ? VIC_ADDR_SPRITE_DATA1 :
               (p == VIC_PLAN_SPRITE_DATA2) ? VIC_ADDR_SPRITE_DATA2 :
               VIC_ADDR_NONE;
    }

    static constexpr VicAddrFormula plaAddrFormula1FromPlan(VicFetchPlan p) {
        return (p == VIC_PLAN_VIDEO_MEMPTR_SCREEN) ? VIC_ADDR_VIDEO_SCREEN : VIC_ADDR_NONE;
    }

    static constexpr std::array<VicStepRule, 2 * 63 * 8> buildPlaTable() {
        std::array<VicStepRule, 2 * 63 * 8> table = {};
        for (int p = 0; p < 2; ++p) {
            const VicHalfPhase phase = (p == 0) ? VIC_PHI1 : VIC_PHI2;
            for (int c = 0; c < 63; ++c) {
                const VicBusOp op = plaOpForCycle(c);
                for (int px = 0; px < 8; ++px) {
                    table[plaIndex(phase, c, px)] = VicStepRule{
                        op,
                        plaBusWindowFor(op, phase, px),
                        plaFetchFor(op, phase, px),
                        plaTickActionsBase(phase, c, px),
                        static_cast<uint8_t>(kPredicateMaskByOp[static_cast<int>(op)] | plaTickPredicateMask(plaTickActionsBase(phase, c, px))),
                        static_cast<uint8_t>(plaFetchPlanFromAction(plaFetchFor(op, phase, px))),
                        static_cast<uint8_t>(plaCommitPlanFromAction(plaFetchFor(op, phase, px))),
                        static_cast<uint8_t>(plaAddrFormula0FromPlan(plaFetchPlanFromAction(plaFetchFor(op, phase, px)))),
                        static_cast<uint8_t>(plaAddrFormula1FromPlan(plaFetchPlanFromAction(plaFetchFor(op, phase, px))))
                    };
                }
            }
        }
        return table;
    }

    static const std::array<VicStepRule, 2 * 63 * 8> &vicPlaTable() {
        static constexpr std::array<VicStepRule, 2 * 63 * 8> kPla = buildPlaTable();
        return kPla;
    }

    static bool evaluatePredicateMask(uint8_t mask, bool spriteDmaActive, bool badline, bool busLockedNow) {
        if ((mask & VIC_PRED_REQUIRE_SPRITE_DMA) != 0 && !spriteDmaActive) return false;
        if ((mask & VIC_PRED_REQUIRE_BADLINE) != 0 && !badline) return false;
        if ((mask & VIC_PRED_REQUIRE_BUSLOCKED) != 0 && !busLockedNow) return false;
        return true;
    }

    static const char *busOpName(VicBusOp op) {
        switch (op) {
            case VIC_OP_REFRESH: return "REFRESH";
            case VIC_OP_VIDEO: return "VIDEO";
            case VIC_OP_SPRITE: return "SPRITE";
            default: return "UNKNOWN";
        }
    }

    static const char *fetchName(VicFetchAction f) {
        switch (f) {
            case VIC_FETCH_NONE: return "NONE";
            case VIC_FETCH_VIDEO_MEMPTR_SCREEN: return "VIDEO_MEMPTR_SCREEN";
            case VIC_FETCH_VIDEO_COLOR: return "VIDEO_COLOR";
            case VIC_FETCH_VIDEO_PATTERN: return "VIDEO_PATTERN";
            case VIC_FETCH_REFRESH: return "REFRESH";
            case VIC_FETCH_SPRITE_PTR: return "SPRITE_PTR";
            case VIC_FETCH_SPRITE_DATA0: return "SPRITE_DATA0";
            case VIC_FETCH_SPRITE_DATA1: return "SPRITE_DATA1";
            case VIC_FETCH_SPRITE_DATA2: return "SPRITE_DATA2";
            default: return "UNKNOWN";
        }
    }

    static const char *planName(VicFetchPlan p) {
        switch (p) {
            case VIC_PLAN_NONE: return "NONE";
            case VIC_PLAN_VIDEO_MEMPTR_SCREEN: return "VIDEO_MEMPTR_SCREEN";
            case VIC_PLAN_VIDEO_COLOR: return "VIDEO_COLOR";
            case VIC_PLAN_VIDEO_PATTERN: return "VIDEO_PATTERN";
            case VIC_PLAN_REFRESH: return "REFRESH";
            case VIC_PLAN_SPRITE_PTR: return "SPRITE_PTR";
            case VIC_PLAN_SPRITE_DATA0: return "SPRITE_DATA0";
            case VIC_PLAN_SPRITE_DATA1: return "SPRITE_DATA1";
            case VIC_PLAN_SPRITE_DATA2: return "SPRITE_DATA2";
            default: return "UNKNOWN";
        }
    }

    static const char *commitName(VicCommitPlan c) {
        switch (c) {
            case VIC_COMMIT_NONE: return "NONE";
            case VIC_COMMIT_VIDEO_MEMPTR_SCREEN: return "VIDEO_MEMPTR_SCREEN";
            case VIC_COMMIT_VIDEO_COLOR: return "VIDEO_COLOR";
            case VIC_COMMIT_VIDEO_PATTERN: return "VIDEO_PATTERN";
            case VIC_COMMIT_REFRESH: return "REFRESH";
            case VIC_COMMIT_SPRITE_PTR: return "SPRITE_PTR";
            case VIC_COMMIT_SPRITE_DATA0: return "SPRITE_DATA0";
            case VIC_COMMIT_SPRITE_DATA1: return "SPRITE_DATA1";
            case VIC_COMMIT_SPRITE_DATA2: return "SPRITE_DATA2";
            default: return "UNKNOWN";
        }
    }

    static const char *addrFormulaName(VicAddrFormula a) {
        switch (a) {
            case VIC_ADDR_NONE: return "NONE";
            case VIC_ADDR_VIDEO_MEMPTR: return "VIDEO_MEMPTR";
            case VIC_ADDR_VIDEO_SCREEN: return "VIDEO_SCREEN";
            case VIC_ADDR_VIDEO_COLOR: return "VIDEO_COLOR";
            case VIC_ADDR_VIDEO_PATTERN: return "VIDEO_PATTERN";
            case VIC_ADDR_REFRESH: return "REFRESH";
            case VIC_ADDR_SPRITE_PTR: return "SPRITE_PTR";
            case VIC_ADDR_SPRITE_DATA0: return "SPRITE_DATA0";
            case VIC_ADDR_SPRITE_DATA1: return "SPRITE_DATA1";
            case VIC_ADDR_SPRITE_DATA2: return "SPRITE_DATA2";
            default: return "UNKNOWN";
        }
    }

    static std::string plaSnapshotDigestHex() {
        const auto &pla = vicPlaTable();
        uint64_t d = 1469598103934665603ULL;
        for (size_t i = 0; i < pla.size(); ++i) {
            const VicStepRule &r = pla[i];
            d ^= static_cast<uint64_t>(r.op); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.busWindow ? 1 : 0); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.fetch); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.tickActions); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.predicateMask); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.fetchPlan); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.commitPlan); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.addrFormula0); d *= 1099511628211ULL;
            d ^= static_cast<uint64_t>(r.addrFormula1); d *= 1099511628211ULL;
        }
        std::ostringstream oss;
        oss << std::hex << std::uppercase << d;
        return oss.str();
    }

    static bool exportPlaSpecAndSnapshot(const std::string &specPath, const std::string &snapshotPath) {
        try {
            const std::filesystem::path specP(specPath);
            const std::filesystem::path snapP(snapshotPath);
            if (specP.has_parent_path()) std::filesystem::create_directories(specP.parent_path());
            if (snapP.has_parent_path()) std::filesystem::create_directories(snapP.parent_path());

            const auto &pla = vicPlaTable();
            const std::string digest = plaSnapshotDigestHex();

            std::ofstream csv(specPath, std::ios::binary);
            if (!csv.is_open()) return false;
            csv << "phase,cycle,pixel,op,bus_window,fetch,tick_actions,predicate_mask,fetch_plan,commit_plan,addr_formula0,addr_formula1\n";
            for (int ph = 0; ph < 2; ++ph) {
                for (int cyc = 0; cyc < 63; ++cyc) {
                    for (int pix = 0; pix < 8; ++pix) {
                        const VicStepRule &r = pla[plaIndex((ph == 0) ? VIC_PHI1 : VIC_PHI2, cyc, pix)];
                        csv << ((ph == 0) ? "PHI1" : "PHI2") << ","
                            << cyc << ","
                            << pix << ","
                            << busOpName(r.op) << ","
                            << (r.busWindow ? 1 : 0) << ","
                            << fetchName(r.fetch) << ","
                            << static_cast<int>(r.tickActions) << ","
                            << static_cast<int>(r.predicateMask) << ","
                            << planName(static_cast<VicFetchPlan>(r.fetchPlan)) << ","
                            << commitName(static_cast<VicCommitPlan>(r.commitPlan)) << ","
                            << addrFormulaName(static_cast<VicAddrFormula>(r.addrFormula0)) << ","
                            << addrFormulaName(static_cast<VicAddrFormula>(r.addrFormula1))
                            << "\n";
                    }
                }
            }

            std::ofstream md(snapshotPath, std::ios::binary);
            if (!md.is_open()) return false;
            md << "# VIC PLA Snapshot\n\n";
            md << "- digest: `" << digest << "`\n";
            md << "- rows: `" << pla.size() << "`\n";
            md << "- key entries:\n";
            const int kRows[8][3] = {
                {0,0,0}, {0,12,0}, {0,15,0}, {0,55,0},
                {1,15,1}, {1,55,0}, {1,57,3}, {1,8,0}
            };
            for (int i = 0; i < 8; ++i) {
                const VicHalfPhase ph = (kRows[i][0] == 0) ? VIC_PHI1 : VIC_PHI2;
                const int cyc = kRows[i][1];
                const int pix = kRows[i][2];
                const VicStepRule &r = pla[plaIndex(ph, cyc, pix)];
                md << "  - " << ((ph == VIC_PHI1) ? "PHI1" : "PHI2")
                   << " c=" << cyc << " p=" << pix
                   << " op=" << busOpName(r.op)
                   << " fetch=" << fetchName(r.fetch)
                   << " plan=" << planName(static_cast<VicFetchPlan>(r.fetchPlan))
                   << " commit=" << commitName(static_cast<VicCommitPlan>(r.commitPlan))
                   << " pred=" << static_cast<int>(r.predicateMask)
                   << " tick=" << static_cast<int>(r.tickActions)
                   << "\n";
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    VicStepRule getStepRule(bool badline) const {
        if (cycleInLine < 0 || cycleInLine >= 63 || pixelClock < 0 || pixelClock >= 8) {
            return VicStepRule{VIC_OP_REFRESH, false, VIC_FETCH_NONE, VIC_TICK_ACTION_NONE, VIC_PRED_NONE, VIC_PLAN_NONE, VIC_COMMIT_NONE};
        }

        VicStepRule rule = vicPlaTable()[plaIndex(halfPhase, cycleInLine, pixelClock)];
        rule.tickActions = plaTickActionsWithRevision(halfPhase, cycleInLine, pixelClock);
        rule.predicateMask = static_cast<uint8_t>(kPredicateMaskByOp[static_cast<int>(rule.op)] | plaTickPredicateMask(rule.tickActions));
        const bool spriteDmaActive = (spriteDmaMask != 0);
        const uint8_t opMask = kPredicateMaskByOp[static_cast<int>(rule.op)];
        if (!evaluatePredicateMask(opMask, spriteDmaActive, badline, busLocked)) {
            rule.busWindow = false;
            rule.fetch = VIC_FETCH_NONE;
            rule.fetchPlan = VIC_PLAN_NONE;
            rule.commitPlan = VIC_COMMIT_NONE;
        }
        const uint8_t tickMask = plaTickPredicateMask(rule.tickActions);
        if (!evaluatePredicateMask(tickMask, spriteDmaActive, badline, busLocked)) {
            rule.tickActions = VIC_TICK_ACTION_NONE;
        }
        return rule;
    }

    void applyBaAecEdge() {
        baLine = baNext;
        aecLine = aecNext;
    }

    void scheduleHalfCycleOps(const VicStepRule &rule, bool badline) {
        if (halfPhase != VIC_PHI1 || bus == nullptr) {
            return;
        }

        pendingVicOp = (rule.op == VIC_OP_VIDEO) ? 'V' : ((rule.op == VIC_OP_SPRITE) ? 'S' : 'R');
        pendingFetchAction = rule.fetch;
        pendingReadCount = 0;
        pendingSprite = 0xFF;
        pendingFetchIndex = -1;
        pendingRow = -1;

        const uint8_t planIdx = rule.fetchPlan;
        const auto &handlers = fetchPlanHandlers();
        if (planIdx < handlers.size()) {
            (this->*handlers[planIdx])();
        }

        if (badline && cycleInLine >= 15 && cycleInLine <= 54 && rule.op == VIC_OP_SPRITE) {
            pendingReadCount = 0;
            pendingFetchAction = VIC_FETCH_NONE;
        }
    }

    void executeHalfCycleOps() {
        if (halfPhase != VIC_PHI2 || bus == nullptr) {
            return;
        }

        const uint8_t planIdx = static_cast<uint8_t>(plaCommitPlanFromAction(pendingFetchAction));
        const auto &handlers = commitPlanHandlers();
        if (planIdx < handlers.size()) {
            (this->*handlers[planIdx])();
        }

        pendingReadCount = 0;
        pendingFetchAction = VIC_FETCH_NONE;
    }

    uint8_t pickSpriteForCycle(int cycle) const {
        int slot = -1;
        if (cycle >= 8 && cycle <= 14) {
            slot = cycle - 8;
        } else if (cycle >= 57 && cycle <= 62) {
            slot = cycle - 55;
        }

        if (slot < 0 || slot >= 8) {
            return 0xFF;
        }

        if ((spriteDmaMask & static_cast<uint8_t>(1u << slot)) != 0) {
            return static_cast<uint8_t>(slot);
        }

        for (int s = 0; s < 8; ++s) {
            if ((spriteDmaMask & static_cast<uint8_t>(1u << s)) != 0) {
                return static_cast<uint8_t>(s);
            }
        }
        return 0xFF;
    }

    void executeTickActions(uint8_t actions) {
        if ((actions & VIC_TICK_ACTION_BADLINE_LATCH) != 0) {
            busLocked = true;
            badlineActive = true;
            logRasterEvent(EVENT_BADLINE_ON);
        }

        if ((actions & VIC_TICK_ACTION_SPRITE_DMA_UPDATE) != 0) {
            for (int s = 0; s < 8; ++s) {
                const uint8_t bit = static_cast<uint8_t>(1u << s);
                const bool enabled = (sprEnable & bit) != 0;
                const bool active = (spriteDmaMask & bit) != 0;
                const bool canStartDma = enabled && !active && rasterLine == sprY[s] &&
                                         (!revisionProfile.spriteDmaRequiresDisplayEnable || (ctrl1 & 0x10));
                if (canStartDma) {
                    spriteDmaMask = static_cast<uint8_t>(spriteDmaMask | bit);
                    logRasterEvent(EVENT_SPR_DMA_ON, static_cast<uint8_t>(s));
                }
                const int stopLine = static_cast<int>(sprY[s]) +
                                     static_cast<int>(21 + (revisionProfile.spriteDmaStopLineMask & 0x01));
                if (active && (rasterLine >= stopLine || !enabled)) {
                    spriteDmaMask = static_cast<uint8_t>(spriteDmaMask & static_cast<uint8_t>(~bit));
                    logRasterEvent(EVENT_SPR_DMA_OFF, static_cast<uint8_t>(s));
                }
            }
        }

        if ((actions & VIC_TICK_ACTION_BADLINE_RELEASE) != 0) {
            busLocked = false;
            badlineActive = false;
            logRasterEvent(EVENT_BADLINE_OFF);
        }
    }

    void fetchPlanNone() {}

    void fetchPlanVideoMemptrScreen() {
        const int vicCol = (cycleInLine - 15);
        pendingFetchIndex = vicCol;
        pendingRow = (rasterLine - 48) / 8;
        if (vicCol >= 0 && vicCol < 40 && pendingRow >= 0 && pendingRow < 25) {
            pendingReadAddr[pendingReadCount++] = 0xD018;
            const uint16_t screenBase = static_cast<uint16_t>((memPtr & 0x0F) << 10);
            const uint16_t screenAddr = static_cast<uint16_t>(screenBase + pendingRow * 40 + pendingFetchIndex);
            pendingReadAddr[pendingReadCount++] = screenAddr;
        }
    }

    void fetchPlanVideoColor() {
        const int vicCol = (cycleInLine - 15);
        pendingFetchIndex = vicCol;
        pendingRow = (rasterLine - 48) / 8;
        if (vicCol >= 0 && vicCol < 40 && pendingRow >= 0 && pendingRow < 25) {
            pendingReadAddr[pendingReadCount++] = static_cast<uint16_t>(0xD800 + pendingRow * 40 + pendingFetchIndex);
        }
    }

    void fetchPlanVideoPattern() {
        const int vicCol = (cycleInLine - 15);
        pendingFetchIndex = vicCol;
        pendingRow = (rasterLine - 48) / 8;
        if (vicCol >= 0 && vicCol < 40 && pendingRow >= 0 && pendingRow < 25) {
            const int yCharLine = (rasterLine - 48) % 8;
            const uint16_t charsetBase = static_cast<uint16_t>(((memPtr >> 4) & 0x0F) << 10);
            const uint16_t charPatternAddr = static_cast<uint16_t>(charsetBase + (uint16_t)prefetchTmp.charCode * 8 + yCharLine);
            pendingReadAddr[pendingReadCount++] = charPatternAddr;
        }
    }

    void fetchPlanRefresh() {
        const uint16_t refreshAddr = static_cast<uint16_t>((((memPtr & 0x30) << 6) & 0xC000) | (refreshCounter6 & 0x3F));
        pendingReadAddr[pendingReadCount++] = static_cast<uint16_t>(refreshAddr & 0x3FFF);
    }

    void fetchPlanSpritePtr() {
        pendingSprite = pickSpriteForCycle(cycleInLine);
        if (pendingSprite != 0xFF) {
            pendingReadAddr[pendingReadCount++] = static_cast<uint16_t>(0x03F8 + pendingSprite);
        }
    }

    void fetchPlanSpriteData0() {
        pendingSprite = pickSpriteForCycle(cycleInLine);
        if (pendingSprite != 0xFF) {
            const int lineOff = (rasterLine >= sprY[pendingSprite]) ? (rasterLine - sprY[pendingSprite]) : 0;
            const uint16_t spriteBase = static_cast<uint16_t>(spritePointerLatch[pendingSprite]) << 6;
            const uint16_t rowBase = static_cast<uint16_t>(spriteBase + (lineOff * 3));
            pendingReadAddr[pendingReadCount++] = rowBase;
        }
    }

    void fetchPlanSpriteData1() {
        pendingSprite = pickSpriteForCycle(cycleInLine);
        if (pendingSprite != 0xFF) {
            const int lineOff = (rasterLine >= sprY[pendingSprite]) ? (rasterLine - sprY[pendingSprite]) : 0;
            const uint16_t spriteBase = static_cast<uint16_t>(spritePointerLatch[pendingSprite]) << 6;
            const uint16_t rowBase = static_cast<uint16_t>(spriteBase + (lineOff * 3));
            pendingReadAddr[pendingReadCount++] = static_cast<uint16_t>(rowBase + 1);
        }
    }

    void fetchPlanSpriteData2() {
        pendingSprite = pickSpriteForCycle(cycleInLine);
        if (pendingSprite != 0xFF) {
            const int lineOff = (rasterLine >= sprY[pendingSprite]) ? (rasterLine - sprY[pendingSprite]) : 0;
            const uint16_t spriteBase = static_cast<uint16_t>(spritePointerLatch[pendingSprite]) << 6;
            const uint16_t rowBase = static_cast<uint16_t>(spriteBase + (lineOff * 3));
            pendingReadAddr[pendingReadCount++] = static_cast<uint16_t>(rowBase + 2);
        }
    }

    static const std::array<FetchPlanHandler, 9> &fetchPlanHandlers() {
        static const std::array<FetchPlanHandler, 9> handlers = {
            &VICII::fetchPlanNone,
            &VICII::fetchPlanVideoMemptrScreen,
            &VICII::fetchPlanVideoColor,
            &VICII::fetchPlanVideoPattern,
            &VICII::fetchPlanRefresh,
            &VICII::fetchPlanSpritePtr,
            &VICII::fetchPlanSpriteData0,
            &VICII::fetchPlanSpriteData1,
            &VICII::fetchPlanSpriteData2
        };
        return handlers;
    }

    void commitPlanNone() {}

    void commitPlanVideoMemptrScreen() {
        if (pendingFetchIndex >= 0 && pendingFetchIndex < 40 && pendingRow >= 0 && pendingRow < 25 && pendingReadCount >= 2) {
            uint8_t memPtrVal = bus->read(pendingReadAddr[0]);
            memPtr = memPtrVal;
            prefetchTmp.charCode = bus->read(pendingReadAddr[1]);
            vc = static_cast<uint16_t>(pendingReadAddr[1] & 0x03FF);
        }
    }

    void commitPlanVideoColor() {
        if (pendingFetchIndex >= 0 && pendingFetchIndex < 40 && pendingRow >= 0 && pendingRow < 25 && pendingReadCount >= 1) {
            prefetchTmp.colorCode = bus->read(pendingReadAddr[0]);
        }
    }

    void commitPlanVideoPattern() {
        if (pendingFetchIndex >= 0 && pendingFetchIndex < 40 && pendingRow >= 0 && pendingRow < 25 && pendingReadCount >= 1) {
            const uint8_t pattern = bus->read(pendingReadAddr[0]);
            VicCharSlot slot = prefetchTmp;
            slot.charPattern = pattern;
            stagedPrefetch[pendingFetchIndex] = slot;
            stagedValid[pendingFetchIndex] = true;
            charFetchBuffer[pendingFetchIndex] = slot.charCode;
            colorFetchBuffer[pendingFetchIndex] = slot.colorCode;
            rc = static_cast<uint8_t>((rasterLine - 48) & 0x07);
            vcBase = vc;
        }
    }

    void commitPlanRefresh() {
        if (pendingReadCount >= 1) {
            volatile uint8_t dr = bus->read(pendingReadAddr[0]);
            (void)dr;
            refreshCounter6 = static_cast<uint8_t>((refreshCounter6 + 1) & 0x3F);
        }
    }

    void commitPlanSpritePtr() {
        if (pendingSprite != 0xFF && pendingReadCount >= 1) {
            spritePointerLatch[pendingSprite] = bus->read(pendingReadAddr[0]);
        }
    }

    void commitPlanSpriteData0() {
        if (pendingSprite != 0xFF && pendingReadCount >= 1) {
            spriteDataBytes[pendingSprite][0] = bus->read(pendingReadAddr[0]);
            spriteDataLatch[pendingSprite] = spriteDataBytes[pendingSprite][0];
        }
    }

    void commitPlanSpriteData1() {
        if (pendingSprite != 0xFF && pendingReadCount >= 1) {
            spriteDataBytes[pendingSprite][1] = bus->read(pendingReadAddr[0]);
        }
    }

    void commitPlanSpriteData2() {
        if (pendingSprite != 0xFF && pendingReadCount >= 1) {
            spriteDataBytes[pendingSprite][2] = bus->read(pendingReadAddr[0]);
        }
    }

    static const std::array<CommitPlanHandler, 9> &commitPlanHandlers() {
        static const std::array<CommitPlanHandler, 9> handlers = {
            &VICII::commitPlanNone,
            &VICII::commitPlanVideoMemptrScreen,
            &VICII::commitPlanVideoColor,
            &VICII::commitPlanVideoPattern,
            &VICII::commitPlanRefresh,
            &VICII::commitPlanSpritePtr,
            &VICII::commitPlanSpriteData0,
            &VICII::commitPlanSpriteData1,
            &VICII::commitPlanSpriteData2
        };
        return handlers;
    }

    void clearRasterEventLog() {
        rasterEventCount = 0;
    }

    size_t getRasterEventCount() const {
        return rasterEventCount;
    }

    uint64_t getLastFrameHash() const {
        return frameHashLast;
    }

    bool hasFrameHash() const {
        return frameHashValid;
    }

    void resetFrameHashing() {
        frameHashCurrent = 1469598103934665603ULL;
        frameHashLast = 0;
        frameHashValid = false;
    }

    uint64_t getRasterEventDigest() const {
        uint64_t d = 1469598103934665603ULL;
        for (size_t i = 0; i < rasterEventCount; ++i) {
            const RasterEvent &ev = rasterEvents[i];
            d ^= ev.line;
            d *= 1099511628211ULL;
            d ^= ev.cycle;
            d *= 1099511628211ULL;
            d ^= ev.pixel;
            d *= 1099511628211ULL;
            d ^= ev.type;
            d *= 1099511628211ULL;
            d ^= ev.data;
            d *= 1099511628211ULL;
        }
        return d;
    }

    void logRasterEvent(uint8_t type, uint8_t data = 0) {
        if (rasterEventCount >= rasterEvents.size()) {
            return;
        }
        rasterEvents[rasterEventCount++] = RasterEvent{
            static_cast<uint16_t>(rasterLine & 0x1FF),
            static_cast<uint8_t>(cycleInLine & 0x3F),
            static_cast<uint8_t>(pixelClock & 0x07),
            type,
            data
        };
    }

    void hashPixel(int x, int y, uint8_t color) {
        frameHashCurrent ^= static_cast<uint64_t>((x & 0x1FF) | ((y & 0x1FF) << 9) | ((color & 0x0F) << 18));
        frameHashCurrent *= 1099511628211ULL;
    }

    /*
    // =========================================================
    // Verifica se il VIC-II sta occupando il bus per fetch
    // =========================================================
    bool isFetching() const {
        if (cycleInLine < 0 || cycleInLine >= 63)
            return false;

        static constexpr char vicBusPatternPAL[63] = {
            // 0-14: refresh DRAM
            'R','R','R','R','R','R','R','R','R','R','R','R','R','R','R',
            // 15-54: fetch alternati carattere/grafica
            'C','G','C','G','C','G','C','G','C','G',
            'C','G','C','G','C','G','C','G','C','G',
            'C','G','C','G','C','G','C','G','C','G',
            'C','G','C','G','C','G','C','G',
            // 55-62: refresh/border
            'R','R','R','R','R','R','R','R'
        };

        char op = vicBusPatternPAL[cycleInLine];

        // Badline condition (legge la character matrix)
        bool badline = (rasterLine >= 48 && rasterLine < 248 &&
                        (rasterLine & 0x07) == (ctrl1 & 0x07) &&
                        (ctrl1 & 0x10));

        if (badline && cycleInLine >= 15 && cycleInLine <= 54)
            op = 'C';

        // Solo C (character) e G (graphic/bitmap) sono veri fetch video
        return (op == 'C' || op == 'G');
    }
    */


    // -----------------------------------------------------------------------------
    // dumpFrameToPPM
    // -----------------------------------------------------------------------------
    // Salva il framebuffer VIC-II (312 linee x 320 pixel) in un file PPM ("P6").
    //
    // Ogni chiamata genera un file "vic_frame_XXXX.ppm" incrementale che rappresenta
    // il frame corrente del VIC-II, utile per il debug video o per analisi grafica.
    //
    // Il framebuffer 'fb' contiene i codici colore VIC-II (0-15), che vengono tradotti
    // nella palette RGB standard del C64 (tabella 'palette' 16x3).
    // -----------------------------------------------------------------------------
    void dumpFrameToPPM(uint8_t fb[312][320]) {
        static int frameIndex = 0;
        std::ostringstream name;
        name << "vic_frame_" << std::setw(4) << std::setfill('0') << frameIndex++ << ".ppm";

        std::ofstream out(name.str(), std::ios::binary);
        if (!out.is_open()) return;

        out << "P6\n320 312\n255\n";

        static const uint8_t palette[16][3] = {
            {0,0,0}, {255,255,255}, {136,0,0}, {170,255,238},
            {204,68,204}, {0,204,85}, {0,0,170}, {238,238,119},
            {221,136,85}, {102,68,0}, {255,119,119}, {51,51,51},
            {119,119,119}, {170,255,102}, {0,136,255}, {187,187,187}
        };

        for (int y = 0; y < 312; ++y) {
            for (int x = 0; x < 320; ++x) {
                uint8_t c = fb[y][x] & 0x0F;
                out.put(palette[c][0]);
                out.put(palette[c][1]);
                out.put(palette[c][2]);
            }
        }

        out.close();
        std::cout << "[VIC-II] Frame salvato: " << name.str() << std::endl;
    }

    /*

    Riassunto delle fasi VIC per rasterline (PAL)
    ---------------------------------------------
    Fase        CiclI VIC   Descrizione
    FET0-FET14  0-14        Prefetch, sprite DMA, fetch iniziali
    G0-G39      15-54       Fetch colonne visibili (char/color/bitmap)
    FET55-FET62 55-62       Sprite prefetch, idle

    */

    void tickHalf() {
        constexpr int CYCLES_PER_LINE = 63;
        constexpr int LINES_PER_FRAME = 312;

        bool displayEnabled  = (ctrl1 & 0x10) != 0;
        uint8_t yScroll      = ctrl1 & 0x07;
        bool inVisibleRange  = (rasterLine >= static_cast<int>(revisionProfile.visibleLineStart) &&
                                rasterLine <= static_cast<int>(revisionProfile.visibleLineEnd));
        bool badline         = displayEnabled && inVisibleRange && ((rasterLine & 0x07) == yScroll);

        uint8_t borderColor = bus ? (bus->read(0xD020) & 0x0F) : 0;
        uint8_t backgroundColor = bus ? (bus->read(0xD021) & 0x0F) : 0;

        const VicStepRule stepRule = getStepRule(badline);

        // Stage setup on PHI1, bus sample on PHI2.
        scheduleHalfCycleOps(stepRule, badline);
        updateBaAecForCurrentSubstep(stepRule);
        applyBaAecEdge();

        // ============================
        // 0 STAGING COMMIT: push P fetch del ciclo precedente
        // ============================
        if (halfPhase == VIC_PHI1 && pixelClock == 0) {
            for (int fi = 0; fi < 40; ++fi) {
                if (stagedValid[fi]) {
                    if (prefetchCount < 3) {
                        int tail = (prefetchHead + prefetchCount) % 3;
                        prefetch[tail] = stagedPrefetch[fi];
                        prefetchCount++;
                    } else {
                        prefetch[prefetchHead] = stagedPrefetch[fi];
                        prefetchHead = (prefetchHead + 1) % 3;
                    }
                    stagedValid[fi] = false;
                }
            }
        }

        // ============================
        // 1 Gestione BADLINE + sprite DMA latching
        // ============================
        executeTickActions(stepRule.tickActions);

        // ============================
        // 2 FETCH execute on PHI2
        // ============================
        executeHalfCycleOps();

        // ============================
        // 3 Generazione PIXEL
        // ============================
        if (cycleInLine >= 0 && cycleInLine <= 54) {
            int visibleCol = cycleInLine - 15;
            int pixelInChar = pixelClock;
            int pixelX = visibleCol * 8 + pixelInChar;

            if (shiftBitsRemaining == 0 && prefetchCount > 0) {
                VicCharSlot slot = prefetch[prefetchHead];
                prefetchHead = (prefetchHead + 1) % 3;
                prefetchCount--;

                shiftPattern = slot.charPattern;
                shiftColor   = slot.colorCode;
                shiftBitsRemaining = 8;
            }

            uint8_t outColor = borderColor;
            if (shiftBitsRemaining > 0) {
                bool pixelOn = (shiftPattern & 0x80) != 0;
                outColor = pixelOn ? shiftColor : backgroundColor;
                shiftPattern <<= 1;
                shiftBitsRemaining--;
            } else {
                outColor = backgroundColor;
            }

            if (pixelX >= 0 && pixelX < 320)
                pixelRowBuffer[pixelX] = outColor;
            if (pixelX >= 0 && pixelX < 320 && rasterLine >= 0 && rasterLine < LINES_PER_FRAME) {
                hashPixel(pixelX, rasterLine, outColor);
            }

            if (cycleInLine < static_cast<int>(revisionProfile.borderOpenCycle) ||
                cycleInLine > static_cast<int>(revisionProfile.borderCloseCycle - 1)) {
                int base = (cycleInLine >= 15) ? (cycleInLine - 15) * 8 : (cycleInLine * 8);
                for (int i = 0; i < 8; ++i) {
                    int x = base + i;
                    if (x >= 0 && x < 320) pixelRowBuffer[x] = borderColor;
                }
            }
        }

        // ============================
        // 5 Clocking e avanzamento
        // ============================
        // advance PHI phase and only then pixel/cycle counters
        if (halfPhase == VIC_PHI1) {
            halfPhase = VIC_PHI2;
        } else {
            halfPhase = VIC_PHI1;
            pixelClock++;
            if (pixelClock >= 8) {
                pixelClock = 0;
                cycleInLine++;

                if (cycleInLine >= CYCLES_PER_LINE) {
                    int finishedLine = rasterLine;
                    if (finishedLine >= 0 && finishedLine < LINES_PER_FRAME) {
                        for (int x = 0; x < 320; ++x)
                            frameBuffer[finishedLine][x] = pixelRowBuffer[x];
                    }

                    cycleInLine = 0;
                    rasterLine++;

                    if (rasterLine >= LINES_PER_FRAME) {
                        frameHashLast = frameHashCurrent;
                        frameHashValid = true;
                        frameHashCurrent = 1469598103934665603ULL;
                        if (std::getenv("VIC_DUMP_FRAMES") != nullptr) {
                            dumpFrameToPPM(frameBuffer);
                        }
                        rasterLine = 0;
                    }

                    int compareValue = getRasterCompareValue();
                    if (rasterLine == compareValue) {
                        const bool allowRasterIrq = (irqMask & 0x01) &&
                            (!revisionProfile.rasterIrqNeedsMaskEdge || rasterIrqMaskEdgeArmed);
                        if (allowRasterIrq && !rasterIRQPending) {
                            irqFlags |= 0x01;
                            rasterIRQPending = true;
                            rasterIrqMaskEdgeArmed = false;
                            logRasterEvent(EVENT_RASTER_IRQ);
                        }
                    } else {
                        rasterIRQPending = false;
                    }
                }
            }
        }
        halfTickCounter++;
    }

    void tickPixel() {
        // One pixel clock now equals one full PHI1+PHI2 pair.
        tickHalf();
        tickHalf();
    }


    // TICK: chiamato ogni 8 cicli CPU (1 raster cycle)
    void tick() {

        cycleInLine++;

        if (cycleInLine >= 63) {
            cycleInLine = 0;
            rasterLine++;
            if (rasterLine >= 312) {
                rasterLine = 0;
            }

            // Controllo IRQ Raster
            uint16_t compareValue = getRasterCompareValue();
            if (rasterLine == compareValue) {
                const bool allowRasterIrq = (irqMask & 0x01) &&
                    (!revisionProfile.rasterIrqNeedsMaskEdge || rasterIrqMaskEdgeArmed);
                if (allowRasterIrq) {
                    if (!rasterIRQPending) {   // evita retrigger sulla stessa linea
                        irqFlags |= 0x01;      // Bit 0 = Raster IRQ
                        rasterIRQPending = true;
                        rasterIrqMaskEdgeArmed = false;
                        logRasterEvent(EVENT_RASTER_IRQ);
                    }
                }
            } else {
                // Condizione raster non piu vera -> reset del pending
                rasterIRQPending = false;
            }
        }
    }


    // Valore di confronto: $D012 + bit 7 di $D011
    int getRasterCompareValue() const {
        return ((ctrl1 & 0x80) << 1) | raster;
    }

    // Scrittura reale (gia presente, ma adattata per mantenere latch)
    void write(uint16_t addr, uint8_t val) {
        uint8_t reg = addr & 0x3F;
        switch (reg) {
            case 0x11: {
                const uint8_t old = ctrl1;
                ctrl1 = val;
                const bool yScrollChanged = ((old ^ val) & 0x07) != 0;
                const bool fldWindow = (cycleInLine >= 14 && cycleInLine <= 18);
                if (yScrollChanged && fldWindow) {
                    if (!revisionProfile.fldRequiresBadlineCarry || badlineActive) {
                        fldTriggered = true;
                    }
                }
                break;
            }
            case 0x12: raster = val; break;
            case 0x16: {
                const uint8_t old = ctrl2;
                ctrl2 = val;
                const bool bit3Toggled = ((old ^ val) & 0x08) != 0;
                const bool vspWindow = (cycleInLine == 14 && pixelClock <= 1);
                if (bit3Toggled && vspWindow && revisionProfile.vspFlickerSensitive) {
                    vspTriggered = true;
                }
                break;
            }
            case 0x19: irqFlags &= ~(val & 0x0F); break; // ACK IRQs
            case 0x1A:
                irqMask = val & 0x0F;
                if (irqMask & 0x01) {
                    rasterIrqMaskEdgeArmed = true;
                }
                break;      // Enable IRQs
            default: break;
        }
        internalBusLatch = val;

        std::cout << "[VIC-II] Write $" << std::hex << (int)val
                  << " to $" << std::hex << (0xD000 + reg) << std::endl;
    }

    // Read attiva
    uint8_t read(uint16_t addr, uint8_t busVal) {
        uint8_t reg = addr & 0x3F;
        uint8_t value = busVal;

        switch (reg) {
            case 0x11: value = ctrl1; break;
            case 0x12: value = raster; break;
            case 0x13: value = lightpenX; break;
            case 0x14: value = lightpenY; break;
            case 0x15: value = sprEnable; break;
            case 0x16: value = (ctrl2 & 0x1F) | (busVal & 0xE0); break;
            case 0x19: value = irqFlags; break;
            case 0x1A: value = irqMask; break;
            default: value = busVal; break;
        }

        internalBusLatch = value;
        return value;
    }

    // Peek passivo
    uint8_t peek(uint16_t addr, uint8_t busVal) const {
        uint8_t reg = addr & 0x3F;
        switch (reg) {
            case 0x11: return ctrl1;
            case 0x12: return raster;
            case 0x13: return lightpenX;
            case 0x14: return lightpenY;
            case 0x15: return sprEnable;
            case 0x16: return (ctrl2 & 0x1F) | (busVal & 0xE0);
            case 0x19: return irqFlags;
            case 0x1A: return irqMask;
            default:   return busVal;
        }
    }
};
