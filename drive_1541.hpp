#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <utility>
#include <string>
#include <vector>

#include "advanced_image_backends.hpp"
#include "d64_image_backend.hpp"
#include "drive1541_physical/i_flux_image_backend.hpp"
#include "drive1541_physical/flux_image_backend_g64.hpp"
#include "drive1541_physical/flux_image_backend_nib.hpp"
#include "drive1541_physical/flux_image_backend_raw.hpp"
#include "drive1541_physical/drive_cpu_domain.hpp"
#include "drive1541_physical/drive_dos_memory_map.hpp"
#include "drive1541_physical/drive_iec_port.hpp"
#include "drive1541_physical/drive_power_controller.hpp"
#include "drive1541_physical/drive_scheduler.hpp"
#include "drive1541_physical/drive_signal_model.hpp"
#include "drive1541_physical/drive_via_domain.hpp"
#include "drive1541_physical/bitcell_timing_model.hpp"
#include "drive1541_physical/flux_track_model.hpp"
#include "drive1541_physical/gcr_codec.hpp"
#include "drive1541_physical/mechanics_model.hpp"
#include "drive1541_physical/physical_profile.hpp"
#include "drive1541_physical/read_channel_pll.hpp"
#include "image_backend.hpp"
#include "iec_device.hpp"

class Drive1541 : public IIecDevice {
public:
    using PhysicalProfile = drive1541_physical::PhysicalProfile;

    enum Revision : uint8_t {
        REV_1541 = 0,
        REV_1541C = 1,
        REV_1541II = 2
    };

    struct RevisionProfile {
        Revision revision = REV_1541;
        uint8_t cpuCyclesPerStep = 1;
        bool iecStrictEoiAck = true;
        bool iecCommandNeedsAtnLow = true;
        bool iecHandshakeNeedsClockLowAck = true;
        uint32_t iecAtnAckTicksOverride = 16;
        bool iecRxAckOnAnyDataByte = false;
    };

    static constexpr RevisionProfile makeRevisionProfile(Revision rev) {
        return (rev == REV_1541C)
            ? RevisionProfile{REV_1541C, 2, false, false, false, 8, true}
            : (rev == REV_1541II)
                ? RevisionProfile{REV_1541II, 2, false, false, false, 6, true}
            : RevisionProfile{REV_1541, 1, true, true, true, 16, false};
    }

    Revision revision = REV_1541;
    RevisionProfile revisionProfile = makeRevisionProfile(REV_1541);
    PhysicalProfile physicalProfile = PhysicalProfile::Level1Functional;

    drive1541_physical::DriveScheduler physicalScheduler;
    drive1541_physical::DriveCpuDomain physicalCpuDomain;
    drive1541_physical::DriveViaDomain physicalViaDomain;
    drive1541_physical::DriveDosMemoryMap physicalDosMemoryMap;
    drive1541_physical::DriveIecPort physicalIecPort;
    drive1541_physical::DrivePowerController powerController;
    drive1541_physical::DriveSignalModel signalModel;

    drive1541_physical::MechanicsModel physicalMechanicsModel;
    drive1541_physical::BitcellTimingModel physicalBitcellTimingModel;
    drive1541_physical::FluxTrackModel physicalFluxTrackModel;
    drive1541_physical::GcrCodec physicalGcrCodec;

    bool physicalPipelineInitialized = false;
    uint32_t physicalPipelineAbsTick = 0;
    uint64_t physicalPipelineRuns = 0;
    uint32_t physicalPipelineLastCellTicks = 0;
    bool physicalPipelineLastFluxEdge = false;
    uint8_t physicalPipelineLastByte = 0;
    uint8_t physicalPipelineLastBit = 0;
    uint16_t physicalPipelineLastHalfTrack = 36;
    double physicalPipelineLastAngleNorm = 0.0;
    drive1541_physical::ReadChannelPllSample physicalPipelineLastPll{};
    bool driveLedMotorOn = false;
    bool driveLedActivity = false;
    bool driveLedError = false;
    bool driveLedIecLoad = false;

    using PowerState = drive1541_physical::PowerState;

    enum class C64PowerState : uint8_t {
        Off = 0,
        On = 1,
        Resetting = 2
    };

    enum class PowerMatrixMode : uint8_t {
        C64OffDriveOff = 0,
        C64OffDriveOn = 1,
        C64OnDriveOff = 2,
        C64OnDriveOn = 3
    };

    struct PowerMatrixState {
        C64PowerState c64 = C64PowerState::On;
        PowerState drive = PowerState::On;
        PowerMatrixMode mode = PowerMatrixMode::C64OnDriveOn;
        bool c64_powered = true;
        bool drive_powered = true;
        bool command_rearm_required = false;
    };

    void setC64Power(bool enabled) {
        const C64PowerState prev = c64PowerState;
        c64PowerState = enabled ? C64PowerState::On : C64PowerState::Off;
        if (!enabled) {
            forceIecIdleForC64PowerOff();
            armCommandReacquire();
        }
        if (enabled && prev != C64PowerState::On) {
            armCommandReacquire();
        }
        if (!enabled) {
            setIecLines(true, true, true);
        }
    }

    void setC64PowerState(C64PowerState state) {
        const C64PowerState prev = c64PowerState;
        c64PowerState = state;
        if (state == C64PowerState::Off) {
            forceIecIdleForC64PowerOff();
            armCommandReacquire();
        }
        if (state == C64PowerState::Resetting) {
            forceIecIdleForC64PowerOff();
            armCommandReacquire();
        }
        if (state == C64PowerState::On && prev != C64PowerState::On) {
            armCommandReacquire();
        }
        if (!isC64DrivingIecLines()) {
            setIecLines(true, true, true);
        }
    }

    C64PowerState getC64PowerState() const {
        return c64PowerState;
    }

    void setDrivePower(bool enabled) {
        if (enabled) {
            if (!powerController.isOn() && powerController.state() != PowerState::SpinningUp) {
                powerController.powerOn(true);
            }
            return;
        }
        if (powerController.state() != PowerState::Off && powerController.state() != PowerState::SpinningDown) {
            powerController.powerOff();
        }
    }

    PowerMatrixState getPowerMatrixState() const {
        const bool c64On = (c64PowerState != C64PowerState::Off);
        const bool driveOn = powerController.isOn();
        const PowerMatrixMode mode =
            c64On
                ? (driveOn ? PowerMatrixMode::C64OnDriveOn : PowerMatrixMode::C64OnDriveOff)
                : (driveOn ? PowerMatrixMode::C64OffDriveOn : PowerMatrixMode::C64OffDriveOff);
        return PowerMatrixState{
            c64PowerState,
            powerController.state(),
            mode,
            c64On,
            driveOn,
            iecRequireFreshCommandAfterC64Resume
        };
    }

    bool isC64DrivingIecLines() const {
        return c64PowerState == C64PowerState::On;
    }

    drive1541_physical::IecLines resolveHostLinesForPowerMatrix(bool atnHigh, bool clkHigh, bool dataHigh) const {
        if (!isC64DrivingIecLines()) {
            return drive1541_physical::IecLines{true, true, true};
        }
        return drive1541_physical::IecLines{atnHigh, clkHigh, dataHigh};
    }

    bool isDriveOutputAllowedForPowerMatrix() const {
        return powerController.isDriveOutputAllowed();
    }

    void forceIecIdleForC64PowerOff() {
        iecListening = false;
        iecTalking = false;
        iecSerialState = IecSerialState::Idle;
        iecSerialPullDATA = false;
        iecAtnAckPullDATA = false;
        iecRxByteAckPullDATA = false;
        iecTxByteActive = false;
        iecTalkStartPending = false;
        iecEoiPendingAck = false;
        iecDrivePullCLK = false;
        iecDrivePullDATA = false;
    }

    void armCommandReacquire() {
        iecRequireFreshCommandAfterC64Resume = true;
        iecCommandRearmTransitions++;
    }

    void powerOn(bool coldBoot) {
        powerController.powerOn(coldBoot);
    }

    void powerOff() {
        powerController.powerOff();
    }

    void powerReset() {
        powerController.reset();
    }

    bool isPoweredOn() const {
        return powerController.isOn();
    }

    drive1541_physical::DriveSignalState getDriveSignalState() const {
        return signalModel.state();
    }

    PowerState getPowerState() const {
        return powerController.state();
    }

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

    static PhysicalProfile parsePhysicalProfile(const std::string &profile) {
        std::string v = profile;
        for (char &c : v) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (v == "level2-cycle") {
            return PhysicalProfile::Level2Cycle;
        }
        if (v == "level3-physical") {
            return PhysicalProfile::Level3Physical;
        }
        if (v == "level4-accuracy") {
            return PhysicalProfile::Level4Accuracy;
        }
        if (v == "level5-coupling") {
            return PhysicalProfile::Level5Coupling;
        }
        return PhysicalProfile::Level1Functional;
    }

    void setPhysicalProfile(PhysicalProfile profile) {
        physicalProfile = profile;
        driveCpuUseMicroOpEngine = (physicalProfile == PhysicalProfile::Level5Coupling);
        applyMountedBackendRouting();
    }

    PhysicalProfile getPhysicalProfile() const {
        return physicalProfile;
    }

    enum class IecSerialState {
        Idle,
        Command,
        ListenData,
        TalkData,
        TalkEoiAck
    };

    static constexpr uint16_t IEC_TOTAL_VIRTUAL_BLOCKS = 664;
    static constexpr size_t IEC_MAX_VIRTUAL_CATALOG_ENTRIES = 32;

    struct VirtualCatalogEntry {
        bool used = false;
        uint8_t channel = 0xFF;
        uint8_t track = 0;
        uint8_t sector = 0;
        uint16_t blocks = 0;
        std::string name;
        std::string type = "PRG";
        std::string mode;
    };

    struct IecRxEvent {
        uint8_t byte = 0;
        bool isCommand = false;
    };

    std::array<uint8_t, 0x10000> memory = {0};
    drive1541_physical::DriveViaDomain::Via6522 via1; // $1800-$180F
    drive1541_physical::DriveViaDomain::Via6522 via2; // $1C00-$1C0F

    bool romLoaded = false;
    uint16_t pc = 0;
    uint64_t cycles = 0;

    // IEC lines (active-low on real bus; represented here as logical levels)
    bool iecATN = true;
    bool iecCLK = true;
    bool iecDATA = true;

    // Lines pulled low by the drive (open-collector model).
    bool iecDrivePullCLK = false;
    bool iecDrivePullDATA = false;

    // Minimal IEC command state (LISTEN/TALK subset)
    uint8_t iecDeviceAddress = 8;
    bool iecListening = false;
    bool iecTalking = false;
    bool iecCommandSeen = false;
    uint8_t lastIecCommand = 0;
    uint8_t iecListenSecondary = 0xFF;
    uint8_t iecTalkSecondary = 0xFF;
    bool iecExpectingNameBytes = false;
    bool iecDirectoryStubPrepared = false;
    std::vector<uint8_t> iecNameBuffer;

    std::deque<IecRxEvent> iecRxQueue;
    std::deque<uint8_t> iecTxQueue;
    uint64_t iecRxProcessed = 0;
    uint64_t iecTxServed = 0;
    uint64_t iecEoiAckCount = 0;
    uint64_t iecAtnFallingSeen = 0;
    uint64_t iecClockRisingSeen = 0;
    uint64_t iecClockRisingAtnLow = 0;
    bool iecRequireFreshCommandAfterC64Resume = false;
    uint64_t iecCommandRearmTransitions = 0;

    bool iecPrevCLK = true;
    bool iecPrevATN = true;
    bool iecPrevDATA = true;
    bool iecEnableAtnAck = false;
    bool iecEnableListenerByteAck = false;
    bool iecKernelCompatSampleBothClockEdges = false;
    bool iecKernelSampleOnFallingClockEdge = false;
    bool iecKernelSampleBothCommandEdges = false;
    bool iecKernelCompatAutoTalkDirectory = false;
    bool iecKernelCompatAutoDirectoryOnTalk0 = false;
    bool iecKernelCompatForceTalkOnIcrSerial = false;
    bool iecKernelIgnoreAtnForTalkDataPhase = false;
    IecSerialState iecSerialState = IecSerialState::Idle;
    uint32_t iecAtnAckTicks = 0;
    bool iecAtnAckPullDATA = false;
    bool iecAtnHandshakeActive = false;
    bool iecAtnAckSawClockLow = false;
    uint32_t iecRxByteAckTicks = 0;
    bool iecRxByteAckPullDATA = false;
    uint8_t iecRxShift = 0;
    uint8_t iecRxBitCount = 0;
    bool iecCommandFrameSync = false;
    bool iecCommandSawStartEdge = false;
    uint32_t iecCommandFrameByteCount = 0;
    bool iecCommandFrameSawListenForDevice = false;
    bool iecCommandFrameSawTalkForDevice = false;
    bool iecCommandFrameSawListenSecondary0 = false;
    bool iecCommandFrameSawTalkSecondary0 = false;

    bool iecTxByteActive = false;
    uint8_t iecTxShift = 0;
    uint8_t iecTxBitCount = 0;
    bool iecTalkStartPending = false;
    uint8_t iecTalkStartByte = 0;
    bool iecTalkStartIsEoi = false;
    bool iecTalkSa0Confirmed = false;
    bool iecTalkFrameArmed = false;
    bool iecTalkSawStartEdge = false;
    bool iecSerialPullDATA = false;
    bool iecTxCurrentIsEoi = false;
    bool iecEoiPendingAck = false;
    bool iecEoiAckLowSeen = false;

    std::array<bool, 16> iecOpenListenChannels = {false};
    std::array<bool, 16> iecOpenTalkChannels = {false};
    uint8_t iecActiveListenChannel = 0xFF;
    uint8_t iecActiveTalkChannel = 0xFF;

    static constexpr uint32_t IEC_SERIAL_TIMEOUT_TICKS = 256;
    static constexpr uint32_t IEC_EOI_TIMEOUT_TICKS = 256;
    static constexpr uint32_t IEC_ATN_ACK_TICKS = 16;
    static constexpr uint32_t IEC_RX_BYTE_ACK_TICKS = 64;
    uint32_t iecRxIdleTicks = 0;
    uint32_t iecTxIdleTicks = 0;
    uint32_t iecEoiWaitTicks = 0;
    uint64_t iecRxTimeoutCount = 0;
    uint64_t iecTxTimeoutCount = 0;
    uint64_t iecEoiTimeoutCount = 0;

    std::string iecStatusLine = "00,OK,00,00";
    std::vector<uint8_t> iecCommandChannelBuffer;
    std::vector<uint8_t> iecCommandResponseQueue;
    uint16_t iecLastExecuteAddr = 0;
    uint8_t iecLastBlockTrack = 0;
    uint8_t iecLastBlockSector = 0;
    uint8_t iecLastBlockChannel = 0xFF;
    std::string iecLastBlockCommand;
    std::string iecLastExecDispatch;
    uint64_t iecExecDispatchCount = 0;

    std::array<uint8_t, 256> iecBlockBuffer = {0};
    bool iecBlockBufferValid = false;
    uint8_t iecBlockBufferTrack = 0;
    uint8_t iecBlockBufferSector = 0;
    std::array<uint8_t, 256> iecDiskMap = {0};
    std::array<uint8_t, 16> iecChannelBufferPos = {0};
    std::array<bool, 16> iecChannelPointerValid = {false};
    std::array<uint8_t, IEC_TOTAL_VIRTUAL_BLOCKS> iecBlockAllocated = {0};
    std::array<uint8_t, IEC_TOTAL_VIRTUAL_BLOCKS> iecAllocOwnerEntry = {0};
    uint16_t iecAllocatedBlockCount = 0;
    std::array<VirtualCatalogEntry, IEC_MAX_VIRTUAL_CATALOG_ENTRIES> iecCatalog = {};
    std::array<int8_t, 16> iecChannelCatalogEntry = {};
    std::array<std::string, 16> iecChannelOpenName = {};
    std::array<bool, 16> iecChannelOpenNameValid = {false};
    std::array<std::string, 16> iecChannelOpenType = {};
    std::array<std::string, 16> iecChannelOpenMode = {};

    bool iecDirectoryFromBlockBuffer = false;
    std::string iecDirectoryWildcardPattern;
    std::string iecDirectoryTypeFilter;
    std::string iecDirectoryModeFilter;
    bool iecDirectoryModeFilterNegated = false;

    std::string mountedImagePath;
    std::string mountedImageFormat;
    bool mountedImageConfigured = false;
    bool mountedImageExists = false;
    std::shared_ptr<IImageBackend> mountedImageBackend;
    std::shared_ptr<IImageBackend> mountedImageLogicalBackend;
    std::shared_ptr<IFluxImageBackend> mountedFluxImageBackend;

    std::shared_ptr<IImageBackend> createLogicalBackendForMountedImage() const {
        if (mountedImageFormat == "d64") {
            return std::make_shared<D64ImageBackend>(mountedImagePath);
        }
        if (mountedImageFormat == "g64") {
            return std::make_shared<G64ImageBackend>(mountedImagePath);
        }
        if (mountedImageFormat == "nib") {
            return std::make_shared<NIBImageBackend>(mountedImagePath);
        }
        if (mountedImageFormat == "raw") {
            return std::make_shared<RAWImageBackend>(mountedImagePath);
        }
        return nullptr;
    }

    std::shared_ptr<IFluxImageBackend> createFluxBackendForMountedImage() const {
        if (mountedImageFormat == "g64") {
            return std::make_shared<G64FluxImageBackend>(mountedImagePath);
        }
        if (mountedImageFormat == "nib") {
            return std::make_shared<NIBFluxImageBackend>(mountedImagePath);
        }
        if (mountedImageFormat == "raw") {
            return std::make_shared<RAWFluxImageBackend>(mountedImagePath);
        }
        return nullptr;
    }

    void applyMountedBackendRouting() {
        mountedImageBackend = mountedImageLogicalBackend;
        if (!mountedImageBackend) {
            return;
        }

        if (!advanced_image_detail::shouldUseFluxLayer(mountedImageFormat, physicalProfile)) {
            return;
        }

        if (mountedFluxImageBackend && mountedFluxImageBackend->isReady()) {
            mountedImageBackend = mountedFluxImageBackend;
        }
    }

    void configureMountedImage(const std::string &path, const std::string &format, bool exists) {
        mountedImageConfigured = !path.empty();
        mountedImagePath = path;
        mountedImageFormat = format;
        mountedImageExists = exists;
        mountedImageBackend.reset();
        mountedImageLogicalBackend.reset();
        mountedFluxImageBackend.reset();
        if (!mountedImageConfigured || !mountedImageExists) {
            return;
        }
        mountedImageLogicalBackend = createLogicalBackendForMountedImage();
        if (advanced_image_detail::isFluxCapableFormat(mountedImageFormat)) {
            mountedFluxImageBackend = createFluxBackendForMountedImage();
        }
        applyMountedBackendRouting();
    }

    // Drive CPU scaffold state (placeholder for real core)
    bool cpuEnabled = false;
    uint8_t cpuLastOpcode = 0;
    uint16_t cpuLastFetchAddr = 0;
    uint64_t cpuStepCount = 0;
    uint8_t cpuCyclesToNext = 0;
    bool cpuReadyEdge = false;

    uint64_t iecCommandDispatchCount = 0;
    uint64_t iecDataDispatchCount = 0;
    uint64_t iecCommandSyntaxErrorCount = 0;

    // Week68 phase-1 ownership counters.
    // These counters do not claim full firmware ownership yet: they provide a deterministic
    // observability layer while command/status handling transitions from scaffold-only flow to
    // CPU/VIA-acknowledged flow for command channel operations.
    bool iecCpuOwnsCommandPath = false;
    uint64_t iecCpuOwnedCommandRows = 0;
    uint64_t iecScaffoldFallbackRows = 0;
    uint64_t iecCmdStatusDivergenceRows = 0;
    uint64_t iecCpuOwnershipCutoverTransitions = 0;

    std::array<uint64_t, 16> iecChannelOpenCount = {0};
    std::array<uint64_t, 16> iecChannelCloseCount = {0};

    uint8_t cpuA = 0;
    uint8_t cpuX = 0;
    uint8_t cpuY = 0;
    uint8_t cpuSP = 0xFF;
    uint8_t cpuP = 0x24;
    C64PowerState c64PowerState = C64PowerState::On;

    enum class DriveCpuMicroOpPhase : uint8_t {
        Fetch = 0,
        Decode = 1,
        Execute = 2,
        Writeback = 3,
        Complete = 4
    };

    struct DriveCpuMicroOpState {
        bool active = false;
        DriveCpuMicroOpPhase phase = DriveCpuMicroOpPhase::Fetch;
        uint8_t ir = 0;
        uint8_t microPc = 0;
        uint8_t operandLo = 0;
        uint8_t operandHi = 0;
        uint8_t dataLatch = 0;
        uint16_t effectiveAddr = 0;
        bool pageCrossPenaltyPending = false;
        uint64_t microOpsExecuted = 0;
        uint64_t busCyclesExecuted = 0;
        uint8_t cyclesConsumed = 0;
    };

    bool driveCpuUseMicroOpEngine = false;
    DriveCpuMicroOpState driveCpuMicroOpState;

    void resetDriveCpuMicroOpState() {
        driveCpuMicroOpState = DriveCpuMicroOpState{};
    }

    void setCpuZN(uint8_t value) {
        if (value == 0) {
            cpuP |= 0x02;
        } else {
            cpuP &= static_cast<uint8_t>(~0x02);
        }
        if ((value & 0x80) != 0) {
            cpuP |= 0x80;
        } else {
            cpuP &= static_cast<uint8_t>(~0x80);
        }
    }

    void setCpuCompareFlags(uint8_t lhs, uint8_t rhs) {
        const uint16_t diff = static_cast<uint16_t>(lhs) - static_cast<uint16_t>(rhs);
        if (lhs >= rhs) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        setCpuZN(static_cast<uint8_t>(diff & 0xFF));
    }

    void setCpuOverflowFromAdd(uint8_t lhs, uint8_t rhs, uint8_t result) {
        if (((~(lhs ^ rhs) & (lhs ^ result)) & 0x80) != 0) {
            cpuP |= 0x40;
        } else {
            cpuP &= static_cast<uint8_t>(~0x40);
        }
    }

    void opAdc(uint8_t rhs) {
        const uint16_t carryIn = ((cpuP & 0x01) != 0) ? 1u : 0u;
        const uint16_t sum = static_cast<uint16_t>(cpuA) + static_cast<uint16_t>(rhs) + carryIn;
        const uint8_t result = static_cast<uint8_t>(sum & 0xFFu);
        if (sum > 0xFFu) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        setCpuOverflowFromAdd(cpuA, rhs, result);
        cpuA = result;
        setCpuZN(cpuA);
    }

    void opSbc(uint8_t rhs) {
        opAdc(static_cast<uint8_t>(rhs ^ 0xFFu));
    }

    void opBit(uint8_t rhs) {
        const uint8_t andValue = static_cast<uint8_t>(cpuA & rhs);
        if (andValue == 0) {
            cpuP |= 0x02;
        } else {
            cpuP &= static_cast<uint8_t>(~0x02);
        }
        if ((rhs & 0x80) != 0) {
            cpuP |= 0x80;
        } else {
            cpuP &= static_cast<uint8_t>(~0x80);
        }
        if ((rhs & 0x40) != 0) {
            cpuP |= 0x40;
        } else {
            cpuP &= static_cast<uint8_t>(~0x40);
        }
    }

    uint16_t read16ZeroPageWrap(uint8_t zpAddr) {
        const uint8_t lo = read(zpAddr);
        const uint8_t hi = read(static_cast<uint8_t>(zpAddr + 1));
        return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
    }

    uint8_t opAslValue(uint8_t value) {
        if ((value & 0x80) != 0) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        const uint8_t result = static_cast<uint8_t>(value << 1);
        setCpuZN(result);
        return result;
    }

    uint8_t opLsrValue(uint8_t value) {
        if ((value & 0x01) != 0) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        const uint8_t result = static_cast<uint8_t>(value >> 1);
        setCpuZN(result);
        return result;
    }

    uint8_t opRolValue(uint8_t value) {
        const uint8_t carryIn = ((cpuP & 0x01) != 0) ? 1 : 0;
        if ((value & 0x80) != 0) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        const uint8_t result = static_cast<uint8_t>((value << 1) | carryIn);
        setCpuZN(result);
        return result;
    }

    uint8_t opRorValue(uint8_t value) {
        const uint8_t carryIn = ((cpuP & 0x01) != 0) ? 0x80 : 0;
        if ((value & 0x01) != 0) {
            cpuP |= 0x01;
        } else {
            cpuP &= static_cast<uint8_t>(~0x01);
        }
        const uint8_t result = static_cast<uint8_t>((value >> 1) | carryIn);
        setCpuZN(result);
        return result;
    }

    bool executeDriveCpuMicroOpBaseOpcode(uint8_t op, uint8_t &cyclesUsed) {
        cyclesUsed = 2;
        switch (op) {
            case 0xEA:
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x78:
                cpuP |= 0x04;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x58:
                cpuP &= static_cast<uint8_t>(~0x04);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xD8:
                cpuP &= static_cast<uint8_t>(~0x08);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xF8:
                cpuP |= 0x08;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x18:
                cpuP &= static_cast<uint8_t>(~0x01);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x38:
                cpuP |= 0x01;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xA9:
                cpuA = read(static_cast<uint16_t>(pc + 1));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xA5:
                cpuA = read(read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xB5:
                cpuA = read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0xA1: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                cpuA = read(read16ZeroPageWrap(zpPtr));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0xAD:
                cpuA = read(read16(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xB1: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = read(addr);
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xBD: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                cpuA = read(addr);
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xB9: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = read(addr);
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xA2:
                cpuX = read(static_cast<uint16_t>(pc + 1));
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xA6:
                cpuX = read(read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xB6:
                cpuX = read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuY));
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0xAE:
                cpuX = read(read16(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xBE: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuX = read(addr);
                setCpuZN(cpuX);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xA0:
                cpuY = read(static_cast<uint16_t>(pc + 1));
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xA4:
                cpuY = read(read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xB4:
                cpuY = read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX));
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0xAC:
                cpuY = read(read16(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xBC: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                cpuY = read(addr);
                setCpuZN(cpuY);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x85:
                write(read(static_cast<uint16_t>(pc + 1)), cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x95:
                write(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX), cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x81: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(read16ZeroPageWrap(zpPtr), cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x8D:
                write(read16(static_cast<uint16_t>(pc + 1)), cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x91: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(read16ZeroPageWrap(zpPtr) + cpuY);
                write(addr, cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x9D:
                write(static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX), cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 5;
                return true;
            case 0x99:
                write(static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuY), cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 5;
                return true;
            case 0x86:
                write(read(static_cast<uint16_t>(pc + 1)), cpuX);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x96:
                write(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuY), cpuX);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x8E:
                write(read16(static_cast<uint16_t>(pc + 1)), cpuX);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x84:
                write(read(static_cast<uint16_t>(pc + 1)), cpuY);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x94:
                write(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX), cpuY);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x8C:
                write(read16(static_cast<uint16_t>(pc + 1)), cpuY);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xAA:
                cpuX = cpuA;
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xA8:
                cpuY = cpuA;
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x8A:
                cpuA = cpuX;
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x98:
                cpuA = cpuY;
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xBA:
                cpuX = cpuSP;
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x9A:
                cpuSP = cpuX;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xE8:
                cpuX = static_cast<uint8_t>(cpuX + 1);
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xC8:
                cpuY = static_cast<uint8_t>(cpuY + 1);
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0xCA:
                cpuX = static_cast<uint8_t>(cpuX - 1);
                setCpuZN(cpuX);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x88:
                cpuY = static_cast<uint8_t>(cpuY - 1);
                setCpuZN(cpuY);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x09:
                cpuA = static_cast<uint8_t>(cpuA | read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0x05:
                cpuA = static_cast<uint8_t>(cpuA | read(read(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x15:
                cpuA = static_cast<uint8_t>(cpuA | read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x0D:
                cpuA = static_cast<uint8_t>(cpuA | read(read16(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x1D: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                cpuA = static_cast<uint8_t>(cpuA | read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x19: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA | read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x01: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                cpuA = static_cast<uint8_t>(cpuA | read(read16ZeroPageWrap(zpPtr)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x11: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA | read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x29:
                cpuA = static_cast<uint8_t>(cpuA & read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0x25:
                cpuA = static_cast<uint8_t>(cpuA & read(read(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x35:
                cpuA = static_cast<uint8_t>(cpuA & read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x2D:
                cpuA = static_cast<uint8_t>(cpuA & read(read16(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x3D: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                cpuA = static_cast<uint8_t>(cpuA & read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x39: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA & read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x21: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                cpuA = static_cast<uint8_t>(cpuA & read(read16ZeroPageWrap(zpPtr)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x31: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA & read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x49:
                cpuA = static_cast<uint8_t>(cpuA ^ read(static_cast<uint16_t>(pc + 1)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0x45:
                cpuA = static_cast<uint8_t>(cpuA ^ read(read(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x55:
                cpuA = static_cast<uint8_t>(cpuA ^ read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x4D:
                cpuA = static_cast<uint8_t>(cpuA ^ read(read16(static_cast<uint16_t>(pc + 1))));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x5D: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                cpuA = static_cast<uint8_t>(cpuA ^ read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x59: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA ^ read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x41: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                cpuA = static_cast<uint8_t>(cpuA ^ read(read16ZeroPageWrap(zpPtr)));
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x51: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                cpuA = static_cast<uint8_t>(cpuA ^ read(addr));
                setCpuZN(cpuA);
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xC9:
                setCpuCompareFlags(cpuA, read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xC5:
                setCpuCompareFlags(cpuA, read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xD5:
                setCpuCompareFlags(cpuA, read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0xCD:
                setCpuCompareFlags(cpuA, read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xDD: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                setCpuCompareFlags(cpuA, read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xD9: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                setCpuCompareFlags(cpuA, read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xC1: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                setCpuCompareFlags(cpuA, read(read16ZeroPageWrap(zpPtr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0xD1: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                setCpuCompareFlags(cpuA, read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xE0:
                setCpuCompareFlags(cpuX, read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xE4:
                setCpuCompareFlags(cpuX, read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xEC:
                setCpuCompareFlags(cpuX, read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xC0:
                setCpuCompareFlags(cpuY, read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xC4:
                setCpuCompareFlags(cpuY, read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xCC:
                setCpuCompareFlags(cpuY, read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xB8:
                cpuP &= static_cast<uint8_t>(~0x40);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x0A:
                cpuA = opAslValue(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x06: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                write(addr, opAslValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0x0E: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                write(addr, opAslValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0x16: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opAslValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x1E: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opAslValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0x4A:
                cpuA = opLsrValue(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x46: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                write(addr, opLsrValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0x4E: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                write(addr, opLsrValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0x56: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opLsrValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x5E: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opLsrValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0x2A:
                cpuA = opRolValue(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x26: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                write(addr, opRolValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0x2E: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                write(addr, opRolValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0x36: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opRolValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x3E: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opRolValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0x6A:
                cpuA = opRorValue(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                return true;
            case 0x66: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                write(addr, opRorValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0x6E: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                write(addr, opRorValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0x76: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opRorValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x7E: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                write(addr, opRorValue(read(addr)));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0xE6: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                const uint8_t value = static_cast<uint8_t>(read(addr) + 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0xF6: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                const uint8_t value = static_cast<uint8_t>(read(addr) + 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0xEE: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                const uint8_t value = static_cast<uint8_t>(read(addr) + 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0xFE: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                const uint8_t value = static_cast<uint8_t>(read(addr) + 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0xC6: {
                const uint16_t addr = read(static_cast<uint16_t>(pc + 1));
                const uint8_t value = static_cast<uint8_t>(read(addr) - 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 5;
                return true;
            }
            case 0xD6: {
                const uint16_t addr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                const uint8_t value = static_cast<uint8_t>(read(addr) - 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0xCE: {
                const uint16_t addr = read16(static_cast<uint16_t>(pc + 1));
                const uint8_t value = static_cast<uint8_t>(read(addr) - 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 6;
                return true;
            }
            case 0xDE: {
                const uint16_t addr = static_cast<uint16_t>(read16(static_cast<uint16_t>(pc + 1)) + cpuX);
                const uint8_t value = static_cast<uint8_t>(read(addr) - 1);
                write(addr, value);
                setCpuZN(value);
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 7;
                return true;
            }
            case 0x48:
                push(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 3;
                return true;
            case 0x68:
                cpuA = pull();
                setCpuZN(cpuA);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 4;
                return true;
            case 0x08:
                push(static_cast<uint8_t>(cpuP | 0x30));
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 3;
                return true;
            case 0x28:
                cpuP = static_cast<uint8_t>((pull() & 0xEF) | 0x20);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 4;
                return true;
            case 0x69:
                opAdc(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0x75:
                opAdc(read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0x7D: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                opAdc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x79: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                opAdc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x61: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                opAdc(read(read16ZeroPageWrap(zpPtr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0x71: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                opAdc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0x65:
                opAdc(read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x6D:
                opAdc(read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0xE9:
                opSbc(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                return true;
            case 0xF5:
                opSbc(read(static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 4;
                return true;
            case 0xFD: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuX);
                opSbc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xF9: {
                const uint16_t base = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                opSbc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = static_cast<uint8_t>(4 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xE1: {
                const uint8_t zpPtr = static_cast<uint8_t>(read(static_cast<uint16_t>(pc + 1)) + cpuX);
                opSbc(read(read16ZeroPageWrap(zpPtr)));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 6;
                return true;
            }
            case 0xF1: {
                const uint8_t zpPtr = read(static_cast<uint16_t>(pc + 1));
                const uint16_t base = read16ZeroPageWrap(zpPtr);
                const uint16_t addr = static_cast<uint16_t>(base + cpuY);
                opSbc(read(addr));
                driveCpuMicroOpState.pageCrossPenaltyPending = ((base & 0xFF00u) != (addr & 0xFF00u));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = static_cast<uint8_t>(5 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                return true;
            }
            case 0xE5:
                opSbc(read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0xED:
                opSbc(read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x24:
                opBit(read(read(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 3;
                return true;
            case 0x2C:
                opBit(read(read16(static_cast<uint16_t>(pc + 1))));
                pc = static_cast<uint16_t>(pc + 3);
                cyclesUsed = 4;
                return true;
            case 0x4C:
                pc = read16(static_cast<uint16_t>(pc + 1));
                cyclesUsed = 3;
                return true;
            case 0x6C: {
                const uint16_t ptr = read16(static_cast<uint16_t>(pc + 1));
                const uint8_t lo = read(ptr);
                const uint16_t hiAddr = static_cast<uint16_t>((ptr & 0xFF00u) | ((ptr + 1) & 0x00FFu));
                const uint8_t hi = read(hiAddr);
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                cyclesUsed = 5;
                return true;
            }
            case 0x20: {
                const uint16_t target = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t ret = static_cast<uint16_t>(pc + 2);
                push(static_cast<uint8_t>((ret >> 8) & 0xFF));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                cyclesUsed = 6;
                return true;
            }
            case 0x60: {
                const uint8_t lo = pull();
                const uint8_t hi = pull();
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 6;
                return true;
            }
            case 0xD0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0xF0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0x90: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x01) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0xB0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x01) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0x10: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x80) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0x30: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x80) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0x50: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x40) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            case 0x70: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                const uint16_t oldPc = static_cast<uint16_t>(pc + 2);
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x40) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    driveCpuMicroOpState.pageCrossPenaltyPending = ((oldPc & 0xFF00u) != (pc & 0xFF00u));
                    cyclesUsed = static_cast<uint8_t>(3 + (driveCpuMicroOpState.pageCrossPenaltyPending ? 1 : 0));
                } else {
                    driveCpuMicroOpState.pageCrossPenaltyPending = false;
                    cyclesUsed = 2;
                }
                return true;
            }
            default:
                return false;
        }
    }

    bool stepDriveCpuMicroOpScaffold() {
        if (!driveCpuUseMicroOpEngine) {
            return false;
        }

        const uint16_t oldPc = pc;
        if (!driveCpuMicroOpState.active) {
            driveCpuMicroOpState.active = true;
            driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Fetch;
            driveCpuMicroOpState.ir = read(pc);
            driveCpuMicroOpState.microPc = 0;
            driveCpuMicroOpState.cyclesConsumed = 0;
            driveCpuMicroOpState.pageCrossPenaltyPending = false;
        }

        driveCpuMicroOpState.microOpsExecuted++;

        if (driveCpuMicroOpState.phase == DriveCpuMicroOpPhase::Fetch) {
            driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Decode;
            driveCpuMicroOpState.microPc++;
            driveCpuMicroOpState.busCyclesExecuted++;
            return true;
        }

        if (driveCpuMicroOpState.phase == DriveCpuMicroOpPhase::Decode) {
            driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Execute;
            driveCpuMicroOpState.microPc++;
            return true;
        }

        if (driveCpuMicroOpState.phase == DriveCpuMicroOpPhase::Execute) {
            uint8_t cyclesUsed = 2;
            const bool handled = executeDriveCpuMicroOpBaseOpcode(driveCpuMicroOpState.ir, cyclesUsed);
            if (!handled) {
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
            }
            if (pc == oldPc) {
                pc = static_cast<uint16_t>(pc + 1);
            }
            driveCpuMicroOpState.cyclesConsumed = cyclesUsed;
            driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Writeback;
            driveCpuMicroOpState.microPc++;
            driveCpuMicroOpState.busCyclesExecuted += cyclesUsed;
            return true;
        }

        if (driveCpuMicroOpState.phase == DriveCpuMicroOpPhase::Writeback) {
            const uint8_t scale = (revisionProfile.cpuCyclesPerStep == 0) ? 1 : revisionProfile.cpuCyclesPerStep;
            const uint8_t scaledConsumed = static_cast<uint8_t>((driveCpuMicroOpState.cyclesConsumed / scale) + ((driveCpuMicroOpState.cyclesConsumed % scale) ? 1 : 0));
            cpuCyclesToNext = (scaledConsumed > 0) ? static_cast<uint8_t>(scaledConsumed - 1) : 0;
            cpuLastOpcode = driveCpuMicroOpState.ir;
            cpuLastFetchAddr = oldPc;
            cpuStepCount++;
            driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Complete;
            driveCpuMicroOpState.microPc++;
            return true;
        }

        driveCpuMicroOpState.active = false;
        driveCpuMicroOpState.phase = DriveCpuMicroOpPhase::Fetch;
        driveCpuMicroOpState.microPc = 0;
        return true;
    }

    bool loadRom(const std::string &romPath) {
        std::ifstream in(romPath, std::ios::binary);
        if (!in.is_open()) {
            romLoaded = false;
            return false;
        }

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() != 16384) {
            romLoaded = false;
            return false;
        }

        for (size_t i = 0; i < data.size(); ++i) {
            memory[0xC000 + i] = data[i];
        }

        romLoaded = true;
        bindPhysicalDosMemoryMap();
        physicalDosMemoryMap.set_rom(&memory[0xC000], 0x4000);
        physicalDosMemoryMap.seed_ram(&memory[0x0000], 0xC000);

        reset();
        return true;
    }

    void reset() {
        cycles = 0;
        physicalScheduler.reset();
        physicalIecPort.reset();
        signalModel.reset();
        physicalViaDomain.bind_external(&via1, &via2);
        physicalViaDomain.reset();
        bindPhysicalDosMemoryMap();
        physicalDosMemoryMap.set_rom(&memory[0xC000], 0x4000);
        physicalDosMemoryMap.seed_ram(&memory[0x0000], 0xC000);
        bindPhysicalCpuDomain();
        physicalCpuDomain.reset();
        pc = static_cast<uint16_t>(memory[0xFFFC] | (uint16_t(memory[0xFFFD]) << 8));
        iecListening = false;
        iecTalking = false;
        iecCommandSeen = false;
        lastIecCommand = 0;
        cpuLastOpcode = 0;
        cpuLastFetchAddr = pc;
        cpuStepCount = 0;
        cpuCyclesToNext = 0;
        cpuReadyEdge = false;
        cpuA = 0;
        cpuX = 0;
        cpuY = 0;
        cpuSP = 0xFF;
        cpuP = 0x24;
        resetDriveCpuMicroOpState();
        driveCpuUseMicroOpEngine = (physicalProfile == PhysicalProfile::Level5Coupling);

        iecListenSecondary = 0xFF;
        iecTalkSecondary = 0xFF;
        iecExpectingNameBytes = false;
        iecDirectoryStubPrepared = false;
        iecNameBuffer.clear();
        iecRxQueue.clear();
        iecTxQueue.clear();
        iecRxProcessed = 0;
        iecTxServed = 0;
        iecEoiAckCount = 0;
        iecAtnFallingSeen = 0;
        iecClockRisingSeen = 0;
        iecClockRisingAtnLow = 0;
        iecRequireFreshCommandAfterC64Resume = false;
        iecCommandRearmTransitions = 0;
        driveLedMotorOn = false;
        driveLedActivity = false;
        driveLedError = false;
        driveLedIecLoad = false;

        iecPrevCLK = true;
        iecPrevATN = true;
        iecPrevDATA = true;
        const bool level5CouplingProfile = (physicalProfile == PhysicalProfile::Level5Coupling);
        iecEnableAtnAck = level5CouplingProfile;
        iecEnableListenerByteAck = level5CouplingProfile;
        iecKernelCompatSampleBothClockEdges = level5CouplingProfile;
        iecKernelSampleOnFallingClockEdge = false;
        iecKernelSampleBothCommandEdges = level5CouplingProfile;
        iecKernelCompatAutoTalkDirectory = false;
        iecKernelCompatAutoDirectoryOnTalk0 = false;
        iecKernelCompatForceTalkOnIcrSerial = false;
        iecKernelIgnoreAtnForTalkDataPhase = false;
        iecSerialState = IecSerialState::Idle;
        iecAtnAckTicks = 0;
        iecAtnAckPullDATA = false;
        iecAtnHandshakeActive = false;
        iecAtnAckSawClockLow = false;
        iecRxByteAckTicks = 0;
        iecRxByteAckPullDATA = false;
        iecRxShift = 0;
        iecRxBitCount = 0;
        iecCommandFrameSync = false;
        iecCommandSawStartEdge = false;
        iecCommandFrameByteCount = 0;
        iecCommandFrameSawListenForDevice = false;
        iecCommandFrameSawTalkForDevice = false;
        iecCommandFrameSawListenSecondary0 = false;
        iecCommandFrameSawTalkSecondary0 = false;
        iecTxByteActive = false;
        iecTxShift = 0;
        iecTxBitCount = 0;
        iecTalkStartPending = false;
        iecTalkStartByte = 0;
        iecTalkStartIsEoi = false;
        iecTalkSa0Confirmed = false;
        iecTalkFrameArmed = false;
        iecTalkSawStartEdge = false;
        iecSerialPullDATA = false;
        iecTxCurrentIsEoi = false;
        iecEoiPendingAck = false;
        iecEoiAckLowSeen = false;

        iecOpenListenChannels.fill(false);
        iecOpenTalkChannels.fill(false);
        iecActiveListenChannel = 0xFF;
        iecActiveTalkChannel = 0xFF;

        iecRxIdleTicks = 0;
        iecTxIdleTicks = 0;
        iecEoiWaitTicks = 0;
        iecRxTimeoutCount = 0;
        iecTxTimeoutCount = 0;
        iecEoiTimeoutCount = 0;

        iecStatusLine = "00,OK,00,00";
        iecCommandChannelBuffer.clear();
        iecCommandResponseQueue.clear();
        iecLastExecuteAddr = 0;
        iecLastBlockTrack = 0;
        iecLastBlockSector = 0;
        iecLastBlockChannel = 0xFF;
        iecLastBlockCommand.clear();
        iecLastExecDispatch.clear();
        iecExecDispatchCount = 0;
        iecCommandDispatchCount = 0;
        iecDataDispatchCount = 0;
        iecCommandSyntaxErrorCount = 0;
        iecCpuOwnsCommandPath = false;
        iecCpuOwnedCommandRows = 0;
        iecScaffoldFallbackRows = 0;
        iecCmdStatusDivergenceRows = 0;
        iecCpuOwnershipCutoverTransitions = 0;
        iecChannelOpenCount.fill(0);
        iecChannelCloseCount.fill(0);

        iecBlockBuffer.fill(0);
        iecBlockBufferValid = false;
        iecBlockBufferTrack = 0;
        iecBlockBufferSector = 0;
        iecDiskMap.fill(0);
        iecChannelBufferPos.fill(0);
        iecChannelPointerValid.fill(false);
        iecDirectoryFromBlockBuffer = false;
        iecDirectoryWildcardPattern.clear();
        iecDirectoryTypeFilter.clear();
        iecDirectoryModeFilter.clear();
        iecDirectoryModeFilterNegated = false;
        iecBlockAllocated.fill(0);
        iecAllocOwnerEntry.fill(0xFF);
        iecAllocatedBlockCount = 0;
        iecChannelCatalogEntry.fill(-1);
        iecChannelOpenName.fill(std::string());
        iecChannelOpenNameValid.fill(false);
        iecChannelOpenType.fill(std::string("PRG"));
        iecChannelOpenMode.fill(std::string());
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            iecCatalog[i] = VirtualCatalogEntry{};
        }

        physicalPipelineInitialized = false;
        physicalPipelineAbsTick = 0;
        physicalPipelineRuns = 0;
        physicalPipelineLastCellTicks = 0;
        physicalPipelineLastFluxEdge = false;
        physicalPipelineLastByte = 0;
        physicalPipelineLastBit = 0;
        physicalPipelineLastHalfTrack = 36;
        physicalPipelineLastAngleNorm = 0.0;
    }

    uint8_t read(uint16_t addr) {
        return physicalDosMemoryMap.read(addr);
    }

    void write(uint16_t addr, uint8_t val) {
        physicalDosMemoryMap.write(addr, val);
        if (addr < 0xC000) {
            memory[addr] = val;
        }
    }

    void bindPhysicalDosMemoryMap() {
        physicalDosMemoryMap.bind_io(
            [this](uint16_t ioAddr) -> uint8_t {
                return physicalViaDomain.read_io(ioAddr);
            },
            [this](uint16_t ioAddr, uint8_t v) {
                physicalViaDomain.write_io(ioAddr, v);
            }
        );
    }

    void bindPhysicalCpuDomain() {
        physicalCpuDomain.bind_bus(
            [this](uint16_t addr) -> uint8_t {
                return physicalDosMemoryMap.read(addr);
            },
            [this](uint16_t addr, uint8_t val) {
                physicalDosMemoryMap.write(addr, val);
                if (addr < 0xC000u) {
                    memory[addr] = val;
                }
            }
        );
    }

    void tick() {
        tickIecHalfCycle();
    }

    void tickIecHalfCycle() override {
        const bool wasPoweredOn = powerController.isOn();
        const drive1541_physical::IecLines hostLinesBefore = resolveHostLinesForPowerMatrix(iecATN, iecCLK, iecDATA);
        powerController.tick(1);
        const bool isPoweredOn = powerController.isOn();
        const bool allowDriveOutput = isDriveOutputAllowedForPowerMatrix();
        signalModel.begin_tick(powerController.state());
        if (wasPoweredOn && !allowDriveOutput) {
            physicalIecPort.dropPendingDriveEdges();
            physicalIecPort.setDriveOutput({true, true, true});
        }

        const uint64_t nowAfterPowerTick = physicalScheduler.now();
        physicalIecPort.queueHostLines(nowAfterPowerTick, hostLinesBefore);
        const std::size_t preAppliedEdges = physicalIecPort.applyReady(nowAfterPowerTick, allowDriveOutput);
        signalModel.note_iec_edges(preAppliedEdges);
        {
            const drive1541_physical::IecLines hostIn = physicalIecPort.busInput();
            iecATN = hostIn.atn;
            iecCLK = hostIn.clk;
            iecDATA = hostIn.data;
        }

        if (!allowDriveOutput) {
            iecDrivePullCLK = false;
            iecDrivePullDATA = false;
            const drive1541_physical::DriveSignalState sig = signalModel.state();
            driveLedMotorOn = sig.motor_on;
            driveLedActivity = sig.activity;
            driveLedError = sig.error;
            driveLedIecLoad = sig.iec_load;
            return;
        }

        cycles++;
        physicalScheduler.tickHostCycles(1);

        physicalViaDomain.tick(1);
        physicalCpuDomain.set_irq(physicalViaDomain.irq_asserted());

        processQueuedIecRxBurst();
        if (pendingIecRx() > 0 || pendingIecTx() > 0) {
            signalModel.note_dos_busy();
        }

        stepIecSerial();

        // Minimal IEC drive-side behavior from VIA1 PB6/PB5 as CLK/DATA outputs.
        // This is only a scaffold and not a full 1541 VIA behavior model yet.
        const uint8_t viaPrb = via1.regs[0x00];
        const uint8_t viaDdrb = via1.regs[0x02];
        bool viaPullCLK = false;
        bool viaPullDATA = false;
        if (viaDdrb & 0x40) {
            viaPullCLK = (viaPrb & 0x40) == 0;
        }
        if (viaDdrb & 0x20) {
            viaPullDATA = (viaPrb & 0x20) == 0;
        }

        // During ATN command phase, the host clocks command bytes; keep drive
        // CLK/DATA outputs released except explicit handshake pulls.
        if (iecSerialState == IecSerialState::Command) {
            viaPullCLK = false;
            viaPullDATA = false;
        }

        iecDrivePullCLK = viaPullCLK;
        iecDrivePullDATA = (viaPullDATA || iecSerialPullDATA || iecAtnAckPullDATA || iecRxByteAckPullDATA);

        const drive1541_physical::IecLines driveOut = {
            true,
            !iecDrivePullCLK,
            !iecDrivePullDATA
        };
        const uint64_t now = physicalScheduler.now();
        physicalIecPort.queueDriveLines(now, driveOut);
        const std::size_t postAppliedEdges = physicalIecPort.applyReady(now, allowDriveOutput);
        signalModel.note_iec_edges(postAppliedEdges);

        signalModel.note_status_line(iecStatusLine.c_str());
        const drive1541_physical::DriveSignalState sig = signalModel.state();
        driveLedMotorOn = sig.motor_on;
        driveLedActivity = sig.activity;
        driveLedError = sig.error;
        driveLedIecLoad = sig.iec_load;

        stepDriveCpuCycleAccurate();
    }

    void processQueuedIecRxBurst() {
        uint32_t budget = 0;
        while (!iecRxQueue.empty() && budget < 4) {
            const IecRxEvent ev = iecRxQueue.front();
            iecRxQueue.pop_front();
            consumeReceivedByte(ev.byte, ev.isCommand);
            iecRxProcessed++;
            budget++;
        }
    }

    void stepDriveCpuCycleAccurate() {
        if (!(cpuEnabled && romLoaded)) {
            return;
        }

        if (stepDriveCpuMicroOpScaffold()) {
            if (pc < 0xC000) {
                pc = static_cast<uint16_t>(memory[0xFFFC] | (uint16_t(memory[0xFFFD]) << 8));
            }
            return;
        }

        cpuReadyEdge = !cpuReadyEdge;
        if (!cpuReadyEdge) {
            return;
        }

        if (cpuCyclesToNext > 0) {
            cpuCyclesToNext = static_cast<uint8_t>(cpuCyclesToNext - 1);
            return;
        }

        cpuLastFetchAddr = pc;
        cpuLastOpcode = read(pc);
        const uint8_t consumed = stepCpuScaffoldCycleAccurate(cpuLastOpcode);
        const uint8_t scale = (revisionProfile.cpuCyclesPerStep == 0) ? 1 : revisionProfile.cpuCyclesPerStep;
        const uint8_t scaledConsumed = static_cast<uint8_t>((consumed / scale) + ((consumed % scale) ? 1 : 0));
        cpuCyclesToNext = (scaledConsumed > 0) ? static_cast<uint8_t>(scaledConsumed - 1) : 0;
        cpuStepCount++;

        if (pc < 0xC000) {
            pc = static_cast<uint16_t>(memory[0xFFFC] | (uint16_t(memory[0xFFFD]) << 8));
        }
    }

    uint16_t read16(uint16_t addr) const {
        const uint8_t lo = memory[addr];
        const uint8_t hi = memory[static_cast<uint16_t>(addr + 1)];
        return static_cast<uint16_t>(lo | (uint16_t(hi) << 8));
    }

    void push(uint8_t v) {
        memory[0x0100 | cpuSP] = v;
        cpuSP = static_cast<uint8_t>(cpuSP - 1);
    }

    uint8_t pull() {
        cpuSP = static_cast<uint8_t>(cpuSP + 1);
        return memory[0x0100 | cpuSP];
    }

    uint8_t stepCpuScaffoldCycleAccurate(uint8_t op) {
        const uint16_t oldPC = pc;
        uint8_t cyclesUsed = 2;

        switch (op) {
            case 0xEA:
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x78:
                cpuP |= 0x04;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x58:
                cpuP &= static_cast<uint8_t>(~0x04);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xD8:
                cpuP &= static_cast<uint8_t>(~0x08);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xF8:
                cpuP |= 0x08;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x18:
                cpuP &= static_cast<uint8_t>(~0x01);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0x38:
                cpuP |= 0x01;
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
            case 0xA9:
                cpuA = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0xA2:
                cpuX = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0xA0:
                cpuY = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                cyclesUsed = 2;
                break;
            case 0x4C:
                pc = read16(static_cast<uint16_t>(pc + 1));
                cyclesUsed = 3;
                break;
            case 0x20: {
                const uint16_t target = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t ret = static_cast<uint16_t>(pc + 2);
                push(static_cast<uint8_t>((ret >> 8) & 0xFF));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                cyclesUsed = 6;
                break;
            }
            case 0x60: {
                const uint8_t lo = pull();
                const uint8_t hi = pull();
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 6;
                break;
            }
            case 0xD0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    cyclesUsed = 3;
                } else {
                    cyclesUsed = 2;
                }
                break;
            }
            case 0xF0: {
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                    cyclesUsed = 3;
                } else {
                    cyclesUsed = 2;
                }
                break;
            }
            default:
                pc = static_cast<uint16_t>(pc + 1);
                cyclesUsed = 2;
                break;
        }

        if (pc == oldPC) {
            pc = static_cast<uint16_t>(pc + 1);
        }
        return cyclesUsed;
    }

    void stepCpuScaffold() {
        const uint8_t op = read(pc);
        const uint16_t oldPC = pc;

        switch (op) {
            case 0xEA: // NOP
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x78: // SEI
                cpuP |= 0x04;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x58: // CLI
                cpuP &= static_cast<uint8_t>(~0x04);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xD8: // CLD
                cpuP &= static_cast<uint8_t>(~0x08);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xF8: // SED
                cpuP |= 0x08;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x18: // CLC
                cpuP &= static_cast<uint8_t>(~0x01);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0x38: // SEC
                cpuP |= 0x01;
                pc = static_cast<uint16_t>(pc + 1);
                break;
            case 0xA9: // LDA #
                cpuA = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0xA2: // LDX #
                cpuX = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0xA0: // LDY #
                cpuY = read(static_cast<uint16_t>(pc + 1));
                pc = static_cast<uint16_t>(pc + 2);
                break;
            case 0x4C: // JMP abs
                pc = read16(static_cast<uint16_t>(pc + 1));
                break;
            case 0x20: { // JSR abs
                const uint16_t target = read16(static_cast<uint16_t>(pc + 1));
                const uint16_t ret = static_cast<uint16_t>(pc + 2);
                push(static_cast<uint8_t>((ret >> 8) & 0xFF));
                push(static_cast<uint8_t>(ret & 0xFF));
                pc = target;
                break;
            }
            case 0x60: { // RTS
                const uint8_t lo = pull();
                const uint8_t hi = pull();
                pc = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
                pc = static_cast<uint16_t>(pc + 1);
                break;
            }
            case 0xD0: { // BNE rel
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) == 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                }
                break;
            }
            case 0xF0: { // BEQ rel
                const int8_t rel = static_cast<int8_t>(read(static_cast<uint16_t>(pc + 1)));
                pc = static_cast<uint16_t>(pc + 2);
                if ((cpuP & 0x02) != 0) {
                    pc = static_cast<uint16_t>(pc + rel);
                }
                break;
            }
            default:
                // Unknown opcode in scaffold: advance by one to keep forward progress.
                pc = static_cast<uint16_t>(pc + 1);
                break;
        }

        if (pc == oldPC) {
            pc = static_cast<uint16_t>(pc + 1);
        }
    }

    void setIecLines(bool atnHigh, bool clkHigh, bool dataHigh) override {
        const drive1541_physical::IecLines hostLines = resolveHostLinesForPowerMatrix(atnHigh, clkHigh, dataHigh);
        const uint64_t now = physicalScheduler.now();
        physicalIecPort.queueHostLines(now, hostLines);
        physicalIecPort.applyReady(now, isDriveOutputAllowedForPowerMatrix());
        const drive1541_physical::IecLines applied = physicalIecPort.busInput();
        iecATN = applied.atn;
        iecCLK = applied.clk;
        iecDATA = applied.data;
    }

    bool getIecDrivePullCLK() const override {
        return iecDrivePullCLK;
    }

    bool getIecDrivePullDATA() const override {
        return iecDrivePullDATA;
    }

    void enqueueIecCommandByte(uint8_t cmd) {
        iecRxQueue.push_back({cmd, true});
    }

    void enqueueIecDataByte(uint8_t data) {
        iecRxQueue.push_back({data, false});
    }

    void consumeReceivedByte(uint8_t byte, bool isCommand) {
        if (iecRequireFreshCommandAfterC64Resume) {
            if (!isCommand) {
                return;
            }
            iecRequireFreshCommandAfterC64Resume = false;
        }
        if (isCommand) {
            processIecCommandByte(byte);
        } else {
            processIecDataByte(byte);
        }
    }

    void stepIecSerial() {
        const bool currCLK = iecCLK;
        const bool currATN = iecATN;
        const bool currDATA = iecDATA;
        const bool fallingCLK = (iecPrevCLK && !currCLK);
        const bool risingCLK = (!iecPrevCLK && currCLK);
        const bool fallingATN = (iecPrevATN && !currATN);
        const bool commandPhase = !currATN;
        const bool hasClockEdge = (fallingCLK || risingCLK);

        if (fallingATN) {
            iecAtnFallingSeen++;
        }
        if (risingCLK) {
            iecClockRisingSeen++;
            if (!currATN) {
                iecClockRisingAtnLow++;
            }
        }

        IecSerialState nextState = IecSerialState::Idle;
        if (commandPhase) {
            nextState = IecSerialState::Command;
        } else if (iecTalking || iecKernelCompatForceTalkOnIcrSerial) {
            nextState = iecEoiPendingAck ? IecSerialState::TalkEoiAck : IecSerialState::TalkData;
        } else if (iecListening) {
            nextState = IecSerialState::ListenData;
        }
        const bool enteringCommandState = (iecSerialState != IecSerialState::Command && nextState == IecSerialState::Command);
        const bool enteringTalkData = (iecSerialState != IecSerialState::TalkData && nextState == IecSerialState::TalkData);

        if (!iecEnableAtnAck) {
            iecAtnAckPullDATA = false;
            iecAtnAckTicks = 0;
            iecAtnHandshakeActive = false;
            iecAtnAckSawClockLow = false;
        } else {
            if (fallingATN || enteringCommandState) {
                iecAtnAckTicks = revisionProfile.iecAtnAckTicksOverride;
                iecAtnHandshakeActive = true;
                iecAtnAckSawClockLow = false;
            }
            if (nextState != IecSerialState::Command) {
                iecAtnAckTicks = 0;
                iecAtnAckPullDATA = false;
                iecAtnHandshakeActive = false;
                iecAtnAckSawClockLow = false;
            } else if (iecAtnHandshakeActive) {
                // ATN handshake: listener keeps DATA low until controller pulls CLK low once.
                if (!currCLK && revisionProfile.iecHandshakeNeedsClockLowAck) {
                    iecAtnAckSawClockLow = true;
                    iecAtnAckPullDATA = true;
                    if (iecAtnAckTicks > 0) {
                        iecAtnAckTicks--;
                    }
                } else if (!revisionProfile.iecHandshakeNeedsClockLowAck) {
                    iecAtnAckPullDATA = (iecAtnAckTicks > 0);
                    if (iecAtnAckTicks > 0) {
                        iecAtnAckTicks--;
                    } else {
                        iecAtnHandshakeActive = false;
                    }
                } else if (!iecAtnAckSawClockLow) {
                    iecAtnAckPullDATA = true;
                } else {
                    iecAtnAckPullDATA = false;
                    iecAtnHandshakeActive = false;
                    iecAtnAckSawClockLow = false;
                    iecAtnAckTicks = 0;
                }
            } else {
                iecAtnAckPullDATA = false;
                iecAtnHandshakeActive = false;
            }
        }

        const bool rxPrimaryEdge = iecKernelSampleOnFallingClockEdge ? fallingCLK : risingCLK;
        bool rxClockEdge = rxPrimaryEdge || (iecKernelCompatSampleBothClockEdges && (iecKernelSampleOnFallingClockEdge ? risingCLK : fallingCLK));
        if (nextState == IecSerialState::Command && iecKernelSampleBothCommandEdges) {
            // Command phase decoding is tolerant to either clock edge.
            rxClockEdge = hasClockEdge;
        }
        const bool receiveCommand = (nextState == IecSerialState::Command);
        const bool receiveData = (nextState == IecSerialState::ListenData);
        if (enteringCommandState) {
            iecRxShift = 0;
            iecRxBitCount = 0;
            iecRxIdleTicks = 0;
            iecCommandFrameSync = true;
            iecCommandSawStartEdge = false;
            iecCommandFrameByteCount = 0;
            iecCommandFrameSawListenForDevice = false;
            iecCommandFrameSawTalkForDevice = false;
            iecCommandFrameSawListenSecondary0 = false;
            iecCommandFrameSawTalkSecondary0 = false;
        }
        if (nextState != IecSerialState::Command) {
            iecCommandFrameSync = false;
            iecCommandSawStartEdge = false;
        }
        bool commandEdgeQualified = true;
        if (receiveCommand) {
            if (iecCommandFrameSync && !iecCommandSawStartEdge) {
                if (fallingCLK) {
                    iecCommandSawStartEdge = true;
                }
                commandEdgeQualified = false;
            }
        }
        if (rxClockEdge && (receiveData || (receiveCommand && commandEdgeQualified))) {
            const uint8_t inBit = iecDATA ? 1 : 0;
            iecRxShift = static_cast<uint8_t>(iecRxShift | static_cast<uint8_t>(inBit << iecRxBitCount));
            iecRxBitCount++;
            if (iecRxBitCount >= 8) {
                const uint8_t rxByte = iecRxShift;
                const bool looksLikeCommand = ((rxByte & 0xF0) == 0x20) ||
                                              ((rxByte & 0xF0) == 0x40) ||
                                              ((rxByte & 0xF0) == 0x60) ||
                                              ((rxByte & 0xF0) == 0xE0) ||
                                              ((rxByte & 0xF0) == 0xF0) ||
                                              (rxByte == 0x3F) ||
                                              (rxByte == 0x5F);
                const bool treatAsCommand = receiveCommand || (iecKernelCompatForceTalkOnIcrSerial && looksLikeCommand);
                consumeReceivedByte(rxByte, treatAsCommand);
                iecRxProcessed++;
                if (receiveCommand) {
                    iecCommandFrameByteCount++;
                    if ((rxByte & 0xF0) == 0x20 && ((rxByte & 0x1F) == (iecDeviceAddress & 0x1F))) {
                        iecCommandFrameSawListenForDevice = true;
                    }
                    if ((rxByte & 0xF0) == 0x40 && ((rxByte & 0x1F) == (iecDeviceAddress & 0x1F))) {
                        iecCommandFrameSawTalkForDevice = true;
                    }
                    if ((rxByte & 0xF0) == 0xF0 && ((rxByte & 0x0F) == 0x00)) {
                        iecCommandFrameSawListenSecondary0 = true;
                    }
                    if ((rxByte & 0xF0) == 0x60 && ((rxByte & 0x0F) == 0x00)) {
                        iecCommandFrameSawTalkSecondary0 = true;
                    }
                    if (iecKernelCompatForceTalkOnIcrSerial &&
                        iecCommandFrameSawTalkForDevice &&
                        iecCommandFrameSawTalkSecondary0) {
                        iecTalking = true;
                        iecTalkSecondary = 0;
                        iecActiveTalkChannel = 0;
                        iecOpenTalkChannels[0] = true;
                        if (!iecDirectoryStubPrepared) {
                            iecDirectoryWildcardPattern.clear();
                            iecDirectoryTypeFilter.clear();
                            iecDirectoryModeFilter.clear();
                            iecDirectoryModeFilterNegated = false;
                            buildDirectoryStubPayload();
                        }
                    }
                }
                if (iecEnableListenerByteAck && ((receiveData && !treatAsCommand) || revisionProfile.iecRxAckOnAnyDataByte)) {
                    iecRxByteAckTicks = IEC_RX_BYTE_ACK_TICKS;
                    iecRxByteAckPullDATA = true;
                }
                iecRxShift = 0;
                iecRxBitCount = 0;
            }
        }

        if (iecEnableListenerByteAck && nextState == IecSerialState::ListenData && iecRxByteAckTicks > 0) {
            iecRxByteAckPullDATA = true;
            if (!currCLK) {
                iecRxByteAckTicks = 0;
                iecRxByteAckPullDATA = false;
            } else {
                iecRxByteAckTicks--;
            }
        } else {
            iecRxByteAckPullDATA = false;
        }

        if (iecRxBitCount > 0) {
            if (hasClockEdge) {
                iecRxIdleTicks = 0;
            } else {
                if (nextState != IecSerialState::Command) {
                    iecRxIdleTicks++;
                    if (iecRxIdleTicks > IEC_SERIAL_TIMEOUT_TICKS) {
                        iecRxTimeoutCount++;
                        iecRxBitCount = 0;
                        iecRxShift = 0;
                        iecRxIdleTicks = 0;
                        iecStatusLine = "74,DRIVE NOT READY,00,00";
                    }
                }
            }
        } else {
            iecRxIdleTicks = 0;
        }

        if (nextState == IecSerialState::TalkEoiAck) {
            iecSerialPullDATA = false;
            if (!currDATA) {
                iecEoiAckLowSeen = true;
            }
            if (iecEoiAckLowSeen && currDATA) {
                iecEoiPendingAck = false;
                iecEoiAckLowSeen = false;
                iecEoiWaitTicks = 0;
                iecEoiAckCount++;
            }
            if (!iecEoiAckLowSeen) {
                iecEoiWaitTicks++;
                if (iecEoiWaitTicks > IEC_EOI_TIMEOUT_TICKS) {
                    iecEoiPendingAck = false;
                    iecEoiWaitTicks = 0;
                    iecEoiTimeoutCount++;
                    iecStatusLine = "74,DRIVE NOT READY,00,00";
                }
            }
        } else if (nextState != IecSerialState::TalkData) {
            iecTxByteActive = false;
            iecTxBitCount = 0;
            iecTalkStartPending = false;
            iecTalkFrameArmed = false;
            iecTalkSawStartEdge = false;
            iecSerialPullDATA = false;
            iecTxCurrentIsEoi = false;
            iecEoiPendingAck = false;
            iecEoiAckLowSeen = false;
        } else {
            if (enteringTalkData) {
                iecTalkFrameArmed = true;
                iecTalkSawStartEdge = false;
            }
            const bool talkChannelConfirmed = (iecActiveTalkChannel == 0)
                                                  ? iecTalkSa0Confirmed
                                                  : (iecActiveTalkChannel != 0xFF);
            if (enteringTalkData && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty()) {
                iecTalkStartPending = talkChannelConfirmed;
                iecTalkStartByte = iecTxQueue.front();
                iecTalkStartIsEoi = (iecTxQueue.size() == 1);
                iecSerialPullDATA = false;
            }
            if (!iecTalkStartPending && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty() && talkChannelConfirmed) {
                iecTalkStartPending = true;
                iecTalkStartByte = iecTxQueue.front();
                iecTalkStartIsEoi = (iecTxQueue.size() == 1);
            }
            if (iecKernelCompatForceTalkOnIcrSerial && enteringTalkData && iecTxQueue.empty() && iecDirectoryStubPrepared) {
                buildDirectoryStubPayload();
            }
            if (!currATN && !iecKernelIgnoreAtnForTalkDataPhase) {
                // Pause data shifting while ATN is asserted, but keep TX state latched.
                iecSerialPullDATA = false;
            } else {
                bool startedTxOnFalling = false;
                if (fallingCLK) {
                    if (iecTalkFrameArmed && !iecTalkSawStartEdge) {
                        iecTalkSawStartEdge = true;
                        startedTxOnFalling = true;
                    }
                    if (iecTalkSawStartEdge && iecTalkStartPending && !iecTxByteActive && !iecEoiPendingAck && talkChannelConfirmed) {
                        iecTalkStartPending = false;
                        if (!iecTxQueue.empty()) {
                            const bool lastByte = (iecTxQueue.size() == 1);
                            iecTxShift = iecTxQueue.front();
                            iecTxQueue.pop_front();
                            iecTxServed++;
                            iecTxBitCount = 0;
                            iecTxByteActive = true;
                            iecTxCurrentIsEoi = lastByte;
                            startedTxOnFalling = true;
                        } else {
                            iecSerialPullDATA = false;
                        }
                    }

                    if (iecTalkSawStartEdge && !iecTxByteActive && !iecEoiPendingAck && !iecTxQueue.empty() && talkChannelConfirmed) {
                        const bool lastByte = (iecTxQueue.size() == 1);
                        iecTxShift = iecTxQueue.front();
                        iecTxQueue.pop_front();
                        iecTxServed++;
                        iecTxBitCount = 0;
                        iecTxByteActive = true;
                        iecTxCurrentIsEoi = lastByte;
                        startedTxOnFalling = true;
                    }

                }

                const bool txPrimaryAdvance = risingCLK;
                const bool txAdvanceEdge = txPrimaryAdvance || (iecKernelCompatSampleBothClockEdges && (iecKernelSampleOnFallingClockEdge ? risingCLK : fallingCLK));
                if (txAdvanceEdge && iecTxByteActive) {
                    if (!(risingCLK && startedTxOnFalling)) {
                        iecTxBitCount++;
                        if (iecTxBitCount >= 8) {
                            iecTxByteActive = false;
                            iecTxBitCount = 0;
                            if (iecTxCurrentIsEoi) {
                                iecEoiPendingAck = true;
                                iecEoiAckLowSeen = false;
                                iecEoiWaitTicks = 0;
                            }
                            iecTxCurrentIsEoi = false;
                        }
                    }
                }

                if (iecTxByteActive) {
                    const uint8_t outBit = static_cast<uint8_t>((iecTxShift >> iecTxBitCount) & 0x01);
                    iecSerialPullDATA = (outBit == 0);
                }

                if (iecTxByteActive) {
                    if (hasClockEdge) {
                        iecTxIdleTicks = 0;
                    } else {
                        iecTxIdleTicks++;
                        if (iecTxIdleTicks > IEC_SERIAL_TIMEOUT_TICKS) {
                            iecTxTimeoutCount++;
                            iecTxByteActive = false;
                            iecTxBitCount = 0;
                            iecSerialPullDATA = false;
                            iecTxIdleTicks = 0;
                            iecStatusLine = "74,DRIVE NOT READY,00,00";
                        }
                    }
                } else {
                    iecTxIdleTicks = 0;
                }

                if (iecKernelCompatForceTalkOnIcrSerial && iecActiveTalkChannel == 0 && iecTxQueue.size() < 16) {
                    buildDirectoryStubPayload();
                }
            }
        }

        iecSerialState = nextState;
        iecPrevCLK = currCLK;
        iecPrevATN = currATN;
        iecPrevDATA = currDATA;
    }

    size_t pendingIecRx() const {
        return iecRxQueue.size();
    }

    size_t pendingIecTx() const {
        return iecTxQueue.size();
    }

    bool hostReadTalkByte(uint8_t &out) {
        if (iecTxQueue.empty()) {
            return false;
        }
        out = iecTxQueue.front();
        iecTxQueue.pop_front();
        iecTxServed++;
        return true;
    }

    bool processIecDataByte(uint8_t data) {
        if (!iecListening) {
            return false;
        }

        if (iecActiveListenChannel == 15) {
            iecCommandChannelBuffer.push_back(data);
            iecDataDispatchCount++;
            return true;
        }

        if (iecExpectingNameBytes) {
            iecNameBuffer.push_back(data);
            iecDataDispatchCount++;
            return true;
        }

        if (iecActiveListenChannel != 0) {
            iecStatusLine = "70,NO CHANNEL,00,00";
            iecCommandSyntaxErrorCount++;
            return false;
        }

        iecStatusLine = "64,FILE TYPE MISMATCH,00,00";
        iecCommandSyntaxErrorCount++;
        return false;
    }

    uint8_t toUpperAscii(uint8_t c) const {
        if (c >= static_cast<uint8_t>('a') && c <= static_cast<uint8_t>('z')) {
            return static_cast<uint8_t>(c - 32);
        }
        return c;
    }

    void buildStatusPayload() {
        iecTxQueue.clear();
        for (char c : iecStatusLine) {
            iecTxQueue.push_back(static_cast<uint8_t>(c));
        }
        iecTxQueue.push_back(0x0D);
    }

    static bool parseHexByte(const std::string &s, uint8_t &out) {
        if (s.size() != 2 || !std::isxdigit(static_cast<unsigned char>(s[0])) || !std::isxdigit(static_cast<unsigned char>(s[1]))) {
            return false;
        }
        const int hi = std::isdigit(static_cast<unsigned char>(s[0])) ? (s[0] - '0') : (10 + (std::toupper(static_cast<unsigned char>(s[0])) - 'A'));
        const int lo = std::isdigit(static_cast<unsigned char>(s[1])) ? (s[1] - '0') : (10 + (std::toupper(static_cast<unsigned char>(s[1])) - 'A'));
        out = static_cast<uint8_t>((hi << 4) | lo);
        return true;
    }

    static bool parseHexWord(const std::string &s, uint16_t &out) {
        if (s.size() != 4) {
            return false;
        }
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!parseHexByte(s.substr(0, 2), hi) || !parseHexByte(s.substr(2, 2), lo)) {
            return false;
        }
        out = static_cast<uint16_t>((uint16_t(hi) << 8) | lo);
        return true;
    }

    static std::vector<std::string> splitComma(const std::string &in) {
        std::vector<std::string> parts;
        std::string token;
        for (char c : in) {
            if (c == ',') {
                parts.push_back(token);
                token.clear();
            } else {
                token.push_back(c);
            }
        }
        parts.push_back(token);
        return parts;
    }

    static std::string trimAscii(const std::string &s) {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) {
            b++;
        }
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) {
            e--;
        }
        return s.substr(b, e - b);
    }

    int statusCodeOf(const std::string &status) const {
        if (status.size() >= 2 &&
            std::isdigit(static_cast<unsigned char>(status[0])) &&
            std::isdigit(static_cast<unsigned char>(status[1]))) {
            return (status[0] - '0') * 10 + (status[1] - '0');
        }
        return -1;
    }

    // Conservative expectation model for phase-1 ownership counters.
    // We only declare an expected status when the command class has a deterministic code in
    // this scaffold. Unknown/complex classes return -1 and are excluded from divergence counting.
    int expectedStatusForCommandHeuristic(const std::string &cmd) const {
        if (cmd.empty()) {
            return 30;
        }
        if (cmd.rfind("M-R", 0) == 0 || cmd.rfind("M-W", 0) == 0 || cmd.rfind("M-E", 0) == 0) {
            return 0;
        }
        if (cmd.rfind("B-A", 0) == 0 || cmd.rfind("B-W", 0) == 0 || cmd.rfind("B-P", 0) == 0 || cmd.rfind("B-F", 0) == 0) {
            return 0;
        }
        if (cmd.rfind("B-R", 0) == 0) {
            return -1;
        }
        if (cmd == "U1" || cmd == "U2" || cmd == "I0" || cmd == "UI" || cmd == "UJ" || cmd == "N0:" || cmd == "NEW") {
            return 0;
        }
        return -1;
    }

    bool commandClassEligibleForCpuOwnership(const std::string &cmd) const {
        return cmd.rfind("M-", 0) == 0 ||
               cmd.rfind("B-", 0) == 0 ||
               cmd == "U1" || cmd == "U2" ||
               cmd == "I0" || cmd == "UI" || cmd == "UJ" ||
               cmd == "N0:" || cmd == "NEW";
    }

    void setIecCpuOwnershipCutoverPhase1(bool enabled) {
        if (iecCpuOwnsCommandPath != enabled) {
            iecCpuOwnershipCutoverTransitions++;
        }
        iecCpuOwnsCommandPath = enabled;
    }

    void buildCommandResponsePayload() {
        iecTxQueue.clear();
        for (uint8_t b : iecCommandResponseQueue) {
            iecTxQueue.push_back(b);
        }
        if (!iecTxQueue.empty()) {
            iecTxQueue.push_back(0x0D);
        }
        iecCommandResponseQueue.clear();
    }

    void buildDirectoryFromBlockBufferPayload(uint8_t channel) {
        if (!iecBlockBufferValid) {
            buildDirectoryStubPayload();
            return;
        }

        const uint8_t start = iecChannelBufferPos[channel];
        iecTxQueue.clear();
        iecTxQueue.push_back(0x01);
        iecTxQueue.push_back(0x08);
        for (uint16_t i = start; i < 256; ++i) {
            iecTxQueue.push_back(iecBlockBuffer[i]);
        }
        iecTxQueue.push_back(0x00);
        iecTxQueue.push_back(0x00);
    }

    void processCommandChannelBuffer() {
        if (iecCommandChannelBuffer.empty()) {
            return;
        }

        std::string cmd;
        cmd.reserve(iecCommandChannelBuffer.size());
        for (uint8_t c : iecCommandChannelBuffer) {
            if (c == 0x0D || c == 0x00) {
                break;
            }
            cmd.push_back(static_cast<char>(toUpperAscii(c)));
        }
        iecCommandChannelBuffer.clear();

        if (cmd.empty()) {
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            iecCommandSyntaxErrorCount++;
            return;
        }

        const bool cpuOwnedTracking = iecCpuOwnsCommandPath;
        const int expectedStatusForDivergence = cpuOwnedTracking ? expectedStatusForCommandHeuristic(cmd) : -1;
        if (cpuOwnedTracking) {
            iecCpuOwnedCommandRows++;
            if (!commandClassEligibleForCpuOwnership(cmd)) {
                iecScaffoldFallbackRows++;
            }
        }

        struct CpuOwnershipFinalize {
            Drive1541 *self;
            bool enabled;
            int expectedStatus;
            ~CpuOwnershipFinalize() {
                if (!enabled || expectedStatus < 0) {
                    return;
                }
                const int actualStatus = self->statusCodeOf(self->iecStatusLine);
                if (actualStatus != expectedStatus) {
                    self->iecCmdStatusDivergenceRows++;
                }
            }
        } finalize{this, cpuOwnedTracking, expectedStatusForDivergence};

        if (cmd.rfind("M-R", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 4) {
                uint16_t addr = 0;
                uint8_t len = 0;
                const std::string a = trimAscii(parts[1]);
                const std::string l = trimAscii(parts[3]);
                if (parseHexWord(a, addr) && parseHexByte(l, len) && len > 0) {
                    iecCommandResponseQueue.clear();
                    for (uint16_t i = 0; i < len; ++i) {
                        iecCommandResponseQueue.push_back(memory[static_cast<uint16_t>(addr + i)]);
                    }
                    iecStatusLine = "00,OK,00,00";
                    return;
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("M-W", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 5) {
                uint16_t addr = 0;
                uint8_t len = 0;
                const std::string a = trimAscii(parts[1]);
                const std::string l = trimAscii(parts[3]);
                if (!parseHexWord(a, addr) || !parseHexByte(l, len) || len == 0) {
                    iecStatusLine = "30,SYNTAX ERROR,00,00";
                    return;
                }

                if (parts.size() < static_cast<size_t>(4 + len)) {
                    iecStatusLine = "30,SYNTAX ERROR,00,00";
                    return;
                }

                for (uint16_t i = 0; i < len; ++i) {
                    uint8_t b = 0;
                    if (!parseHexByte(trimAscii(parts[4 + i]), b)) {
                        iecStatusLine = "30,SYNTAX ERROR,00,00";
                        return;
                    }
                    write(static_cast<uint16_t>(addr + i), b);
                }
                iecStatusLine = "00,OK,00,00";
                return;
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("M-E", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            if (parts.size() >= 2) {
                uint16_t addr = 0;
                const std::string a = trimAscii(parts[1]);
                if (parseHexWord(a, addr)) {
                    iecLastExecuteAddr = addr;
                    dispatchExecuteStub(addr);
                    return;
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd.rfind("B-", 0) == 0) {
            const std::vector<std::string> parts = splitComma(cmd);
            const std::string op = (parts.size() >= 1) ? trimAscii(parts[0]) : std::string();
            if (parts.size() >= 4) {
                uint8_t ch = 0;
                uint8_t trk = 0;
                uint8_t sec = 0;
                if (parseHexByte(trimAscii(parts[1]), ch) &&
                    parseHexByte(trimAscii(parts[2]), trk) &&
                    parseHexByte(trimAscii(parts[3]), sec)) {
                    if (op == "B-R" || op == "B-W" || op == "B-P" || op == "B-A" || op == "B-F") {
                        iecLastBlockChannel = ch;
                        iecLastBlockTrack = trk;
                        iecLastBlockSector = sec;
                        iecLastBlockCommand = op;

                        if (op == "B-R") {
                            if (!isVirtualBlockAllocated(trk, sec)) {
                                iecStatusLine = "65,NO BLOCK,00,00";
                                return;
                            }
                            loadVirtualBlock(trk, sec);
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-W") {
                            if (!isVirtualBlockAllocated(trk, sec)) {
                                const bool allocated = allocateVirtualBlock(trk, sec, ch);
                                if (!allocated) {
                                    iecStatusLine = "63,FILE EXISTS,00,00";
                                    return;
                                }
                            }
                            iecStatusLine = "00,OK,00,00";
                            flushVirtualBlock(trk, sec);
                        } else if (op == "B-P") {
                            uint8_t ptr = 0;
                            if (parts.size() < 5 || !parseHexByte(trimAscii(parts[4]), ptr)) {
                                iecStatusLine = "30,SYNTAX ERROR,00,00";
                                return;
                            }
                            iecChannelBufferPos[ch & 0x0F] = ptr;
                            iecChannelPointerValid[ch & 0x0F] = true;
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-F") {
                            const bool freed = freeVirtualBlock(trk, sec);
                            if (!freed) {
                                iecStatusLine = "65,NO BLOCK,00,00";
                                return;
                            }
                            iecBlockBuffer.fill(0);
                            iecBlockBufferValid = true;
                            iecBlockBufferTrack = trk;
                            iecBlockBufferSector = sec;
                            iecStatusLine = "00,OK,00,00";
                        } else if (op == "B-A") {
                            const bool allocated = allocateVirtualBlock(trk, sec, ch);
                            if (!allocated) {
                                iecStatusLine = "63,FILE EXISTS,00,00";
                                return;
                            }
                            iecStatusLine = "00,OK,00,00";
                        } else {
                            iecStatusLine = "00,OK,00,00";
                        }
                        return;
                    }
                }
            }
            iecStatusLine = "30,SYNTAX ERROR,00,00";
            return;
        }

        if (cmd == "U1" || cmd == "U2") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        if (cmd == "I0" || cmd == "UI" || cmd == "UJ") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        if (cmd == "N0:" || cmd == "NEW") {
            iecStatusLine = "00,OK,00,00";
            return;
        }

        iecStatusLine = "30,SYNTAX ERROR,00,00";
    }

    uint16_t blockLinearBase(uint8_t track, uint8_t sector) const {
        return static_cast<uint16_t>(((uint16_t(track) << 8) | sector) & 0xBFFF);
    }

    bool isMountedD64BackendActive() const {
        return mountedImageBackend != nullptr && std::string(mountedImageBackend->formatName()) == "d64" && mountedImageBackend->isReady();
    }

    bool isMountedBlockBackendActive() const {
        return mountedImageBackend != nullptr && mountedImageBackend->isReady();
    }

    bool isMountedFluxBackendActiveFor(const std::string &format) const {
        return mountedFluxImageBackend != nullptr &&
               std::string(mountedFluxImageBackend->formatName()) == format &&
               mountedFluxImageBackend->isReady();
    }

    bool isMountedImageBackendActiveFor(const std::string &format) const {
        return mountedImageBackend != nullptr && std::string(mountedImageBackend->formatName()) == format && mountedImageBackend->isReady();
    }

    bool isMountedBackendRoutedToFlux() const {
        return mountedImageBackend != nullptr &&
               mountedFluxImageBackend != nullptr &&
               mountedImageBackend.get() == static_cast<IImageBackend *>(mountedFluxImageBackend.get());
    }

    uint16_t blockAllocIndex(uint8_t track, uint8_t sector) const {
        const uint32_t key = static_cast<uint32_t>(track) * 21u + static_cast<uint32_t>(sector);
        return static_cast<uint16_t>(key % IEC_TOTAL_VIRTUAL_BLOCKS);
    }

    static std::string hex2(uint8_t v) {
        std::ostringstream oss;
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(v);
        return oss.str();
    }

    std::string makeCatalogName(uint8_t track, uint8_t sector) const {
        std::string name = "BLK-";
        name += hex2(track);
        name += hex2(sector);
        return name;
    }

    std::string normalizeOpenName(const std::vector<uint8_t> &raw) const {
        std::string out;
        out.reserve(raw.size());
        for (uint8_t c : raw) {
            if (c == 0x00 || c == 0x0D) {
                break;
            }
            if (c >= 'a' && c <= 'z') {
                c = static_cast<uint8_t>(c - 32);
            }
            if (c == '"') {
                continue;
            }
            if (c >= 32 && c <= 126) {
                out.push_back(static_cast<char>(c));
            }
            if (out.size() >= 16) {
                break;
            }
        }
        return out;
    }

    std::string normalizeUpperAscii(const std::string &in) const {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            uint8_t u = static_cast<uint8_t>(c);
            if (u >= 'a' && u <= 'z') {
                u = static_cast<uint8_t>(u - 32);
            }
            if (u >= 32 && u <= 126) {
                out.push_back(static_cast<char>(u));
            }
        }
        return out;
    }

    void parseOpenSpec(uint8_t channel, const std::string &raw) {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        const std::string up = normalizeUpperAscii(raw);
        std::vector<std::string> parts = splitComma(up);
        if (parts.empty()) {
            iecChannelOpenName[ch].clear();
            iecChannelOpenNameValid[ch] = false;
            iecChannelOpenType[ch] = "PRG";
            iecChannelOpenMode[ch].clear();
            return;
        }

        std::string name = trimAscii(parts[0]);
        if (!name.empty() && name.front() == '"' && name.back() == '"' && name.size() >= 2) {
            name = name.substr(1, name.size() - 2);
        }

        iecChannelOpenName[ch] = name;
        iecChannelOpenNameValid[ch] = !name.empty();
        iecChannelOpenType[ch] = "PRG";
        iecChannelOpenMode[ch].clear();

        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string tok = trimAscii(parts[i]);
            if (tok == "P") {
                iecChannelOpenType[ch] = "PRG";
            } else if (tok == "S") {
                iecChannelOpenType[ch] = "SEQ";
            } else if (tok == "U") {
                iecChannelOpenType[ch] = "USR";
            } else if (tok == "L") {
                iecChannelOpenType[ch] = "REL";
            } else if (tok == "W" || tok == "R" || tok == "A") {
                iecChannelOpenMode[ch] = tok;
            }
        }
    }

    void commitOpenNameFromBuffer(uint8_t channel) {
        if (channel >= 16) {
            return;
        }
        const std::string spec = normalizeOpenName(iecNameBuffer);
        parseOpenSpec(channel, spec);
    }

    std::string pickCatalogNameForChannel(uint8_t channel, uint8_t track, uint8_t sector) const {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        if (iecChannelOpenNameValid[ch] && !iecChannelOpenName[ch].empty() && iecChannelOpenName[ch] != "$") {
            return iecChannelOpenName[ch];
        }
        return makeCatalogName(track, sector);
    }

    struct DirectoryFilters {
        std::string pattern;
        std::string type;
        std::string mode;
        bool modeNegated = false;
    };

    DirectoryFilters extractDirectoryFilters() const {
        const std::string spec = normalizeOpenName(iecNameBuffer);
        if (spec.empty()) {
            return DirectoryFilters{};
        }

        const std::vector<std::string> parts = splitComma(spec);
        std::string first = parts.empty() ? std::string() : parts[0];
        first = trimAscii(first);
        if (!first.empty() && first.front() == '"' && first.back() == '"' && first.size() >= 2) {
            first = first.substr(1, first.size() - 2);
        }

        if (first.empty() || first[0] != '$') {
            return DirectoryFilters{};
        }

        std::string pat = first.substr(1);
        pat = normalizeUpperAscii(pat);
        pat = trimAscii(pat);

        std::string typeFilter;
        std::string modeFilter;
        bool modeNegated = false;
        if (parts.size() >= 2) {
            std::string t = trimAscii(normalizeUpperAscii(parts[1]));
            if (t == "P") typeFilter = "PRG";
            else if (t == "S") typeFilter = "SEQ";
            else if (t == "U") typeFilter = "USR";
            else if (t == "L") typeFilter = "REL";
        }
        if (parts.size() >= 3) {
            std::string m = trimAscii(normalizeUpperAscii(parts[2]));
            if (!m.empty() && m[0] == '!') {
                modeNegated = true;
                m = trimAscii(m.substr(1));
            }
            if (m == "R" || m == "W" || m == "A") {
                modeFilter = m;
            } else {
                modeNegated = false;
            }
        }

        DirectoryFilters out;
        out.pattern = pat;
        out.type = typeFilter;
        out.mode = modeFilter;
        out.modeNegated = modeNegated;
        return out;
    }

    bool wildcardMatch(const std::string &pattern, const std::string &text) const {
        if (pattern.empty()) {
            return true;
        }

        const std::string upText = normalizeUpperAscii(text);
        const size_t pn = pattern.size();
        const size_t tn = upText.size();
        size_t p = 0;
        size_t t = 0;
        size_t star = std::string::npos;
        size_t match = 0;

        while (t < tn) {
            if (p < pn && (pattern[p] == '?' || pattern[p] == upText[t])) {
                p++;
                t++;
            } else if (p < pn && pattern[p] == '*') {
                star = p++;
                match = t;
            } else if (star != std::string::npos) {
                p = star + 1;
                t = ++match;
            } else {
                return false;
            }
        }

        while (p < pn && pattern[p] == '*') {
            p++;
        }
        return p == pn;
    }

    int findFreeCatalogSlot() const {
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            if (!iecCatalog[i].used) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int ensureCatalogEntryForChannel(uint8_t channel, uint8_t track, uint8_t sector) {
        const uint8_t ch = static_cast<uint8_t>(channel & 0x0F);
        const int8_t mapped = iecChannelCatalogEntry[ch];
        if (mapped >= 0) {
            const int idx = static_cast<int>(mapped);
            if (idx >= 0 && idx < static_cast<int>(iecCatalog.size()) && iecCatalog[idx].used) {
                if (iecChannelOpenNameValid[ch] && !iecChannelOpenName[ch].empty() && iecChannelOpenName[ch] != "$") {
                    iecCatalog[idx].name = iecChannelOpenName[ch];
                    iecCatalog[idx].type = iecChannelOpenType[ch];
                    iecCatalog[idx].mode = iecChannelOpenMode[ch];
                }
                return idx;
            }
        }

        const int freeIdx = findFreeCatalogSlot();
        if (freeIdx < 0) {
            return -1;
        }

        iecCatalog[freeIdx] = VirtualCatalogEntry{};
        iecCatalog[freeIdx].used = true;
        iecCatalog[freeIdx].channel = ch;
        iecCatalog[freeIdx].track = track;
        iecCatalog[freeIdx].sector = sector;
        iecCatalog[freeIdx].blocks = 0;
        iecCatalog[freeIdx].name = pickCatalogNameForChannel(ch, track, sector);
        iecCatalog[freeIdx].type = iecChannelOpenType[ch];
        iecCatalog[freeIdx].mode = iecChannelOpenMode[ch];
        iecChannelCatalogEntry[ch] = static_cast<int8_t>(freeIdx);
        return freeIdx;
    }

    void removeCatalogEntryByIndex(int idx) {
        if (idx < 0 || idx >= static_cast<int>(iecCatalog.size())) {
            return;
        }
        const uint8_t ch = iecCatalog[idx].channel;
        if (ch < 16 && iecChannelCatalogEntry[ch] == idx) {
            iecChannelCatalogEntry[ch] = -1;
        }
        iecCatalog[idx] = VirtualCatalogEntry{};
    }

    uint16_t virtualBlocksFree() const {
        if (iecAllocatedBlockCount >= IEC_TOTAL_VIRTUAL_BLOCKS) {
            return 0;
        }
        return static_cast<uint16_t>(IEC_TOTAL_VIRTUAL_BLOCKS - iecAllocatedBlockCount);
    }

    bool allocateVirtualBlock(uint8_t track, uint8_t sector, uint8_t channel) {
        const uint16_t idx = blockAllocIndex(track, sector);
        if (iecBlockAllocated[idx]) {
            return false;
        }

        const int catIdx = ensureCatalogEntryForChannel(channel, track, sector);
        if (catIdx < 0) {
            return false;
        }

        iecBlockAllocated[idx] = 1;
        iecAllocOwnerEntry[idx] = static_cast<uint8_t>(catIdx);
        iecAllocatedBlockCount = static_cast<uint16_t>(iecAllocatedBlockCount + 1);
        iecCatalog[catIdx].blocks = static_cast<uint16_t>(iecCatalog[catIdx].blocks + 1);
        return true;
    }

    bool freeVirtualBlock(uint8_t track, uint8_t sector) {
        const uint16_t idx = blockAllocIndex(track, sector);
        if (!iecBlockAllocated[idx]) {
            return false;
        }

        const uint8_t owner = iecAllocOwnerEntry[idx];
        iecBlockAllocated[idx] = 0;
        iecAllocOwnerEntry[idx] = 0xFF;
        if (iecAllocatedBlockCount > 0) {
            iecAllocatedBlockCount = static_cast<uint16_t>(iecAllocatedBlockCount - 1);
        }

        if (owner != 0xFF && owner < iecCatalog.size() && iecCatalog[owner].used) {
            if (iecCatalog[owner].blocks > 0) {
                iecCatalog[owner].blocks = static_cast<uint16_t>(iecCatalog[owner].blocks - 1);
            }
            if (iecCatalog[owner].blocks == 0) {
                removeCatalogEntryByIndex(static_cast<int>(owner));
            }
        }

        return true;
    }

    bool isVirtualBlockAllocated(uint8_t track, uint8_t sector) const {
        const uint16_t idx = blockAllocIndex(track, sector);
        return iecBlockAllocated[idx] != 0;
    }

    void loadVirtualBlock(uint8_t track, uint8_t sector) {
        bool loadedFromImage = false;
        if (isMountedBlockBackendActive()) {
            ImageIoError err = ImageIoError::None;
            if (mountedImageBackend->readBlock(track, sector, iecBlockBuffer, err)) {
                loadedFromImage = true;
                signalModel.note_flux_read();
                if (advanced_image_detail::isFluxCapableFormat(mountedImageFormat)) {
                    runPhysicalLevel3ReadPipeline(track, sector);
                }
            }
        }

        if (!loadedFromImage) {
            const uint16_t base = blockLinearBase(track, sector);
            for (uint16_t i = 0; i < 256; ++i) {
                iecBlockBuffer[i] = memory[static_cast<uint16_t>((base + i) & 0xBFFF)];
            }
        }
        iecBlockBufferValid = true;
        iecBlockBufferTrack = track;
        iecBlockBufferSector = sector;
        iecDiskMap[0] = track;
        iecDiskMap[1] = sector;
    }

    uint8_t physicalZoneFromTrack(uint8_t track) const {
        if (track <= 17) return 0;
        if (track <= 24) return 1;
        if (track <= 30) return 2;
        return 3;
    }

    void initializePhysicalLevel3Pipeline(uint8_t track, uint8_t sector) {
        if (physicalPipelineInitialized) {
            return;
        }

        const uint32_t seed = static_cast<uint32_t>((uint32_t(iecDeviceAddress) << 16) |
                                                     (uint32_t(track) << 8) |
                                                     uint32_t(sector));
        physicalMechanicsModel.reset();
        physicalMechanicsModel.set_motor_on(true);
        physicalBitcellTimingModel.reset(seed == 0 ? 0x1541u : seed);
        physicalBitcellTimingModel.set_drive_revision(static_cast<uint8_t>(revision));
        const bool level4OrHigher =
            (physicalProfile == PhysicalProfile::Level4Accuracy ||
             physicalProfile == PhysicalProfile::Level5Coupling);
        physicalBitcellTimingModel.set_level4_enabled(level4OrHigher);
        if (level4OrHigher) {
            const uint16_t baseNoise = static_cast<uint16_t>(22u + static_cast<uint16_t>(iecDeviceAddress & 0x03u) * 3u);
            const uint16_t jitterScale = static_cast<uint16_t>(30u + static_cast<uint16_t>(revision) * 4u);
            const uint16_t lockGain = static_cast<uint16_t>(250u + static_cast<uint16_t>(revision) * 25u);
            physicalBitcellTimingModel.set_level4_calibration(baseNoise, jitterScale, lockGain);
        }

        std::vector<drive1541_physical::FluxTransition> transitions;
        transitions.push_back({7u});
        transitions.push_back({9u});
        transitions.push_back({8u});
        physicalFluxTrackModel.set_transitions(std::move(transitions));

        std::vector<drive1541_physical::WeakRegion> weakRegions;
        weakRegions.push_back({96u, 128u});
        weakRegions.push_back({224u, 256u});
        physicalFluxTrackModel.set_weak_regions(std::move(weakRegions));

        physicalPipelineAbsTick = static_cast<uint32_t>(physicalScheduler.now() & 0xFFFFFFFFu);
        physicalPipelineInitialized = true;
    }

    void runPhysicalLevel3ReadPipeline(uint8_t track, uint8_t sector) {
        if (physicalProfile != PhysicalProfile::Level3Physical &&
            physicalProfile != PhysicalProfile::Level4Accuracy &&
            physicalProfile != PhysicalProfile::Level5Coupling) {
            return;
        }
        if (!mountedImageBackend || iecBlockBuffer.empty()) {
            return;
        }

        initializePhysicalLevel3Pipeline(track, sector);

        const uint8_t zone = physicalZoneFromTrack(track);
        physicalBitcellTimingModel.set_zone(zone);

        const uint64_t now = physicalScheduler.now();
        if (now > physicalPipelineAbsTick) {
            const uint64_t elapsed = now - static_cast<uint64_t>(physicalPipelineAbsTick);
            physicalMechanicsModel.tick(elapsed > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(elapsed));
            physicalPipelineAbsTick = static_cast<uint32_t>(now & 0xFFFFFFFFu);
        }

        const uint16_t targetHalf = static_cast<uint16_t>(2u + static_cast<uint16_t>(track - 1u) * 2u);
        while (physicalMechanicsModel.half_track() < targetHalf) {
            physicalMechanicsModel.step_in();
        }
        while (physicalMechanicsModel.half_track() > targetHalf) {
            physicalMechanicsModel.step_out();
        }

        const uint32_t cellTicks = physicalBitcellTimingModel.next_cell_ticks();
        physicalPipelineAbsTick = static_cast<uint32_t>(physicalPipelineAbsTick + cellTicks);
        const bool fluxEdge = physicalFluxTrackModel.advance(cellTicks, physicalPipelineAbsTick);

        const size_t bytesToSample = std::min<size_t>(8, iecBlockBuffer.size());
        const std::vector<uint8_t> encoded = physicalGcrCodec.encode_4to5(iecBlockBuffer.data(), bytesToSample);
        const drive1541_physical::GcrDecodeResult decoded =
            physicalGcrCodec.decode_5to4(encoded.data(), encoded.size());

        uint8_t byteSample = iecBlockBuffer[0];
        if (decoded.ok && !decoded.data.empty()) {
            byteSample = decoded.data[0];
        }

        physicalPipelineRuns++;
        physicalPipelineLastCellTicks = cellTicks;
        physicalPipelineLastFluxEdge = fluxEdge;
        physicalPipelineLastByte = byteSample;
        physicalPipelineLastBit = static_cast<uint8_t>((byteSample >> (physicalPipelineAbsTick & 0x07u)) & 0x01u);
        physicalPipelineLastHalfTrack = physicalMechanicsModel.half_track();
        physicalPipelineLastAngleNorm = physicalMechanicsModel.spindle_angle_norm();
        physicalPipelineLastPll = physicalBitcellTimingModel.last_pll_sample();
    }

    void flushVirtualBlock(uint8_t track, uint8_t sector) {
        if (!iecBlockBufferValid) {
            iecStatusLine = "66,ILLEGAL TRACK OR SECTOR,00,00";
            return;
        }

        bool flushedToImage = false;
        if (isMountedBlockBackendActive()) {
            ImageIoError err = ImageIoError::None;
            if (!mountedImageBackend->writeBlock(track, sector, iecBlockBuffer, err)) {
                if (err == ImageIoError::InvalidAddress) {
                    iecStatusLine = "66,ILLEGAL TRACK OR SECTOR,00,00";
                } else if (err == ImageIoError::WriteProtected) {
                    iecStatusLine = "26,WRITE PROTECT ON,00,00";
                } else {
                    iecStatusLine = "74,DRIVE NOT READY,00,00";
                }
                return;
            }
            flushedToImage = true;
            signalModel.note_flux_write();
        }

        if (!flushedToImage) {
            const uint16_t base = blockLinearBase(track, sector);
            for (uint16_t i = 0; i < 256; ++i) {
                write(static_cast<uint16_t>((base + i) & 0xBFFF), iecBlockBuffer[i]);
            }
        }
        iecDiskMap[0] = track;
        iecDiskMap[1] = sector;
        signalModel.note_status_line(iecStatusLine.c_str());
    }

    void dispatchExecuteStub(uint16_t addr) {
        iecExecDispatchCount++;
        if (addr >= 0xC000) {
            iecLastExecDispatch = "DOS_ENTRY";
            iecStatusLine = "00,OK,00,00";
            return;
        }
        if (addr >= 0x0400 && addr <= 0x07FF) {
            iecLastExecDispatch = "BUFFER_ENTRY";
            iecStatusLine = "00,OK,00,00";
            return;
        }
        iecLastExecDispatch = "UNKNOWN_ENTRY";
        iecStatusLine = "31,SYNTAX ERROR,00,00";
    }

    bool isDirectoryRequest() const {
        if (iecNameBuffer.empty()) {
            return false;
        }
        return iecNameBuffer[0] == static_cast<uint8_t>('$');
    }

    bool buildDirectoryPayloadFromMountedD64() {
        if (!isMountedD64BackendActive()) {
            return false;
        }

        ImageDirectoryListing listing;
        ImageIoError err = ImageIoError::None;
        if (!mountedImageBackend->readDirectoryListing(listing, err)) {
            return false;
        }

        std::vector<uint8_t> prg;
        prg.reserve(2048);
        prg.push_back(0x01);
        prg.push_back(0x08);

        uint16_t cursor = 0x0801;
        enqueueBasicLine(prg, cursor, 0, std::string("0 \"") + listing.diskName + "\" 00 2A");

        uint16_t lineNo = 1;
        bool anyCatalog = false;
        for (const auto &entry : listing.entries) {
            if (!wildcardMatch(iecDirectoryWildcardPattern, entry.name)) {
                continue;
            }
            if (!iecDirectoryTypeFilter.empty() && entry.type != iecDirectoryTypeFilter) {
                continue;
            }
            if (!iecDirectoryModeFilter.empty()) {
                const bool modeMatch = (entry.mode == iecDirectoryModeFilter);
                if ((!iecDirectoryModeFilterNegated && !modeMatch) ||
                    (iecDirectoryModeFilterNegated && modeMatch)) {
                    continue;
                }
            }

            anyCatalog = true;
            std::ostringstream row;
            row << entry.blocks << " \"" << entry.name << "\" " << entry.type;
            if (!entry.mode.empty()) {
                row << "," << entry.mode;
            }
            enqueueBasicLine(prg, cursor, lineNo, row.str());
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        if (!anyCatalog) {
            enqueueBasicLine(prg, cursor, lineNo, "1 \"$\" PRG");
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        std::ostringstream freeLine;
        freeLine << listing.freeBlocks << " BLOCKS FREE.";
        enqueueBasicLine(prg, cursor, lineNo, freeLine.str());

        prg.push_back(0x00);
        prg.push_back(0x00);

        iecTxQueue.clear();
        for (uint8_t b : prg) {
            iecTxQueue.push_back(b);
        }
        iecDirectoryStubPrepared = true;
        iecDirectoryFromBlockBuffer = false;
        return true;
    }

    void enqueueBasicLine(std::vector<uint8_t> &prg, uint16_t &cursor, uint16_t lineNo, const std::string &text) {
        const uint16_t next = static_cast<uint16_t>(cursor + 2 + 2 + text.size() + 1);
        prg.push_back(static_cast<uint8_t>(next & 0xFF));
        prg.push_back(static_cast<uint8_t>((next >> 8) & 0xFF));
        prg.push_back(static_cast<uint8_t>(lineNo & 0xFF));
        prg.push_back(static_cast<uint8_t>((lineNo >> 8) & 0xFF));
        for (char c : text) {
            prg.push_back(static_cast<uint8_t>(c));
        }
        prg.push_back(0x00);
        cursor = next;
    }

    void buildDirectoryStubPayload() {
        std::vector<uint8_t> prg;
        prg.reserve(512);

        prg.push_back(0x01);
        prg.push_back(0x08);

        uint16_t cursor = 0x0801;
        enqueueBasicLine(prg, cursor, 0, "0 \"OPENCODE 1541\" 00 2A");

        uint16_t lineNo = 1;
        bool anyCatalog = false;
        for (size_t i = 0; i < iecCatalog.size(); ++i) {
            if (!iecCatalog[i].used) {
                continue;
            }
            if (!wildcardMatch(iecDirectoryWildcardPattern, iecCatalog[i].name)) {
                continue;
            }
            if (!iecDirectoryTypeFilter.empty() && iecCatalog[i].type != iecDirectoryTypeFilter) {
                continue;
            }
            if (!iecDirectoryModeFilter.empty()) {
                const bool modeMatch = (iecCatalog[i].mode == iecDirectoryModeFilter);
                if ((!iecDirectoryModeFilterNegated && !modeMatch) ||
                    (iecDirectoryModeFilterNegated && modeMatch)) {
                    continue;
                }
            }
            anyCatalog = true;
            std::ostringstream entry;
            entry << iecCatalog[i].blocks << " \"" << iecCatalog[i].name << "\" " << iecCatalog[i].type;
            if (!iecCatalog[i].mode.empty()) {
                entry << "," << iecCatalog[i].mode;
            }
            enqueueBasicLine(prg, cursor, lineNo, entry.str());
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        if (!anyCatalog) {
            enqueueBasicLine(prg, cursor, lineNo, "1 \"$\" PRG");
            lineNo = static_cast<uint16_t>(lineNo + 1);
        }

        std::ostringstream freeLine;
        freeLine << virtualBlocksFree() << " BLOCKS FREE.";
        enqueueBasicLine(prg, cursor, lineNo, freeLine.str());

        prg.push_back(0x00);
        prg.push_back(0x00);

        iecTxQueue.clear();
        for (uint8_t b : prg) {
            iecTxQueue.push_back(b);
        }
        iecDirectoryStubPrepared = true;
        iecDirectoryFromBlockBuffer = false;
    }

    bool processIecCommandByte(uint8_t cmd) {
        iecCommandSeen = true;
        lastIecCommand = cmd;
        iecCommandDispatchCount++;

        if ((cmd & 0xF0) == 0xE0) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecOpenListenChannels[ch] = false;
            iecOpenTalkChannels[ch] = false;
            iecChannelCloseCount[ch]++;
            if (iecActiveListenChannel == ch) {
                iecActiveListenChannel = 0xFF;
                iecExpectingNameBytes = false;
                if (ch == 15) {
                    iecCommandChannelBuffer.clear();
                }
            }
            if (iecActiveTalkChannel == ch) {
                iecActiveTalkChannel = 0xFF;
                if (ch == 0) {
                    iecTalkSa0Confirmed = false;
                }
            }
            return true;
        }

        // UNLISTEN / UNTALK (global)
        if (cmd == 0x3F) {
            if (iecExpectingNameBytes) {
                if (iecActiveListenChannel < 16) {
                    commitOpenNameFromBuffer(iecActiveListenChannel);
                }
                if (iecActiveListenChannel == 0 && isDirectoryRequest()) {
                    const DirectoryFilters filters = extractDirectoryFilters();
                    iecDirectoryWildcardPattern = filters.pattern;
                    iecDirectoryTypeFilter = filters.type;
                    iecDirectoryModeFilter = filters.mode;
                    iecDirectoryModeFilterNegated = filters.modeNegated;
                    const bool builtMounted = buildDirectoryPayloadFromMountedD64();
                    if (!builtMounted) {
                        buildDirectoryStubPayload();
                        if (iecBlockBufferValid) {
                            buildDirectoryFromBlockBufferPayload(0);
                            iecDirectoryFromBlockBuffer = true;
                        }
                    }
                    iecOpenTalkChannels[0] = true;
                }
                iecExpectingNameBytes = false;
            }

            if (iecActiveListenChannel == 15) {
                processCommandChannelBuffer();
            }

            iecListening = false;
            iecListenSecondary = 0xFF;
            return true;
        }
        if (cmd == 0x5F) {
            iecTalking = false;
            iecTalkSecondary = 0xFF;
            iecActiveTalkChannel = 0xFF;
            iecTalkSa0Confirmed = false;
            iecEoiPendingAck = revisionProfile.iecStrictEoiAck ? false : iecEoiPendingAck;
            iecEoiAckLowSeen = false;
            return true;
        }

        if (iecListening && (cmd & 0xF0) == 0xF0) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecListenSecondary = ch;
            iecActiveListenChannel = ch;
            iecOpenListenChannels[ch] = true;
            iecChannelOpenCount[ch]++;
            iecNameBuffer.clear();
            iecExpectingNameBytes = true;
            iecChannelOpenName[ch].clear();
            iecChannelOpenNameValid[ch] = false;
            return true;
        }

        if (iecTalking && (cmd & 0xF0) == 0x60) {
            const uint8_t ch = static_cast<uint8_t>(cmd & 0x0F);
            iecTalkSecondary = ch;
            iecActiveTalkChannel = ch;
            iecOpenTalkChannels[ch] = true;
            iecChannelOpenCount[ch]++;
            iecTalkSa0Confirmed = (ch == 0);
            if (ch == 0) {
                if (!iecDirectoryStubPrepared && iecKernelCompatAutoDirectoryOnTalk0) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
                if (iecTxQueue.empty() && iecDirectoryStubPrepared) {
                    if (iecDirectoryFromBlockBuffer) {
                        buildDirectoryFromBlockBufferPayload(ch);
                    } else {
                        buildDirectoryStubPayload();
                    }
                }
            } else if (ch == 15) {
                if (!iecCommandResponseQueue.empty()) {
                    buildCommandResponsePayload();
                } else {
                    buildStatusPayload();
                }
            } else {
                iecStatusLine = "70,NO CHANNEL,00,00";
                iecCommandSyntaxErrorCount++;
            }
            return true;
        }

        // LISTEN 0x20..0x3E, TALK 0x40..0x5E
        if ((cmd & 0xF0) == 0x20) {
            if (revisionProfile.iecCommandNeedsAtnLow && iecSerialState == IecSerialState::Command && iecATN) {
                return false;
            }
            iecListening = ((cmd & 0x1F) == (iecDeviceAddress & 0x1F));
            if (!iecListening) {
                iecExpectingNameBytes = false;
                iecListenSecondary = 0xFF;
                iecActiveListenChannel = 0xFF;
            } else {
                iecOpenListenChannels.fill(false);
                iecOpenListenChannels[0] = true;
                iecActiveListenChannel = 0;
                iecListenSecondary = 0;
                iecExpectingNameBytes = true;
                iecNameBuffer.clear();
            }
            return iecListening;
        }
        if ((cmd & 0xF0) == 0x40) {
            if (revisionProfile.iecCommandNeedsAtnLow && iecSerialState == IecSerialState::Command && iecATN) {
                return false;
            }
            iecTalking = ((cmd & 0x1F) == (iecDeviceAddress & 0x1F));
            if (iecTalking && iecKernelCompatAutoTalkDirectory) {
                if (!iecDirectoryStubPrepared) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
                if (iecActiveTalkChannel == 0xFF) {
                    iecTalkSecondary = 0;
                    iecActiveTalkChannel = 0;
                    iecOpenTalkChannels[0] = true;
                    iecTalkSa0Confirmed = true;
                }
                if (iecTxQueue.empty() && iecDirectoryStubPrepared) {
                    buildDirectoryStubPayload();
                }
            }
            if (iecTalking && iecKernelCompatForceTalkOnIcrSerial) {
                if (iecActiveTalkChannel == 0xFF) {
                    iecTalkSecondary = 0;
                    iecActiveTalkChannel = 0;
                    iecOpenTalkChannels[0] = true;
                    iecTalkSa0Confirmed = true;
                }
                if (!iecDirectoryStubPrepared) {
                    iecDirectoryWildcardPattern.clear();
                    iecDirectoryTypeFilter.clear();
                    iecDirectoryModeFilter.clear();
                    iecDirectoryModeFilterNegated = false;
                    buildDirectoryStubPayload();
                }
            }
            if (!iecTalking) {
                iecTalkSecondary = 0xFF;
                iecActiveTalkChannel = 0xFF;
                iecTalkSa0Confirmed = false;
            } else if (iecActiveTalkChannel != 0) {
                iecTalkSa0Confirmed = false;
            }
            return iecTalking;
        }

        return false;
    }
};
