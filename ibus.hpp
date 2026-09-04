#pragma once

class IBus {
public:
    virtual ~IBus() = default;

    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t val) = 0;
};
