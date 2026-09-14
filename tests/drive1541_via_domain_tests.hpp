#pragma once

#include <iostream>

#include "../drive1541_physical/drive_via_domain.hpp"

static void runDrive1541ViaDomainTests() {
    using drive1541_physical::DriveViaDomain;

    DriveViaDomain via;
    via.reset();

    via.write_io(0x180Eu, 0xC0u);
    via.write_io(0x1804u, 0x01u);
    via.write_io(0x1805u, 0x00u);
    via.tick(3);
    const std::uint8_t ifrAfterT1 = static_cast<std::uint8_t>(via.read_io(0x180Du) & 0x7Fu);
    if ((ifrAfterT1 & 0x40u) == 0) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: T1 underflow did not set IFR bit 6" << std::endl;
        assert(false);
    }

    via.write_io(0x180Du, 0x40u);
    const std::uint8_t ifrAfterClear = static_cast<std::uint8_t>(via.read_io(0x180Du) & 0x7Fu);
    if ((ifrAfterClear & 0x40u) != 0) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: IFR clear write did not clear bit 6" << std::endl;
        assert(false);
    }

    via.reset();
    via.write_io(0x1804u, 0x02u);
    via.write_io(0x1805u, 0x00u);
    via.tick(1);
    const std::uint8_t t1Lo1 = via.read_io(0x1804u);
    if (t1Lo1 != 0x01u) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: timer countdown step 1 mismatch" << std::endl;
        assert(false);
    }
    via.tick(1);
    const std::uint8_t t1Lo2 = via.read_io(0x1804u);
    if (t1Lo2 != 0x00u) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: timer countdown step 2 mismatch" << std::endl;
        assert(false);
    }
    via.tick(1);
    const std::uint8_t ifrAfterUnderflow = static_cast<std::uint8_t>(via.read_io(0x180Du) & 0x7Fu);
    if ((ifrAfterUnderflow & 0x40u) == 0) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: timer underflow did not assert IFR" << std::endl;
        assert(false);
    }

    via.reset();
    via.write_io(0x1C0Eu, 0xC0u);
    via.write_io(0x1C04u, 0x00u);
    via.write_io(0x1C05u, 0x00u);
    via.tick(1);
    if (!via.irq_asserted()) {
        std::cerr << "[1541 VIA DOMAIN] FAIL: composite IRQ not asserted when VIA2 interrupts" << std::endl;
        assert(false);
    }

    std::cerr << "[1541 VIA DOMAIN] PASS: dual VIA timer/IFR/IER behavior and composite IRQ" << std::endl;
}
