#pragma once
#include <string>
#include <vector>
namespace northstar::ps4::mods {
inline std::string PdefCode(std::string line) {
    const auto comment = line.find("//");
    if (comment != std::string::npos) line.resize(comment);
    const auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return line.substr(start, line.find_last_not_of(" \t\r\n") - start + 1);
}
// Only inspect top-level declarations. Never mistake a struct member,
// enum value or comment for an existing root field; refuse conflicting types.
inline bool PdefRootDeclaration(const std::string& text, const char* wanted, const char* type, bool& found, std::string& error) {
    found = false;
    std::vector<std::string> blocks;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find('\n', pos); if (end == std::string::npos) end = text.size();
        std::string line = PdefCode(text.substr(pos, end-pos)); pos = end+1;
        auto split = line.find_first_of(" \t");
        const std::string first = line.substr(0, split);
        if (first == "$ENUM_START" || first == "$STRUCT_START") { blocks.push_back(first); continue; }
        if (first == "$ENUM_END" || first == "$STRUCT_END") {
            if (blocks.empty() || blocks.back() != (first == "$ENUM_END" ? "$ENUM_START" : "$STRUCT_START")) {
                error = "unbalanced PDEF block"; return false;
            }
            blocks.pop_back(); continue;
        }
        if (!blocks.empty() || split == std::string::npos) continue;
        const std::string name = PdefCode(line.substr(split));
        const auto suffix = name.find_first_of("[ \t=");
        const std::string expected(wanted);
        if (name.substr(0, suffix) != expected.substr(0, expected.find('['))) continue;
        if (found || first != type || name != wanted) {
            error = std::string("conflicting root declaration: ") + wanted; return false;
        }
        found = true;
    }
    if (!blocks.empty()) { error = "unterminated PDEF block"; return false; }
    return true;
}
// Merge selected PC enums by appending new names, preserving existing indices.
// Enum-sized arrays grow: this is a generated local schema, not a pdata converter.
inline bool PdefEnum(const std::string& text, const char* wanted, std::vector<std::string>& values,
                     std::size_t& closing, std::string& error) {
    bool active = false, found = false;
    values.clear(); closing = std::string::npos;
    for (std::size_t pos = 0; pos < text.size();) {
        auto end = text.find('\n', pos); if (end == std::string::npos) end = text.size();
        const auto line = PdefCode(text.substr(pos, end-pos));
        const auto split = line.find_first_of(" \t");
        if (line.substr(0, split) == "$ENUM_START" && split != std::string::npos && PdefCode(line.substr(split)) == wanted) {
            if (found) { error = "duplicate PDEF enum"; return false; }
            active = found = true;
        } else if (active && line == "$ENUM_END") { closing = pos; active = false; }
        else if (active && !line.empty()) {
            if (line.find_first_of(" \t$=") != std::string::npos) { error = "unsupported PDEF enum entry"; return false; }
            for (const auto& value : values) if (value == line) { error = "duplicate PDEF enum value"; return false; }
            values.push_back(line);
        }
        pos = end+1;
    }
    if (!found || active || closing == std::string::npos) { error = std::string("missing or incomplete PDEF enum: ") + wanted; return false; }
    return true;
}
inline bool ExtendPs4LoadoutEnums(const std::string& base, const std::string& pc, std::string& output, std::string& error) {
    std::string merged = base;
    for (const char* name : {"loadoutWeaponsAndAbilities", "titanPassive"}) {
        std::vector<std::string> existing, reference;
        std::size_t closing, ignored;
        if (!PdefEnum(merged, name, existing, closing, error) || !PdefEnum(pc, name, reference, ignored, error)) return false;
        std::string additions;
        for (const auto& value : reference) {
            bool found = false;
            for (const auto& old : existing) if (old == value) { found = true; break; }
            if (!found) additions += "\t" + value + "\n";
        }
        merged.insert(closing, additions);
    }
    output = merged; return true;
}
struct PdefIntField { std::string scope, name; unsigned count; std::size_t start, end; };
inline bool PdefIntFields(const std::string& text, std::vector<PdefIntField>& fields, std::string& error) {
    std::string scope;
    bool inEnum = false;
    for (std::size_t pos = 0; pos < text.size();) {
        auto end = text.find('\n', pos); if (end == std::string::npos) end = text.size();
        auto line = PdefCode(text.substr(pos, end-pos));
        const auto split = line.find_first_of(" \t");
        const auto type = line.substr(0, split);
        if (type == "$ENUM_START") inEnum = true;
        else if (type == "$ENUM_END") inEnum = false;
        else if (type == "$STRUCT_START" && split != std::string::npos) scope = PdefCode(line.substr(split));
        else if (type == "$STRUCT_END") scope.clear();
        else if (!inEnum && type == "int" && split != std::string::npos) {
            auto name = PdefCode(line.substr(split));
            const auto bracket = name.find('[');
            unsigned count = 1;
            bool literal = true;
            if (bracket != std::string::npos) {
                if (name.back() != ']' || bracket+2 >= name.size()) literal = false;
                else {
                    count = 0;
                    for (auto i = bracket+1; i+1 < name.size(); ++i) {
                        if (name[i] < '0' || name[i] > '9' || count > 4096) { literal = false; break; }
                        count = count*10 + (name[i]-'0');
                    }
                }
                name.resize(bracket);
            }
            if (literal && count > 0 && count <= 4096 && name.find_first_of(" \t=") == std::string::npos) {
                for (const auto& field : fields) if (field.scope == scope && field.name == name) {
                    error = "duplicate PDEF integer field"; return false;
                }
                fields.push_back({scope, name, count, pos, end});
            }
        }
        pos = end+1;
    }
    return true;
}
// Match known integer capacities by name and struct scope. Never shrink a PS4
// field or replace an enum-sized array. This deliberately changes local layout.
inline bool ExtendPs4IntCapacities(const std::string& base, const std::string& pc, std::string& output, std::string& error) {
    std::vector<PdefIntField> existing, reference;
    if (!PdefIntFields(base, existing, error) || !PdefIntFields(pc, reference, error)) return false;
    std::string merged = base;
    for (auto it = existing.rbegin(); it != existing.rend(); ++it) {
        for (const auto& wanted : reference) {
            if (it->scope != wanted.scope || it->name != wanted.name || it->count >= wanted.count) continue;
            merged.replace(it->start, it->end-it->start, "int " + it->name + "[" + std::to_string(wanted.count) + "]");
            break;
        }
    }
    output = merged; return true;
}
inline bool ExtendPs4PersistenceRoots(const std::string& ps4, const std::string& pc,
                                     std::string& output, std::string& error) {
    error.clear(); std::string additions;
    // Validate PC declarations instead of inventing defaults in calling scripts.
    // These additive fields do not resize existing arrays or structures.
    // Enum-sized additions use the PS4 schema's existing enum, never PC pdata.
    struct Root { const char* type; const char* name; };
    const Root roots[] = {
        {"int", "newPrimeTitans"}, {"int", "netWorth"},
        {"int", "newTitanExecutions"}, {"int", "unlockedTitanExecutions"},
        {"bool", "factionGiftsFixed"},
        {"int", "newCommsIcons[5]"}, {"int", "unlockedCommsIcons[5]"},
        {"bool", "custom_emoji_initialized"}, {"int", "custom_emoji[4]"},
        {"int", "randomFactionLevelUnlocks[faction]"},
        {"int", "randomPlayerLevelUnlocks"}, {"int", "randomTitanLevelUnlocks[titanClasses]"},
        {"int", "randomWeaponLevelUnlocks[loadoutWeaponsAndAbilities]"}, {"int", "randomColiseumUnlocks"}
    };
    for (const auto& root : roots) {
        bool inPc = false, inPs4 = false;
        if (!PdefRootDeclaration(pc, root.name, root.type, inPc, error) || !inPc) {
            if (error.empty()) error = std::string("PC schema missing root: ") + root.name;
            return false;
        }
        if (!PdefRootDeclaration(ps4, root.name, root.type, inPs4, error)) return false;
        if (!inPs4) additions += std::string(root.type) + " " + root.name + "\n";
    }
    // Preserve every existing byte/declaration and therefore the existing
    // field order. New root fields are appended, never inserted into structs.
    output = ps4;
    if (!additions.empty()) output += "\n// Northstar PS4 additive root declarations\n" + additions;
    return true;
}
}
