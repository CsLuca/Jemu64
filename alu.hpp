#pragma once

class ALU {
public:
    static void setFlag(uint8_t &P, StatusFlags f, bool v) {
        if (v) P |= f; else P &= ~f;
    }

    static bool getFlag(uint8_t P, StatusFlags f) {
        return (P & f) != 0;
    }

    // ---------------- ADC ----------------
    // --- ADC (binario + BCD NMOS) ---
    static void adc(Registers &R, uint8_t val) {
        uint8_t A = R.A;
        uint8_t C = getFlag(R.P, CARRY) ? 1 : 0;

        uint16_t sum = uint16_t(A) + uint16_t(val) + uint16_t(C);
        uint8_t binaryResult = static_cast<uint8_t>(sum & 0xFF);
        bool overflow = ((~(A ^ val) & (A ^ binaryResult)) & 0x80) != 0;

        if (getFlag(R.P, DECIMAL)) {
            const uint8_t n1l = static_cast<uint8_t>(A & 0x0F);
            const uint8_t n1h = static_cast<uint8_t>(A & 0xF0);
            const uint8_t n2l = static_cast<uint8_t>(val & 0x0F);
            const uint8_t n2h = static_cast<uint8_t>(val & 0xF0);

            uint16_t low = static_cast<uint16_t>(n1l) + static_cast<uint16_t>(n2l) + static_cast<uint16_t>(C);
            uint8_t x = 0;
            if (low >= 0x0A) {
                x = 1;
                low = (low + 0x06) & 0x0F;
            } else {
                low &= 0x0F;
            }

            uint16_t acc = static_cast<uint16_t>((low & 0x0F) | n1h);
            const uint8_t highAdd = x ? static_cast<uint8_t>(n2h | 0x0F) : n2h;
            acc = acc + static_cast<uint16_t>(highAdd) + static_cast<uint16_t>(x ? 1 : 0);

            const bool carryAfterHigh = (acc > 0xFF);
            uint8_t acc8 = static_cast<uint8_t>(acc & 0xFF);
            const bool carryOut = carryAfterHigh || (acc8 >= 0xA0);
            if (carryOut) {
                acc8 = static_cast<uint8_t>((static_cast<uint16_t>(acc8) + 0x60) & 0xFF);
            }

            R.A = acc8;
            setFlag(R.P, CARRY, carryOut);
            setFlag(R.P, ZERO, binaryResult == 0);
            setFlag(R.P, NEGATIVE, binaryResult & 0x80);
            setFlag(R.P, OVERFLOW, overflow);
            return;
        }

        R.A = binaryResult;
        setFlag(R.P, CARRY, sum > 0xFF);
        setFlag(R.P, ZERO, R.A == 0);
        setFlag(R.P, NEGATIVE, R.A & 0x80);
        setFlag(R.P, OVERFLOW, overflow);
    }

    // ---------------- SBC ----------------
    // --- SBC (binario + BCD NMOS) ---
    static void sbc(Registers &R, uint8_t val) {
        uint8_t A = R.A;
        uint8_t C = getFlag(R.P, CARRY) ? 1 : 0;

        int16_t diff = int16_t(A) - int16_t(val) - int16_t(1 - C);
        uint8_t binaryResult = static_cast<uint8_t>(diff & 0xFF);
        bool overflow = ((A ^ val) & (A ^ binaryResult) & 0x80) != 0;

        if (getFlag(R.P, DECIMAL)) {
            const uint8_t n1l = static_cast<uint8_t>(A & 0x0F);
            const uint8_t n1h = static_cast<uint8_t>(A & 0xF0);
            const uint8_t n2l = static_cast<uint8_t>(val & 0x0F);
            const uint8_t n2h = static_cast<uint8_t>(val & 0xF0);

            int16_t low = static_cast<int16_t>(n1l) - static_cast<int16_t>(n2l) - static_cast<int16_t>(1 - C);
            uint8_t x = 0;
            bool carryToHigh = (low >= 0);
            if (!carryToHigh) {
                x = 1;
                low = (low - 0x06) & 0x0F;
                carryToHigh = false;
            }

            uint8_t acc8 = static_cast<uint8_t>((low & 0x0F) | n1h);
            const uint8_t highSub = x ? static_cast<uint8_t>(n2h | 0x0F) : n2h;
            int16_t high = static_cast<int16_t>(acc8)
                         - static_cast<int16_t>(highSub)
                         - static_cast<int16_t>(carryToHigh ? 0 : 1);
            const bool carryAfterHigh = (high >= 0);
            acc8 = static_cast<uint8_t>(high & 0xFF);

            if (!carryAfterHigh) {
                high = static_cast<int16_t>(acc8) - 0x60;
                acc8 = static_cast<uint8_t>(high & 0xFF);
            }

            R.A = acc8;
            const bool carryOut = (diff >= 0);
            setFlag(R.P, CARRY, carryOut); // carry=1 se nessun prestito
            setFlag(R.P, ZERO, binaryResult == 0);
            setFlag(R.P, NEGATIVE, binaryResult & 0x80);
            setFlag(R.P, OVERFLOW, overflow);
            return;
        }

        R.A = binaryResult;
        setFlag(R.P, CARRY, diff >= 0); // carry=1 se nessun prestito
        setFlag(R.P, ZERO, R.A == 0);
        setFlag(R.P, NEGATIVE, R.A & 0x80);
        setFlag(R.P, OVERFLOW, overflow);
    }


    // ---------------- CMP ----------------
    static void cmp(Registers &R, uint8_t val) {
        int16_t result = R.A - val;
        setFlag(R.P, CARRY, R.A >= val);
        setFlag(R.P, ZERO, R.A == val);
        setFlag(R.P, NEGATIVE, result & 0x80);
    }
};
