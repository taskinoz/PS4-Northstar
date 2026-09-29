#pragma once

// The private lobby's Host Options (Northstar.PS4 ui/ps4_host_options_menu.nut)
// are saved to a file so they last across restarts, where PC keeps them in a
// server config or on the command line. One `name=value` line per console
// variable, for the options changed in the menu; only the variables below are
// read back, so the file can't set anything else. Host-tested in
// tests/host_options.cpp.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::hostoptions {

inline constexpr const char* kConVars[] = {
    "ns_server_name",
    "ns_server_desc",
    "ns_server_password",
    "ns_report_server_to_masterserver",
    "ns_auth_allow_insecure",
    "ns_allow_duplicate_accounts",
};

inline bool IsHostOption(const std::string& name) {
    for (const char* known : kConVars)
        if (name == known) return true;
    return false;
}

// A line break would start a new line in the file, so it becomes a space.
inline std::string OneLine(const std::string& value) {
    std::string out = value;
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}

inline std::string Serialize(const std::vector<std::pair<std::string, std::string>>& values) {
    std::string out;
    for (const auto& value : values) {
        if (!IsHostOption(value.first)) continue;
        out += value.first + "=" + OneLine(value.second) + "\n";
    }
    return out;
}

// Later lines for the same name win.
inline std::vector<std::pair<std::string, std::string>> Parse(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string name = line.substr(0, equals);
        if (!IsHostOption(name)) continue;
        const std::string value = line.substr(equals + 1);
        bool replaced = false;
        for (auto& existing : out)
            if (existing.first == name) {
                existing.second = value;
                replaced = true;
            }
        if (!replaced) out.emplace_back(name, value);
    }
    return out;
}

// Changes one option in what the file holds; the others stay as saved, so
// values that came from startup arguments are not written.
inline void Set(std::vector<std::pair<std::string, std::string>>& values, const std::string& name,
    const std::string& value) {
    for (auto& existing : values)
        if (existing.first == name) {
            existing.second = OneLine(value);
            return;
        }
    values.emplace_back(name, OneLine(value));
}

} // namespace northstar::ps4::hostoptions
