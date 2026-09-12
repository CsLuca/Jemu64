#pragma once

namespace drive1541_physical {

struct IecLines {
    bool atn = true;
    bool clk = true;
    bool data = true;
};

class DriveIecPort {
public:
    void reset() noexcept {}

    void setBusLines(const IecLines &lines) noexcept {
        in_ = lines;
    }

    IecLines busDriveOutput() const noexcept {
        return out_;
    }

private:
    IecLines in_{};
    IecLines out_{};
};

} // namespace drive1541_physical
