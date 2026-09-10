#pragma once

class IIecDevice {
public:
    virtual ~IIecDevice() {}

    virtual void tickIecHalfCycle() = 0;
    virtual void setIecLines(bool atnHigh, bool clkHigh, bool dataHigh) = 0;
    virtual bool getIecDrivePullCLK() const = 0;
    virtual bool getIecDrivePullDATA() const = 0;
};
