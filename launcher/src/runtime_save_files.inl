// Northstar's Safe I/O ("save file") API, included inside namespace uiapi.
//
// PC Northstar isolates each mod's storage by asking the Squirrel call stack
// which script file invoked the native, then mapping that file back to its
// owning mod (squirrel.h getcallingmod + ModManager::m_ModFiles). The same
// two steps are reproduced here: CallingSource reads the PS4 VM's call stack,
// and CallingModFolder resolves the source file through the same mod search
// roots the filesystem hook serves, so the highest-priority mod that actually
// provides the file owns the data. A shared folder would break that isolation
// and is deliberately not used.
constexpr const char* kSaveRoot = "/data/northstar_ps4/save_data";
constexpr std::size_t kSaveFileBufferLimit = 1024 * 1024;

// Call-stack layout of the supported PS4 client.prx, read (never written) the
// way the client's own callstack printer does at VA 0x67f062-0x67f0bf:
//   sqvm+0x38 _callstack, sqvm+0x40 _callstacksize, CallInfo stride 0x48,
//   ci+0x20/0x28 an OT_FUNCPROTO object, proto+0x38/0x40 its source SQString,
//   string characters at +0x30 (the same +0x30 TextArg already relies on).
constexpr std::size_t kCallInfoStride = 0x48;
const char* CallingSource(void* vm, int depth) noexcept {
    if (!vm) return nullptr;
    auto bytes = static_cast<char*>(vm);
    const auto size = *reinterpret_cast<const std::int32_t*>(bytes + 0x40);
    const std::int32_t level = 1 + depth;
    if (level < 0 || level >= size) return nullptr;
    auto stack = *reinterpret_cast<char* const*>(bytes + 0x38);
    if (!stack) return nullptr;
    const char* frame = stack + static_cast<std::size_t>(size - level - 1) * kCallInfoStride;
    if (*reinterpret_cast<const std::uint32_t*>(frame + 0x20) != 0x8002000) return nullptr;
    auto proto = *reinterpret_cast<char* const*>(frame + 0x28);
    if (!proto || *reinterpret_cast<const std::uint32_t*>(proto + 0x38) != 0x8000010) return nullptr;
    auto source = *reinterpret_cast<char* const*>(proto + 0x40);
    return source ? source + 0x30 : nullptr;
}

// PC looks the compiled source up in the mod file map, which records the
// winning mod for each path. The equivalent here is the search order the
// OpenEx hook already uses: highest priority root first.
bool CallingModFolder(void* vm, int depth, char* out, std::size_t capacity) noexcept {
    const char* source = CallingSource(vm, depth);
    char normalized[256]{};
    if (!source || !NormalizeRequestedPath(source, normalized, sizeof(normalized))) return false;
    char scriptPath[300];
    const int written = std::snprintf(scriptPath, sizeof(scriptPath), "scripts/vscripts/%s", normalized);
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(scriptPath)) return false;
    const std::int32_t i = FindModFileRoot(scriptPath);
    if (i >= 0) {
        // g_modRoots entries are "<mods>/<folder>/mod"; PC keys storage on the
        // same mod directory name rather than the display name.
        const char* root = g_modRoots[i];
        const char* tail = std::strrchr(root, '/');
        if (!tail || tail == root) return false;
        const char* nameStart = tail - 1;
        while (nameStart > root && nameStart[-1] != '/') --nameStart;
        const std::size_t length = static_cast<std::size_t>(tail - nameStart);
        if (length == 0 || length + 1 > capacity) return false;
        std::memcpy(out, nameStart, length);
        out[length] = '\0';
        return SaveFolderNameSafe(out);
    }
    return false;
}

// PC's NSGetCurrentModName/NSGetCallingModName report the mod's display Name
// from mod.json, not its folder, so the folder is resolved first and the
// metadata read for the name.
bool CallingModDisplayName(void* vm, int depth, char* out, std::size_t capacity) noexcept {
    char folder[128]{}, path[256], json[kModJsonBufferSize];
    std::size_t size = 0;
    if (!CallingModFolder(vm, depth, folder, sizeof(folder))) return false;
    std::snprintf(path, sizeof(path), "%s/%s/mod.json", kModsRoot, folder);
    ModInfo info{};
    if (!ReadFileIntoBuffer(path, json, sizeof(json), size) || !ParseModMetadata(json, info)) return false;
    const std::size_t length = std::strlen(info.name);
    if (length + 1 > capacity) return false;
    std::memcpy(out, info.name, length + 1);
    return true;
}

bool SaveDirectoryFor(void* vm, int depth, char* out, std::size_t capacity) noexcept {
    char folder[128]{};
    if (!CallingModFolder(vm, depth, folder, sizeof(folder))) return false;
    const int n = std::snprintf(out, capacity, "%s/%s", kSaveRoot, folder);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

bool MakeDirectories(const char* path) noexcept {
    char buffer[512];
    const int n = std::snprintf(buffer, sizeof(buffer), "%s", path);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(buffer)) return false;
    for (char* p = buffer + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buffer, 0777) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return mkdir(buffer, 0777) == 0 || errno == EEXIST;
}

// PC's folder cap is advisory ("prevents mods taking gigabytes", not an exact
// quota), so the same directory walk is used, skipping one file by name.
std::uint64_t FolderSize(const char* directory, const char* skip) noexcept {
    DIR* dir = opendir(directory);
    if (!dir) return 0;
    std::uint64_t total = 0;
    while (dirent* entry = readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        if (skip && !std::strcmp(entry->d_name, skip)) continue;
        char path[512];
        const int n = std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (n < 0 || static_cast<std::size_t>(n) >= sizeof(path)) continue;
        struct stat info{};
        if (stat(path, &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) total += FolderSize(path, nullptr);
        else total += static_cast<std::uint64_t>(info.st_size);
    }
    closedir(dir);
    return total;
}

// Resolves the caller's save directory and validates the requested relative
// path, raising the same script errors PC raises. `depth` matches PC's
// getcallingmod depth: 1 where a Northstar .gnut wrapper calls the native.
bool ResolveSavePath(void* vm, int depth, int argument, bool allowEmpty,
    char* directory, std::size_t directoryCapacity, char* full, std::size_t fullCapacity,
    const char** relativeOut, int* error) noexcept {
    *error = 0;
    if (!SaveDirectoryFor(vm, depth, directory, directoryCapacity)) {
        *error = Error(vm, "Has to be called from a mod function!");
        return false;
    }
    const char* relative = TextArg(vm, argument);
    if (!relative) relative = "";
    if (!SavePathSafe(relative, allowEmpty)) {
        char message[320];
        std::snprintf(message, sizeof(message),
            "File name invalid (%s)! Make sure it does not contain any non-ASCII character, "
            "and results in a path inside your mod's save folder.", relative);
        *error = Error(vm, message);
        return false;
    }
    const int n = *relative
        ? std::snprintf(full, fullCapacity, "%s/%s", directory, relative)
        : std::snprintf(full, fullCapacity, "%s", directory);
    if (n < 0 || static_cast<std::size_t>(n) >= fullCapacity) {
        *error = Error(vm, "Save file path is too long for this profile");
        return false;
    }
    if (relativeOut) *relativeOut = relative;
    return true;
}

// Shared by NSSaveFile and NSSaveJSONFile, which differ only in how they
// produce the contents. The quota and whitelist checks live here so both take
// the same path PC routes through SaveFileManager::SaveFileAsync.
int WriteSaveFile(void* vm, const char* directory, const char* full, const char* relative,
    const char* contents, std::size_t size) {
    if (!MakeDirectories(directory)) return Error(vm, "Cannot create the mod save folder");
    // PC checks the cap before dispatching the write, then again in the writer.
    const char* leaf = std::strrchr(relative, '/');
    if (FolderSize(directory, leaf ? leaf + 1 : relative) + size > kMaxSaveFolderSize)
        return Error(vm, "This mod has reached the maximum save folder size. Ask the mod developer to reduce its data usage.");
    // The extension whitelist is enforced on PC inside the writer, which logs
    // and drops the write instead of raising. That behaviour is preserved.
    if (!SaveExtensionAllowed(relative)) {
        LogFormat("[NorthstarPS4] save rejected: disallowed file extension: %s\n", relative);
        return 0;
    }
    char parent[512];
    std::snprintf(parent, sizeof(parent), "%s", full);
    if (char* slash = std::strrchr(parent, '/')) {
        *slash = '\0';
        if (!MakeDirectories(parent)) return Error(vm, "Cannot create the requested save sub-folder");
    }
    char temporary[540];
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", full);
    FILE* file = std::fopen(temporary, "wb");
    if (!file) { LogFormat("[NorthstarPS4] save failed to open: %s\n", full); return 0; }
    bool ok = std::fwrite(contents, 1, size, file) == size;
    if (std::fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (!ok || std::rename(temporary, full) != 0) {
        std::remove(temporary);
        LogFormat("[NorthstarPS4] save failed to commit: %s\n", full);
        return 0;
    }
    LogFormat("[NorthstarPS4] save committed: %s (%zu bytes)\n", full, size);
    return 0;
}

int SaveFile(void* vm) {
    char directory[320], full[512];
    const char* relative = nullptr;
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, false, directory, sizeof(directory), full, sizeof(full), &relative, &error)) return error;
    const char* contents = TextArg(vm, 2);
    if (!contents) return Error(vm, "NSSaveFile expects a file name and string contents");
    const std::size_t size = std::strlen(contents);
    if (!SaveContentsValid(contents, size))
        return Error(vm, "File contents may not contain NUL characters! Make sure your strings are valid!");
    return WriteSaveFile(vm, directory, full, relative, contents, size);
}

// Load results reach NSHandleLoadResult after the calling script has
// registered its callbacks, exactly as PC's message buffer arranges. The read
// itself is synchronous: PS4 has no background VM thread to hand a result to.
struct PendingLoad { void* vm; int handle; bool success; std::string contents; };
std::vector<PendingLoad> pendingLoads;
int lastLoadHandle = 0;

int LoadFile(void* vm) {
    // Depth 1: NS_InternalLoadFile is always reached through NSLoadFile.
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 1, 1, false, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    PendingLoad pending{vm, ++lastLoadHandle, false, std::string()};
    struct stat info{};
    if (stat(full, &info) == 0 && S_ISREG(info.st_mode) &&
        static_cast<std::uint64_t>(info.st_size) <= kSaveFileBufferLimit) {
        if (FILE* file = std::fopen(full, "rb")) {
            pending.contents.resize(static_cast<std::size_t>(info.st_size));
            const std::size_t read = pending.contents.empty() ? 0
                : std::fread(&pending.contents[0], 1, pending.contents.size(), file);
            pending.success = read == pending.contents.size();
            pending.contents.resize(read);
            std::fclose(file);
        }
    }
    if (!pending.success) {
        pending.contents.clear();
        LogFormat("[NorthstarPS4] save load failed: %s\n", full);
    }
    pendingLoads.push_back(std::move(pending));
    Integer(vm, lastLoadHandle);
    return 1;
}

int DeleteSaveFile(void* vm) {
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, false, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    if (std::remove(full) != 0) LogFormat("[NorthstarPS4] save delete failed: %s\n", full);
    return 0;
}

int FileExists(void* vm) {
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, false, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    struct stat info{};
    Boolean(vm, stat(full, &info) == 0);
    return 1;
}

int FileSize(void* vm) {
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, false, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    struct stat info{};
    if (stat(full, &info) != 0) return Error(vm, "GET FILE SIZE FAILED! Is the path valid?");
    Integer(vm, static_cast<int>(static_cast<std::uint64_t>(info.st_size) / 1024));
    return 1;
}

int IsFolder(void* vm) {
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, true, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    struct stat info{};
    Boolean(vm, stat(full, &info) == 0 && S_ISDIR(info.st_mode));
    return 1;
}

int GetAllFiles(void* vm) {
    // Depth 1: PC documents this as always called through NSGetAllFiles.
    char directory[320], full[512];
    int error = 0;
    if (!ResolveSavePath(vm, 1, 1, true, directory, sizeof(directory), full, sizeof(full), nullptr, &error)) return error;
    DIR* dir = opendir(full);
    if (!dir) return Error(vm, "DIR ITERATE FAILED! Is the path valid?");
    Array(vm);
    while (dirent* entry = readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        String(vm, entry->d_name);
        Append(vm);
    }
    closedir(dir);
    return 1;
}

int SpaceRemaining(void* vm) {
    char folder[128], directory[320];
    if (!CallingModFolder(vm, 0, folder, sizeof(folder))) return Error(vm, "Has to be called from a mod function!");
    std::snprintf(directory, sizeof(directory), "%s/%s", kSaveRoot, folder);
    const std::uint64_t used = FolderSize(directory, nullptr);
    const std::uint64_t remaining = used >= kMaxSaveFolderSize ? 0 : kMaxSaveFolderSize - used;
    Integer(vm, static_cast<int>(remaining / 1024));
    return 1;
}

// Calls a global UI script function with arguments, the PS4 equivalent of the
// engine's own dispatch at VA 0x679280: look the function up, push it, push
// the root table, push the arguments, then sq_call. sq_call pops the
// arguments; the closure it leaves behind is popped here, as the engine does.
void Pop(void* vm, int count) noexcept {
    auto bytes = static_cast<char*>(vm);
    auto& top = *reinterpret_cast<std::uint32_t*>(bytes + 0x68);
    auto stack = *reinterpret_cast<Object**>(bytes + 0x70);
    while (count-- > 0 && top > 0) {
        auto slot = stack + (--top);
        void* previous = (slot->tag & 0x08000000) ? reinterpret_cast<void*>(slot->value) : nullptr;
        *slot = {0x1000001, 0};
        if (previous) {
            auto refs = reinterpret_cast<std::uint32_t*>(static_cast<char*>(previous) + 8);
            if (--*refs == 0) {
                auto table = *reinterpret_cast<std::uintptr_t**>(previous);
                reinterpret_cast<void (*)(void*)>(table[2])(previous);
            }
        }
    }
}

bool DispatchLoadResult(void* vm, const PendingLoad& load) noexcept {
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(0x685cf0)(vm, "NSHandleLoadResult", &function, nullptr) < 0) {
        LogFormat("[NorthstarPS4] NSHandleLoadResult is not defined; load handle=%d dropped\n", load.handle);
        return false;
    }
    auto push = At<void (*)(void*, std::uint64_t, void*)>(0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    Integer(vm, load.handle);
    Boolean(vm, load.success);
    String(vm, load.contents.c_str());
    const int result = At<int (*)(void*, int, int, int)>(0x6876c0)(vm, 4, 0, 1);
    Pop(vm, 1);
    LogFormat("[NorthstarPS4] NSHandleLoadResult handle=%d success=%d result=%d\n",
        load.handle, load.success ? 1 : 0, result);
    return result >= 0;
}

// Drained wherever the runtime regains control of a script VM. PC drains this
// queue from CHostState::FrameUpdate; no equivalent PS4 per-frame hook is
// profiled yet, so a load result is delivered at the next code callback for
// that context rather than on the next frame. Results are keyed to the VM
// that requested them, so a CLIENT load never resolves into the UI VM.
void DrainPendingLoads(void* owner) noexcept {
    if (pendingLoads.empty() || !owner) return;
    void* vm = *reinterpret_cast<void**>(static_cast<char*>(owner) + 8);
    if (!vm) return;
    std::vector<PendingLoad> keep, batch;
    for (auto& load : pendingLoads) (load.vm == vm ? batch : keep).push_back(std::move(load));
    pendingLoads.swap(keep);
    for (const auto& load : batch) DispatchLoadResult(vm, load);
}

// Discards results for a VM that is going away before they were delivered.
void DropPendingLoads(void* owner) noexcept {
    if (pendingLoads.empty() || !owner) return;
    void* vm = *reinterpret_cast<void**>(static_cast<char*>(owner) + 8);
    if (!vm) return;
    std::vector<PendingLoad> keep;
    for (auto& load : pendingLoads) if (load.vm != vm) keep.push_back(std::move(load));
    if (keep.size() != pendingLoads.size())
        LogFormat("[NorthstarPS4] dropped %zu undelivered load results for a closing VM\n",
            pendingLoads.size() - keep.size());
    pendingLoads.swap(keep);
}
