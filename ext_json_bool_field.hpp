#pragma once

static bool extractJsonBoolField(const std::string &obj, const std::string &key, bool &out) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*(true|false)", std::regex_constants::icase);
    std::smatch m;
    if (!std::regex_search(obj, m, re)) {
        return false;
    }
    std::string v = m[1].str();
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[i])));
    }
    out = (v == "true");
    return true;
}
