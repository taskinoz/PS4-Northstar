// Included inside the runtime's anonymous namespace, experimental build only.
// Addresses and preimages belong exclusively to the supported PS4 client.prx.
namespace uiapi {
struct Object { std::uint64_t tag, value; };
struct SquirrelHelperBinding { void* vm; std::uintptr_t base; };
SquirrelHelperBinding g_squirrelHelperBindings[3]{};

void BindSquirrelHelpers(void* owner, int context, std::uintptr_t base) noexcept {
    if (!owner || context < 0 || context >= 3) return;
    g_squirrelHelperBindings[context] = {
        *reinterpret_cast<void**>(static_cast<char*>(owner) + 8), base};
}
void UnbindSquirrelHelpers(void* owner) noexcept {
    if (!owner) return;
    void* vm = *reinterpret_cast<void**>(static_cast<char*>(owner) + 8);
    for (auto& binding : g_squirrelHelperBindings)
        if (binding.vm == vm) binding = {};
}
std::uintptr_t SquirrelHelperBase(void* vm) noexcept {
    for (const auto& binding : g_squirrelHelperBindings)
        if (binding.vm == vm && binding.base) return binding.base;
    return g_runtimeClientBase;
}
std::uintptr_t SquirrelHelperVa(void* vm, std::uintptr_t clientVa) noexcept {
    if (SquirrelHelperBase(vm) == g_runtimeClientBase) return clientVa;
    switch (clientVa) {
        case 0x682a60: return 0x634320; // sq_throwerror
        case 0x682e00: return 0x6346c0; // sq_pushstring
        case 0x682f40: return 0x634800; // sq_pushasset
        case 0x683230: return 0x634af0; // sq_newtable
        case 0x683330: return 0x634bf0; // sq_newarray
        case 0x6835e0: return 0x634ea0; // sq_arrayappend
        case 0x684970: return 0x636230; // sq_newstruct
        // server.prx has no callable sq_sealstructslot counterpart. Its retail
        // natives inline the operation; Seal reproduces that path below.
        case 0x684b00: return 0;
        case 0x685cf0: return 0x6374d0; // FindFunction
        case 0x6875f0: return 0x638ce0; // PushObject
        case 0x6876c0: return 0x638db0; // sq_call
        case 0x6a96a0: return 0x666600; // SQString::Create
        case 0x6ab3e0: return 0x668470; // SQTable::NewSlot
        default: return 0;
    }
}
template<typename Fn> Fn At(void* vm, std::uintptr_t clientVa) {
    const std::uintptr_t va = SquirrelHelperVa(vm, clientVa);
    return va ? reinterpret_cast<Fn>(SquirrelHelperBase(vm) + va) : nullptr;
}
template<typename Fn> Fn AtClient(std::uintptr_t va) {
    return reinterpret_cast<Fn>(g_runtimeClientBase + va);
}
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
void Vector(void* vm, float x, float y, float z) {
    std::uint32_t xb, yb, zb;
    std::memcpy(&xb, &x, sizeof(x));
    std::memcpy(&yb, &y, sizeof(y));
    std::memcpy(&zb, &z, sizeof(z));
    PushPrimitive(vm, 0x40000ull | (static_cast<std::uint64_t>(xb) << 32),
        static_cast<std::uint64_t>(yb) | (static_cast<std::uint64_t>(zb) << 32));
}
void String(void* vm, const char* value) { At<void (*)(void*, const char*, int)>(vm, 0x682e00)(vm, value, -1); }
void Asset(void* vm, const char* value) { At<void (*)(void*, const char*, int)>(vm, 0x682f40)(vm, value, -1); }
void Array(void* vm) { At<void (*)(void*, int)>(vm, 0x683330)(vm, 0); }
void Append(void* vm) { At<int (*)(void*, int)>(vm, 0x6835e0)(vm, -2); }
void Struct(void* vm, int fields) { At<void (*)(void*, int)>(vm, 0x684970)(vm, fields); }
void ReleaseObject(Object& object) {
    if (!(object.tag & 0x08000000) || !object.value) return;
    auto pointer = reinterpret_cast<void*>(object.value);
    auto refs = reinterpret_cast<std::uint32_t*>(static_cast<char*>(pointer) + 8);
    if (--*refs == 0) {
        auto table = *reinterpret_cast<std::uintptr_t**>(pointer);
        reinterpret_cast<void (*)(void*)>(table[2])(pointer);
    }
}
void Seal(void* vm, int slot) {
    if (SquirrelHelperBase(vm) == g_runtimeClientBase) {
        At<void (*)(void*, int)>(vm, 0x684b00)(vm, slot);
        return;
    }
    // server.prx inlines this sequence after sq_newstruct rather than emitting
    // the client module's helper. The stack holds [struct, value]. The upper
    // tag word is the struct's base-slot offset; each field is a 16-byte
    // SQObject beginning at +0x38. Retain before replacing and release the
    // popped stack copy, leaving one reference owned by the struct field.
    auto bytes = static_cast<char*>(vm);
    auto& top = *reinterpret_cast<std::uint32_t*>(bytes + 0x68);
    auto stack = *reinterpret_cast<Object**>(bytes + 0x70);
    if (top < 2) return;
    Object& structure = stack[top - 2];
    Object& value = stack[top - 1];
    const auto baseSlot = static_cast<std::int32_t>(structure.tag >> 32);
    auto field = reinterpret_cast<Object*>(structure.value + 0x38 +
        static_cast<std::int64_t>(baseSlot + slot) * sizeof(Object));
    if ((value.tag & 0x08000000) && value.value)
        ++*reinterpret_cast<std::uint32_t*>(value.value + 8);
    ReleaseObject(*field);
    *field = value;
    --top;
    ReleaseObject(stack[top]);
    stack[top] = {0x1000001, 0};
}
int Error(void* vm, const char* message) { At<int (*)(void*, const char*)>(vm, 0x682a60)(vm, message); return -1; }
const Object& Arg(void* vm, int index) { return (*reinterpret_cast<Object**>(static_cast<char*>(vm) + 0x48))[index]; }
const char* TextArg(void* vm, int index) {
    const auto& arg = Arg(vm, index);
    return arg.tag == 0x8000010 ? reinterpret_cast<const char*>(arg.value + 0x30) : nullptr;
}
#include "runtime_save_files.inl"
#include "runtime_json.inl"
struct CatalogEntry { ModInfo info; std::string download; bool required, enabled, remote; };
std::vector<CatalogEntry> catalog;
bool catalogReady = false;
void LoadCatalog() {
    if (catalogReady) return;
    static ModDiscovery all;
    CollectModNames(all, true);
    char settings[kModJsonBufferSize] = "{}";
    std::size_t size = 0;
    if (!ReadEnabledSettings(settings, sizeof(settings))) return;
    for (int i = 0; i < all.count; ++i) {
        char path[256], json[kModJsonBufferSize];
        std::snprintf(path, sizeof(path), "%s/mod.json", all.dirs[i]);
        CatalogEntry entry{};
        if (!ReadFileIntoBuffer(path, json, sizeof(json), size) || !ParseModMetadata(json, entry.info)) continue;
        char download[512]{};
        const char* link = JsonFindMember(json, "DownloadLink");
        if (link) JsonExtractString(link, download, sizeof(download));
        entry.download = download;
        const char* required = JsonFindMember(json, "RequiredOnClient");
        entry.required = required && std::strncmp(JsonSkipWs(required), "true", 4) == 0;
        entry.remote = all.remote[i];
        entry.enabled = ModEnabledNow(settings, entry.info, entry.remote);
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
    Boolean(vm, entry.remote); Seal(vm, 7);
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
    // Rebuilt for the next VM generation: the script's `uiscript_reset` and the
    // connect's map load that follow compile against the new set, as on PC.
    ReloadModState();
    return 0;
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

// NSTryAuthWithLocalServer and NSCompleteAuthWithLocalServer are in
// runtime_server_join.inl, with the other Atlas session requests.
// Falls back to the identity message: nothing may have set a reason yet, and an
// empty string in the menu tells the player nothing.
int AuthFailReason(void* vm) {
    String(vm, authFailure.empty() ? AtlasIdentityMessage() : authFailure.c_str());
    return 1;
}
#include "runtime_server_list.inl"
#include "runtime_main_menu_promos.inl"
#include "runtime_server_join.inl"
#include "runtime_mod_download.inl"
// Defined in runtime_http_script.inl, after the HTTP transport: script HTTP
// requests and the per-context queue of calls into script.
int MakeScriptHttpRequestUi(void* vm);
int MakeScriptHttpRequestClient(void* vm);
int MakeScriptHttpRequestServer(void* vm);
int ScriptHttpEnabled(void* vm);
int ScriptLocalHttpAllowed(void* vm);
int RunAsyncCallsUi(void* vm);
int RunAsyncCallsClient(void* vm);
int RunAsyncCallsServer(void* vm);
void ResetAsyncCalls(int context, const char* reason);
// Defined in runtime_atlas_server.inl: the hosted server's Atlas presence.
int UpdateServerPresence(void* vm);
int IsClientAtlasAuthenticated(void* vm);
// Defined in runtime_chat_ui.inl, which needs the CLIENT VM's lifecycle state.
int OpenChatKeyboard(void* vm);
int UpdateChatKeyboard(void* vm);
int OpenTextInput(void* vm);
int UpdateTextInput(void* vm);
int GetTextInput(void* vm);
int SetHostOption(void* vm);
int IsServerListed(void* vm);
int AuthResult(void* vm) {
    Struct(vm, 3);
    Boolean(vm, AtlasIdentityReady()); Seal(vm, 0);
    String(vm, AtlasIdentityCode()); Seal(vm, 1);
    String(vm, AtlasIdentityMessage()); Seal(vm, 2);
    return 1;
}
// PC reports the mouse cursor. This platform is driven by a gamepad and has
// no cursor to report, and the declared return type is "vector ornull", so
// null is the honest answer rather than an invented coordinate.
int CursorPosition(void*) { return 0; }
// CLIENT-context natives.
//
// NSChatWrite, NSChatWriteLine and NSChatWriteRaw: PC's LocalChatWriter,
// defined in runtime_chat_client.inl with the chat HUD layout.
int ChatWrite(void* vm);
int ChatWriteLine(void* vm);
int ChatWriteRaw(void* vm);
// PC: chatcommand.cpp's NSSendMessage calls the engine's ClientSayText
// (engine.dll 0x54780) past its own hook. The PS4 function (engine+0x473e0)
// has the same arguments: in-game chat goes to the server as
// clc_ClientSayText, anything else to the party-chat path.
int SendMessage(void* vm) {
    const char* text = TextArg(vm, 1);
    if (!text || Arg(vm, 2).tag != 0x1000008 || Arg(vm, 3).tag != 0x1000008)
        return Error(vm, "NSSendMessage expects string message, bool isIngame, bool isTeam");
    if (!g_clientSayText) return Error(vm, "NSSendMessage: chat sending is unavailable on this build");
    // Party chat (isIngame false) has no PS4 path worth taking; in-game chat
    // is sent only while a local client exists.
    if (Arg(vm, 2).value == 0 || !SendChat(text, Arg(vm, 3).value != 0))
        LogFormat("[NorthstarPS4] NSSendMessage not sent: %s\n", Arg(vm, 2).value == 0 ? "party chat" : "not in a match");
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

// A player entity argument, as server.prx's own methods read `this`
// (GetUID, server+0x7a66b0, through 0x62e9a0): the object is a class instance
// (tag bit 0x408000) whose entity is at instance+0x40. Its word at +0x68 is the
// entity index, and client = index - 1. The entity must be the one the player
// table holds for that index, so anything that is not a player is refused.
int PlayerClientArg(void* vm, int index) {
    const auto& arg = Arg(vm, index);
    if (!(arg.tag & 0x408000) || !arg.value) return -1;
    void* entity = *reinterpret_cast<void**>(arg.value + 0x40);
    if (!entity) return -1;
    const int entityIndex = *reinterpret_cast<const std::int16_t*>(static_cast<char*>(entity) + 0x68);
    if (entityIndex < 1 || ServerPlayerByIndex(entityIndex) != entity) return -1;
    return entityIndex - 1;
}

// PC compares the platform UID, but PS4 deliberately supports a PC and the
// console using the same imported Atlas identity. A UID comparison therefore
// misclassifies that remote PC as the listen host: its LeaveMatch ends the
// match for everyone instead of disconnecting only that client. PS4 has no
// dedicated-server mode, and the listen host owns client slot zero, so use
// actual connection ownership rather than account identity.
int ServerIsLocalPlayer(void* vm) {
    const int client = PlayerClientArg(vm, 1);
    if (client < 0) {
        LogFormat("[NorthstarPS4] NSIsPlayerLocalPlayer got null player\n");
        Boolean(vm, false);
        return 1;
    }
    Boolean(vm, client == 0);
    return 1;
}

// There is no dedicated-server build for this platform.
int ServerIsDedicated(void* vm) { Boolean(vm, false); return 1; }

// PC: MasterServerManager::m_bSavingPersistentData, true while a pdata write
// to Atlas is in flight (runtime_atlas_server.inl).
int ServerWritingPersistence(void* vm) { Boolean(vm, g_remotePdataWriting && g_remotePdataWriting()); return 1; }
// PC writes a leaving player's persistence to Atlas early, and only for a
// player whose data Atlas supplied (READY_REMOTE, runtime_persistence.inl);
// anyone else has nothing to write.
int ServerWritePersistenceForLeave(void* vm) {
    const int client = PlayerClientArg(vm, 1);
    if (client < 0) {
        LogFormat("[NorthstarPS4] NSEarlyWritePlayerPersistenceForLeave got null player\n");
        return 0;
    }
    WriteRemoteSave(client, "early write for leave", true);
    return 0;
}

// PC: CBaseClient::Disconnect on the player's client, with a default reason.
int ServerDisconnectPlayer(void* vm) {
    const int client = PlayerClientArg(vm, 1);
    const char* reason = TextArg(vm, 2);
    if (client < 0) {
        LogFormat("[NorthstarPS4] Attempted to call NSDisconnectPlayer() with null player.\n");
        Boolean(vm, false);
        return 1;
    }
    if (!g_disconnectClient) {
        WarnServerStub(kStubDisconnect, "NSDisconnectPlayer");
        Boolean(vm, false);
        return 1;
    }
    Boolean(vm, g_disconnectClient(client, reason ? reason : "Disconnected by the server."));
    return 1;
}
// PS4-only, for Northstar.PS4's host options, which pass a client index
// (`player.GetEntIndex() - 1`). Like NSDisconnectPlayer above, it leaves bots
// and unconnected slots alone (DisconnectClient, as kickid does); PC's
// NSDisconnectPlayer would disconnect a bot too.
int ServerDisconnectClient(void* vm) {
    const auto& index = Arg(vm, 1);
    const char* reason = TextArg(vm, 2);
    if (index.tag != 0x5000002 || !reason) return Error(vm, "NSPS4_DisconnectClient expects int client, string reason");
    Boolean(vm, g_disconnectClient && g_disconnectClient(static_cast<int>(static_cast<std::int32_t>(index.value)), reason));
    return 1;
}
// PC: CGameClient::ClientPrintf(client, "%s", msg), to the player's console.
int ServerSendClientPrint(void* vm) {
    const int client = PlayerClientArg(vm, 1);
    const char* message = TextArg(vm, 2);
    if (client < 0) {
        LogFormat("[NorthstarPS4] NSSendClientPrint(): got null player\n");
        return 0;
    }
    if (!g_clientPrint || !g_clientPrint(client, message ? message : ""))
        WarnServerStub(kStubClientPrint, "NSSendClientPrint");
    return 0;
}
// NSBroadcastMessage( int fromPlayerIndex, int toPlayerIndex, string text,
// bool isTeam, bool isDead, int messageType ): PC's ChatBroadcastMessage, a
// SayText the vanilla code cannot send (runtime_chat.inl).
int ServerBroadcastMessage(void* vm) {
    const char* text = TextArg(vm, 3);
    if (Arg(vm, 1).tag != 0x5000002 || Arg(vm, 2).tag != 0x5000002 || !text || Arg(vm, 4).tag != 0x1000008 ||
        Arg(vm, 5).tag != 0x1000008 || Arg(vm, 6).tag != 0x5000002)
        return Error(vm, "NSBroadcastMessage expects int fromPlayerIndex, int toPlayerIndex, string text, bool isTeam, "
                         "bool isDead, int messageType");
    const int type = static_cast<std::int32_t>(Arg(vm, 6).value);
    if (type < 1) {
        char message[64];
        std::snprintf(message, sizeof(message), "Invalid message type %d", type);
        return Error(vm, message);
    }
    if (!ServerChatBroadcast(static_cast<std::int32_t>(Arg(vm, 1).value), static_cast<std::int32_t>(Arg(vm, 2).value),
            text, Arg(vm, 4).value != 0, Arg(vm, 5).value != 0, type))
        WarnServerStub(kStubBroadcast, "NSBroadcastMessage");
    return 0;
}
// PC's ChatSendMessage: the vanilla SayText send past the chat hook
// (runtime_chat.inl).
int ServerSendMessage(void* vm) {
    const char* text = TextArg(vm, 2);
    if (Arg(vm, 1).tag != 0x5000002 || !text || Arg(vm, 3).tag != 0x1000008)
        return Error(vm, "NSSendMessage expects int playerIndex, string text, bool isTeam");
    ServerChatSend(static_cast<int>(static_cast<std::int32_t>(Arg(vm, 1).value)), text, Arg(vm, 3).value != 0);
    return 0;
}

// NSGetLoadedMapNames(): PC's RefreshMapList (util/printmaps.cpp), in its
// order: enabled mods' loose maps/<map>.bsp, then the retail map archives
// (englishclient_<map>.bsp.pak000_dir.vpk, except frontend; mp_common holds
// mp_lobby), then loose maps in the game's r2/maps. The PS4 archives live in
// vpk_ps4 rather than vpk.
void AppendMapName(void* vm, const std::string& name) {
    String(vm, name.c_str());
    Append(vm);
}
int ServerLoadedMapNames(void* vm) {
    Array(vm);
    if (const ModOverlay* overlay = CurrentModOverlay()) {
        for (const auto& entry : overlay->index) {
            const std::string& key = entry.key;
            if (key.size() > 9 && key.compare(0, 5, "maps/") == 0 && key.find('/', 5) == std::string::npos &&
                key.compare(key.size() - 4, 4, ".bsp") == 0)
                AppendMapName(vm, key.substr(5, key.size() - 9));
        }
    }
    constexpr char kPrefix[] = "englishclient_";
    constexpr char kSuffix[] = ".bsp.pak000_dir.vpk";
    if (DIR* dir = opendir("/app0/vpk_ps4")) {
        while (dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            const std::size_t prefix = sizeof(kPrefix) - 1, suffix = sizeof(kSuffix) - 1;
            if (file.size() <= prefix + suffix || file.compare(0, prefix, kPrefix) != 0 ||
                file.compare(file.size() - suffix, suffix, kSuffix) != 0)
                continue;
            std::string map = file.substr(prefix, file.size() - prefix - suffix);
            if (map == "frontend") continue;
            if (map == "mp_common") map = "mp_lobby";
            AppendMapName(vm, map);
        }
        closedir(dir);
    }
    if (DIR* dir = opendir("/app0/r2/maps")) {
        while (dirent* entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() > 4 && file.compare(file.size() - 4, 4, ".bsp") == 0)
                AppendMapName(vm, file.substr(0, file.size() - 4));
        }
        closedir(dir);
    }
    return 1;
}

// GetUserInfoKV*_Internal( entity player, string key, <type> defaultValue ):
// PC's scriptuserinfo.cpp, a player's userinfo convar through
// m_ConVars->Get*(key, default). KeyValues parses a string value with atoi
// for GetInt and atof for GetFloat, as here; the bool variant is GetInt != 0.
// A null player is a script error, as on PC.
const char* UserInfoArgs(void* vm, const char* fallback, bool* found, const char* name) {
    const int client = PlayerClientArg(vm, 1);
    const char* key = TextArg(vm, 2);
    *found = false;
    if (client < 0) {
        Error(vm, "player is null");
        return nullptr;
    }
    if (!key) {
        Error(vm, name);
        return nullptr;
    }
    return g_clientUserInfo ? g_clientUserInfo(client, key, fallback, found) : fallback;
}
constexpr std::uint64_t kSqFloatTag = 0x5000004;
float NumberArg(void* vm, int index) {
    const auto& arg = Arg(vm, index);
    if (arg.tag == kSqFloatTag) {
        float value;
        const auto bits = static_cast<std::uint32_t>(arg.value);
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    return static_cast<float>(static_cast<std::int32_t>(arg.value));
}
int UserInfoKvString(void* vm) {
    const char* fallback = TextArg(vm, 3);
    bool found = false;
    const char* value = UserInfoArgs(vm, fallback ? fallback : "", &found, "GetUserInfoKVString expects entity player, string key");
    if (!value) return -1;
    String(vm, value);
    return 1;
}
int UserInfoKvAsset(void* vm) {
    const char* fallback = TextArg(vm, 3);
    bool found = false;
    const char* value = UserInfoArgs(vm, fallback ? fallback : "", &found, "GetUserInfoKVAsset expects entity player, string key");
    if (!value) return -1;
    Asset(vm, value);
    return 1;
}
int UserInfoKvInt(void* vm) {
    bool found = false;
    const char* value = UserInfoArgs(vm, "", &found, "GetUserInfoKVInt expects entity player, string key");
    if (!value) return -1;
    Integer(vm, found ? std::atoi(value) : static_cast<int>(NumberArg(vm, 3)));
    return 1;
}
int UserInfoKvFloat(void* vm) {
    bool found = false;
    const char* value = UserInfoArgs(vm, "", &found, "GetUserInfoKVFloat expects entity player, string key");
    if (!value) return -1;
    const float result = found ? static_cast<float>(std::atof(value)) : NumberArg(vm, 3);
    std::uint32_t bits;
    std::memcpy(&bits, &result, sizeof(bits));
    PushPrimitive(vm, kSqFloatTag, bits);
    return 1;
}
int UserInfoKvBool(void* vm) {
    bool found = false;
    const char* value = UserInfoArgs(vm, "", &found, "GetUserInfoKVBool expects entity player, string key");
    if (!value) return -1;
    Boolean(vm, found ? std::atoi(value) != 0 : Arg(vm, 3).value != 0);
    return 1;
}

#include "runtime_datatables.inl"

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
    // One entry per context, so each request's result is queued for the VM
    // that made it (runtime_http_script.inl).
    {"NS_InternalMakeHttpRequest", "int", "int method, string baseUrl, table<string, array<string> > headers, table<string, array<string> > queryParams, string contentType, string body, int timeout, string userAgent", MakeScriptHttpRequestUi, kCtxUi},
    {"NS_InternalMakeHttpRequest", "int", "int method, string baseUrl, table<string, array<string> > headers, table<string, array<string> > queryParams, string contentType, string body, int timeout, string userAgent", MakeScriptHttpRequestClient, kCtxClient},
    {"NS_InternalMakeHttpRequest", "int", "int method, string baseUrl, table<string, array<string> > headers, table<string, array<string> > queryParams, string contentType, string body, int timeout, string userAgent", MakeScriptHttpRequestServer, kCtxServer},
    {"NSIsHttpEnabled", "bool", "", ScriptHttpEnabled, kCtxAll},
    {"NSIsLocalHttpAllowed", "bool", "", ScriptLocalHttpAllowed, kCtxAll},
    // PS4-only diagnostic/compatibility entry point. Normal delivery now runs
    // natively from the verified host-state frame call.
    {"NSPS4_RunAsyncCalls", "void", "", RunAsyncCallsUi, kCtxUi},
    {"NSPS4_RunAsyncCalls", "void", "", RunAsyncCallsClient, kCtxClient},
    {"NSPS4_RunAsyncCalls", "void", "", RunAsyncCallsServer, kCtxServer},
    // PS4-only, for Northstar.PS4's ps4_server_presence.nut and host options
    // (runtime_atlas_server.inl).
    {"NSPS4_UpdateServerPresence", "void", "string map, string playlist, int maxPlayers, int playerCount", UpdateServerPresence, kCtxServer},
    {"NSPS4_IsClientAtlasAuthenticated", "bool", "int client", IsClientAtlasAuthenticated, kCtxServer},
    {"NSFetchVerifiedModsManifesto", "void", "", FetchVerifiedMods, kCtxAll},
    {"NSIsModDownloadable", "bool", "string name, string version", IsModDownloadable, kCtxAll},
    {"NSDownloadMod", "void", "string name, string version", DownloadMod, kCtxAll},
    {"NSGetModInstallState", "ModInstallState", "", GetModInstallState, kCtxAll},
    {"NSCancelModDownload", "void", "", CancelModDownload, kCtxAll},
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
    {"NSHasCustomMainMenuPromoData", "bool", "", HasPromos, kCtxUi},
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
    {"NSPS4_DisconnectClient", "bool", "int client, string reason", ServerDisconnectClient, kCtxServer},
    {"NSPS4_OpenChatKeyboard", "bool", "bool isTeam", OpenChatKeyboard, kCtxUi},
    {"NSPS4_UpdateChatKeyboard", "int", "", UpdateChatKeyboard, kCtxUi},
    {"NSPS4_OpenTextInput", "bool", "string title, string text, int maxLength, bool secret", OpenTextInput, kCtxUi},
    {"NSPS4_UpdateTextInput", "int", "", UpdateTextInput, kCtxUi},
    {"NSPS4_GetTextInput", "string", "", GetTextInput, kCtxUi},
    {"NSPS4_SetHostOption", "bool", "string name, string value", SetHostOption, kCtxUi},
    {"NSPS4_IsServerListed", "bool", "", IsServerListed, kCtxUi},
    {"NSIsDedicated", "bool", "", ServerIsDedicated, kCtxServer},
    {"NSIsWritingPlayerPersistence", "bool", "", ServerWritingPersistence, kCtxServer},
    {"NSEarlyWritePlayerPersistenceForLeave", "void", "entity player", ServerWritePersistenceForLeave, kCtxServer},
    {"NSDisconnectPlayer", "bool", "entity player, string reason", ServerDisconnectPlayer, kCtxServer},
    {"NSSendClientPrint", "void", "entity player, string msg", ServerSendClientPrint, kCtxServer},
    {"NSBroadcastMessage", "void", "int fromPlayerIndex, int toPlayerIndex, string text, bool isTeam, bool isDead, int messageType", ServerBroadcastMessage, kCtxServer},
    {"NSGetLoadedMapNames", "array<string>", "", ServerLoadedMapNames, kCtxAll},
    {"GetUserInfoKVString_Internal", "string", "entity player, string key, string defaultValue = \"\"", UserInfoKvString, kCtxServer},
    {"GetUserInfoKVAsset_Internal", "asset", "entity player, string key, asset defaultValue = $\"\"", UserInfoKvAsset, kCtxServer},
    {"GetUserInfoKVInt_Internal", "int", "entity player, string key, int defaultValue = 0", UserInfoKvInt, kCtxServer},
    {"GetUserInfoKVFloat_Internal", "float", "entity player, string key, float defaultValue = 0", UserInfoKvFloat, kCtxServer},
    {"GetUserInfoKVBool_Internal", "bool", "entity player, string key, bool defaultValue = false", UserInfoKvBool, kCtxServer},
};
} // namespace uiapi
bool RegisterRuntimeUiNatives(void* owner, int context, bool deferred = false) noexcept {
    // Validate every native helper before installing any callbacks.
    const struct { std::uintptr_t va; const char* bytes; std::size_t size; } gates[] = {
        {0x67a3c0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x68", 17},
        {0x68214b, "\xc7\x02\x08\x00\x00\x01\xc7\x44\x01\x04\x00\x00\x00\x00\x48\xc7\x44\x01\x08\x00\x00\x00\x00", 23},
        {0x682e00, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
        {0x682f40, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
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
        uiapi::AtClient<void (*)(void*, void*, void*, int, int)>(kClientRegisterSquirrelFuncVa)(owner, record, nullptr, 1, 0);
        LogFormat("[NorthstarPS4] runtime %s native registered: %s\n", label, r.name);
        ++registered;
    }
    LogFormat("[NorthstarPS4] %s natives registered deferred=%d count=%zu\n", label, deferred ? 1 : 0, registered);
    return true;
}
