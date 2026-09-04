#pragma once

static bool endsWithInsensitive(const std::string &text, const std::string &suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    const std::string t = toLowerAscii(text);
    const std::string s = toLowerAscii(suffix);
    return t.compare(t.size() - s.size(), s.size(), s) == 0;
}
