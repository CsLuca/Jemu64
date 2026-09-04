#pragma once

static std::vector<ExternalRomCase> loadExternalManifest(const std::string &manifestPath) {
    std::ifstream in(manifestPath, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open manifest: " + manifestPath);
    }

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<ExternalRomCase> out;

    std::regex objRe("\\{[^\\{\\}]*\\}");
    auto begin = std::sregex_iterator(text.begin(), text.end(), objRe);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const std::string obj = it->str();
        ExternalRomCase t;
        uint32_t n = 0;

        if (!extractJsonStringField(obj, "name", t.name)) continue;
        if (!extractJsonStringField(obj, "rom_path", t.romPath)) continue;

        std::string fmt;
        if (extractJsonStringField(obj, "format", fmt)) {
            t.format = toLowerAscii(fmt);
        }

        if (extractJsonNumberField(obj, "load_address", n)) {
            t.loadAddress = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.loadAddress = 0;
        }

        if (extractJsonNumberField(obj, "reset_vector", n)) {
            t.resetVector = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.resetVector = 0;
        }

        if (extractJsonNumberField(obj, "start_pc", n)) {
            t.startPC = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.startPC = 0;
        }

        if (!extractJsonNumberField(obj, "max_halfcycles", n)) continue;
        t.maxHalfCycles = n;

        if (extractJsonNumberField(obj, "pass_pc", n)) {
            t.passPC = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.passPC = 0;
        }

        if (extractJsonNumberField(obj, "fail_pc", n)) {
            t.failPC = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.failPC = 0;
        }

        if (extractJsonNumberField(obj, "require_pc", n)) {
            t.requirePC = static_cast<uint16_t>(n & 0xFFFF);
        } else {
            t.requirePC = 0;
        }

        if (extractJsonNumberField(obj, "require_pc_hits", n)) {
            t.requirePCHits = n;
        } else {
            t.requirePCHits = 0;
        }

        if (extractJsonNumberField(obj, "min_unique_pc", n)) {
            t.minUniquePC = n;
        } else {
            t.minUniquePC = 0;
        }

        if (extractJsonNumberField(obj, "expect_mem_addr", n)) {
            t.expectMemAddr = static_cast<uint16_t>(n & 0xFFFF);
            t.hasExpectedMem = true;
            if (extractJsonNumberField(obj, "expect_mem_value", n)) {
                t.expectMemValue = static_cast<uint8_t>(n & 0xFF);
            } else {
                t.expectMemValue = 0;
            }
        } else {
            t.expectMemAddr = 0;
            t.expectMemValue = 0;
            t.hasExpectedMem = false;
        }

        bool traceEnabled = true;
        if (extractJsonBoolField(obj, "trace", traceEnabled)) {
            t.traceEnabled = traceEnabled;
        } else {
            t.traceEnabled = true;
        }

        bool optional = false;
        if (extractJsonBoolField(obj, "optional", optional)) {
            t.optional = optional;
        } else {
            t.optional = false;
        }

        std::string referenceTracePath;
        if (extractJsonStringField(obj, "reference_trace", referenceTracePath)) {
            t.referenceTracePath = referenceTracePath;
        } else {
            t.referenceTracePath.clear();
        }

        std::string referenceMode;
        if (extractJsonStringField(obj, "reference_mode", referenceMode)) {
            t.referenceMode = toLowerAscii(referenceMode);
        } else {
            t.referenceMode = "full";
        }

        if (extractJsonNumberField(obj, "max_trace_mismatches", n)) {
            t.maxTraceMismatches = n;
        } else {
            t.maxTraceMismatches = 0;
        }

        bool referenceOptional = false;
        if (extractJsonBoolField(obj, "reference_optional", referenceOptional)) {
            t.referenceOptional = referenceOptional;
        } else {
            t.referenceOptional = false;
        }

        std::string envField;
        if (extractJsonStringField(obj, "env_cpu_revision", envField)) t.envCpuRevision = envField;
        if (extractJsonStringField(obj, "env_vic_revision", envField)) t.envVicRevision = envField;
        if (extractJsonStringField(obj, "env_cia_revision", envField)) t.envCiaRevision = envField;
        if (extractJsonStringField(obj, "env_openbus_revision", envField)) t.envOpenBusRevision = envField;
        if (extractJsonStringField(obj, "env_drive_revision", envField)) t.envDriveRevision = envField;

        out.push_back(t);
    }

    if (out.empty()) {
        throw std::runtime_error("No valid test cases found in manifest: " + manifestPath);
    }

    return out;
}
