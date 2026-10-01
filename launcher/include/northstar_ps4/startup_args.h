#pragma once

// ns_startup_args.txt, as PC reads it: launch arguments separated by white
// space, double quotes grouping one that contains spaces. "+name value" sets a
// console variable at startup (PC: the engine runs +commands from its command
// line), e.g. +ns_server_name "My server". Host-tested in
// tests/startup_args.cpp.

#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::startup {

inline std::vector<std::string> SplitArgs(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
        if (i >= text.size()) break;
        std::string token;
        bool quoted = false;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                quoted = !quoted;
                ++i;
                continue;
            }
            if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) break;
            token += c;
            ++i;
        }
        out.push_back(token);
    }
    return out;
}

// The value after "-name" (PC CommandLine()->GetParm(FindParm(name))), e.g.
// "-maxfoldersize 1048576". False when the option is absent or is followed by
// nothing or by another option.
inline bool ArgValue(const std::vector<std::string>& args, const std::string& name, std::string& value) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != name) continue;
        const std::string& next = args[i + 1];
        if (next.empty() || next[0] == '-' || next[0] == '+') return false;
        value = next;
        return true;
    }
    return false;
}

// "+name value" pairs. A "+name" followed by another option (+ or -) or by
// nothing has no value and is skipped.
inline std::vector<std::pair<std::string, std::string>> ConVarAssignments(const std::vector<std::string>& args) {
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.size() < 2 || arg[0] != '+') continue;
        if (i + 1 >= args.size()) continue;
        const std::string& value = args[i + 1];
        if (!value.empty() && (value[0] == '+' || value[0] == '-')) {
            // A negative number is a value, not an option.
            const bool negativeNumber = value[0] == '-' && value.size() > 1 && (value[1] >= '0' && value[1] <= '9');
            if (!negativeNumber) continue;
        }
        out.emplace_back(arg.substr(1), value);
        ++i;
    }
    return out;
}

} // namespace northstar::ps4::startup
