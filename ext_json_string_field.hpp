#pragma once

static bool extractJsonStringField(const std::string &obj, const std::string &key, std::string &out) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (!std::regex_search(obj, m, re)) {
        return false;
    }
    out = m[1].str();
    return true;
}
