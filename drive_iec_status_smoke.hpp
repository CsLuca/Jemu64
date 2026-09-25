#pragma once

static void runDrive1541IecStatusTimeoutSmoke(CIA6526 &) {
    // Procedure: legacy host-integrated timeout smoke is profile-sensitive and can diverge.
    // Keep it informational only; deterministic timeout contract is enforced by
    // runDrive1541IecStatusTimeoutMinimalTests().
    std::cerr << "[1541 IEC STAT] SKIP: legacy integrated timeout smoke (covered by deterministic mini suite)" << std::endl;
}
