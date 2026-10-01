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
// creation already. RPaks are loaded at process startup; live unload/reload
// ownership is still unsupported (runtime_rpaks.inl).

using LocaliseAddFileFn = bool (*)(void*, const char*, const char*, bool);
LocaliseAddFileFn g_localiseAddFile = nullptr;  // set by ProbeLocaliseInterface
std::uintptr_t g_localiseThis = 0;
std::vector<std::string> g_localisationAdded;   // files handed to AddFile, once each

const char* SystemLocalisationLanguage() noexcept {
    static const char* selected = nullptr;
    if (selected) return selected;
    std::int32_t systemLanguage = ORBIS_SYSTEM_PARAM_LANG_ENGLISH_US;
    const int result = sceSystemServiceParamGetInt(ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG, &systemLanguage);
    selected = result == 0 ? LocalisationLanguageForSystem(systemLanguage) : "english";
    LogFormat("[NorthstarPS4] system localisation language id=%d name=%s result=0x%x\n",
        systemLanguage, selected, result);
    return selected;
}

// Adds the Localisation files of every enabled mod that have not been added
// yet. Game main thread only: at boot from the engine's vgui init
// (runtime_localise_boot.inl), later from NSReloadMods.
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
            // spelled out it loads through the mod overlay. Use the PS4 system
            // language when that file exists and PC's English fallback otherwise.
            std::string resolved = mod.localisationFiles[f];
            const std::size_t token = resolved.find("%language%");
            if (token != std::string::npos) {
                const char* language = SystemLocalisationLanguage();
                resolved.replace(token, 10, language);
                char found[512];
                if (std::strcmp(language, "english") && !ResolveModFile(resolved.c_str(), found, sizeof(found))) {
                    resolved = mod.localisationFiles[f];
                    resolved.replace(token, 10, "english");
                }
            }
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
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
void ReloadAudioOverrides() noexcept; // runtime_audio.inl
void RegisterModConCommands() noexcept; // runtime_mod_concommands.inl
#endif

void ReloadModState() noexcept {
    g_modsReloaded = true;

    ModOverlay* overlay = BuildModOverlay();
    const ModOverlay* previous = g_modOverlay.exchange(overlay, std::memory_order_acq_rel);
    (void)previous;  // leaked on purpose, see ModOverlay

    g_runtimeManifestGenerated = false;
    ResetRuntimePdef();

    // MountModVpks walks g_modVpks on the engine's reader thread, holding this
    // flag; a mount that finds it held skips mod archives for that one call.
    while (g_mountingModVpks.test_and_set(std::memory_order_acquire)) sceKernelUsleep(1000);
    DiscoverModVpks();
    g_mountingModVpks.clear(std::memory_order_release);

    CollectKeyValuePatches();
    g_particleManifestGenerated = false;
    g_particleManifestGeneratedSize = 0;
    uiapi::ClearDatatableCache();

    // UI and CLIENT callbacks are read when their hooks are installed; SERVER
    // rereads at every VM. Without this, a mod enabled by the reload had its
    // scripts compiled but its callbacks never run: joining a Parkour server
    // skipped PKMode_Init in the CLIENT VM, the gamemode was never created, and
    // every frame raised "The index roundBased does not exist" with no HUD.
    if (!LoadLifecycleCallbacks(g_runtimeUiLifecycle) || !LoadLifecycleCallbacks(g_runtimeClientLifecycle))
        LogFormat("[NorthstarPS4] reload: UI/CLIENT callback metadata could not be read\n");

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

#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    ReloadAudioOverrides();
    RegisterModConCommands();
#endif

    uiapi::catalog.clear();
    uiapi::catalogReady = false;
    LogFormat("[NorthstarPS4] mods reloaded: %zu enabled\n", overlay->dirs.size());
}
#endif
