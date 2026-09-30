#pragma once

// mod.json "ConCommands": console commands that call a script function
// (PC: mods/mod.cpp ParseConCommands, modmanager.cpp ModConCommandCallback).
//
//   "ConCommands": [ { "Name": "my_command", "Function": "MyCommand",
//                      "Context": "CLIENT", "HelpString": "...", "Flags": "CHEAT" } ]
//
// Name, Function and Context (UI, CLIENT or SERVER) are required; an entry
// without them is skipped. Flags is an integer, or FCVAR names without the
// prefix joined by '|', as PC's ParseConVarFlagsString reads them (unknown
// names are reported and ignored). Host-tested in tests/mod_concommands.cpp.

#include "northstar_ps4/mod_catalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace northstar::ps4::mods {

enum class ScriptContext { Invalid, Ui, Client, Server };

struct ModConCommand {
    std::string name;
    std::string function;
    std::string help;
    ScriptContext context = ScriptContext::Invalid;
    int flags = 0;
};

inline ScriptContext ScriptContextFromString(const std::string& text) {
    if (text == "UI") return ScriptContext::Ui;
    if (text == "CLIENT") return ScriptContext::Client;
    if (text == "SERVER") return ScriptContext::Server;
    return ScriptContext::Invalid;
}

// PC's g_PrintCommandFlags, including its quirks: PRINTABLEONLY and
// GAMEDLL_FOR_REMOTE_CLIENTS are both 1 << 10, and CLIENTCMD_CAN_EXECUTE is
// listed as "UNKNOWN".
inline int ParseConVarFlagsString(const std::string& text, std::vector<std::string>* unknown = nullptr) {
    static const struct { const char* name; int value; } kFlags[] = {
        {"UNREGISTERED", 1 << 0}, {"DEVELOPMENTONLY", 1 << 1}, {"GAMEDLL", 1 << 2}, {"CLIENTDLL", 1 << 3},
        {"HIDDEN", 1 << 4}, {"PROTECTED", 1 << 5}, {"SPONLY", 1 << 6}, {"ARCHIVE", 1 << 7}, {"NOTIFY", 1 << 8},
        {"USERINFO", 1 << 9}, {"PRINTABLEONLY", 1 << 10}, {"GAMEDLL_FOR_REMOTE_CLIENTS", 1 << 10},
        {"UNLOGGED", 1 << 11}, {"NEVER_AS_STRING", 1 << 12}, {"REPLICATED", 1 << 13}, {"CHEAT", 1 << 14},
        {"SS", 1 << 15}, {"DEMO", 1 << 16}, {"DONTRECORD", 1 << 17}, {"SS_ADDED", 1 << 18}, {"RELEASE", 1 << 19},
        {"RELOAD_MATERIALS", 1 << 20}, {"RELOAD_TEXTURES", 1 << 21}, {"NOT_CONNECTED", 1 << 22},
        {"MATERIAL_SYSTEM_THREAD", 1 << 23}, {"ARCHIVE_PLAYERPROFILE", 1 << 24},
        {"ACCESSIBLE_FROM_THREADS", 1 << 25}, {"SERVER_CAN_EXECUTE", 1 << 28}, {"SERVER_CANNOT_QUERY", 1 << 29},
        {"UNKNOWN", 1 << 30}};
    int flags = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('|', start);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(start, end - start);
        const std::size_t first = part.find_first_not_of(" \t\n\f\v\r");
        const std::size_t last = part.find_last_not_of(" \t\n\f\v\r");
        part = first == std::string::npos ? std::string() : part.substr(first, last - first + 1);
        if (!part.empty()) {
            bool found = false;
            for (const auto& flag : kFlags)
                if (part == flag.name) {
                    flags |= flag.value;
                    found = true;
                    break;
                }
            if (!found && unknown) unknown->push_back(part);
        }
        start = end + 1;
    }
    return flags;
}

// Commands in order; `warnings` gets PC's skip messages.
inline std::vector<ModConCommand> ParseModConCommands(const char* json, std::vector<std::string>* warnings = nullptr) {
    std::vector<ModConCommand> out;
    const char* list = JsonFindMember(json, "ConCommands");
    if (!list) return out;
    const char* p = JsonSkipWs(list);
    if (*p != '[') {
        if (warnings) warnings->push_back("'ConCommands' field is not an array, skipping...");
        return out;
    }
    p = JsonSkipWs(p + 1);
    char buffer[256];
    auto text = [&](const char* object, const char* key, std::string& value) {
        const char* member = JsonFindMember(object, key);
        if (!member || *JsonSkipWs(member) != '"' || !JsonExtractString(member, buffer, sizeof(buffer))) return false;
        value = buffer;
        return true;
    };
    while (*p && *p != ']') {
        if (*p == '{') {
            ModConCommand command;
            std::string context;
            if (!text(p, "Name", command.name)) {
                if (warnings) warnings->push_back("ConCommand does not have a Name, skipping...");
            } else if (!text(p, "Function", command.function)) {
                if (warnings) warnings->push_back("ConCommand '" + command.name + "' does not have a Function, skipping...");
            } else if (!text(p, "Context", context)) {
                if (warnings) warnings->push_back("ConCommand '" + command.name + "' does not have a Context, skipping...");
            } else if ((command.context = ScriptContextFromString(context)) == ScriptContext::Invalid) {
                if (warnings)
                    warnings->push_back("ConCommand '" + command.name + "' has invalid context '" + context + "', skipping...");
            } else {
                text(p, "HelpString", command.help);
                if (const char* flags = JsonFindMember(p, "Flags")) {
                    const char* value = JsonSkipWs(flags);
                    if (*value == '"') {
                        std::string names;
                        text(p, "Flags", names);
                        std::vector<std::string> unknown;
                        command.flags = ParseConVarFlagsString(names, &unknown);
                        for (const auto& name : unknown)
                            if (warnings)
                                warnings->push_back("Mod ConCommand " + command.name + " has unknown flag " + name);
                    } else if ((*value >= '0' && *value <= '9') || *value == '-') {
                        command.flags = static_cast<int>(JsonExtractInteger(value));
                    }
                }
                out.push_back(command);
            }
        }
        p = JsonSkipWs(JsonSkipValue(p));
        if (*p != ',') break;
        p = JsonSkipWs(p + 1);
    }
    return out;
}

} // namespace northstar::ps4::mods
