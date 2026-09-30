#pragma once

#include <cctype>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::mods {

inline std::string PdefTrim(const std::string& value) {
    std::size_t first = 0, last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

inline std::vector<std::string> PdefLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? text.size() - start : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

inline bool PdefIdentifier(const std::string& value) noexcept {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_')) return false;
    for (char c : value)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

inline bool PdefPositiveSize(const std::string& text, std::size_t& value) noexcept {
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const std::size_t digit = static_cast<std::size_t>(c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return value != 0;
}

struct PdiffEnumAdd { std::string name; std::vector<std::string> lines; };
struct PdiffParts { std::vector<PdiffEnumAdd> enums; std::vector<std::string> properties; };

inline bool ParsePdiff(const std::string& text, PdiffParts& out, std::string& error) {
    out = {};
    error.clear();
    PdiffEnumAdd* current = nullptr;
    bool properties = false;
    std::size_t lineNumber = 0;
    for (const auto& raw : PdefLines(text)) {
        ++lineNumber;
        const std::size_t comment = raw.find("//");
        const std::string significant = PdefTrim(raw.substr(0, comment));
        if (properties) { out.properties.push_back(raw); continue; }
        if (current) {
            if (significant == "$ENUM_END") { current = nullptr; continue; }
            if (significant.rfind("$", 0) == 0) {
                error = "unexpected directive in enum block at line " + std::to_string(lineNumber);
                return false;
            }
            if (!significant.empty()) current->lines.push_back(raw);
            continue;
        }
        if (significant.empty()) continue;
        if (significant.rfind("$ENUM_ADD", 0) == 0 &&
            (significant.size() == 9 || std::isspace(static_cast<unsigned char>(significant[9])))) {
            const std::string name = PdefTrim(significant.substr(9));
            if (!PdefIdentifier(name)) {
                error = "invalid enum name at line " + std::to_string(lineNumber);
                return false;
            }
            out.enums.push_back({name, {}});
            current = &out.enums.back();
        } else if (significant == "$PROP_START") {
            properties = true;
        } else {
            error = "unexpected pdiff content at line " + std::to_string(lineNumber);
            return false;
        }
    }
    if (current) { error = "unterminated $ENUM_ADD block"; return false; }
    return true;
}

inline bool PdefEnumEndOffset(const std::string& text, const std::string& name,
    std::size_t& offset, std::string& error) {
    const std::string marker = "$ENUM_START " + name;
    bool inside = false;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t length = (newline == std::string::npos ? text.size() : newline) - start;
        std::string raw = text.substr(start, length);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::string significant = PdefTrim(raw.substr(0, raw.find("//")));
        if (!inside && significant == marker) inside = true;
        else if (inside && significant == "$ENUM_END") { offset = start; return true; }
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    error = inside ? "enum " + name + " has no $ENUM_END" : "enum " + name + " does not exist";
    return false;
}

inline bool ApplyPdiff(std::string& pdef, const std::string& pdiff, const std::string& modName,
    std::string& error) {
    PdiffParts parts;
    if (!ParsePdiff(pdiff, parts, error)) return false;
    std::string merged = pdef;
    for (const auto& addition : parts.enums) {
        std::size_t end = 0;
        if (!PdefEnumEndOffset(merged, addition.name, end, error)) return false;
        std::string inserted;
        for (const auto& line : addition.lines) inserted += line + "\n";
        merged.insert(end, inserted);
    }
    if (!parts.properties.empty()) {
        if (!merged.empty() && merged.back() != '\n') merged.push_back('\n');
        merged += "\n// $PROP_START " + modName + "\n";
        for (const auto& line : parts.properties) merged += line + "\n";
    }
    pdef.swap(merged);
    return true;
}

struct PdefField { std::string type, name; };

inline bool PersistenceDefinitionSize(const std::string& text, std::size_t& result, std::string& error) {
    std::map<std::string, std::size_t> enums;
    std::map<std::string, std::vector<PdefField>> structs;
    std::vector<PdefField> top;
    std::string currentStruct, currentEnum;
    for (const auto& raw : PdefLines(text)) {
        const std::string line = PdefTrim(raw.substr(0, raw.find("//")));
        if (line.empty()) continue;
        if (line.rfind("$ENUM_START", 0) == 0 &&
            (line.size() == 11 || std::isspace(static_cast<unsigned char>(line[11])))) {
            currentEnum = PdefTrim(line.substr(11));
            if (!PdefIdentifier(currentEnum)) { error = "invalid enum name"; return false; }
            currentStruct.clear(); enums[currentEnum] = 0; continue;
        }
        if (line == "$ENUM_END") { currentEnum.clear(); continue; }
        if (line.rfind("$STRUCT_START", 0) == 0 &&
            (line.size() == 13 || std::isspace(static_cast<unsigned char>(line[13])))) {
            currentStruct = PdefTrim(line.substr(13));
            if (!PdefIdentifier(currentStruct)) { error = "invalid struct name"; return false; }
            currentEnum.clear(); structs[currentStruct] = {}; continue;
        }
        if (line == "$STRUCT_END") { currentStruct.clear(); continue; }
        if (!currentEnum.empty()) { ++enums[currentEnum]; continue; }
        const std::size_t split = line.find_first_of(" \t");
        if (split == std::string::npos) { error = "invalid declaration: " + line; return false; }
        PdefField field{line.substr(0, split), PdefTrim(line.substr(split + 1))};
        (currentStruct.empty() ? top : structs[currentStruct]).push_back(std::move(field));
    }

    std::map<std::string, std::size_t> cache;
    std::set<std::string> active;
    std::function<bool(const std::string&, std::size_t&)> typeSize;
    std::function<bool(const PdefField&, std::size_t&)> fieldSize;
    typeSize = [&](const std::string& type, std::size_t& size) {
        if (type == "int" || type == "float") { size = 4; return true; }
        if (type == "bool" || enums.count(type)) { size = 1; return true; }
        if (type.rfind("string{", 0) == 0 && type.back() == '}') {
            std::size_t value = 0;
            if (!PdefPositiveSize(type.substr(7, type.size() - 8), value)) return false;
            size = value; return true;
        }
        auto known = cache.find(type);
        if (known != cache.end()) { size = known->second; return true; }
        auto structure = structs.find(type);
        if (structure == structs.end() || active.count(type)) return false;
        active.insert(type);
        std::size_t total = 0;
        for (const auto& field : structure->second) {
            std::size_t fieldBytes = 0;
            if (!fieldSize(field, fieldBytes)) return false;
            if (fieldBytes > std::numeric_limits<std::size_t>::max() - total) return false;
            total += fieldBytes;
        }
        active.erase(type);
        cache[type] = size = total;
        return true;
    };
    fieldSize = [&](const PdefField& field, std::size_t& size) {
        std::size_t count = 1;
        const std::size_t open = field.name.find('[');
        if (open != std::string::npos) {
            const std::size_t close = field.name.find(']', open + 1);
            if (close == std::string::npos || close + 1 != field.name.size() ||
                !PdefIdentifier(field.name.substr(0, open))) return false;
            const std::string dimension = PdefTrim(field.name.substr(open + 1, close - open - 1));
            if (dimension.empty()) return false;
            std::size_t numeric = 0;
            if (PdefPositiveSize(dimension, numeric)) count = numeric;
            else {
                if (!PdefIdentifier(dimension)) return false;
                auto enumeration = enums.find(dimension);
                if (enumeration == enums.end()) return false;
                count = enumeration->second;
            }
        } else if (!PdefIdentifier(field.name)) return false;
        std::size_t unit = 0;
        if (!typeSize(field.type, unit)) return false;
        if (count && unit > std::numeric_limits<std::size_t>::max() / count) return false;
        size = unit * count;
        return true;
    };
    result = 0;
    for (const auto& field : top) {
        std::size_t bytes = 0;
        if (!fieldSize(field, bytes)) { error = "unknown or invalid field " + field.type + " " + field.name; return false; }
        if (bytes > std::numeric_limits<std::size_t>::max() - result) {
            error = "persistence definition size overflow";
            return false;
        }
        result += bytes;
    }
    return true;
}

} // namespace northstar::ps4::mods
