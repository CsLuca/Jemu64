#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

void runViciiChecklist(VICII &vic, Bus &bus) {
    std::cout << "[VIC CHECK] Starting VIC-II checklist..." << std::endl;

    // -----------------------------------------------------------------
    // 0) PLA table integrity sanity checks (cycle/phase/pixel -> rule)
    // -----------------------------------------------------------------
    {
        const auto &pla = VICII::vicPlaTable();
        assert(pla.size() == (2u * 63u * 8u));

        auto ruleAt = [&](VICII::VicHalfPhase ph, int cyc, int pix) -> const VICII::VicStepRule& {
            return pla[VICII::plaIndex(ph, cyc, pix)];
        };

        const auto &v150 = ruleAt(VICII::VIC_PHI1, 15, 0);
        assert(v150.op == VICII::VIC_OP_VIDEO);
        assert(v150.fetch == VICII::VIC_FETCH_VIDEO_MEMPTR_SCREEN);
        assert(!v150.busWindow);

        const auto &v251 = ruleAt(VICII::VIC_PHI2, 25, 1);
        assert(v251.op == VICII::VIC_OP_VIDEO);
        assert(v251.fetch == VICII::VIC_FETCH_NONE);
        assert(v251.busWindow);

        const auto &s80 = ruleAt(VICII::VIC_PHI1, 8, 0);
        assert(s80.op == VICII::VIC_OP_SPRITE);
        assert(s80.fetch == VICII::VIC_FETCH_SPRITE_PTR);
        assert(!s80.busWindow);

        const auto &s92 = ruleAt(VICII::VIC_PHI1, 9, 2);
        assert(s92.op == VICII::VIC_OP_SPRITE);
        assert(s92.fetch == VICII::VIC_FETCH_SPRITE_DATA1);

        const auto &r00 = ruleAt(VICII::VIC_PHI1, 0, 0);
        assert(r00.op == VICII::VIC_OP_REFRESH);
        assert(r00.fetch == VICII::VIC_FETCH_REFRESH);

        const auto &r57p2 = ruleAt(VICII::VIC_PHI2, 57, 3);
        assert(r57p2.op == VICII::VIC_OP_SPRITE);
        assert(r57p2.busWindow);

        const auto &tickBadline = ruleAt(VICII::VIC_PHI1, 12, 0);
        assert((tickBadline.tickActions & VICII::VIC_TICK_ACTION_BADLINE_LATCH) != 0);

        const auto &tickSpriteDma = ruleAt(VICII::VIC_PHI1, 55, 0);
        assert((tickSpriteDma.tickActions & VICII::VIC_TICK_ACTION_SPRITE_DMA_UPDATE) != 0);

        const auto &tickRelease = ruleAt(VICII::VIC_PHI2, 55, 0);
        assert((tickRelease.tickActions & VICII::VIC_TICK_ACTION_BADLINE_RELEASE) != 0);

        for (int ph = 0; ph < 2; ++ph) {
            for (int cyc = 0; cyc < 63; ++cyc) {
                for (int pix = 0; pix < 8; ++pix) {
                    const auto &r = ruleAt((ph == 0) ? VICII::VIC_PHI1 : VICII::VIC_PHI2, cyc, pix);
                    assert(r.op <= VICII::VIC_OP_SPRITE);
                    assert(r.fetch <= VICII::VIC_FETCH_SPRITE_DATA2);
                    assert(r.fetchPlan <= VICII::VIC_PLAN_SPRITE_DATA2);
                    assert(r.commitPlan <= VICII::VIC_COMMIT_SPRITE_DATA2);
                    if (ph == 1) {
                        assert(r.fetch == VICII::VIC_FETCH_NONE);
                    }
                    if (r.op == VICII::VIC_OP_REFRESH && ph == 1) {
                        assert(!r.busWindow);
                    }
                    if ((r.predicateMask & VICII::VIC_PRED_REQUIRE_BADLINE) != 0) {
                        assert((r.tickActions & VICII::VIC_TICK_ACTION_BADLINE_LATCH) != 0);
                    }
                    if ((r.predicateMask & VICII::VIC_PRED_REQUIRE_BUSLOCKED) != 0) {
                        assert((r.tickActions & VICII::VIC_TICK_ACTION_BADLINE_RELEASE) != 0);
                    }
                    if (r.fetch != VICII::VIC_FETCH_NONE) {
                        assert(r.fetchPlan != VICII::VIC_PLAN_NONE);
                        assert(r.commitPlan != VICII::VIC_COMMIT_NONE);
                    }
                }
            }
        }

        const std::string plaDigest = VICII::plaSnapshotDigestHex();
        std::string snapshotDigest;
        {
            std::ifstream snapIn("reference/edge/vic_pla_snapshot.md", std::ios::binary);
            if (snapIn.is_open()) {
                std::string line;
                while (std::getline(snapIn, line)) {
                    const std::string key = "- digest: `";
                    const size_t p = line.find(key);
                    if (p != std::string::npos) {
                        const size_t s = p + key.size();
                        const size_t e = line.find('`', s);
                        if (e != std::string::npos && e > s) {
                            snapshotDigest = line.substr(s, e - s);
                        }
                        break;
                    }
                }
            }
        }

        const bool exportPla = (std::getenv("VIC_EXPORT_PLA_SPEC") != nullptr);
        if (snapshotDigest.empty() && exportPla) {
            const bool okExport = VICII::exportPlaSpecAndSnapshot(
                "reference/edge/vic_pla_spec.csv",
                "reference/edge/vic_pla_snapshot.md");
            if (!okExport) {
                std::cerr << "[VIC CHECK] FAIL: PLA spec export failed" << std::endl;
                assert(false);
            }

            std::ifstream snapIn("reference/edge/vic_pla_snapshot.md", std::ios::binary);
            if (snapIn.is_open()) {
                std::string line;
                while (std::getline(snapIn, line)) {
                    const std::string key = "- digest: `";
                    const size_t p = line.find(key);
                    if (p != std::string::npos) {
                        const size_t s = p + key.size();
                        const size_t e = line.find('`', s);
                        if (e != std::string::npos && e > s) {
                            snapshotDigest = line.substr(s, e - s);
                        }
                        break;
                    }
                }
            }
        }

        if (snapshotDigest.empty()) {
            std::cerr << "[VIC CHECK] FAIL: missing PLA snapshot digest in reference/edge/vic_pla_snapshot.md" << std::endl;
            assert(false);
        }
        if (plaDigest != snapshotDigest) {
            std::cerr << "[VIC CHECK] FAIL: PLA digest mismatch got=" << plaDigest
                      << " expected=" << snapshotDigest << std::endl;
            assert(false);
        }

        if (exportPla) {
            const bool okExport = VICII::exportPlaSpecAndSnapshot(
                "reference/edge/vic_pla_spec.csv",
                "reference/edge/vic_pla_snapshot.md");
            if (!okExport) {
                std::cerr << "[VIC CHECK] FAIL: PLA spec export failed" << std::endl;
                assert(false);
            }
        }

        std::cout << "[VIC CHECK] PLA table integrity: PASS" << std::endl;
    }

    // Keep and restore state so this function is non-destructive for normal flow.
    const int savedCycle = vic.cycleInLine;
    const int savedRasterLine = vic.rasterLine;
    const int savedPixelClock = vic.pixelClock;
    const uint8_t savedCtrl1 = vic.ctrl1;
    const uint8_t savedRaster = vic.raster;
    const uint8_t savedIrqMask = vic.irqMask;
    const uint8_t savedIrqFlags = vic.irqFlags;
    const bool savedRasterPending = vic.rasterIRQPending;
    const bool savedRasterMaskEdgeArmed = vic.rasterIrqMaskEdgeArmed;
    const bool savedVspTriggered = vic.vspTriggered;
    const bool savedFldTriggered = vic.fldTriggered;
    const bool savedBusLocked = vic.busLocked;
    const bool savedBaLine = vic.baLine;
    const bool savedAecLine = vic.aecLine;
    const bool savedBadlineActive = vic.badlineActive;
    const uint8_t savedSpriteDmaMask = vic.spriteDmaMask;
    uint8_t savedSpritePointerLatch[8];
    uint8_t savedSpriteDataLatch[8];
    uint8_t savedSpriteDataBytes[8][3];
    for (int s = 0; s < 8; ++s) {
        savedSpritePointerLatch[s] = vic.spritePointerLatch[s];
        savedSpriteDataLatch[s] = vic.spriteDataLatch[s];
        for (int b = 0; b < 3; ++b) {
            savedSpriteDataBytes[s][b] = vic.spriteDataBytes[s][b];
        }
    }
    const uint16_t savedVc = vic.vc;
    const uint16_t savedVcBase = vic.vcBase;
    const uint8_t savedRc = vic.rc;
    const uint64_t savedFrameHashCurrent = vic.frameHashCurrent;
    const uint64_t savedFrameHashLast = vic.frameHashLast;
    const bool savedFrameHashValid = vic.frameHashValid;
    const size_t savedRasterEventCount = vic.rasterEventCount;
    auto savedBusReadTap = bus.readTap;
    auto savedBusWriteTap = bus.writeTap;
    struct VicBusSample {
        int line;
        int cycle;
        int pixel;
        uint16_t addr;
    };
    std::vector<VicBusSample> samples;
    samples.reserve(256);

    // -----------------------------------------------------------------
    // 1) Raster IRQ timing
    // -----------------------------------------------------------------
    vic.ctrl1 = 0x10;            // display enabled, raster MSB=0
    vic.raster = 50;             // compare line low byte
    vic.write(0xD01A, 0x01);     // enable raster IRQ (+ revision-specific arming)
    vic.irqFlags = 0x00;
    vic.rasterIRQPending = false;
    vic.clearRasterEventLog();
    vic.rasterLine = 49;
    vic.cycleInLine = 62;
    vic.pixelClock = 7;

    vic.tickPixel();
    assert(vic.rasterLine == 50);
    assert((vic.irqFlags & 0x01) != 0);
    assert(vic.rasterIRQPending);
    assert(vic.getRasterEventCount() >= 1);

    // ACK behavior ($D019 write with bit0 set clears raster IRQ latch bit).
    vic.write(0xD019, 0x01);
    assert((vic.irqFlags & 0x01) == 0);

    // Advance one full line; pending must clear when compare no longer matches.
    for (int i = 0; i < (63 * 8); ++i) {
        vic.tickPixel();
    }
    assert(vic.rasterLine == 51);
    assert(!vic.rasterIRQPending);

    std::cout << "[VIC CHECK] Raster IRQ timing: PASS" << std::endl;

    // Revision-specific raster IRQ gate: 8565 requires mask-edge arm.
    {
        VICII gate6569;
        gate6569.setRevision(VICII::REV_6569);
        gate6569.ctrl1 = 0x10;
        gate6569.raster = 50;
        gate6569.rasterLine = 49;
        gate6569.cycleInLine = 62;
        gate6569.pixelClock = 7;
        gate6569.irqMask = 0x01;
        gate6569.tickPixel();
        assert((gate6569.irqFlags & 0x01) != 0);

        VICII gate8565;
        gate8565.setRevision(VICII::REV_8565);
        gate8565.ctrl1 = 0x10;
        gate8565.raster = 50;
        gate8565.rasterLine = 49;
        gate8565.cycleInLine = 62;
        gate8565.pixelClock = 7;
        gate8565.irqMask = 0x01;
        gate8565.rasterIrqMaskEdgeArmed = false;
        gate8565.tickPixel();
        assert((gate8565.irqFlags & 0x01) == 0);
        gate8565.write(0xD01A, 0x01);
        gate8565.rasterLine = 49;
        gate8565.cycleInLine = 62;
        gate8565.pixelClock = 7;
        gate8565.tickPixel();
        assert((gate8565.irqFlags & 0x01) != 0);
    }

    // Revision-specific sprite DMA gate: 8565 requires display enable.
    {
        VICII dma6569;
        dma6569.setRevision(VICII::REV_6569);
        dma6569.ctrl1 = 0x00;
        dma6569.rasterLine = 48;
        dma6569.sprEnable = 0x01;
        dma6569.sprY[0] = 48;
        dma6569.cycleInLine = 55;
        dma6569.pixelClock = 0;
        dma6569.halfPhase = VICII::VIC_PHI1;
        dma6569.tickHalf();
        assert((dma6569.spriteDmaMask & 0x01) != 0);

        VICII dma8565;
        dma8565.setRevision(VICII::REV_8565);
        dma8565.ctrl1 = 0x00;
        dma8565.rasterLine = 48;
        dma8565.sprEnable = 0x01;
        dma8565.sprY[0] = 48;
        dma8565.cycleInLine = 55;
        dma8565.pixelClock = 0;
        dma8565.halfPhase = VICII::VIC_PHI1;
        dma8565.tickHalf();
        assert((dma8565.spriteDmaMask & 0x01) == 0);

        dma8565.ctrl1 = 0x10;
        dma8565.cycleInLine = 55;
        dma8565.pixelClock = 0;
        dma8565.halfPhase = VICII::VIC_PHI1;
        dma8565.tickHalf();
        assert((dma8565.spriteDmaMask & 0x01) != 0);
    }

    {
        VICII fld6569;
        fld6569.setRevision(VICII::REV_6569);
        fld6569.cycleInLine = 15;
        fld6569.pixelClock = 0;
        fld6569.badlineActive = false;
        fld6569.ctrl1 = 0x10;
        fld6569.write(0xD011, 0x12);
        assert(fld6569.fldTriggered);

        VICII fld8565;
        fld8565.setRevision(VICII::REV_8565);
        fld8565.cycleInLine = 15;
        fld8565.pixelClock = 0;
        fld8565.badlineActive = false;
        fld8565.ctrl1 = 0x10;
        fld8565.write(0xD011, 0x12);
        assert(!fld8565.fldTriggered);
        fld8565.badlineActive = true;
        fld8565.write(0xD011, 0x13);
        assert(fld8565.fldTriggered);
    }

    {
        VICII vsp6569;
        vsp6569.setRevision(VICII::REV_6569);
        vsp6569.cycleInLine = 14;
        vsp6569.pixelClock = 0;
        vsp6569.ctrl2 = 0x00;
        vsp6569.write(0xD016, 0x08);
        assert(vsp6569.vspTriggered);

        VICII vsp8565;
        vsp8565.setRevision(VICII::REV_8565);
        vsp8565.cycleInLine = 14;
        vsp8565.pixelClock = 0;
        vsp8565.ctrl2 = 0x00;
        vsp8565.write(0xD016, 0x08);
        assert(!vsp8565.vspTriggered);
    }

    // -----------------------------------------------------------------
    // 2) Bad line gating and bus lock window
    // -----------------------------------------------------------------
    vic.ctrl1 = 0x10 | 0x00;     // display enabled, y-scroll=0
    vic.rasterLine = 48;         // visible line, badline when (line & 7) == y-scroll
    vic.cycleInLine = 12;
    vic.pixelClock = 0;
    vic.busLocked = false;
    vic.badlineActive = false;
    vic.clearRasterEventLog();

    vic.tickPixel();
    assert(vic.busLocked);
    assert(vic.badlineActive);

    // The current implementation releases at cycle 55 / pixelClock 0.
    bool released = false;
    for (int i = 0; i < 600; ++i) {
        vic.tickPixel();
        if (!vic.busLocked) {
            released = true;
            break;
        }
    }
    assert(released);
    assert(!vic.badlineActive);

    std::cout << "[VIC CHECK] Bad line lock/release timing: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 3) Sprite DMA / fetch contention basics (isFetching contract)
    // -----------------------------------------------------------------
    vic.cycleInLine = 15;
    vic.pixelClock = 0;
    assert(vic.isFetching());
    vic.pixelClock = 1;
    assert(vic.isFetching());
    vic.pixelClock = 2;
    assert(vic.isFetching());
    vic.pixelClock = 3;
    assert(!vic.isFetching());

    vic.cycleInLine = 8;
    vic.pixelClock = 0;
    vic.spriteDmaMask = 0;
    assert(!vic.isFetching());
    vic.spriteDmaMask = 0x01;
    assert(vic.isFetching());
    vic.pixelClock = 1;
    assert(vic.isFetching());
    vic.pixelClock = 2;
    assert(vic.isFetching());
    vic.pixelClock = 3;
    assert(vic.isFetching());
    vic.pixelClock = 4;
    assert(!vic.isFetching());

    std::cout << "[VIC CHECK] Fetch contention contract (including sprite slots): PASS" << std::endl;

    // -----------------------------------------------------------------
    // 3b) BA/AEC low-level gating + badline sample point
    // -----------------------------------------------------------------
    vic.ctrl1 = 0x10 | 0x00;
    vic.rasterLine = 48;
    vic.cycleInLine = 12;
    vic.pixelClock = 0;
    vic.spriteDmaMask = 0x01;
    vic.busLocked = false;
    vic.badlineActive = false;
    vic.tickPixel(); // evaluates badline sample at cycle 12, pixel 0
    assert(vic.badlineActive);
    assert(vic.busLocked);
    assert(!vic.baLine);
    assert(!vic.aecLine);

    // Move into non-fetch substep and verify release of BA/AEC only when not owned by VIC.
    vic.cycleInLine = 10;
    vic.pixelClock = 6;
    vic.spriteDmaMask = 0;
    vic.busLocked = false;
    vic.badlineActive = false;
    vic.tickPixel();
    assert(vic.baLine);
    assert(vic.aecLine);

    std::cout << "[VIC CHECK] BA/AEC subcycle gating + badline sample: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 3c) Sprite pointer + 3-byte data fetch sequence
    // -----------------------------------------------------------------
    samples.clear();
    bus.readTap = [&](uint16_t addr, uint8_t) {
        samples.push_back(VicBusSample{vic.rasterLine, vic.cycleInLine, vic.pixelClock, addr});
    };

    vic.sprEnable = 0x01;
    vic.sprY[0] = 48;
    vic.spriteDmaMask = 0x01;
    vic.rasterLine = 48;
    vic.cycleInLine = 8;
    vic.pixelClock = 0;
    vic.halfPhase = VICII::VIC_PHI1;
    bus.memory[0x03F8] = 0x20;
    bus.memory[0x0800] = 0x11;
    bus.memory[0x0801] = 0x22;
    bus.memory[0x0802] = 0x33;

    for (int i = 0; i < 8; ++i) {
        vic.tickHalf();
    }

    int idxPtr = -1;
    int idxD0 = -1;
    int idxD1 = -1;
    int idxD2 = -1;
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto &ev = samples[i];
        if (ev.line != 48 || ev.cycle < 8 || ev.cycle > 14) {
            continue;
        }
        if (idxPtr < 0 && ev.addr == 0x03F8) idxPtr = static_cast<int>(i);
        if (idxD0 < 0 && ev.addr == 0x0800) idxD0 = static_cast<int>(i);
        if (idxD1 < 0 && ev.addr == 0x0801) idxD1 = static_cast<int>(i);
        if (idxD2 < 0 && ev.addr == 0x0802) idxD2 = static_cast<int>(i);
    }
    assert(idxPtr >= 0);
    assert(idxD0 >= 0);
    assert(idxD1 >= 0);
    assert(idxD2 >= 0);
    assert(idxPtr < idxD0);
    assert(idxD0 < idxD1);
    assert(idxD1 < idxD2);
    assert(vic.spriteDataBytes[0][0] == 0x11);
    assert(vic.spriteDataBytes[0][1] == 0x22);
    assert(vic.spriteDataBytes[0][2] == 0x33);

    std::cout << "[VIC CHECK] Sprite pointer/data(3-byte) micro-sequence: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 3d) Half-cycle trace battery (half,cycle,phi,addr,rw,ba,aec,vc,rc)
    // -----------------------------------------------------------------
    struct HalfTraceRow {
        uint64_t half;
        int cycle;
        int pixel;
        int phi;
        uint16_t addr;
        char rw;
        int ba;
        int aec;
        uint16_t vc;
        uint8_t rc;
    };
    std::vector<HalfTraceRow> halfTrace;
    halfTrace.reserve(256);

    auto captureHalf = [&](uint16_t addr, char rw) {
        const bool isSpritePtr = (addr >= 0x03F8 && addr <= 0x03FF);
        const bool isSpriteData = (addr >= 0x0800 && addr <= 0x08FF);
        const bool isVideoCtl = (addr == 0xD018);
        const bool isColorRam = (addr >= 0xD800 && addr <= 0xDBE7);
        if (!(isSpritePtr || isSpriteData || isVideoCtl || isColorRam)) {
            return;
        }
        halfTrace.push_back(HalfTraceRow{
            vic.halfTickCounter,
            vic.cycleInLine,
            vic.pixelClock,
            (vic.halfPhase == VICII::VIC_PHI1) ? 1 : 2,
            addr,
            rw,
            vic.baLine ? 1 : 0,
            vic.aecLine ? 1 : 0,
            vic.vc,
            vic.rc
        });
    };

    bus.readTap = [&](uint16_t addr, uint8_t) { captureHalf(addr, 'R'); };
    bus.writeTap = [&](uint16_t addr, uint8_t) { captureHalf(addr, 'W'); };

    vic.ctrl1 = 0x10 | 0x00;
    vic.sprEnable = 0x01;
    vic.sprY[0] = 48;
    vic.spriteDmaMask = 0x01;
    vic.rasterLine = 48;
    vic.cycleInLine = 8;
    vic.pixelClock = 0;
    vic.halfPhase = VICII::VIC_PHI1;
    bus.memory[0x03F8] = 0x20;
    bus.memory[0x0800] = 0x11;
    bus.memory[0x0801] = 0x22;
    bus.memory[0x0802] = 0x33;

    for (int i = 0; i < 64; ++i) {
        vic.tickHalf();
    }

    assert(!halfTrace.empty());
    for (const auto &row : halfTrace) {
        assert(row.phi == 2); // tracked bus sample/read/write is PHI2-only in this model
    }

    if (const char *refHalfTrace = std::getenv("VIC_REF_HALF_TRACE")) {
        std::ifstream in(refHalfTrace, std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "[VIC CHECK] FAIL: cannot open VIC_REF_HALF_TRACE=" << refHalfTrace << std::endl;
            assert(false);
        }
        std::string header;
        std::getline(in, header);
        size_t idx = 0;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            assert(idx < halfTrace.size());

            std::ostringstream cur;
            cur << halfTrace[idx].half << ","
                << halfTrace[idx].cycle << ","
                << halfTrace[idx].pixel << ","
                << halfTrace[idx].phi << ","
                << std::hex << halfTrace[idx].addr << std::dec << ","
                << halfTrace[idx].rw << ","
                << halfTrace[idx].ba << ","
                << halfTrace[idx].aec << ","
                << std::hex << halfTrace[idx].vc << std::dec << ","
                << static_cast<int>(halfTrace[idx].rc);
            if (line != cur.str()) {
                std::cerr << "[VIC CHECK] FAIL: half-trace mismatch at row " << idx
                          << " expected='" << line << "' got='" << cur.str() << "'"
                          << std::endl;
                assert(false);
            }
            idx++;
        }
        assert(idx == halfTrace.size());
    }

    std::cout << "[VIC CHECK] Half-cycle trace battery: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 4) Sprite DMA activation/deactivation and raster events
    // -----------------------------------------------------------------
    vic.clearRasterEventLog();
    vic.sprEnable = 0x01;
    vic.sprY[0] = 90;
    vic.spriteDmaMask = 0;
    vic.rasterLine = 90;
    vic.cycleInLine = 55;
    vic.pixelClock = 0;
    vic.tickPixel();
    assert((vic.spriteDmaMask & 0x01) != 0);

    for (int i = 0; i < (63 * 8 * 22); ++i) {
        vic.tickPixel();
    }
    assert((vic.spriteDmaMask & 0x01) == 0);

    const uint64_t spriteEventDigest = vic.getRasterEventDigest();
    assert(spriteEventDigest != 0);

    std::cout << "[VIC CHECK] Sprite DMA lifecycle + event log: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 5) Badline + sprite DMA collision micro-step priority
    // -----------------------------------------------------------------

    samples.clear();
    bus.readTap = [&](uint16_t addr, uint8_t) {
        samples.push_back(VicBusSample{vic.rasterLine, vic.cycleInLine, vic.pixelClock, addr});
    };

    vic.ctrl1 = 0x10 | 0x00; // display enable + yscroll=0 -> badline at line 48,56,...
    vic.sprEnable = 0x01;
    vic.sprY[0] = 48;
    vic.spriteDmaMask = 0x01; // force active DMA to create overlap window
    vic.rasterLine = 48;
    vic.cycleInLine = 11;
    vic.pixelClock = 7;

    for (int i = 0; i < (8 * 8); ++i) {
        vic.tickPixel();
    }

    bool sawSpritePtrInOverlap = false;
    bool sawVideoCAt15 = false;
    bool sawVideoHAt15 = false;
    bool sawVideoGAt15 = false;
    bool badPriority = false;

    for (size_t i = 0; i < samples.size(); ++i) {
        const VicBusSample &ev = samples[i];
        const bool spritePtr = (ev.addr >= 0x03F8 && ev.addr <= 0x03FF);
        const bool colorRead = (ev.addr >= 0xD800 && ev.addr <= 0xDBE7);

        if (ev.line == 48 && ev.cycle >= 12 && ev.cycle <= 14) {
            if (spritePtr && ev.pixel == 0) {
                sawSpritePtrInOverlap = true;
            }
        }

        if (ev.line == 48 && ev.cycle == 15) {
            if (spritePtr) {
                badPriority = true;
            }
            if (ev.pixel == 0 && ev.addr == 0xD018) {
                sawVideoCAt15 = true;
            }
            if (ev.pixel == 1 && colorRead) {
                sawVideoHAt15 = true;
            }
            if (ev.pixel == 2 && !spritePtr && ev.addr != 0xD018 && !colorRead) {
                sawVideoGAt15 = true;
            }
        }
    }

    assert(sawSpritePtrInOverlap);
    assert(sawVideoCAt15);
    assert(sawVideoHAt15);
    assert(sawVideoGAt15);
    assert(!badPriority);

    std::cout << "[VIC CHECK] Badline+sprite DMA collision micro-step priority: PASS" << std::endl;

    // -----------------------------------------------------------------
    // 6) Visual smoke checks (basic pixel color expectations)
    // -----------------------------------------------------------------
    bus.memory[0xD020] = 0x0E; // border color
    bus.memory[0xD021] = 0x06; // background color

    // Border region sample: line outside visible area, cycle 0 must paint border chunk.
    vic.rasterLine = 40;
    vic.cycleInLine = 0;
    vic.pixelClock = 0;
    vic.tickPixel();
    for (int x = 0; x < 8; ++x) {
        assert(vic.pixelRowBuffer[x] == 0x0E);
    }

    // Visible region sample: first visible character cycle should output background if no glyph data staged.
    vic.rasterLine = 48;
    vic.cycleInLine = 15;
    vic.pixelClock = 0;
    vic.tickPixel();
    assert(vic.pixelRowBuffer[0] == 0x06);

    std::cout << "[VIC CHECK] Visual smoke checks (border/background): PASS" << std::endl;

    // -----------------------------------------------------------------
    // 7) Frame hash + raster-event digest determinism and optional reference diff
    // -----------------------------------------------------------------
    vic.resetFrameHashing();
    vic.clearRasterEventLog();
    vic.ctrl1 = 0x10;
    vic.raster = 120;
    vic.irqMask = 0x01;
    vic.rasterIRQPending = false;
    vic.rasterLine = 0;
    vic.cycleInLine = 0;
    vic.pixelClock = 0;
    vic.sprEnable = 0x03;
    vic.sprY[0] = 40;
    vic.sprY[1] = 120;
    for (int i = 0; i < (312 * 63 * 8); ++i) {
        vic.tickPixel();
    }
    assert(vic.hasFrameHash());
    const uint64_t frameHash = vic.getLastFrameHash();
    const uint64_t eventDigest = vic.getRasterEventDigest();
    assert(frameHash != 0);
    assert(eventDigest != 0);

    if (const char *refHash = std::getenv("VIC_REF_FRAME_HASH")) {
        const uint64_t expected = static_cast<uint64_t>(std::strtoull(refHash, nullptr, 16));
        if (frameHash != expected) {
            std::cerr << "[VIC CHECK] FAIL: frame hash mismatch expected=$" << std::hex << expected
                      << " got=$" << frameHash << std::dec << std::endl;
            assert(false);
        }
    }
    if (const char *refEv = std::getenv("VIC_REF_EVENT_DIGEST")) {
        const uint64_t expected = static_cast<uint64_t>(std::strtoull(refEv, nullptr, 16));
        if (eventDigest != expected) {
            std::cerr << "[VIC CHECK] FAIL: raster event digest mismatch expected=$" << std::hex << expected
                      << " got=$" << eventDigest << std::dec << std::endl;
            assert(false);
        }
    }

    std::cerr << "[VIC CHECK] Frame hash/event digest: PASS"
              << " frame=$" << std::hex << frameHash
              << " events=$" << eventDigest
              << std::dec << std::endl;

    // Restore state.
    vic.cycleInLine = savedCycle;
    vic.rasterLine = savedRasterLine;
    vic.pixelClock = savedPixelClock;
    vic.ctrl1 = savedCtrl1;
    vic.raster = savedRaster;
    vic.irqMask = savedIrqMask;
    vic.irqFlags = savedIrqFlags;
    vic.rasterIRQPending = savedRasterPending;
    vic.rasterIrqMaskEdgeArmed = savedRasterMaskEdgeArmed;
    vic.vspTriggered = savedVspTriggered;
    vic.fldTriggered = savedFldTriggered;
    vic.busLocked = savedBusLocked;
    vic.baLine = savedBaLine;
    vic.aecLine = savedAecLine;
    vic.badlineActive = savedBadlineActive;
    vic.spriteDmaMask = savedSpriteDmaMask;
    for (int s = 0; s < 8; ++s) {
        vic.spritePointerLatch[s] = savedSpritePointerLatch[s];
        vic.spriteDataLatch[s] = savedSpriteDataLatch[s];
        for (int b = 0; b < 3; ++b) {
            vic.spriteDataBytes[s][b] = savedSpriteDataBytes[s][b];
        }
    }
    vic.vc = savedVc;
    vic.vcBase = savedVcBase;
    vic.rc = savedRc;
    vic.frameHashCurrent = savedFrameHashCurrent;
    vic.frameHashLast = savedFrameHashLast;
    vic.frameHashValid = savedFrameHashValid;
    vic.rasterEventCount = savedRasterEventCount;
    bus.readTap = savedBusReadTap;
    bus.writeTap = savedBusWriteTap;

    std::cout << "[VIC CHECK] Checklist completed successfully." << std::endl;
}
