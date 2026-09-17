#pragma once
#include "mod_catalog.h"
#include <string>
#include <vector>
namespace northstar::ps4::mods {
struct ScriptCallback { std::string before, after, destroy; };
inline bool ParseScriptCallbacks(const char* json, const char* contextKey, std::vector<ScriptCallback>& output) {
    std::vector<ScriptCallback> parsed;
    const char* scripts = JsonFindMember(json, "Scripts");
    if (!scripts) { output.clear(); return true; }
    const char* p = JsonSkipWs(scripts);
    if (*p++ != '[') return false;
    for (;;) {
        p = JsonSkipWs(p);
        if (*p == ']') { output.swap(parsed); return true; }
        if (*p != '{') return false;
        if (const char* callback = JsonFindMember(p, contextKey)) {
            if (*JsonSkipWs(callback) != '{') return false;
            ScriptCallback entry;
            const char* fields[] = {"Before", "After", "Destroy"};
            std::string* destinations[] = {&entry.before, &entry.after, &entry.destroy};
            for (int i = 0; i < 3; ++i) if (const char* value = JsonFindMember(callback, fields[i])) {
                char name[128];
                if (!JsonExtractString(value, name, sizeof(name))) return false;
                *destinations[i] = name;
            }
            parsed.push_back(entry);
        }
        const char* next = JsonSkipWs(JsonSkipValue(p));
        if (next <= p) return false;
        if (*next == ',') p = next + 1;
        else if (*next == ']') p = next;
        else return false;
    }
}
}
