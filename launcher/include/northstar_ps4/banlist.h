#pragma once

// Server ban list (PC: server/auth/bansystem.cpp). One uid per line in
// banlist.txt; lines starting with '#' are comments, "123 # reason" carries an
// inline comment, spaces and tabs are ignored. Banning appends the uid, making
// sure the previous line ended; unbanning comments the matching lines out and
// notes the date; clearing empties the file. Host-tested in tests/banlist.cpp.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace northstar::ps4::bans {

inline std::string WithoutBlanks(const std::string& line) {
    std::string out;
    for (char c : line)
        if (c != ' ' && c != '\t' && c != '\r') out += c;
    return out;
}

// The uid a line bans, or 0 for a comment or empty line.
inline std::uint64_t LineUid(const std::string& line) {
    const std::string compact = WithoutBlanks(line);
    if (compact.empty() || compact.front() == '#') return 0;
    const std::string uid = compact.substr(0, compact.find('#'));
    return std::strtoull(uid.c_str(), nullptr, 10);
}

inline std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
        start = end + 1;
    }
    return out;
}

inline std::vector<std::uint64_t> Parse(const std::string& text) {
    std::vector<std::uint64_t> out;
    for (const auto& line : Lines(text))
        if (const std::uint64_t uid = LineUid(line)) out.push_back(uid);
    return out;
}

inline bool IsBanned(const std::string& text, std::uint64_t uid) {
    for (std::uint64_t banned : Parse(text))
        if (banned == uid) return true;
    return false;
}

inline std::string Ban(const std::string& text, std::uint64_t uid) {
    std::string out = text;
    if (!out.empty() && out.back() != '\n') out += '\n';
    return out + std::to_string(uid) + '\n';
}

// `date` as PC writes it: "YYYY-MM-DD hh:mm". Unchanged when not banned.
inline std::string Unban(const std::string& text, std::uint64_t uid, const std::string& date) {
    std::string out;
    for (const auto& line : Lines(text)) {
        if (LineUid(line) == uid) out += "# " + line + " # unban date: " + date + '\n';
        else out += line + '\n';
    }
    return out;
}

} // namespace northstar::ps4::bans
