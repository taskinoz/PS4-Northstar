#pragma once

// Custom audio: a mod's audio/<name>.json replaces the samples of sound events
// with the .wav files under audio/<name>/ (PC: client/audio.cpp).
//
//   { "EventId": [ "pilot_grapple_fire" ],          // or one string
//     "EventIdRegex": [ "^diag_sp_.*" ],            // optional
//     "AudioSelectionStrategy": "random" }           // or "sequential" (default)
//
// PC's rules, kept here:
//   - an event is looked up exactly, then under "*", then against every regex;
//     a regex match is cached under the event's name;
//   - the first mod (in load order) to claim an event or regex keeps it;
//   - a sample whose parent folder is named after an event another definition
//     already claimed is skipped;
//   - "!event" in EventId leaves that event alone, and "*" leaves ambient and
//     emitter events (names containing "_amb_", "_emit_" or "amb_") alone;
//   - a definition with no samples silences its events.
// Host-tested in tests/audio_override.cpp.

#include "northstar_ps4/mod_catalog.h"
#include "northstar_ps4/regex_lite.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace northstar::ps4::audio {

enum class Strategy { Sequential, Random };

struct Definition {
    std::vector<std::string> eventIds;
    std::vector<std::string> eventRegexes;
    Strategy strategy = Strategy::Sequential;
};

namespace detail {
inline bool ReadStrings(const char* value, std::vector<std::string>& out, std::string& error, const char* field) {
    char buffer[512];
    const char* p = mods::JsonSkipWs(value);
    if (*p == '"') {
        if (!mods::JsonExtractString(p, buffer, sizeof(buffer))) {
            error = std::string(field) + " is not a valid string";
            return false;
        }
        out.emplace_back(buffer);
        return true;
    }
    if (*p != '[') {
        error = std::string(field) + " must be a string or an array of strings";
        return false;
    }
    p = mods::JsonSkipWs(p + 1);
    while (*p && *p != ']') {
        if (*p != '"' || !mods::JsonExtractString(p, buffer, sizeof(buffer))) {
            error = std::string(field) + " array has a value of invalid type, all must be strings";
            return false;
        }
        out.emplace_back(buffer);
        p = mods::JsonSkipWs(mods::JsonSkipString(p));
        if (*p != ',') break;
        p = mods::JsonSkipWs(p + 1);
    }
    return true;
}
} // namespace detail

inline bool ParseDefinition(const char* json, Definition& out, std::string& error) {
    out = Definition{};
    if (*mods::JsonSkipWs(json) != '{') {
        error = "file is not a JSON object";
        return false;
    }
    const char* ids = mods::JsonFindMember(json, "EventId");
    if (!ids) {
        error = "JSON object does not have the EventId property";
        return false;
    }
    if (!detail::ReadStrings(ids, out.eventIds, error, "EventId")) return false;
    if (const char* regexes = mods::JsonFindMember(json, "EventIdRegex"))
        if (!detail::ReadStrings(regexes, out.eventRegexes, error, "EventIdRegex")) return false;
    if (const char* strategy = mods::JsonFindMember(json, "AudioSelectionStrategy")) {
        char value[32];
        if (*mods::JsonSkipWs(strategy) != '"' || !mods::JsonExtractString(strategy, value, sizeof(value))) {
            error = "AudioSelectionStrategy property must be a string";
            return false;
        }
        if (std::strcmp(value, "sequential") == 0) out.strategy = Strategy::Sequential;
        else if (std::strcmp(value, "random") == 0) out.strategy = Strategy::Random;
        else {
            error = "AudioSelectionStrategy string must be either \"sequential\" or \"random\"";
            return false;
        }
    }
    return true;
}

// PC ShouldPlayAudioEvent: false leaves the event's own sound.
inline bool ShouldOverride(const char* eventName, const std::vector<std::string>& eventIds) {
    const std::string blacklisted = std::string("!") + eventName;
    for (const auto& id : eventIds) {
        if (id == blacklisted) return false;
        if (id == "*" && (std::strstr(eventName, "_amb_") || std::strstr(eventName, "_emit_") ||
                             std::strstr(eventName, "amb_")))
            return false;
    }
    return true;
}

struct Override {
    std::string mod;
    std::string definitionPath;
    Definition definition;
    std::vector<std::string> samples; // .wav paths, in order
    std::size_t next = 0;
};

class Registry {
public:
    // The event names a definition's samples would be skipped for: those
    // already claimed (PC passes the claimed keys to the definition).
    bool IsClaimed(const std::string& eventId) const { return byEvent_.count(eventId) != 0; }

    // Registers what is still free; returns the log lines PC would print.
    std::vector<std::string> Add(std::unique_ptr<Override> entry) {
        std::vector<std::string> log;
        Override* raw = entry.get();
        for (const auto& id : raw->definition.eventIds) {
            if (byEvent_.count(id)) {
                log.push_back("\"" + raw->mod + "\" mod tried to override sound event \"" + id +
                    "\" but it is already overriden, skipping.");
                continue;
            }
            byEvent_.emplace(id, raw);
        }
        for (const auto& pattern : raw->definition.eventRegexes) {
            bool taken = false;
            for (const auto& existing : regexes_) taken = taken || existing.pattern == pattern;
            if (taken) {
                log.push_back("\"" + raw->mod + "\" mod tried to override sound event regex \"" + pattern +
                    "\" but it is already overriden, skipping.");
                continue;
            }
            regexlite::Regex compiled;
            std::string error;
            if (!compiled.Compile(pattern, error)) {
                log.push_back("Malformed regex \"" + pattern + "\" in audio override file " + raw->definitionPath +
                    ": " + error);
                continue;
            }
            regexes_.push_back({pattern, std::move(compiled), raw});
        }
        owned_.push_back(std::move(entry));
        return log;
    }

    // Exact name, then "*", then regexes (cached); null when nothing applies.
    Override* Find(const char* eventName) {
        auto it = byEvent_.find(eventName);
        if (it != byEvent_.end()) return it->second;
        it = byEvent_.find("*");
        if (it != byEvent_.end()) return it->second;
        Override* found = nullptr;
        for (const auto& regex : regexes_)
            if (regex.compiled.Search(eventName)) found = regex.target;
        if (found) byEvent_.emplace(eventName, found);
        return found;
    }

    bool Empty() const { return owned_.empty(); }
    std::size_t Size() const { return owned_.size(); }

private:
    struct RegexEntry {
        std::string pattern;
        regexlite::Regex compiled;
        Override* target;
    };
    std::unordered_map<std::string, Override*> byEvent_;
    std::vector<RegexEntry> regexes_;
    std::vector<std::unique_ptr<Override>> owned_;
};

// The sample to play next: sequential walks the list, random takes
// `randomValue % count`. -1 when the definition has no samples (silence).
inline int PickSample(Override& entry, unsigned randomValue) {
    if (entry.samples.empty()) return -1;
    if (entry.definition.strategy == Strategy::Random)
        return static_cast<int>(randomValue % entry.samples.size());
    const std::size_t index = entry.next++;
    if (entry.next >= entry.samples.size()) entry.next = 0;
    return static_cast<int>(index);
}

// PC's EMPTY_WAVE: a silent stereo 44.1 kHz 16-bit file, for "no samples".
inline const unsigned char kEmptyWave[45] = {0x52, 0x49, 0x46, 0x46, 0x25, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56,
    0x45, 0x66, 0x6D, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x44, 0xAC, 0x00, 0x00, 0x88, 0x58,
    0x01, 0x00, 0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x74, 0x00, 0x00, 0x00, 0x00};

// The folder a sample sits in names the event it is for (PC reads the parent
// folder's name to skip samples of already claimed events).
inline std::string SampleEventFolder(const std::string& samplePath) {
    const std::size_t slash = samplePath.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return std::string();
    const std::size_t start = samplePath.find_last_of('/', slash - 1);
    return samplePath.substr(start == std::string::npos ? 0 : start + 1, slash - (start == std::string::npos ? 0 : start + 1));
}

} // namespace northstar::ps4::audio
