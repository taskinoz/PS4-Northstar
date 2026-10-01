// Client string commands on a PS4 host (PC: shared/exploit_fixes/exploitfixes.cpp
// CGameClient__ExecuteStringCommand, ns_limits.cpp CheckStringCommandLimits).
//
// CGameClient::ExecuteStringCommand is engine+0xd8920 (this, command): the same
// function as PC's engine.dll+0x1022E0, which compares the command with
// "demorestart" first. Its two callers are ProcessStringCmd at engine+0xd8900
// (vtable slot engine+0x3acd08; it passes the message's text at +0x20) and a
// this-8 thunk at engine+0xd8d60. Both call sites are rewritten to
// StringCommandGuard, which then calls the original.
//
// Ported from PC:
//   - sv_quota_stringcmdspersecond (60; -1 off): a client sending more in one
//     second is disconnected with "Sent too many stringcmd commands";
//   - with sv_cheats 0, the abusable legacy commands emit, pre_go_to_hub,
//     pre_go_to_calibration, end_movie and load_recent_checkpoint (the
//     instant-respawn exploit) are dropped;
//   - ns_should_log_all_clientcommands logs every command.
//   - a remote client may only run a ConCommand flagged
//     FCVAR_GAMEDLL_FOR_REMOTE_CLIENTS (1 << 10); the host's own client may run
//     any. Before the first check the flag is added, as PC's misccommands.cpp
//     does, to the engine's client commands - the 17-entry table at
//     engine+0x3ace80 that ExecuteStringCommand walks (status, pause, ping,
//     rpt_*, ...; PC's engine.dll+0x7C5EF0) - and to PC's list of cheat
//     commands, and removed from migrateme, recheck, rpt_client_enable and
//     rpt_password. Script client commands (AddClientCommandCallback) are not
//     ConCommands and are not affected.
//     As on PC, only ConCommands are checked (ICvar::FindCommand): a client's
//     ConVar line such as "save_enable 0" is not.
// ICvar::FindCommandBase is slot 14 and FindCommand slot 18, as on PC (FindVar,
// slot 16, already matches); ConCommandBase keeps its name at +0x18 and flags
// at +0x28. Both slots are checked before the rule is turned on.

namespace stringcommands {

constexpr std::uintptr_t kExecuteStringCommandVa = 0xd8920;
constexpr std::uintptr_t kEngineClientCommandsVa = 0x3ace80;
constexpr int kEngineClientCommandCount = 17;
constexpr int kFcvarGameDllForRemoteClients = 1 << 10;
constexpr int kFcvarServerCanExecute = 1 << 28;
constexpr std::size_t kCommandNameOffset = 0x18;
constexpr std::size_t kCommandFlagsOffset = 0x28;
using FindCommandBaseFn = void* (*)(void* cvar, const char* name);
std::uintptr_t g_engineBase = 0;
bool g_flagsFixed = false;
bool g_remoteRuleOn = false;
constexpr int kMaxTrackedClients = 128;

using ExecuteStringCommandFn = bool (*)(void* client, const char* command);
ExecuteStringCommandFn g_original = nullptr;
void* g_quotaConVar = nullptr;
void* g_logConVar = nullptr;
void* g_cheatsConVar = nullptr;
struct Quota {
    std::uint64_t start;
    int count;
};
Quota g_quotas[kMaxTrackedClients]{};

int ConVarIntValue(void* convar, int fallback) noexcept {
    return convar ? *reinterpret_cast<const std::int32_t*>(static_cast<char*>(convar) + kConVarIntValueOffset) : fallback;
}

// The client slot `client` (a CBaseClient at slot + 0x250) belongs to, or -1.
int SlotOf(const void* client) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine) return -1;
    const auto first = engine + kEngineClientArrayVa + kClientObjectOffset;
    const auto address = reinterpret_cast<std::uintptr_t>(client);
    if (address < first) return -1;
    const std::size_t slot = (address - first) / kEngineClientStride;
    if ((address - first) % kEngineClientStride != 0 || slot >= kMaxTrackedClients) return -1;
    return static_cast<int>(slot);
}

// The first word of the command, lower-cased.
std::string FirstWord(const char* command) noexcept {
    std::string word;
    const char* p = command;
    while (*p == ' ' || *p == '\t') ++p;
    const bool quoted = *p == '"';
    if (quoted) ++p;
    for (; *p && (quoted ? *p != '"' : (*p != ' ' && *p != '\t' && *p != ';')); ++p)
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return word;
}

bool SameName(const char* a, const char* b) noexcept {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

void* Lookup(int vtableSlot, const char* name) noexcept {
    if (!g_modConVarCvar || !name || !*name) return nullptr;
    auto vtable = *reinterpret_cast<void***>(g_modConVarCvar);
    void* found = reinterpret_cast<FindCommandBaseFn>(vtable[vtableSlot])(g_modConVarCvar, name);
    // Only trust a result that names what was asked for (lookups ignore case).
    if (found) {
        const char* foundName = *reinterpret_cast<const char* const*>(static_cast<char*>(found) + kCommandNameOffset);
        if (!foundName || !SameName(foundName, name)) return nullptr;
    }
    return found;
}

// A command or a ConVar.
void* FindCommandBase(const char* name) noexcept { return Lookup(14, name); }
// Commands only, as PC's check uses: a client's ConVar line is not refused.
void* FindCommand(const char* name) noexcept { return Lookup(18, name); }

std::int32_t& Flags(void* command) noexcept {
    return *reinterpret_cast<std::int32_t*>(static_cast<char*>(command) + kCommandFlagsOffset);
}

// PC FixupCvarFlags (the remote-client part), once every module's commands exist.
void FixFlags() noexcept {
    g_flagsFixed = true;
    int marked = 0;
    auto table = reinterpret_cast<const char* const*>(g_engineBase + kEngineClientCommandsVa);
    for (int i = 0; i < kEngineClientCommandCount; ++i)
        if (void* command = FindCommandBase(table[i])) {
            Flags(command) |= kFcvarGameDllForRemoteClients;
            ++marked;
        }
    static const char* const kAdd[] = {"give", "give_server", "givecurrentammo", "takecurrentammo", "switchclass", "set",
        "_setClassVarServer", "ent_create", "ent_throw", "ent_setname", "ent_teleport", "ent_remove", "ent_remove_all",
        "ent_fire", "particle_create", "particle_recreate", "particle_kill", "test_setteam", "melee_lunge_ent"};
    for (const char* name : kAdd)
        if (void* command = FindCommandBase(name)) {
            Flags(command) |= kFcvarGameDllForRemoteClients;
            ++marked;
        }
    const struct { const char* name; int flags; } kRemove[] = {
        {"migrateme", kFcvarServerCanExecute | kFcvarGameDllForRemoteClients}, {"recheck", kFcvarGameDllForRemoteClients},
        {"rpt_client_enable", kFcvarGameDllForRemoteClients}, {"rpt_password", kFcvarGameDllForRemoteClients}};
    int cleared = 0;
    for (const auto& entry : kRemove)
        if (void* command = FindCommandBase(entry.name)) {
            Flags(command) &= ~entry.flags;
            ++cleared;
        }
    LogFormat("[NorthstarPS4] remote client commands: %d marked, %d cleared\n", marked, cleared);
}

bool IsHostClient(const void* client, int slot) noexcept {
    if (slot == 0) return true;
    const std::uint64_t local = serverbans::LocalUid();
    const auto uid = *reinterpret_cast<const std::uint64_t*>(
        static_cast<const char*>(client) + (kClientConnectUidOffset - kClientObjectOffset));
    return local && uid == local;
}

bool Guard(void* client, const char* command) noexcept {
    if (!command) return g_original(client, command);
    const int slot = SlotOf(client);
    if (ConVarIntValue(g_logConVar, 0) != 0) {
        const char* name = slot >= 0 ? static_cast<const char*>(client) + 0x16 : "?";
        LogFormat("[NorthstarPS4] player %s (client #%d) sent command: \"%s\"\n", name, slot, command);
    }
    const int quota = ConVarIntValue(g_quotaConVar, 60);
    if (quota != -1 && slot >= 0) {
        const std::uint64_t now = sceKernelGetProcessTime();
        Quota& state = g_quotas[slot];
        if (now - state.start >= 1000000) {
            state.start = now;
            state.count = 0;
        }
        if (++state.count > quota) {
            LogFormat("[NorthstarPS4] client #%d sent more than %d string commands in a second; disconnecting\n", slot, quota);
            state.count = 0;
            if (g_clientDisconnect) g_clientDisconnect(client, 1, "%s", "Sent too many stringcmd commands");
            return false;
        }
    }
    if (g_remoteRuleOn) {
        if (!g_flagsFixed) FixFlags();
        const std::string word = FirstWord(command);
        void* found = word.empty() ? nullptr : FindCommand(word.c_str());
        if (found && !(Flags(found) & kFcvarGameDllForRemoteClients) && !IsHostClient(client, slot)) {
            LogFormat("[NorthstarPS4] client #%d command \"%s\" refused: not allowed for remote clients\n", slot,
                word.c_str());
            return false;
        }
    }
    if (ConVarIntValue(g_cheatsConVar, 0) == 0) {
        static const char* const kBlocked[] = {"emit", "pre_go_to_hub", "pre_go_to_calibration", "end_movie",
            "load_recent_checkpoint"};
        const std::string word = FirstWord(command);
        for (const char* blocked : kBlocked)
            if (word == blocked) {
                LogFormat("[NorthstarPS4] client #%d command \"%s\" blocked (sv_cheats is 0)\n", slot, blocked);
                return false;
            }
    }
    return g_original(client, command);
}

} // namespace stringcommands

bool StringCommandGuard(void* client, const char* command) noexcept {
    return stringcommands::Guard(client, command);
}

// From InstallAtlasServer, with the other server-side protections.
void InstallStringCommandGuard(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    using namespace stringcommands;
    constexpr std::uint8_t functionBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48,
        0x81, 0xec, 0x20, 0x06, 0x00, 0x00};
    struct Site { std::uintptr_t va; std::uint8_t bytes[5]; };
    constexpr Site sites[] = {{0xd8908, {0xe8, 0x13, 0x00, 0x00, 0x00}}, {0xd8d6c, {0xe8, 0xaf, 0xfb, 0xff, 0xff}}};
    if (!ValidateEnginePreimage(engineBase, engineSize, kExecuteStringCommandVa, functionBytes, sizeof(functionBytes))) {
        LogFormat("[NorthstarPS4] string command guard refused: engine profile mismatch\n");
        return;
    }
    for (const auto& site : sites)
        if (!ValidateEnginePreimage(engineBase, engineSize, site.va, site.bytes, 5)) {
            LogFormat("[NorthstarPS4] string command guard refused: call site %lx differs\n", site.va);
            return;
        }
    if (g_modConVarCvar && g_modConVarFindVar && g_modConVarConstructor) {
        alignas(16) static std::uint8_t quotaStorage[0x90]{};
        alignas(16) static std::uint8_t logStorage[0x90]{};
        g_quotaConVar = g_modConVarFindVar(g_modConVarCvar, "sv_quota_stringcmdspersecond");
        if (!g_quotaConVar) {
            g_modConVarConstructor(quotaStorage, "sv_quota_stringcmdspersecond", "60", 1 << 2,
                "How many string commands per second clients are allowed to submit, 0 to disallow all string commands, "
                "-1 to disable", nullptr);
            g_quotaConVar = g_modConVarFindVar(g_modConVarCvar, "sv_quota_stringcmdspersecond");
        }
        g_logConVar = g_modConVarFindVar(g_modConVarCvar, "ns_should_log_all_clientcommands");
        if (!g_logConVar) {
            g_modConVarConstructor(logStorage, "ns_should_log_all_clientcommands", "0", 0,
                "Whether to log all clientcommands", nullptr);
            g_logConVar = g_modConVarFindVar(g_modConVarCvar, "ns_should_log_all_clientcommands");
        }
        g_cheatsConVar = g_modConVarFindVar(g_modConVarCvar, "sv_cheats");
    }
    g_original = reinterpret_cast<ExecuteStringCommandFn>(engineBase + kExecuteStringCommandVa);
    g_engineBase = engineBase;
    // The lookups the remote-client rule depends on: a command registered here
    // with FCVAR_GAMEDLL (bans) must come back from both with its name and
    // flags, and a ConVar (hostport) from FindCommandBase but not FindCommand.
    void* ban = FindCommandBase("ban");
    if (ban && FindCommand("ban") == ban && FindCommandBase("hostport") && !FindCommand("hostport")) {
        g_remoteRuleOn = true;
        LogFormat("[NorthstarPS4] command lookup check: ban flags=0x%x\n", Flags(ban));
    } else
        LogFormat("[NorthstarPS4] command lookup check failed; remote-client rule off\n");
    int patched = 0;
    for (const auto& site : sites) {
        const auto call = engineBase + site.va;
        const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&StringCommandGuard)) -
            static_cast<std::int64_t>(call + 5);
        if (relative < -2147483648LL || relative > 2147483647LL) continue;
        std::uint8_t bytes[5] = {0xe8};
        const auto displacement = static_cast<std::int32_t>(relative);
        std::memcpy(bytes + 1, &displacement, 4);
        if (WriteEngineCode(call, bytes, 5)) ++patched;
    }
    LogFormat("[NorthstarPS4] string command guard installed (%d/2 call sites)\n", patched);
}
