#pragma once
#include <cstddef>
#include <string>
#include <vector>

// Valve KeyValues text: parse, merge and serialise.
//
// This exists because the engine's playlist loader does not honour `#base`, so
// a Northstar KeyValues patch cannot be delegated to the engine the way PC
// does it (see TECHNICAL-NOTES). The merge has to happen here and produce one
// complete file.
//
// Duplicate keys within a block are real and must survive: the shipped
// playlists_v2.txt contains 18 of them, including eleven `lang` blocks under
// `playlists/LocalizedStrings`, one per language. A map keyed by name would
// silently collapse those, so entries are an ordered list and merging targets
// the *first* match, which is what Valve's own FindKey lookup returns.
namespace northstar::ps4::mods {
constexpr int kKeyValuesMaxDepth = 32;

struct KeyValue;
using KeyValueList = std::vector<KeyValue>;

struct KeyValue {
    std::string key;
    std::string value;      // meaningful when !isBlock
    std::string condition;  // platform conditional, e.g. "$PC", without brackets
    KeyValueList children;  // meaningful when isBlock
    bool isBlock = false;
};

struct KeyValuesToken {
    enum Kind { End, Open, Close, Text, Condition } kind = End;
    std::string text;
};

// Scanner: whitespace-separated bare words, "quoted strings" with backslash
// escapes, braces, and `//` line comments. The shipped files use CRLF and
// contain escaped quotes, so both are handled.
//
// Escape sequences are carried through *verbatim* - `\"` is stored as the two
// characters and written back as the two characters. The engine does honour
// them (rescanning the shipped playlist with escapes disabled turns 639 keys
// into fragments of German prose, so the file is only coherent with them on),
// but exactly which ones it expands is its business, not this module's: the
// playlist holds 2525 `\n` and 138 `\"` inside localised strings, and decoding
// them here would mean re-encoding them on the way out and having to match the
// engine's table to avoid corrupting menu text. Passing the bytes through
// means a merged file says byte for byte what the original said.
inline const char* KeyValuesNextToken(const char* p, KeyValuesToken& token) {
    token.text.clear();
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            continue;
        }
        break;
    }
    if (!*p) { token.kind = KeyValuesToken::End; return p; }
    if (*p == '{') { token.kind = KeyValuesToken::Open; return p + 1; }
    if (*p == '}') { token.kind = KeyValuesToken::Close; return p + 1; }
    // A platform conditional such as `[$PC]` or `[!$JAPANESE && !$TCHINESE]`
    // belongs to the entry before it, not to the key stream. Reading it as an
    // ordinary token shifts every following key/value pair by one, which is
    // how `resource/fontfiletable.txt` came out as nonsense.
    if (*p == '[') {
        token.kind = KeyValuesToken::Condition;
        ++p;
        while (*p && *p != ']') token.text.push_back(*p++);
        if (*p == ']') ++p;
        return p;
    }
    token.kind = KeyValuesToken::Text;
    if (*p == '"') {
        ++p;
        while (*p && *p != '"') {
            // An escaped quote does not end the string, but both characters
            // are kept so the text can be written back unchanged.
            if (*p == '\\' && p[1]) {
                token.text.push_back(*p);
                token.text.push_back(p[1]);
                p += 2;
                continue;
            }
            token.text.push_back(*p++);
        }
        if (*p == '"') ++p;
        return p;
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
           *p != '{' && *p != '}' && *p != '"' && *p != '[') token.text.push_back(*p++);
    return p;
}

inline bool ParseKeyValueBlock(const char*& p, KeyValueList& out, int depth, std::string& error) {
    if (depth > kKeyValuesMaxDepth) { error = "nested too deeply"; return false; }
    for (;;) {
        KeyValuesToken key;
        const char* afterKey = KeyValuesNextToken(p, key);
        if (key.kind == KeyValuesToken::End) { p = afterKey; return depth == 0; }
        if (key.kind == KeyValuesToken::Close) {
            p = afterKey;
            if (depth == 0) { error = "unexpected '}'"; return false; }
            return true;
        }
        if (key.kind == KeyValuesToken::Open) { error = "unexpected '{'"; return false; }
        if (key.kind == KeyValuesToken::Condition) { error = "conditional with no entry"; return false; }
        p = afterKey;

        KeyValue entry;
        entry.key = key.text;

        // A conditional may sit either side of the value, so check both.
        KeyValuesToken next;
        const char* afterNext = KeyValuesNextToken(p, next);
        if (next.kind == KeyValuesToken::Condition) {
            entry.condition = next.text;
            p = afterNext;
            afterNext = KeyValuesNextToken(p, next);
        }
        if (next.kind == KeyValuesToken::Open) {
            p = afterNext;
            entry.isBlock = true;
            if (!ParseKeyValueBlock(p, entry.children, depth + 1, error)) return false;
        } else if (next.kind == KeyValuesToken::Text) {
            p = afterNext;
            entry.value = next.text;
        } else {
            error = "key '" + key.text + "' has no value";
            return false;
        }
        if (entry.condition.empty()) {
            KeyValuesToken trailing;
            const char* afterTrailing = KeyValuesNextToken(p, trailing);
            if (trailing.kind == KeyValuesToken::Condition) {
                entry.condition = trailing.text;
                p = afterTrailing;
            }
        }
        out.push_back(std::move(entry));
    }
}

inline bool ParseKeyValues(const char* text, KeyValueList& out, std::string& error) {
    if (!text) { error = "no document"; return false; }
    out.clear();
    error.clear();
    const char* p = text;
    if (!ParseKeyValueBlock(p, out, 0, error)) {
        if (error.empty()) error = "unbalanced braces";
        return false;
    }
    return true;
}

// Northstar precedence: the patch wins over the base. Blocks merge key by key,
// scalars replace. A key the base does not have is appended, preserving the
// patch's own order. Where the base holds the key more than once only the
// first is touched, matching engine lookup.
inline void MergeKeyValues(KeyValueList& base, const KeyValueList& patch) {
    for (const KeyValue& incoming : patch) {
        // Prefer the entry guarded by the same conditional: the shipped font
        // table holds `lucida console` twice, once `[$PC]` and once
        // `[$GAMECONSOLE]`, and a patch for one must not land on the other.
        KeyValue* existing = nullptr;
        if (!incoming.condition.empty()) {
            for (KeyValue& candidate : base) {
                if (candidate.key == incoming.key && candidate.condition == incoming.condition) {
                    existing = &candidate;
                    break;
                }
            }
        }
        if (!existing) {
            for (KeyValue& candidate : base) {
                if (candidate.key == incoming.key) { existing = &candidate; break; }
            }
        }
        if (!existing) { base.push_back(incoming); continue; }
        if (existing->isBlock && incoming.isBlock) {
            MergeKeyValues(existing->children, incoming.children);
        } else {
            // A scalar replacing a block (or the reverse) takes the patch
            // wholesale rather than trying to reconcile the two shapes.
            *existing = incoming;
        }
    }
}

// Text is stored exactly as it appeared between the quotes, escapes included,
// so writing it is a quote, the bytes, a quote. Re-escaping here would double
// every backslash the shipped files already contain.
inline void AppendKeyValuesQuoted(std::string& out, const std::string& text) {
    out.push_back('"');
    out.append(text);
    out.push_back('"');
}

// `indent` is off for generated files. Indentation is pure readability and it
// costs about 24 KB on the playlist, which matters because the engine will not
// read a served file that is larger than the one it measured.
inline void SerialiseKeyValueList(const KeyValueList& list, std::string& out, int indent, bool useIndent) {
    for (const KeyValue& entry : list) {
        if (useIndent) out.append(static_cast<std::size_t>(indent), '\t');
        // Directives such as `#base "other.txt"` are recognised by the parser
        // as bare tokens, so they must be written back bare. Quoting one turns
        // a working include into an inert key. None of the files patched today
        // contain one, but several shipped weapon and aisettings files do.
        if (!entry.key.empty() && entry.key[0] == '#') out.append(entry.key);
        else AppendKeyValuesQuoted(out, entry.key);
        if (entry.isBlock) {
            // For a block the conditional goes on the key line; for a value it
            // goes after the value, which is where the shipped files put it.
            if (!entry.condition.empty()) {
                out.append(" [");
                out.append(entry.condition);
                out.push_back(']');
            }
            out.push_back('\n');
            if (useIndent) out.append(static_cast<std::size_t>(indent), '\t');
            out.append("{\n");
            SerialiseKeyValueList(entry.children, out, indent + 1, useIndent);
            if (useIndent) out.append(static_cast<std::size_t>(indent), '\t');
            out.append("}\n");
        } else {
            out.push_back(' ');
            AppendKeyValuesQuoted(out, entry.value);
            if (!entry.condition.empty()) {
                out.append(" [");
                out.append(entry.condition);
                out.push_back(']');
            }
            out.push_back('\n');
        }
    }
}

inline std::string SerialiseKeyValues(const KeyValueList& list, bool useIndent = true) {
    std::string out;
    SerialiseKeyValueList(list, out, 0, useIndent);
    return out;
}

// Convenience for tests and callers: first child block matching a path.
inline const KeyValue* FindKeyValue(const KeyValueList& list, const std::string& key) {
    for (const KeyValue& entry : list)
        if (entry.key == key) return &entry;
    return nullptr;
}

inline std::size_t CountKeyValue(const KeyValueList& list, const std::string& key) {
    std::size_t count = 0;
    for (const KeyValue& entry : list)
        if (entry.key == key) ++count;
    return count;
}
} // namespace northstar::ps4::mods
