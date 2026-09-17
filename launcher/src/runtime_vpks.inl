using FsMountVpkFn = void* (*)(void*, const char*);
FsMountVpkFn g_originalMountVpk = nullptr;
struct RuntimeModVpk { std::string path, stem; bool preload; };
std::vector<RuntimeModVpk> g_modVpks;
std::atomic_flag g_mountingModVpks = ATOMIC_FLAG_INIT;
bool g_modVpkHookReady = false;
void DiscoverModVpks() {
    g_modVpks.clear();
    ModDiscovery mods{}; CollectModNames(mods);
    for (int i = 0; i < mods.count; ++i) {
        const std::string directory = std::string(kModsRoot) + "/" + mods.names[i] + "/vpk";
        DIR* dir = opendir(directory.c_str());
        if (!dir) continue;
        char config[kModJsonBufferSize]{}; std::size_t size = 0;
        const bool readConfig = ReadFileIntoBuffer((directory + "/vpk.json").c_str(), config, sizeof(config), size);
        const bool preload = VpkPreload(readConfig ? config : nullptr);
        std::vector<std::string> stems;
        while (dirent* entry = readdir(dir)) {
            std::string stem;
            if (ModVpkStem(entry->d_name, stem)) stems.push_back(stem);
        }
        closedir(dir);
        std::sort(stems.begin(), stems.end());
        for (const auto& stem : stems) {
            const std::string path = directory + "/" + stem;
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
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
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
#endif
    g_mountingModVpks.clear();
    return result;
}
void* ModMountVpk(void* self, const char* requested) {
    void* result = g_originalMountVpk(self, requested);
    LogFormat("[NorthstarPS4] engine VPK mount: %s result=%p\n", requested ? requested : "(null)", result);
    return MountModVpks(self, requested, result);
}
