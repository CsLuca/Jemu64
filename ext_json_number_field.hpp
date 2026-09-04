#pragma once

static bool extractJsonNumberField(const std::string &obj, const std::string &key, uint32_t &out) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"?(0x[0-9A-Fa-f]+|[0-9]+)\\\"?");
    std::smatch m;
    if (!std::regex_search(obj, m, re)) {
        return false;
    }
    const std::string v = m[1].str();
    out = static_cast<uint32_t>(std::stoul(v, nullptr, 0));
    return true;
}
