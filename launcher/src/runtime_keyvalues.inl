// Northstar KeyValues patches.
//
// A mod patches a KeyValues file by shipping `<mod>/keyvalues/<path>`, which is
// a *sibling* of the `<mod>/mod` tree the filesystem overlay serves, so these
// files are never served directly and were ignored entirely before this. That
// left the client on the stock `playlists_v2.txt` with none of Northstar's
// custom gamemodes defined while a PC server ran the merged one, which is the
// single cause behind every custom-gamemode failure (see TECHNICAL-NOTES).
//
// PC delegates the merge to the engine: `ModManager::TryBuildKeyValues` writes
// a wrapper that `#base`-includes each patch and the original. **That does not
// work here.** This engine honours `#base` in the loaders that shipped using it
// (`scripts/aisettings`, `scripts/weapons`, `resource/ui/menus`, ...) but the
// playlist loader ignores it: a served wrapper was read, none of its includes
// were opened, and the empty root object produced
// `FatalError: Failed to load playlist data` and no main menu.
//
// So the merge happens here instead and emits one complete file. That is
// uniform - it needs no per-loader knowledge of whether `#base` is honoured -
// and `keyvalues.h` is host-tested against the real 360 KB playlist and both
// shipped patches.
using northstar::ps4::mods::KeyValueList;
using northstar::ps4::mods::MergeKeyValues;
using northstar::ps4::mods::ParseKeyValues;
using northstar::ps4::mods::SerialiseKeyValues;

// Kill switch. Serving a broken playlist is a boot failure, so keep this easy
// to flip if a future patch turns out to defeat the merge.
constexpr bool kKeyValuesMergeEnabled = true;
constexpr const char* kKeyValuesRoot = "/data/northstar_ps4/kv";
constexpr std::size_t kMaxKeyValuePatches = 64;
constexpr std::size_t kKeyValuePathCapacity = 192;
constexpr std::size_t kMaxKeyValueFileSize = 8 * 1024 * 1024;

char g_keyValuePatchPaths[kMaxKeyValuePatches][kKeyValuePathCapacity]{};
std::int32_t g_keyValuePatchCount = 0;
char g_keyValueBuiltPaths[kMaxKeyValuePatches][kKeyValuePathCapacity]{};
std::int32_t g_keyValueBuiltCount = 0;

// `<mods>/<name>/mod` -> `<mods>/<name>/keyvalues`
bool KeyValuesRootForMod(std::int32_t index, char* out, std::size_t capacity) noexcept {
    const char* root = g_modRoots[index];
    const char* tail = std::strrchr(root, '/');
    if (!tail || std::strcmp(tail, "/mod") != 0) return false;
    const std::size_t length = static_cast<std::size_t>(tail - root);
    const int n = std::snprintf(out, capacity, "%.*s/keyvalues", static_cast<int>(length), root);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

void RecordKeyValuePatch(const char* relative) noexcept {
    for (std::int32_t i = 0; i < g_keyValuePatchCount; ++i)
        if (!std::strcmp(g_keyValuePatchPaths[i], relative)) return;
    if (g_keyValuePatchCount >= static_cast<std::int32_t>(kMaxKeyValuePatches)) {
        LogFormat("[NorthstarPS4] keyvalues patch table full, ignoring %s\n", relative);
        return;
    }
    std::snprintf(g_keyValuePatchPaths[g_keyValuePatchCount++], kKeyValuePathCapacity, "%s", relative);
}

void ScanKeyValueDirectory(const char* absolute, const char* relative, int depth) noexcept {
    if (depth > 4) return;
    DIR* dir = opendir(absolute);
    if (!dir) return;
    while (dirent* entry = readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        char childAbsolute[512], childRelative[kKeyValuePathCapacity];
        if (std::snprintf(childAbsolute, sizeof(childAbsolute), "%s/%s", absolute, entry->d_name) < 0) continue;
        const int n = *relative
            ? std::snprintf(childRelative, sizeof(childRelative), "%s/%s", relative, entry->d_name)
            : std::snprintf(childRelative, sizeof(childRelative), "%s", entry->d_name);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(childRelative)) continue;
        struct stat info{};
        if (stat(childAbsolute, &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) ScanKeyValueDirectory(childAbsolute, childRelative, depth + 1);
        else RecordKeyValuePatch(childRelative);
    }
    closedir(dir);
}

void CollectKeyValuePatches() noexcept {
    g_keyValuePatchCount = 0;
    g_keyValueBuiltCount = 0;
    for (std::int32_t i = 0; i < g_modRootCount; ++i) {
        char keyvalues[512];
        if (!KeyValuesRootForMod(i, keyvalues, sizeof(keyvalues))) continue;
        ScanKeyValueDirectory(keyvalues, "", 0);
    }
    for (std::int32_t i = 0; i < g_keyValuePatchCount; ++i)
        LogFormat("[NorthstarPS4] keyvalues patch declared: %s\n", g_keyValuePatchPaths[i]);
    LogFormat("[NorthstarPS4] keyvalues patches discovered=%d\n", g_keyValuePatchCount);
}

bool IsKeyValuePatched(const char* normalized) noexcept {
    for (std::int32_t i = 0; i < g_keyValuePatchCount; ++i)
        if (!std::strcmp(g_keyValuePatchPaths[i], normalized)) return true;
    return false;
}

bool KeyValuesAlreadyBuilt(const char* normalized) noexcept {
    for (std::int32_t i = 0; i < g_keyValueBuiltCount; ++i)
        if (!std::strcmp(g_keyValueBuiltPaths[i], normalized)) return true;
    return false;
}

// The merged file keeps the requested path under the generated root so two
// patched files with the same leaf name in different directories cannot
// collide.
bool KeyValuesOutputPath(const char* normalized, char* out, std::size_t capacity) noexcept {
    const int n = std::snprintf(out, capacity, "%s/%s", kKeyValuesRoot, normalized);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

bool MakeKeyValueDirectories(const char* path) noexcept {
    char buffer[512];
    const int n = std::snprintf(buffer, sizeof(buffer), "%s", path);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buffer)) return false;
    for (char* p = buffer + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buffer, 0777) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return true;
}

// Reads through the engine filesystem, bypassing this module's own hook so the
// vanilla content is what gets merged rather than a previously generated file.
bool ReadOriginalKeyValues(void* self, const char* normalized, std::string& out) noexcept {
    void* handle = g_originalFsOpenEx(self, normalized, "rb", 0, "GAME", nullptr);
    if (!handle) return false;
    char chunk[4096];
    int count = 0;
    do {
        count = g_originalFsRead(reinterpret_cast<char*>(self) + 8, chunk, sizeof(chunk), handle);
        if (count > 0) out.append(chunk, count);
    } while (count == static_cast<int>(sizeof(chunk)) && out.size() < kMaxKeyValueFileSize);
    g_originalFsClose(reinterpret_cast<char*>(self) + 8, handle);
    return count >= 0 && !out.empty() && out.size() < kMaxKeyValueFileSize;
}

bool ReadModKeyValues(const char* absolute, std::string& out) noexcept {
    FILE* file = std::fopen(absolute, "rb");
    if (!file) return false;
    char chunk[4096];
    std::size_t count = 0;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        out.append(chunk, count);
        if (out.size() >= kMaxKeyValueFileSize) break;
    }
    std::fclose(file);
    return !out.empty() && out.size() < kMaxKeyValueFileSize;
}

bool BuildKeyValuesPatch(void* self, const char* normalized) noexcept;

// The engine sizes its parse buffer with Size(fileName, pathID), which walks
// its own search paths and never reaches the Open hook. Serving a file larger
// than the one it measured therefore gets read short: the first attempt at
// this died on `FatalError: KeyValues Error: Error reading token` at byte
// 360403 of a 372535 byte playlist - exactly the original's length, mid-token
// inside a localised description. So the size has to be answered here too.
bool KeyValuesServedSize(void* self, const char* normalized, std::uint64_t& size) noexcept {
    if (!KeyValuesAlreadyBuilt(normalized) && !BuildKeyValuesPatch(self, normalized)) return false;
    char outputPath[512];
    if (!KeyValuesOutputPath(normalized, outputPath, sizeof(outputPath))) return false;
    struct stat info{};
    if (stat(outputPath, &info) != 0) return false;
    size = static_cast<std::uint64_t>(info.st_size);
    return true;
}

bool BuildKeyValuesPatch(void* self, const char* normalized) noexcept {
    std::string originalText;
    if (!ReadOriginalKeyValues(self, normalized, originalText)) {
        LogFormat("[NorthstarPS4] keyvalues skipped, no base file: %s\n", normalized);
        return false;
    }
    KeyValueList merged;
    std::string error;
    if (!ParseKeyValues(originalText.c_str(), merged, error)) {
        LogFormat("[NorthstarPS4] keyvalues base parse failed %s: %s\n", normalized, error.c_str());
        return false;
    }
    // Ascending priority: g_modRoots[0] is the lowest, and the last merge wins,
    // so the highest-priority mod must be applied last.
    int applied = 0;
    for (std::int32_t i = 0; i < g_modRootCount; ++i) {
        char keyvalues[512], absolute[768];
        if (!KeyValuesRootForMod(i, keyvalues, sizeof(keyvalues))) continue;
        if (std::snprintf(absolute, sizeof(absolute), "%s/%s", keyvalues, normalized) < 0) continue;
        std::string patchText;
        if (!ReadModKeyValues(absolute, patchText)) continue;
        KeyValueList patch;
        if (!ParseKeyValues(patchText.c_str(), patch, error)) {
            LogFormat("[NorthstarPS4] keyvalues patch parse failed %s: %s\n", absolute, error.c_str());
            return false;
        }
        MergeKeyValues(merged, patch);
        ++applied;
        LogFormat("[NorthstarPS4] keyvalues merged %s (%zu bytes)\n", absolute, patchText.size());
    }
    if (applied == 0) return false;

    // Written without indentation: the engine reads a served file short if it
    // is longer than the one it measured, and dropping the tabs buys about
    // 24 KB on the playlist - far more than the patches add.
    const std::string output = SerialiseKeyValues(merged, false);
    // A merge can only add to or replace within the original, so a result much
    // smaller than the input means something went wrong. Refuse rather than
    // serve it: an empty playlist is a boot failure, not a degraded mode.
    if (output.size() < originalText.size() / 2) {
        LogFormat("[NorthstarPS4] keyvalues refused %s: merged %zu bytes from %zu, suspiciously small\n",
            normalized, output.size(), originalText.size());
        return false;
    }
    // The engine sizes its parse buffer from the original file and reads only
    // that many bytes, so anything longer arrives truncated mid-token and is a
    // FatalError, not a degraded file. Falling back to vanilla loses the patch
    // but keeps the game bootable, and says so.
    if (output.size() > originalText.size()) {
        LogFormat("[NorthstarPS4] keyvalues refused %s: merged %zu bytes exceeds original %zu, would be read short\n",
            normalized, output.size(), originalText.size());
        return false;
    }
    char outputPath[512];
    if (!KeyValuesOutputPath(normalized, outputPath, sizeof(outputPath))) return false;
    if (!MakeKeyValueDirectories(outputPath)) {
        LogFormat("[NorthstarPS4] keyvalues output directory failed: %s\n", outputPath);
        return false;
    }
    FILE* file = std::fopen(outputPath, "wb");
    if (!file) {
        LogFormat("[NorthstarPS4] keyvalues output not writable: %s\n", outputPath);
        return false;
    }
    bool ok = std::fwrite(output.data(), 1, output.size(), file) == output.size();
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        LogFormat("[NorthstarPS4] keyvalues write failed: %s\n", outputPath);
        return false;
    }
    if (g_keyValueBuiltCount < static_cast<std::int32_t>(kMaxKeyValuePatches))
        std::snprintf(g_keyValueBuiltPaths[g_keyValueBuiltCount++], kKeyValuePathCapacity, "%s", normalized);
    LogFormat("[NorthstarPS4] keyvalues built %s patches=%d original=%zu merged=%zu bytes\n",
        normalized, applied, originalText.size(), output.size());
    return true;
}
