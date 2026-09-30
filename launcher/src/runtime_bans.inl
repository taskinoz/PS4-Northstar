// Server bans (PC: server/auth/bansystem.cpp; the file rules are in banlist.h).
//
// - `ban <name or uid>` bans a connected player and disconnects them with
//   "Banned from server"; `unban <uid>` and `clearbanlist` edit the list.
// - The list is read again for every check, as PC does, so editing the file
//   by hand takes effect for the next connection.
// - The host's own uid (platform_user_id, what the host connects with) is
//   always allowed, as PC allows its local player.
// - Checked where PC checks: a connect packet from a banned uid is refused
//   with the engine's own reject message "Banned From Server." (PC:
//   CBaseClient::Connect), and an Atlas connect request for a banned uid is
//   answered with reject "Banned from this server." (PC: masterserver.cpp).
//
// PC keeps banlist.txt in the profile folder. /app0 is read-only on a PS4, so
// the list lives in the writable data folder, like the port's enabledmods.json.
//
// Client names: the PS4 client slot holds PC's CBaseClient at +0x250 with PC's
// field offsets (UID 0xf500 -> slot +0xf750, signon 0x2a0 -> +0x4f0), so
// m_Name (0x16) is at slot +0x266.

namespace serverbans {

constexpr const char* kBanlistPath = "/data/northstar_ps4/banlist.txt";
constexpr std::size_t kClientNameOffset = 0x266;
constexpr std::size_t kClientUidStringOffset = 0xf750;
constexpr std::uintptr_t kRejectConnectionVa = 0xe4850;
constexpr int kFcvarGameDll = 1 << 2;

// RejectConnection(unused, socket, from, format, ...): engine+0xe4850. Every
// caller in ProcessConnectionlessPacket passes the server's socket (this+0xc)
// and the packet, whose first field is the sender's address.
using RejectConnectionFn = void (*)(void*, int, const void*, const char*, ...);
RejectConnectionFn g_rejectConnection = nullptr;

std::string ReadList() noexcept {
    static char text[256 * 1024];
    std::size_t size = 0;
    if (!ReadFileIntoBuffer(kBanlistPath, text, sizeof(text), size)) return std::string();
    return std::string(text, size);
}

bool WriteList(const std::string& text) noexcept {
    mkdir("/data/northstar_ps4", 0777);
    const int fd = open(kBanlistPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LogFormat("[NorthstarPS4] banlist not written: can't open %s\n", kBanlistPath);
        return false;
    }
    const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    close(fd);
    if (!written) LogFormat("[NorthstarPS4] banlist not written: write failed\n");
    return written;
}

std::uint64_t LocalUid() noexcept {
    const char* value = g_platformUserIdConVar ? ReadConVarValue(g_platformUserIdConVar) : nullptr;
    return value ? std::strtoull(value, nullptr, 10) : 0;
}

// PC ServerBanSystem::IsUIDAllowed.
bool IsUidAllowed(std::uint64_t uid) noexcept {
    const std::uint64_t local = LocalUid();
    if (local && uid == local) return true;
    return !bans::IsBanned(ReadList(), uid);
}

char* ClientSlot(int client) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine || client < 0 || client >= *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa))
        return nullptr;
    return reinterpret_cast<char*>(engine + kEngineClientArrayVa) + static_cast<std::size_t>(client) * kEngineClientStride;
}

void ConCommandBan(const CCommandView* command) noexcept {
    if (!command || command->argc < 2 || !command->argv[1]) return;
    const char* target = command->argv[1];
    const auto engine = g_engineBaseForPersistence;
    const int count = engine ? *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa) : 0;
    for (int client = 0; client < count; ++client) {
        char* slot = ClientSlot(client);
        if (!slot || *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset) < 2) continue;
        char name[65]{};
        char uidText[33]{};
        std::memcpy(name, slot + kClientNameOffset, 64);
        std::memcpy(uidText, slot + kClientUidStringOffset, 32);
        // m_UID is only filled once scripts have asked for the player's UID;
        // the uid the client connected with is always there.
        const std::uint64_t connectUid = *reinterpret_cast<const std::uint64_t*>(slot + kClientConnectUidOffset);
        if (!uidText[0] && connectUid) std::snprintf(uidText, sizeof(uidText), "%llu", static_cast<unsigned long long>(connectUid));
        if (std::strcmp(name, target) != 0 && std::strcmp(uidText, target) != 0) continue;
        const std::uint64_t uid = std::strtoull(uidText, nullptr, 10);
        if (!uid) {
            LogFormat("[NorthstarPS4] ban: client #%d (%s) has no uid\n", client, name);
            return;
        }
        if (uid == LocalUid()) {
            LogFormat("[NorthstarPS4] ban: %s is this machine's own player; not banned\n", name);
            return;
        }
        if (WriteList(bans::Ban(ReadList(), uid))) LogFormat("[NorthstarPS4] %llu was banned (%s)\n",
            static_cast<unsigned long long>(uid), name);
        DisconnectClient(client, "Banned from server");
        return;
    }
    LogFormat("[NorthstarPS4] ban: no connected player named or with uid %s\n", target);
}

void ConCommandUnban(const CCommandView* command) noexcept {
    if (!command || command->argc < 2 || !command->argv[1]) return;
    const std::uint64_t uid = std::strtoull(command->argv[1], nullptr, 10);
    const std::string before = ReadList();
    if (!bans::IsBanned(before, uid)) return;
    char date[32] = "unknown";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* local = std::localtime(&now))
        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", local);
    if (WriteList(bans::Unban(before, uid, date)))
        LogFormat("[NorthstarPS4] %llu was unbanned\n", static_cast<unsigned long long>(uid));
}

void ConCommandClearBanlist(const CCommandView*) noexcept {
    if (WriteList(std::string())) LogFormat("[NorthstarPS4] banlist cleared\n");
}

} // namespace serverbans

// From InstallAtlasServer, which runs inside RegisterNativeConCommands.
void InstallBans(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    constexpr std::uint8_t rejectBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec,
        0xd8, 0x04, 0x00, 0x00, 0x49, 0x89, 0xd6, 0x89, 0xf3};
    if (ValidateEnginePreimage(engineBase, engineSize, serverbans::kRejectConnectionVa, rejectBytes, sizeof(rejectBytes)))
        serverbans::g_rejectConnection = reinterpret_cast<serverbans::RejectConnectionFn>(engineBase + serverbans::kRejectConnectionVa);
    else
        LogFormat("[NorthstarPS4] ban check refused: RejectConnection profile mismatch\n");
    if (!g_conCommandConstruct) return;
    struct Definition { const char* name; ConCommandCallbackFn callback; const char* help; };
    static const Definition definitions[] = {
        {"ban", &serverbans::ConCommandBan, "bans a given player by uid or name"},
        {"unban", &serverbans::ConCommandUnban, "unbans a given player by uid"},
        {"clearbanlist", &serverbans::ConCommandClearBanlist, "clears all uids on the banlist"},
    };
    alignas(16) static std::uint8_t objects[3][0x60]{};
    for (std::size_t i = 0; i < 3; ++i) {
        g_conCommandConstruct(objects[i], definitions[i].name, definitions[i].callback, definitions[i].help,
            serverbans::kFcvarGameDll, nullptr);
        LogFormat("[NorthstarPS4] native concommand registered: %s\n", definitions[i].name);
    }
}
