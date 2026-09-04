#pragma once

static std::string sanitizeFileName(const std::string &in) {
    std::string out = in;
    for (size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            out[i] = '_';
        }
    }
    if (out.empty()) {
        out = "case";
    }
    return out;
}
