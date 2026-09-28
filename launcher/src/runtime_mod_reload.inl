// Live mod reload: NSReloadMods, PC's ModManager::LoadMods after boot.
//
// Included inside the runtime's anonymous namespace after runtime_vpks.inl and
// runtime_rpaks.inl, in the runtime-manifest build.
//
// The server browser calls ReloadMods() (menu_ns_modmenu.nut) whenever joining
// a server changes which client-required mods are on: NSReloadMods, then
// `reload_localization`, `loadPlaylists`, `weapon_reparse` and `uiscript_reset`,
// and the connect's map load follows. On PS4 each of those was checked in the
// lobby: `uiscript_reset` rebuilds the UI VM through this runtime's VM hooks,
// and `loadPlaylists` and `weapon_reparse` run cleanly. `reload_localization`
// does not: it rebuilds localize.prx's token table while another thread reads
// it, and the game faulted at localize+0x7aed, a hash-chain walk, on the first
// try. So its command is replaced (runtime_concommands.inl) with the part that
// matters here, adding newly enabled mods' files, which never clears the table.
//
// What a reload rebuilds, all from the enabled set on disk:
//   - the file overlay (mod search paths and file index), as a new snapshot;
//   - scripts.rson, regenerated on its next open (the UI VM reset opens it);
//   - the mod VPK list;
//   - KeyValues patches, remerged on their next open;
//   - the ConVars of newly enabled mods (PC keeps existing ones too);
//   - the mod catalog the menus read.
// VM-side state - InitScripts, callbacks, natives - is read again at each VM
// creation already. Rpaks stay off (runtime_rpaks.inl).

using LocaliseAddFileFn = bool (*)(void*, const char*, const char*, bool);
LocaliseAddFileFn g_localiseAddFile = nullptr;  // set by ProbeLocaliseInterface
std::uintptr_t g_localiseThis = 0;
std::vector<std::string> g_localisationAdded;   // files handed to AddFile, once each

// Adds the Localisation files of every enabled mod that have not been added
// yet. Main thread (or the module tracker at boot) only.
void AddModLocalisationFiles(std::int32_t& total, std::int32_t& loaded) noexcept {
    if (!g_localiseAddFile || !g_localiseThis) return;
    // AddFile routes through vtable slot 9 (its own address) when this+0x48 is
    // zero; force the field to 1 so the direct path is taken, then restore it.
    auto* const fallbackField = reinterpret_cast<std::uint8_t*>(g_localiseThis + 0x48);
    const std::uint8_t savedFallback = *fallbackField;
    *fallbackField = 1;
    static ModDiscovery discovery;
    static char json[kModJsonBufferSize];
    static ModInfo mod;
    CollectModNames(discovery);
    for (std::int32_t i = 0; i < discovery.count; ++i) {
        char path[kModDirCapacity + 16];
        std::snprintf(path, sizeof(path), "%s/mod.json", discovery.dirs[i]);
        std::size_t size = 0;
        if (!ReadFileIntoBuffer(path, json, sizeof(json) - 1, size) || !ParseModMetadata(json, mod)) {
            LogFormat("[NorthstarPS4] localise mod metadata unreadable: %s\n", path);
            continue;
        }
        for (std::int32_t f = 0; f < mod.localisationCount; ++f) {
            // The PS4 AddFile does not expand %language%: given the token it only
            // probes the stock resource folders and returns true without opening
            // anything, so every mod-only token stayed unresolved. With the name
            // spelled out it loads through the mod overlay. English is what PC
            // falls back to; picking the system language is not done yet.
            std::string resolved = mod.localisationFiles[f];
            const std::size_t token = resolved.find("%language%");
            if (token != std::string::npos) resolved.replace(token, 10, "english");
            if (std::find(g_localisationAdded.begin(), g_localisationAdded.end(), resolved) != g_localisationAdded.end())
                continue;
            const bool ok = g_localiseAddFile(reinterpret_cast<void*>(g_localiseThis), resolved.c_str(), nullptr, false);
            LogFormat("[NorthstarPS4] localise %s file=%s result=%d\n", mod.name, resolved.c_str(), ok ? 1 : 0);
            g_localisationAdded.push_back(resolved);
            ++total;
            if (ok) ++loaded;
        }
    }
    *fallbackField = savedFallback;
}

#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
// Called by NSReloadMods on the UI thread after enabledmods.json is saved.
void ReloadModState() noexcept {
    g_modsReloaded = true;

    ModOverlay* overlay = BuildModOverlay();
    const ModOverlay* previous = g_modOverlay.exchange(overlay, std::memory_order_acq_rel);
    (void)previous;  // leaked on purpose, see ModOverlay

    g_runtimeManifestGenerated = false;

    // MountModVpks walks g_modVpks on the engine's reader thread, holding this
    // flag; a mount that finds it held skips mod archives for that one call.
    while (g_mountingModVpks.test_and_set(std::memory_order_acquire)) sceKernelUsleep(1000);
    DiscoverModVpks();
    g_mountingModVpks.clear(std::memory_order_release);

    CollectKeyValuePatches();

    if (g_modConVarCvar && g_modConVarFindVar && g_modConVarConstructor) {
        static ModDiscovery discovery;
        static char json[kModJsonBufferSize];
        static ModInfo mod;
        CollectModNames(discovery);
        for (std::int32_t i = 0; i < discovery.count; ++i) {
            char path[kModDirCapacity + 16];
            std::snprintf(path, sizeof(path), "%s/mod.json", discovery.dirs[i]);
            std::size_t size = 0;
            if (!ReadFileIntoBuffer(path, json, sizeof(json) - 1, size) || !ParseModMetadata(json, mod)) continue;
            RegisterModConVars(mod, g_modConVarSlot, g_modConVarCvar, g_modConVarFindVar, g_modConVarConstructor);
        }
    }

    uiapi::catalog.clear();
    uiapi::catalogReady = false;
    LogFormat("[NorthstarPS4] mods reloaded: %zu enabled\n", overlay->dirs.size());
}
#endif
