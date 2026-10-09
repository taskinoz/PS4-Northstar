using FsMountVpkFn = void* (*)(void*, const char*);
FsMountVpkFn g_originalMountVpk = nullptr;
struct RuntimeModVpk { std::string path, stem; bool preload; };
std::vector<RuntimeModVpk> g_modVpks;
std::atomic_flag g_mountingModVpks = ATOMIC_FLAG_INIT;
bool g_modVpkHookReady = false;

// MountVPK lowercases the path it is given. A PS4's /data is case-sensitive,
// so Northstar.Custom's archive under /data/northstar_ps4/R2Northstar never
// mounted there (result 0), and fastball stopped on a BT animation only that
// archive has. Where the lowercased path does not reach a mod's
// vpk/ folder, the folder is copied once to an all-lowercase one. (libkernel
// exports no link(2), and a raw system call would run on the host under
// shadPS4, whose case-insensitive host never needs the copy anyway.)
constexpr const char* kVpkMirrorRoot = "/data/northstar_ps4/runtime/vpk";

std::string LowerCase(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

bool CopyFile(const char* from, const char* to) noexcept {
    const int in = open(from, O_RDONLY);
    if (in < 0) return false;
    const std::string partial = std::string(to) + ".partial";
    const int out = open(partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    bool ok = out >= 0;
    static char buffer[256 * 1024];
    while (ok) {
        const ssize_t got = read(in, buffer, sizeof(buffer));
        if (got <= 0) {
            ok = got == 0;
            break;
        }
        ok = write(out, buffer, static_cast<std::size_t>(got)) == got;
    }
    close(in);
    if (out >= 0) close(out);
    ok = ok && rename(partial.c_str(), to) == 0;
    if (!ok) unlink(partial.c_str());
    return ok;
}

// The lowercase folder holding `directory`'s files, or `directory` itself
// when it is already lowercase or cannot be mirrored.
std::string LowerCaseVpkFolder(const std::string& directory, const std::string& modFolder) {
    if (directory == LowerCase(directory) || IsDirectory(LowerCase(directory).c_str())) return directory;
    const std::string mirror = std::string(kVpkMirrorRoot) + "/" + LowerCase(modFolder);
    if (!uiapi::MakeDirectories(mirror.c_str())) {
        LogFormat("[NorthstarPS4] mod VPK mirror: could not create %s\n", mirror.c_str());
        return directory;
    }
    DIR* dir = opendir(directory.c_str());
    if (!dir) return directory;
    int copied = 0, current = 0, failed = 0;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string from = directory + "/" + entry->d_name;
        const std::string to = mirror + "/" + LowerCase(entry->d_name);
        struct stat source{}, target{};
        if (stat(from.c_str(), &source) != 0 || !S_ISREG(source.st_mode)) continue;
        std::uint64_t fromSize = 0, toSize = 0;
        if (stat(to.c_str(), &target) == 0 && uiapi::FileSizeOf(from.c_str(), fromSize) &&
            uiapi::FileSizeOf(to.c_str(), toSize) && fromSize == toSize && target.st_mtime >= source.st_mtime) {
            ++current;
            continue;
        }
        if (CopyFile(from.c_str(), to.c_str())) ++copied;
        else ++failed;
    }
    closedir(dir);
    LogFormat("[NorthstarPS4] mod VPK mirror %s: %d copied, %d current, %d failed\n", mirror.c_str(), copied, current,
        failed);
    return failed ? directory : mirror;
}

void DiscoverModVpks() {
    g_modVpks.clear();
    static ModDiscovery mods;
    CollectModNames(mods);
    for (int i = 0; i < mods.count; ++i) {
        const std::string modDirectory = mods.dirs[i];
        const std::string directory = modDirectory + "/vpk";
        DIR* dir = opendir(directory.c_str());
        if (!dir) continue;
        std::vector<char> configBuffer(kModJsonBufferSize);
        char* const config = configBuffer.data();
        std::size_t size = 0;
        const bool readConfig = ReadFileIntoBuffer((directory + "/vpk.json").c_str(), config, configBuffer.size(), size);
        const bool preload = VpkPreload(readConfig ? config : nullptr);
        std::vector<std::string> stems;
        while (dirent* entry = readdir(dir)) {
            std::string stem;
            if (ModVpkStem(entry->d_name, stem)) stems.push_back(stem);
        }
        closedir(dir);
        std::sort(stems.begin(), stems.end());
        const std::string mountFolder = stems.empty() ? directory
            : LowerCaseVpkFolder(directory, modDirectory.substr(modDirectory.find_last_of('/') + 1));
        for (const auto& stem : stems) {
            const std::string path = mountFolder + "/" + (mountFolder == directory ? stem : LowerCase(stem));
            // MountVPK formats "%s.pak000" into 0x104 bytes.
            if (path.size() + sizeof(".pak000") > 0x104) {
                LogFormat("[NorthstarPS4] mod VPK path too long: %s\n", path.c_str()); continue;
            }
            g_modVpks.push_back({path, stem, preload});
            LogFormat("[NorthstarPS4] mod VPK discovered: %s preload=%d\n", path.c_str(), preload);
        }
    }
}
void* MountModVpks(void* self, const char* requested, void* result) {
    if (!g_modVpkHookReady || g_mountingModVpks.test_and_set()) return result;
    for (const auto& vpk : g_modVpks) {
        if (!vpk.preload && !VpkMatchesMount(vpk.stem, requested)) continue;
        void* mounted = g_originalMountVpk(self, vpk.path.c_str());
        LogFormat("[NorthstarPS4] mod VPK mount: %s result=%p\n", vpk.path.c_str(), mounted);
        if (!result) result = mounted; // PC fallback for map-supplied mod archives
    }
    // One diagnostic for the model that exposed the missing mount integration.
    // It is absent from stock frontend/MP VPKs; this proves engine lookup, not
    // just an allocated VPK handle. Rendering/material validation is separate.
    static bool assetProbed = false;
    if (!assetProbed && !g_modVpks.empty()) {
        assetProbed = true;
        const char* asset = "models/titans/buddy/titan_buddy.mdl";
        void* file = g_originalFsOpenEx(self, asset, "rb", 0, "GAME", nullptr);
        unsigned char header[12]{};
        int bytes = file ? g_originalFsRead(static_cast<char*>(self) + 8, header, sizeof(header), file) : -1;
        if (file) g_originalFsClose(static_cast<char*>(self) + 8, file);
        LogFormat("[NorthstarPS4] VPK asset lookup: %s read=%d IDST=%d\n", asset, bytes,
            bytes == sizeof(header) && !std::memcmp(header, "IDST", 4));
    }
    g_mountingModVpks.clear();
    return result;
}
void* ModMountVpk(void* self, const char* requested) {
    void* result = g_originalMountVpk(self, requested);
    LogFormat("[NorthstarPS4] engine VPK mount: %s result=%p\n", requested ? requested : "(null)", result);
    return MountModVpks(self, requested, result);
}
