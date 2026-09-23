// Included inside the runtime's anonymous namespace, experimental build only.
// Addresses and preimages belong exclusively to the supported PS4 client.prx.
namespace uiapi {
struct Object { std::uint64_t tag, value; };
template<typename Fn> Fn At(std::uintptr_t va) { return reinterpret_cast<Fn>(g_runtimeClientBase + va); }
void PushPrimitive(void* vm, std::uint64_t tag, std::uint64_t value) {
    auto bytes = static_cast<char*>(vm);
    auto& top = *reinterpret_cast<std::uint32_t*>(bytes + 0x68);
    auto slot = *reinterpret_cast<Object**>(bytes + 0x70) + top++;
    void* previous = (slot->tag & 0x08000000) ? reinterpret_cast<void*>(slot->value) : nullptr;
    *slot = {tag, value};
    if (previous) {
        auto refs = reinterpret_cast<std::uint32_t*>(static_cast<char*>(previous) + 8);
        if (--*refs == 0) {
            auto table = *reinterpret_cast<std::uintptr_t**>(previous);
            reinterpret_cast<void (*)(void*)>(table[2])(previous);
        }
    }
}
void Boolean(void* vm, bool value) { PushPrimitive(vm, 0x1000008, value); }
void Integer(void* vm, int value) { PushPrimitive(vm, 0x5000002, static_cast<std::uint32_t>(value)); }
void String(void* vm, const char* value) { At<void (*)(void*, const char*, int)>(0x682e00)(vm, value, -1); }
void Asset(void* vm, const char* value) { At<void (*)(void*, const char*, int)>(0x682f40)(vm, value, -1); }
void Array(void* vm) { At<void (*)(void*, int)>(0x683330)(vm, 0); }
void Append(void* vm) { At<int (*)(void*, int)>(0x6835e0)(vm, -2); }
void Struct(void* vm, int fields) { At<void (*)(void*, int)>(0x684970)(vm, fields); }
void Seal(void* vm, int slot) { At<void (*)(void*, int)>(0x684b00)(vm, slot); }
int Error(void* vm, const char* message) { At<int (*)(void*, const char*)>(0x682a60)(vm, message); return -1; }
const Object& Arg(void* vm, int index) { return (*reinterpret_cast<Object**>(static_cast<char*>(vm) + 0x48))[index]; }
const char* TextArg(void* vm, int index) {
    const auto& arg = Arg(vm, index);
    return arg.tag == 0x8000010 ? reinterpret_cast<const char*>(arg.value + 0x30) : nullptr;
}
#include "runtime_save_files.inl"
#include "runtime_json.inl"
struct CatalogEntry { ModInfo info; std::string download; bool required, enabled; };
std::vector<CatalogEntry> catalog;
bool catalogReady = false;
void LoadCatalog() {
    if (catalogReady) return;
    ModDiscovery all{};
    CollectModNames(all, true);
    char settings[kModJsonBufferSize] = "{}";
    std::size_t size = 0;
    if (!ReadEnabledSettings(settings, sizeof(settings))) return;
    for (int i = 0; i < all.count; ++i) {
        char path[256], json[kModJsonBufferSize];
        std::snprintf(path, sizeof(path), "%s/%s/mod.json", kModsRoot, all.names[i]);
        CatalogEntry entry{};
        if (!ReadFileIntoBuffer(path, json, sizeof(json), size) || !ParseModMetadata(json, entry.info)) continue;
        char download[512]{};
        const char* link = JsonFindMember(json, "DownloadLink");
        if (link) JsonExtractString(link, download, sizeof(download));
        entry.download = download;
        const char* required = JsonFindMember(json, "RequiredOnClient");
        entry.required = required && std::strncmp(JsonSkipWs(required), "true", 4) == 0;
        entry.enabled = IsModEnabled(settings, entry.info);
        catalog.push_back(entry);
    }
    catalogReady = true;
}
void PushMod(void* vm, const CatalogEntry& entry) {
    const auto& m = entry.info;
    Struct(vm, 9);
    String(vm, m.name); Seal(vm, 0);
    String(vm, m.description); Seal(vm, 1);
    String(vm, m.version); Seal(vm, 2);
    String(vm, entry.download.c_str()); Seal(vm, 3);
    Integer(vm, m.loadPriority); Seal(vm, 4);
    Boolean(vm, entry.enabled); Seal(vm, 5);
    Boolean(vm, entry.required); Seal(vm, 6);
    Boolean(vm, false); Seal(vm, 7); // app0 mods are local, never remote packages
    Array(vm);
    for (int i = 0; i < m.conVarCount; ++i) { String(vm, m.conVars[i].name); Append(vm); }
    Seal(vm, 8);
    Append(vm);
}
int GetMods(void* vm) { LoadCatalog(); if (!catalogReady) return Error(vm, "Cannot read mod catalog"); Array(vm); for (const auto& mod : catalog) PushMod(vm, mod); return 1; }
int GetMod(void* vm) {
    const char* name = TextArg(vm, 1);
    if (!name) return Error(vm, "NSGetModInformation expects a mod name");
    LoadCatalog(); if (!catalogReady) return Error(vm, "Cannot read mod catalog"); Array(vm);
    for (const auto& mod : catalog) if (!std::strcmp(name, mod.info.name)) PushMod(vm, mod);
    return 1;
}
int GetNames(void* vm) { LoadCatalog(); if (!catalogReady) return Error(vm, "Cannot read mod catalog"); Array(vm); for (const auto& mod : catalog) { String(vm, mod.info.name); Append(vm); } return 1; }
int SetEnabled(void* vm) {
    const char* name = TextArg(vm, 1); const char* version = TextArg(vm, 2);
    if (!name || !version || Arg(vm, 3).tag != 0x1000008) return Error(vm, "NSSetModEnabled expects name, version and bool");
    LoadCatalog();
    for (auto& mod : catalog) if (!std::strcmp(name, mod.info.name) && !std::strcmp(version, mod.info.version)) {
        mod.enabled = Arg(vm, 3).value != 0;
        return 0;
    }
    return 0;
}
int ReloadMods(void* vm) {
    LoadCatalog();
    if (!catalogReady) return Error(vm, "Cannot reload: enabled settings are unreadable");
    char buffer[kModJsonBufferSize];
    if (!ReadEnabledSettings(buffer, sizeof(buffer))) return Error(vm, "Cannot read enabled settings");
    std::string settings = buffer;
    for (const auto& mod : catalog)
        if (!SetEnabledSetting(settings, mod.info.name, mod.info.version, mod.enabled))
            return Error(vm, "Cannot save enabled settings: unsupported metadata key");
    if (settings.size() >= kModJsonBufferSize) return Error(vm, "Enabled settings exceed the profile capacity");
    mkdir("/data/northstar_ps4", 0777);
    const char* temporary = "/data/northstar_ps4/enabledmods.json.tmp";
    FILE* file = std::fopen(temporary, "wb");
    if (!file) return Error(vm, "Cannot write enabled settings");
    bool ok = std::fwrite(settings.data(), 1, settings.size(), file) == settings.size();
    if (std::fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (!ok || std::rename(temporary, "/data/northstar_ps4/enabledmods.json") != 0)
        return Error(vm, "Cannot commit enabled settings; previous settings preserved");
    // Do not change loose-file roots underneath a live VM: existing compiled
    // globals belong to its current generation. The next boot applies the file.
    LogFormat("[NorthstarPS4] enabled settings committed; restart required to reload script VMs\n");
    return Error(vm, "Mod settings saved. Restart the game to apply them; live VM reload is not implemented on PS4");
}
// Most of these stay explicit about being unimplemented: a request that cannot
// work should finish with a reason rather than hang the UI.
//
// Master-server authentication is the exception, and is no longer
// unconditionally off. When an Atlas identity has been imported - see
// runtime_auth.inl - the session is real; when it has not, the reason says how
// to get one instead of only that it failed.
std::string authFailure;
// Result of the most recent auth attempt, which is what NSWasAuthSuccessful
// reports. Kept separate from the identity state: having an identity is not the
// same as having tried to use it.
bool authSucceeded = false;
int Authenticated(void* vm) { Boolean(vm, false); return 1; }
int MasterServerAuthenticated(void* vm) { Boolean(vm, AtlasIdentityReady()); return 1; }
int AuthSuccessful(void* vm) { Boolean(vm, authSucceeded); return 1; }

// "Launch Northstar" calls this, waits for NSIsAuthenticatingWithServer to go
// false, then branches on NSWasAuthSuccessful: success runs
// NSCompleteAuthWithLocalServer followed by `setplaylist tdm` and
// `map mp_lobby`, which is what actually starts the local lobby and puts the
// Northstar menus up. Failure shows NSGetAuthFailReason in a dialog.
//
// There is nothing to negotiate here. PC authenticates with its own server
// through Atlas because its server half enforces auth; this port does not hook
// server.prx at all, so the local lobby has no Northstar auth to satisfy. An
// imported identity is therefore sufficient, and the attempt completes
// immediately - NSIsAuthenticatingWithServer already reports false.
int TryLocalAuth(void*) {
    authSucceeded = AtlasIdentityReady();
    authFailure = authSucceeded ? std::string() : std::string(AtlasIdentityMessage());
    LogFormat("[NorthstarPS4] local server auth %s%s%s\n",
        authSucceeded ? "succeeded" : "refused",
        authSucceeded ? "" : ": ", authSucceeded ? "" : authFailure.c_str());
    return 0;
}
// Falls back to the identity message: nothing may have set a reason yet, and an
// empty string in the menu tells the player nothing.
int AuthFailReason(void* vm) {
    String(vm, authFailure.empty() ? AtlasIdentityMessage() : authFailure.c_str());
    return 1;
}
// Distinct from CompleteAuth: raising here would abort the script between the
// success branch and the `map mp_lobby` that follows it, so the lobby would
// never start even though authentication had succeeded.
int CompleteLocalAuth(void* vm) {
    if (!authSucceeded) return Error(vm, AtlasIdentityMessage());
    LogFormat("[NorthstarPS4] local server auth completed; lobby launch follows\n");
    return 0;
}
#include "runtime_server_list.inl"
#include "runtime_server_join.inl"
int AuthResult(void* vm) {
    Struct(vm, 3);
    Boolean(vm, AtlasIdentityReady()); Seal(vm, 0);
    String(vm, AtlasIdentityCode()); Seal(vm, 1);
    String(vm, AtlasIdentityMessage()); Seal(vm, 2);
    return 1;
}
int RequestPromos(void*) { LogFormat("[NorthstarPS4] custom promo request failed: transport unavailable\n"); return 0; }
int PromoData(void* vm) { return Error(vm, "Custom promo data is unavailable"); }
// PC reports the mouse cursor. This platform is driven by a gamepad and has
// no cursor to report, and the declared return type is "vector ornull", so
// null is the honest answer rather than an invented coordinate.
int CursorPosition(void*) { return 0; }
// Match PC Northstar's defined behavior when its downloader is absent.
// This completes the script ABI, not the download transport implementation.
int DownloadUnavailable(void*) {
    LogFormat("[NorthstarPS4] mod downloader unavailable; install state NOT_FOUND\n");
    return 0;
}
int CancelDownload(void*) { return 0; }
int ModInstallState(void* vm) {
    Struct(vm, 4);
    Integer(vm, 12); Seal(vm, 0); // PC ModDownloader::NOT_FOUND
    Integer(vm, 0); Seal(vm, 1);
    Integer(vm, 0); Seal(vm, 2);
    PushPrimitive(vm, 0x5000004, 0); Seal(vm, 3); // float 0.0
    return 1;
}
int HttpUnavailable(void* vm) {
    LogFormat("[NorthstarPS4] HTTP handler unavailable; returning request handle -1\n");
    Integer(vm, -1); return 1;
}
// CLIENT-context natives.
//
// PC routes the chat family through LocalChatWriter and engine.dll's
// ClientSayText, neither of which is ported: the PS4 chat HUD and the
// engine-side say path are not profiled yet. These adapters therefore accept
// the call and record it instead of rendering or transmitting anything, which
// keeps CLIENT compilation working without pretending chat functions. This
// replaces the retired Northstar.PS4 script stubs with a native adapter, so
// no mod source is modified. Chat display and sending remain unimplemented.
int ChatWrite(void* vm) {
    const char* text = TextArg(vm, 2);
    LogFormat("[NorthstarPS4] NSChatWrite (not rendered): %s\n", text ? text : "");
    return 0;
}
int ChatWriteLine(void* vm) {
    const char* text = TextArg(vm, 2);
    LogFormat("[NorthstarPS4] NSChatWriteLine (not rendered): %s\n", text ? text : "");
    return 0;
}
int ChatWriteRaw(void* vm) {
    const char* text = TextArg(vm, 2);
    LogFormat("[NorthstarPS4] NSChatWriteRaw (not rendered): %s\n", text ? text : "");
    return 0;
}
int SendMessage(void* vm) {
    const char* text = TextArg(vm, 1);
    LogFormat("[NorthstarPS4] NSSendMessage (not sent): %s\n", text ? text : "");
    return 0;
}

// Portable natives available in every context.
int ToAsset(void* vm) {
    const char* name = TextArg(vm, 1);
    if (!name) return Error(vm, "StringToAsset expects a string");
    Asset(vm, name);
    return 1;
}
int CurrentModName(void* vm) {
    char name[128];
    if (!CallingModDisplayName(vm, 0, name, sizeof(name)))
        return Error(vm, "NSGetModName was called from a non-mod script. This shouldn't be possible");
    String(vm, name);
    return 1;
}
int CallingModName(void* vm) {
    const auto& argument = Arg(vm, 1);
    const int depth = argument.tag == 0x5000002 ? static_cast<int>(static_cast<std::int32_t>(argument.value)) : 0;
    char name[128];
    String(vm, CallingModDisplayName(vm, depth, name, sizeof(name)) ? name : "Unknown");
    return 1;
}

// SERVER-context natives.
//
// The SERVER VM compiles the same Northstar scripts the other two contexts do,
// so every native those scripts name has to exist by the time they compile or
// the load stops dead:
//
//   FatalError: sh_loadouts.nut: SERVER SCRIPT COMPILE ERROR:
//               Undefined variable "NSDisconnectPlayer"
//
// These are the SERVER-only half of PC Northstar's native surface - the
// entries above carrying kCtxServer cover the shared half. The implementations
// are minimal but not fictional: each returns what is actually true of this
// port, and the ones that would need engine plumbing this module has not
// mapped yet say so in the log the first time a script calls them, rather than
// quietly behaving as though they worked.
enum ServerStub { kStubDisconnect, kStubClientPrint, kStubBroadcast, kStubPersistence, kStubMapNames, kStubSendMessage, kStubCount };
bool serverStubWarned[kStubCount]{};
void WarnServerStub(int stub, const char* name) {
    if (serverStubWarned[stub]) return;
    serverStubWarned[stub] = true;
    LogFormat("[NorthstarPS4] native %s has no implementation on this port\n", name);
}

// PC reads the local player's Origin UID out of the engine; this port already
// has it from the imported Atlas identity, and an empty string when nothing
// has been imported - which is the same thing PC reports when not logged in.
int ServerLocalPlayerUid(void* vm) { String(vm, g_atlasUid); return 1; }

// Hosting a listen server on the console the identity belongs to means the
// player this VM is asked about is the local one. Telling remote players apart
// needs the client-array walk PC does, which is not mapped yet, so this is
// correct for the lobby and private matches and optimistic beyond them.
int ServerIsLocalPlayer(void* vm) { Boolean(vm, true); return 1; }

// There is no dedicated-server build for this platform.
int ServerIsDedicated(void* vm) { Boolean(vm, false); return 1; }

// Persistence is never written by this module, so a write is never in flight.
int ServerWritingPersistence(void* vm) { Boolean(vm, false); return 1; }
int ServerWritePersistenceForLeave(void*) { WarnServerStub(kStubPersistence, "NSEarlyWritePlayerPersistenceForLeave"); return 0; }

int ServerDisconnectPlayer(void* vm) { WarnServerStub(kStubDisconnect, "NSDisconnectPlayer"); Boolean(vm, false); return 1; }
int ServerSendClientPrint(void*) { WarnServerStub(kStubClientPrint, "NSSendClientPrint"); return 0; }
int ServerBroadcastMessage(void*) { WarnServerStub(kStubBroadcast, "NSBroadcastMessage"); return 0; }
int ServerSendMessage(void*) { WarnServerStub(kStubSendMessage, "NSSendMessage"); return 0; }

// PC returns the maps the server has loaded from disk. Enumerating those needs
// the engine's map list, so the array is empty rather than invented.
int ServerLoadedMapNames(void* vm) { WarnServerStub(kStubMapNames, "NSGetLoadedMapNames"); Array(vm); return 1; }

// Userinfo convars are per-connected-client state this module does not read
// yet, so each of these hands back the default the caller supplied. Echoing
// the argument's own tag keeps the declared return type exact for the numeric
// and boolean variants; strings and assets are pushed afresh so the new
// reference is counted rather than aliasing the argument's.
int UserInfoKvPrimitive(void* vm) { const auto& fallback = Arg(vm, 3); PushPrimitive(vm, fallback.tag, fallback.value); return 1; }
int UserInfoKvString(void* vm) { const auto& fallback = Arg(vm, 3); String(vm, (fallback.tag & 0x08000000) ? reinterpret_cast<const char*>(fallback.value + 0x30) : ""); return 1; }
int UserInfoKvAsset(void* vm) { const auto& fallback = Arg(vm, 3); Asset(vm, (fallback.tag & 0x08000000) ? reinterpret_cast<const char*>(fallback.value + 0x30) : ""); return 1; }

// Script contexts a native is registered into, matching PC's ADD_SQFUNC
// masks. SERVER entries are live now that server.prx is hooked: they are
// registered from RuntimeServerVmInit rather than from the client VM hook.
constexpr int kCtxClient = 1, kCtxUi = 2, kCtxServer = 4;
constexpr int kCtxAll = kCtxClient | kCtxUi | kCtxServer;
struct Registration { const char* name; const char* returns; const char* args; int (*function)(void*); int contexts; };
#include "runtime_ai_harness.inl"
const Registration registrations[] = {
    {"NSAIHarnessField", "string", "string json, string key", HarnessField, kCtxUi},
    {"NSAIHarnessRead", "string", "", HarnessRead, kCtxUi},
    {"NSAIHarnessReply", "void", "string response", HarnessReply, kCtxUi},
    {"NS_InternalMakeHttpRequest", "int", "int method, string baseUrl, table<string, array<string> > headers, table<string, array<string> > queryParams, string contentType, string body, int timeout, string userAgent", HttpUnavailable, kCtxAll},
    {"NSIsHttpEnabled", "bool", "", Authenticated, kCtxAll},
    {"NSIsLocalHttpAllowed", "bool", "", Authenticated, kCtxAll},
    {"NSFetchVerifiedModsManifesto", "void", "", DownloadUnavailable, kCtxAll},
    {"NSIsModDownloadable", "bool", "string name, string version", Authenticated, kCtxAll},
    {"NSDownloadMod", "void", "string name, string version", DownloadUnavailable, kCtxAll},
    {"NSGetModInstallState", "ModInstallState", "", ModInstallState, kCtxAll},
    {"NSCancelModDownload", "void", "", CancelDownload, kCtxAll},
    {"NSIsMasterServerAuthenticated", "bool", "", MasterServerAuthenticated, kCtxUi},
    {"NSGetMasterServerAuthResult", "MasterServerAuthResult", "", AuthResult, kCtxUi},
    {"NSTryAuthWithLocalServer", "void", "", TryLocalAuth, kCtxUi},
    {"NSTryAuthWithServer", "void", "int serverIndex, string password = ''", TryRemoteAuth, kCtxUi},
    {"NSIsAuthenticatingWithServer", "bool", "", IsAuthenticating, kCtxUi},
    {"NSWasAuthSuccessful", "bool", "", AuthSuccessful, kCtxUi},
    {"NSGetAuthFailReason", "string", "", AuthFailReason, kCtxUi},
    {"NSCompleteAuthWithLocalServer", "void", "", CompleteLocalAuth, kCtxUi},
    {"NSConnectToAuthedServer", "void", "", CompleteAuth, kCtxUi},
    {"NSRequestServerList", "void", "", RequestServers, kCtxUi},
    {"NSIsRequestingServerList", "bool", "", IsRequestingServers, kCtxUi},
    {"NSMasterServerConnectionSuccessful", "bool", "", MasterServerReachable, kCtxUi},
    {"NSGetServerCount", "int", "", ServerCount, kCtxUi},
    {"NSClearRecievedServerList", "void", "", ClearServers, kCtxUi},
    {"NSGetGameServers", "array<ServerInfo>", "", GameServers, kCtxUi},
    {"NSGetModsInformation", "array<ModInfo>", "", GetMods, kCtxAll},
    {"NSGetModInformation", "array<ModInfo>", "string modName", GetMod, kCtxAll},
    {"NSGetModNames", "array<string>", "", GetNames, kCtxAll},
    {"NSReloadMods", "void", "", ReloadMods, kCtxUi},
    {"NSSetModEnabled", "void", "string modName, string modVersion, bool enabled", SetEnabled, kCtxAll},
    {"NSSaveFile", "void", "string file, string data", SaveFile, kCtxAll},
    {"NSSaveJSONFile", "void", "string file, table data", SaveJsonFile, kCtxAll},
    {"DecodeJSON", "table", "string json, bool fatalParseErrors = false", DecodeJson, kCtxAll},
    {"EncodeJSON", "string", "table data", EncodeJson, kCtxAll},
    {"NS_InternalLoadFile", "int", "string file", LoadFile, kCtxAll},
    {"NS_InternalGetAllFiles", "array<string>", "string path", GetAllFiles, kCtxAll},
    {"NSDoesFileExist", "bool", "string file", FileExists, kCtxAll},
    {"NSGetFileSize", "int", "string file", FileSize, kCtxAll},
    {"NSDeleteFile", "void", "string file", DeleteSaveFile, kCtxAll},
    {"NSIsFolder", "bool", "string path", IsFolder, kCtxAll},
    {"NSGetTotalSpaceRemaining", "int", "", SpaceRemaining, kCtxAll},
    {"NSRequestCustomMainMenuPromos", "void", "", RequestPromos, kCtxUi},
    {"NSHasCustomMainMenuPromoData", "bool", "", Authenticated, kCtxUi},
    {"NSGetCustomMainMenuPromoData", "var", "int promoDataKey", PromoData, kCtxUi},
    {"NSGetCursorPosition", "vector ornull", "", CursorPosition, kCtxUi},
    {"NSChatWrite", "void", "int context, string text", ChatWrite, kCtxClient},
    {"NSChatWriteLine", "void", "int context, string text", ChatWriteLine, kCtxClient},
    {"NSChatWriteRaw", "void", "int context, string text", ChatWriteRaw, kCtxClient},
    {"NSSendMessage", "void", "string message, bool isIngame, bool isTeam", SendMessage, kCtxClient},
    // Same name as the CLIENT native above, and deliberately a separate
    // entry: PC declares NSSendMessage twice, once per context, with
    // different arguments. The client one sends the local player's chat;
    // the server one relays a named player's message to everyone.
    {"NSSendMessage", "void", "int playerIndex, string text, bool isTeam", ServerSendMessage, kCtxServer},
    {"StringToAsset", "asset", "string assetName", ToAsset, kCtxAll},
    {"NSGetCurrentModName", "string", "", CurrentModName, kCtxAll},
    {"NSGetCallingModName", "string", "int depth = 0", CallingModName, kCtxAll},
    // Mostly SERVER-only. Registered through server.prx's own registrar -
    // see RegisterServerNatives in runtime_server_vm.inl.
    {"NSGetLocalPlayerUID", "string", "", ServerLocalPlayerUid, kCtxAll},
    {"NSIsPlayerLocalPlayer", "bool", "entity player", ServerIsLocalPlayer, kCtxServer},
    {"NSIsDedicated", "bool", "", ServerIsDedicated, kCtxServer},
    {"NSIsWritingPlayerPersistence", "bool", "", ServerWritingPersistence, kCtxServer},
    {"NSEarlyWritePlayerPersistenceForLeave", "void", "entity player", ServerWritePersistenceForLeave, kCtxServer},
    {"NSDisconnectPlayer", "bool", "entity player, string reason", ServerDisconnectPlayer, kCtxServer},
    {"NSSendClientPrint", "void", "entity player, string msg", ServerSendClientPrint, kCtxServer},
    {"NSBroadcastMessage", "void", "int fromPlayerIndex, int toPlayerIndex, string text, bool isTeam, bool isDead, int messageType", ServerBroadcastMessage, kCtxServer},
    {"NSGetLoadedMapNames", "array<string>", "", ServerLoadedMapNames, kCtxAll},
    {"GetUserInfoKVString_Internal", "string", "entity player, string key, string defaultValue = \"\"", UserInfoKvString, kCtxServer},
    {"GetUserInfoKVAsset_Internal", "asset", "entity player, string key, asset defaultValue = $\"\"", UserInfoKvAsset, kCtxServer},
    {"GetUserInfoKVInt_Internal", "int", "entity player, string key, int defaultValue = 0", UserInfoKvPrimitive, kCtxServer},
    {"GetUserInfoKVFloat_Internal", "float", "entity player, string key, float defaultValue = 0", UserInfoKvPrimitive, kCtxServer},
    {"GetUserInfoKVBool_Internal", "bool", "entity player, string key, bool defaultValue = false", UserInfoKvPrimitive, kCtxServer},
};
} // namespace uiapi
bool RegisterRuntimeUiNatives(void* owner, int context, bool deferred = false) noexcept {
    // Validate every native helper before installing any callbacks.
    const struct { std::uintptr_t va; const char* bytes; std::size_t size; } gates[] = {
        {0x67a3c0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x68", 17},
        {0x68214b, "\xc7\x02\x08\x00\x00\x01\xc7\x44\x01\x04\x00\x00\x00\x00\x48\xc7\x44\x01\x08\x00\x00\x00\x00", 23},
        {0x682e00, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
        {0x683330, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x54\x53", 11},
        {0x6835e0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x54\x53\x48\x83\xec\x10", 15},
        {0x684970, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x50", 14},
        {0x682a60, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x48\x89\xfb", 13},
        {0x684b00, "\x55\x48\x89\xe5\x53\x50\x48\x89\xfb\x48\x63\xf6", 12},
        // Safe I/O support: script-function lookup, object push and sq_call,
        // plus the three call-stack reads CallingSource depends on.
        {0x685cf0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x68\x48\x8b\x05\x78\xca\x41\x00", 24},
        {0x6875f0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x41\x89\xf0\x49\x89\xd7\x44\x89\xc3\x81\xe3\x00\x00\x00", 24},
        {0x6876c0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x18\x4c\x8b\x2d\xa8\xb0\x41\x00", 24},
        {0x67f062, "\x45\x8b\x7e\x40\x45\x39\xe7\x0f\x8e\x00\x03\x00\x00", 13},
        {0x67f089, "\x41\x81\x7c\xc0\x20\x00\x20\x00\x08", 9},
        {0x67f0b3, "\x81\x79\x38\x10\x00\x00\x08\x75\x3e\x48\x8b\x51\x40", 13},
        // JSON support: sq_newtable and the shared-state string table the
        // table insert at 0x6ab3e0 already validated for constants.
        {0x683230, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x48\x8b\x1d\x37\xf5\x41", 16},
    };
    for (const auto& gate : gates) if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, gate.va,
            reinterpret_cast<const std::uint8_t*>(gate.bytes), gate.size)) {
        LogFormat("[NorthstarPS4] native API helper mismatch va=%lx\n", gate.va); return false;
    }
    // The engine keeps the record pointer, so every VM context needs its own
    // copy rather than sharing one static block.
    constexpr std::size_t kRegistrationCount = sizeof(uiapi::registrations) / sizeof(uiapi::registrations[0]);
    alignas(16) static std::uint8_t records[2][kRegistrationCount][0x68]{};
    const int slot = context == uiapi::kCtxUi ? 1 : 0;
    const char* label = context == uiapi::kCtxUi ? "UI" : "CLIENT";
    std::size_t i = 0, registered = 0;
    for (const auto& r : uiapi::registrations) {
        auto record = records[slot][i++];
        if (!(r.contexts & context)) continue;
        const bool needsTypes = std::strstr(r.returns, "ModInfo") || std::strstr(r.returns, "ServerInfo") || std::strstr(r.returns, "AuthResult") || std::strstr(r.returns, "ModInstallState");
        if (needsTypes != deferred) continue;
        *reinterpret_cast<const char**>(record) = r.name;
        *reinterpret_cast<const char**>(record + 8) = r.name;
        *reinterpret_cast<const char**>(record + 0x10) = "Northstar PS4 runtime";
        *reinterpret_cast<const char**>(record + 0x18) = r.returns;
        *reinterpret_cast<const char**>(record + 0x20) = r.args;
        *reinterpret_cast<const void**>(record + 0x60) = reinterpret_cast<const void*>(r.function);
        uiapi::At<void (*)(void*, void*, void*, int, int)>(kClientRegisterSquirrelFuncVa)(owner, record, nullptr, 1, 0);
        LogFormat("[NorthstarPS4] runtime %s native registered: %s\n", label, r.name);
        ++registered;
    }
    LogFormat("[NorthstarPS4] %s natives registered deferred=%d count=%zu\n", label, deferred ? 1 : 0, registered);
    return true;
}
