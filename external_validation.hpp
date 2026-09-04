#pragma once

void runExternalRomValidation(Bus &bus, CPU6510 &cpu) {
    const char *manifestEnv = std::getenv("EXTERNAL_TEST_MANIFEST");
    const std::string manifestPath = (manifestEnv != nullptr && manifestEnv[0] != '\0')
        ? std::string(manifestEnv)
        : std::string("external_tests_manifest.json");
    std::cerr << "[EXT] Loading manifest: " << manifestPath << std::endl;

    std::vector<ExternalRomCase> tests = loadExternalManifest(manifestPath);
    std::cerr << "[EXT] Cases loaded: " << tests.size() << std::endl;

    bool allOk = true;
    for (size_t i = 0; i < tests.size(); ++i) {
        bool ok = runExternalRomCase(bus, cpu, tests[i]);
        if (!ok) {
            const ExternalCaseRunReport &report = externalCaseLastRunReport();
            std::cerr << "[EXT] Failure snapshot"
                      << " case=" << report.caseName
                      << " cycle=" << std::dec << report.cycle
                      << " pc=$" << std::hex << report.pc
                      << " addr=$" << report.addr
                      << " rw=" << (report.isWrite ? "W" : "R")
                      << " data=$" << static_cast<int>(report.data)
                      << std::endl;
        }
        allOk = allOk && ok;
    }

    if (!allOk) {
        std::cerr << "[EXT] External validation FAILED." << std::endl;
        assert(false);
    }

    std::cerr << "[EXT] External validation PASSED." << std::endl;
}
