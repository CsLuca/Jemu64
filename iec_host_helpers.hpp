#pragma once

static void iecHostDriveLines(CIA6526 &cia2, Drive1541 &drive, bool atnHigh, bool clkHigh, bool dataHigh) {
    cia2.ddra = static_cast<uint8_t>(cia2.ddra | 0x38);

    uint8_t out = cia2.pra;
    if (atnHigh) out = static_cast<uint8_t>(out & static_cast<uint8_t>(~0x08));
    else         out = static_cast<uint8_t>(out | 0x08);
    if (clkHigh) out = static_cast<uint8_t>(out & static_cast<uint8_t>(~0x10));
    else         out = static_cast<uint8_t>(out | 0x10);
    if (dataHigh) out = static_cast<uint8_t>(out & static_cast<uint8_t>(~0x20));
    else          out = static_cast<uint8_t>(out | 0x20);
    cia2.pra = out;

    syncIecBus(cia2, drive);
}

static void iecHostStep(CIA6526 &cia2, Drive1541 &drive) {
    drive.tick();
    syncIecBus(cia2, drive);
}

static void iecHostSendBit(CIA6526 &cia2, Drive1541 &drive, bool atnHigh, uint8_t bit) {
    const bool dataHigh = (bit != 0);
    iecHostDriveLines(cia2, drive, atnHigh, false, dataHigh);
    iecHostStep(cia2, drive);
    iecHostDriveLines(cia2, drive, atnHigh, true, dataHigh);
    iecHostStep(cia2, drive);
}

static void iecHostSendByte(CIA6526 &cia2, Drive1541 &drive, bool atnHigh, uint8_t byte) {
    for (int i = 0; i < 8; ++i) {
        const uint8_t bit = static_cast<uint8_t>((byte >> i) & 0x01);
        iecHostSendBit(cia2, drive, atnHigh, bit);
    }
}

static uint8_t iecHostReadByte(CIA6526 &cia2, Drive1541 &drive) {
    uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
        iecHostDriveLines(cia2, drive, true, false, true);
        iecHostStep(cia2, drive);
        const bool dataInHigh = (cia2.getPortACombined() & 0x80) != 0;
        value = static_cast<uint8_t>(value | ((dataInHigh ? 1 : 0) << i));
        iecHostDriveLines(cia2, drive, true, true, true);
        iecHostStep(cia2, drive);
    }
    return value;
}

static void iecHostEoiAckPulse(CIA6526 &cia2, Drive1541 &drive) {
    iecHostDriveLines(cia2, drive, true, true, false);
    iecHostStep(cia2, drive);
    iecHostDriveLines(cia2, drive, true, true, true);
    iecHostStep(cia2, drive);
}

static std::string iecHostReadTextLine(CIA6526 &cia2, Drive1541 &drive, size_t maxLen) {
    std::string out;
    out.reserve(maxLen);
    for (size_t i = 0; i < maxLen; ++i) {
        const uint8_t b = iecHostReadByte(cia2, drive);
        if (b == 0x0D) {
            break;
        }
        out.push_back(static_cast<char>(b));
    }
    return out;
}
