constexpr const char* kPs4PdefPath = "cfg/server/persistent_player_data_version_929.pdef";
constexpr const char* kPcPdefPath = "cfg/server/persistent_player_data_version_231.pdef";
constexpr const char* kGeneratedPdef = "/data/northstar_ps4/persistent_player_data_version_929.pdef";
bool g_pdefAttempted = false, g_pdefReady = false;
std::uint64_t g_pdefSize = 0;
bool ReadEnabledPdef(const char* relative, std::string& output) {
    for (int i = g_modRootCount-1; i >= 0; --i) {
        const std::string path = std::string(g_modRoots[i]) + "/" + relative;
        if (ReadModKeyValues(path.c_str(), output)) return true;
        if (!output.empty()) return false; // Do not use a partial/oversized file.
    }
    return false;
}
bool PreparePersistenceSchema(void* self) {
    if (g_pdefAttempted) return g_pdefReady;
    g_pdefAttempted = true;
    std::string reference, base, roots, enums, merged, error;
    // No enabled PC schema means vanilla behavior, including ordinary SP.
    if (!ReadEnabledPdef(kPcPdefPath, reference)) return false;
    if (!ReadEnabledPdef(kPs4PdefPath, base) &&
        (!base.empty() || !ReadOriginalKeyValues(self, kPs4PdefPath, base))) {
        LogFormat("[NorthstarPS4] persistence schema: cannot read PS4 base\n"); return false;
    }
    if (!ExtendPs4PersistenceRoots(base, reference, roots, error) ||
        !ExtendPs4LoadoutEnums(roots, reference, enums, error) ||
        !ExtendPs4IntCapacities(enums, reference, merged, error)) {
        LogFormat("[NorthstarPS4] persistence schema rejected: %s\n", error.c_str()); return false;
    }
    mkdir("/data/northstar_ps4", 0777);
    const std::string temporary = std::string(kGeneratedPdef) + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool ok = std::fwrite(merged.data(), 1, merged.size(), file) == merged.size();
    if (std::fflush(file) || fsync(fileno(file))) ok = false;
    if (std::fclose(file)) ok = false;
    if (ok) ok = std::rename(temporary.c_str(), kGeneratedPdef) == 0;
    if (!ok) { std::remove(temporary.c_str()); LogFormat("[NorthstarPS4] persistence schema cache write failed\n"); return false; }
    g_pdefSize = merged.size(); g_pdefReady = true;
    LogFormat("[NorthstarPS4] persistence schema generated base=%zu bytes=%zu; PC roots, loadout enums and integer capacities validated\n", base.size(), merged.size());
    return true;
}
