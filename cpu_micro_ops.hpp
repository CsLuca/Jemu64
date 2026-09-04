#pragma once

using MicroOp = std::function<void()>;

enum Phase { PHI1, PHI2 };

struct MicroOpWithPhase {
    Phase phase;
    MicroOp op;
};
