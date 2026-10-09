// A PS4-hosted server on the Atlas server list: PC's server/serverpresence.cpp,
// masterserver/masterserver.cpp (MasterServerPresenceReporter,
// ProcessConnectionlessPacketSigreq1) and server/servernethooks.cpp.
//
// **Presence.** PC creates a presence when a server starts a game, reports it
// from the host frame and destroys it on shutdown. Here Northstar.PS4's
// ps4_server_presence.nut reports the map, playlist, max players and player
// count once a second from the SERVER VM (NSPS4_UpdateServerPresence), and a
// reporter thread does what MasterServerPresenceReporter does with it:
//   - add_server (POST, the presence in the query, modinfo as multipart),
//     giving the server id and the auth token Atlas signs its requests with;
//     five attempts, a 20 s pause after DUPLICATE_SERVER, none after a reply
//     it cannot read or no connection at all;
//   - update_values every ns_server_presence_update_rate ms (5000), which
//     also re-creates the entry if Atlas dropped it;
//   - remove_server (DELETE) once the server has stopped reporting for 60 s,
//     long enough for a map change.
// Atlas verifies a new server by sending the engine's own connect request to
// its game port, so the host's UDP hostport (37015) must be reachable from the
// internet, as for a PC server.
//
// **Atlas requests.** Atlas signs requests to the server with HMAC-SHA256 and
// the server auth token and sends them to the game port as 'T'
// connectionless packets. CBaseServer::ProcessConnectionlessPacket is hooked
// (IConnectionlessPacketHandler vtable engine+0x3acfb0, slot 2 at
// engine+0x3acfc0 -> engine+0xe4b40; PC engine.dll 0x117800); a packet's raw
// data is at packet+0x18 and its size at packet+0x60. A 'T' packet goes to
// the Atlas handler and never to the engine. The one request type, "connect",
// says a player (uid, username) was given a connection token for this server:
// the server fetches the player's pdata (GET /server/connect) and accepts
// (POST /server/connect?reject=).
//
// **Admission.** A client's connect request ('A', atlas_server.h) carries its
// uid and, in serverfilter, that token. When they match an accepted "connect",
// the client is Atlas-authenticated, as PC's CheckAuthentication decides, and
// Northstar.PS4's host options let it stay with ns_auth_allow_insecure 0.
// The record retains its pdata and whether it came from the host's self-auth
// or Atlas's remote-connect path. That ownership scope is carried through the
// connect and late-install paths, so duplicate-account clients cannot take the
// listen host's slot merely because both slots have the same uid.
//
// Included after runtime_concommands.inl and runtime_http.inl.

namespace atlasserver {

using namespace northstar::ps4;

constexpr std::uintptr_t kConnectionlessSlotVa = 0x3acfc0;
constexpr std::uintptr_t kConnectionlessHandlerVa = 0xe4b40;
constexpr std::size_t kPacketDataOffset = 0x18;
constexpr std::size_t kPacketSizeOffset = 0x60;
constexpr int kMaxRegistrationAttempts = 5;
constexpr std::uint64_t kPresenceTimeoutUs = 60ull * 1000 * 1000;
constexpr std::size_t kPersistenceMaxSize = 0xDDCD;  // PC PERSISTENCE_MAX_SIZE
constexpr std::size_t kMaxRecords = 64;
// Accepted connections whose pdata is kept until the player connects (56 KB
// each).
constexpr std::size_t kMaxPdataRecords = 16;

using ConnectionlessFn = bool (*)(void* self, void* packet);
ConnectionlessFn g_originalConnectionless = nullptr;

void* g_serverName = nullptr;
void* g_serverDescription = nullptr;
void* g_serverPassword = nullptr;
void* g_reportServer = nullptr;
void* g_reportSpServer = nullptr;
void* g_presenceUpdateRate = nullptr;
void* g_debugAtlasPacket = nullptr;
void* g_debugAtlasPacketInsecure = nullptr;
void* g_hostPort = nullptr;
void* g_writeRemotePersistence = nullptr;

struct AuthRecord {
    std::string token;
    std::uint64_t uid = 0;
    std::string username;
    std::string pdata;
    bool hostConnection = false;
};

struct PendingPdata {
    std::uint64_t uid = 0;
    bool hostConnection = false;
    std::string pdata;
};

// Everything below is shared between the game threads and the reporter and
// request threads, under g_lock.
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};
std::string g_serverId;
std::string g_serverAuthToken;
// The password the listing was created with: Atlas only takes a password in
// add_server, so a new one needs a new listing.
std::string g_registeredPassword;
struct PresenceState {
    std::string map;
    std::string playlist;
    int maxPlayers = 0;
    int playerCount = 0;
    std::uint64_t lastReport = 0;
    bool alive = false;
} g_presence;
std::vector<std::string> g_handledTokens;
std::vector<AuthRecord> g_records;
std::vector<std::uint64_t> g_authenticatedUids;
std::atomic<bool> g_reporterStarted{false};
// Pdata for players whose connect request carried their Atlas token, until the
// slot is ready for it (runtime_persistence.inl takes it).
std::vector<PendingPdata> g_pendingPdata;
std::atomic<int> g_writesInFlight{0};

std::uint64_t NowUs() { return sceKernelGetProcessTime(); }

int ConVarInt(void* convar, int fallback) {
    return convar ? *reinterpret_cast<const std::int32_t*>(static_cast<char*>(convar) + kConVarIntValueOffset) : fallback;
}
std::string ConVarText(void* convar) {
    const char* value = convar ? ReadConVarValue(convar) : nullptr;
    return value ? value : "";
}

void* RegisterConVar(std::uint8_t* storage, const char* name, const char* value, const char* help) {
    if (!g_modConVarCvar || !g_modConVarFindVar) return nullptr;
    if (void* existing = g_modConVarFindVar(g_modConVarCvar, name)) return existing;
    if (!g_modConVarConstructor) return nullptr;
    g_modConVarConstructor(storage, name, value, 0, help, nullptr);
    return g_modConVarFindVar(g_modConVarCvar, name);
}

// One request to Atlas with the Northstar user agent (the Atlas template of
// runtime_http.inl). False when there was no response at all.
bool AtlasHttp(const char* method, const std::string& url, const char* contentType, const std::string& body,
    std::string& response, int& status) {
    response.clear();
    status = 0;
    if (!InitHttpTransport()) return false;
    const int connection = sceHttpCreateConnectionWithURL(g_httpTemplate, url.c_str(), true);
    if (connection < 0) return false;
    const int request = sceHttpCreateRequestWithURL2(connection, method, url.c_str(), body.size());
    if (request < 0) {
        sceHttpDeleteConnection(connection);
        return false;
    }
    if (contentType) sceHttpAddRequestHeader(request, "Content-Type", contentType, 0);
    if (!body.empty()) sceHttpSetRequestContentLength(request, body.size());
    int result = sceHttpSendRequest(request, body.empty() ? nullptr : body.data(), body.size());
    if (result >= 0) result = sceHttpGetStatusCode(request, &status);
    if (result >= 0) {
        char chunk[4096];
        for (;;) {
            const int read = sceHttpReadData(request, chunk, sizeof(chunk));
            if (read < 0) { result = read; break; }
            if (read == 0) break;
            response.append(chunk, static_cast<std::size_t>(read));
        }
    }
    sceHttpDeleteRequest(request);
    sceHttpDeleteConnection(connection);
    if (result < 0) LogFormat("[NorthstarPS4] atlas %s failed 0x%x%s\n", method, static_cast<unsigned>(result), HttpErrorText(result));
    return result >= 0;
}

std::string JsonString(const char* json, const char* key) {
    const char* member = json ? mods::JsonFindMember(json, key) : nullptr;
    char value[512];
    if (member && mods::JsonExtractString(member, value, sizeof(value))) return value;
    return "";
}

// PC's ModManager::BuildModInfo: the enabled mods, in load order.
std::string BuildModInfo() {
    std::string out = "{\"Mods\":[";
    bool first = true;
    if (const ModOverlay* overlay = CurrentModOverlay()) {
        static char json[kModJsonBufferSize];
        static mods::ModInfo info;
        for (const auto& dir : overlay->dirs) {
            std::size_t size = 0;
            if (!ReadFileIntoBuffer((dir + "/mod.json").c_str(), json, sizeof(json), size) ||
                !mods::ParseModMetadata(json, info))
                continue;
            const char* required = mods::JsonFindMember(json, "RequiredOnClient");
            const bool requiredOnClient = required && std::strncmp(mods::JsonSkipWs(required), "true", 4) == 0;
            std::string pdiff;
            if (FILE* file = std::fopen((dir + "/mod.pdiff").c_str(), "rb")) {
                char buffer[4096];
                std::size_t read;
                while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) pdiff.append(buffer, read);
                std::fclose(file);
            }
            if (!first) out += ',';
            first = false;
            out += "{\"Name\":";
            mods::JsonEscapeInto(out, info.name, std::strlen(info.name));
            out += ",\"Version\":";
            mods::JsonEscapeInto(out, info.version, std::strlen(info.version));
            out += requiredOnClient ? ",\"RequiredOnClient\":true" : ",\"RequiredOnClient\":false";
            out += ",\"Pdiff\":";
            mods::JsonEscapeInto(out, pdiff.data(), pdiff.size());
            out += '}';
        }
    }
    out += "]}";
    return out;
}

atlas::Presence CurrentPresence() {
    atlas::Presence presence;
    presence.port = ConVarInt(g_hostPort, 37015);
    presence.name = atlas::UnescapeUnicode(ConVarText(g_serverName));
    presence.description = atlas::UnescapeUnicode(ConVarText(g_serverDescription));
    presence.password = ConVarText(g_serverPassword);
    Lock lock;
    presence.map = g_presence.map;
    presence.playlist = g_presence.playlist;
    presence.maxPlayers = g_presence.maxPlayers;
    presence.playerCount = g_presence.playerCount;
    return presence;
}

enum class AddResult { Success, Failed, FailedDuplicate, FailedNoRetry, FailedNoConnect };

// PC: InternalAddServer / InternalUpdateServer. `id` empty adds.
AddResult ReportPresence(const std::string& id) {
    const atlas::Presence presence = CurrentPresence();
    if (ConVarInt(g_debugAtlasPacket, 0))
        LogFormat("[NorthstarPS4] presence port=%d name=%s map=%s playlist=%s players=%d/%d password=%zu chars\n",
            presence.port, presence.name.c_str(), presence.map.c_str(), presence.playlist.c_str(), presence.playerCount,
            presence.maxPlayers, presence.password.size());
    const std::string url = std::string(uiapi::MasterServerUrl()) + (id.empty() ? "/server/add_server?" : "/server/update_values?") +
        atlas::PresenceQuery(presence, id);
    if (ConVarInt(g_debugAtlasPacket, 0)) {
        const std::string escaped = http::UrlEscape(presence.password);
        const std::size_t at = url.rfind("&password=");
        LogFormat("[NorthstarPS4] presence url %zu chars: %s<password %zu chars>, ends with the password: %d\n", url.size(),
            url.substr(0, at == std::string::npos ? url.size() : at + 10).c_str(), escaped.size(),
            at != std::string::npos && url.compare(at + 10, std::string::npos, escaped) == 0 ? 1 : 0);
    }
    const std::string boundary = "NorthstarPS4ModInfo7f3a91c2";
    const std::string contentType = "multipart/form-data; boundary=" + boundary;
    std::string response;
    int status = 0;
    if (id.empty())
        LogFormat("[NorthstarPS4] Attempting to register the local server to the master server (%s, %s).\n",
            presence.map.c_str(), presence.playlist.c_str());
    if (!AtlasHttp("POST", url, contentType.c_str(), atlas::ModInfoMultipart(boundary, BuildModInfo()), response, status)) {
        LogFormat("[NorthstarPS4] %s\n", id.empty() ? "Failed adding self to server list: no response"
                                                    : "Heartbeat failed: no response");
        return id.empty() ? AddResult::FailedNoConnect : AddResult::Failed;
    }
    const char* json = response.c_str();
    const char* p = mods::JsonSkipWs(json);
    if (*p != '{') {
        if (id.empty()) LogFormat("[NorthstarPS4] Failed reading masterserver response (status %d)\n", status);
        return id.empty() ? AddResult::FailedNoRetry : AddResult::Success;
    }
    const std::string newId = JsonString(json, "id");
    const std::string newToken = JsonString(json, "serverAuthToken");
    if (const char* error = mods::JsonFindMember(json, "error")) {
        const std::string code = JsonString(error, "enum");
        const std::string message = JsonString(error, "msg");
        LogFormat("[NorthstarPS4] masterserver %s error: %s (%s)\n", id.empty() ? "add_server" : "update_values",
            code.c_str(), message.c_str());
        if (!id.empty()) return AddResult::Failed;
        return code == "DUPLICATE_SERVER" ? AddResult::FailedDuplicate : AddResult::Failed;
    }
    if (id.empty()) {
        const char* success = mods::JsonFindMember(json, "success");
        if (!success || std::strncmp(mods::JsonSkipWs(success), "true", 4) != 0 || newId.empty() || newToken.empty()) {
            LogFormat("[NorthstarPS4] Adding server to masterserver failed: malformed response\n");
            return AddResult::FailedNoRetry;
        }
        LogFormat("[NorthstarPS4] Successfully registered the local server to the master server.\n");
        Lock lock;
        g_registeredPassword = presence.password;
    }
    // update_values sends a new id and token when it had to re-create the
    // entry.
    if (!newId.empty() && !newToken.empty()) {
        Lock lock;
        g_serverId = newId;
        g_serverAuthToken = newToken;
    }
    return AddResult::Success;
}

void RemovePresence(const std::string& id) {
    std::string response;
    int status = 0;
    AtlasHttp("DELETE", std::string(uiapi::MasterServerUrl()) + "/server/remove_server?id=" + http::UrlEscape(id), nullptr, "",
        response, status);
    LogFormat("[NorthstarPS4] removed the local server from the master server (status %d)\n", status);
}

void* PresenceReporter(void*) {
    std::uint64_t lastSend = 0;
    std::uint64_t nextAdd = 0;
    int attempts = 0;
    bool wasAlive = false;
    for (;;) {
        sceKernelUsleep(250 * 1000);
        const std::uint64_t now = NowUs();
        bool alive;
        bool singleplayer;
        std::string id;
        {
            Lock lock;
            alive = g_presence.alive && now - g_presence.lastReport < kPresenceTimeoutUs;
            singleplayer = g_presence.map.compare(0, 3, "sp_") == 0;
            id = g_serverId;
        }
        if (!alive) {
            if (wasAlive) {
                // PC: DestroyPresence, on GameShutdown.
                if (!id.empty()) RemovePresence(id);
                Lock lock;
                g_serverId.clear();
                g_serverAuthToken.clear();
                g_presence.alive = false;
            }
            wasAlive = false;
            continue;
        }
        if (!wasAlive) {
            // PC: CreatePresence, then the reporters' CreatePresence.
            attempts = 0;
            nextAdd = 0;
            lastSend = 0;
            wasAlive = true;
        }
        // PC stops sending heartbeats when reporting is switched off, and Atlas
        // drops the listing some time later. The Host Options menu switches it
        // while hosting, so the listing goes at once, and comes back (with new
        // attempts) when it is switched on again.
        const bool reporting = ConVarInt(g_reportServer, 1) && (!singleplayer || ConVarInt(g_reportSpServer, 0));
        std::string registeredPassword;
        {
            Lock lock;
            registeredPassword = g_registeredPassword;
        }
        const bool passwordChanged = !id.empty() && ConVarText(g_serverPassword) != registeredPassword;
        if (!id.empty() && (!reporting || passwordChanged)) {
            if (passwordChanged) LogFormat("[NorthstarPS4] server password changed; listing the server again\n");
            RemovePresence(id);
            Lock lock;
            g_serverId.clear();
            g_serverAuthToken.clear();
            id.clear();
        }
        if (!reporting) {
            attempts = 0;
            nextAdd = 0;
            lastSend = 0;
            continue;
        }
        if (passwordChanged) {
            attempts = 0;
            nextAdd = 0;
            lastSend = 0;
        }
        const int rate = ConVarInt(g_presenceUpdateRate, 5000);
        if (lastSend && (now - lastSend) / 1000 < static_cast<std::uint64_t>(rate < 0 ? 0 : rate)) continue;
        lastSend = now;
        if (id.empty()) {
            if (attempts >= kMaxRegistrationAttempts || now < nextAdd) continue;
            switch (ReportPresence("")) {
            case AddResult::Success: break;
            case AddResult::FailedNoRetry:
            case AddResult::FailedNoConnect: attempts = kMaxRegistrationAttempts; break;
            case AddResult::Failed: ++attempts; break;
            case AddResult::FailedDuplicate:
                ++attempts;
                nextAdd = NowUs() + 20ull * 1000 * 1000;
                break;
            }
            if (attempts >= kMaxRegistrationAttempts)
                LogFormat("[NorthstarPS4] Reached max ms server registration attempts.\n");
        } else {
            ReportPresence(id);
        }
    }
    return nullptr;
}

// PC: MasterServerManager::ProcessConnectionlessPacketSigreq1.
struct ConnectRequestWork { std::string data; };

void* ProcessSigreq1(void* argument) {
    auto* work = static_cast<ConnectRequestWork*>(argument);
    const std::string data = std::move(work->data);
    delete work;
    const char* json = data.c_str();
    if (*mods::JsonSkipWs(json) != '{') {
        LogFormat("[NorthstarPS4] invalid Atlas connectionless packet request (%s)\n", json);
        return nullptr;
    }
    const std::string type = JsonString(json, "type");
    if (type != "connect") {
        LogFormat("[NorthstarPS4] invalid Atlas connectionless packet request: unknown type %s\n", type.c_str());
        return nullptr;
    }
    const std::string token = JsonString(json, "token");
    if (token.empty()) {
        LogFormat("[NorthstarPS4] failed to handle Atlas connect request: missing or invalid connection token field\n");
        return nullptr;
    }
    {
        Lock lock;
        for (const auto& handled : g_handledTokens)
            if (handled == token) return nullptr;  // already handled
        g_handledTokens.push_back(token);
        if (g_handledTokens.size() > 256) g_handledTokens.erase(g_handledTokens.begin());
    }
    const char* uidMember = mods::JsonFindMember(json, "uid");
    char* end = nullptr;
    const std::uint64_t uid = uidMember ? std::strtoull(mods::JsonSkipWs(uidMember), &end, 10) : 0;
    if (!uidMember || end == mods::JsonSkipWs(uidMember)) {
        LogFormat("[NorthstarPS4] failed to handle Atlas connect request %s: missing or invalid uid field\n", token.c_str());
        return nullptr;
    }
    const std::string username = JsonString(json, "username");
    // PC: a banned uid is rejected before its pdata is fetched.
    if (!serverbans::IsUidAllowed(uid)) {
        LogFormat("[NorthstarPS4] rejecting Atlas connection %s (uid=%llu): banned\n", token.c_str(),
            static_cast<unsigned long long>(uid));
        std::string reply, ownId;
        int rejectStatus = 0;
        {
            Lock lock;
            ownId = g_serverId;
        }
        const std::string url = std::string(uiapi::MasterServerUrl()) + "/server/connect?serverId=" +
            http::UrlEscape(ownId) + "&token=" + http::UrlEscape(token) + "&reject=" +
            http::UrlEscape("Banned from this server.");
        if (!AtlasHttp("POST", url, nullptr, "", reply, rejectStatus) || rejectStatus != 200)
            LogFormat("[NorthstarPS4] failed to respond to Atlas connect request %s: response status %d\n",
                token.c_str(), rejectStatus);
        return nullptr;
    }
    std::string serverId;
    {
        Lock lock;
        serverId = g_serverId;
    }
    LogFormat("[NorthstarPS4] getting pdata for connection %s (uid=%llu username=%s)\n", token.c_str(),
        static_cast<unsigned long long>(uid), username.c_str());
    const std::string base = std::string(uiapi::MasterServerUrl()) + "/server/connect?serverId=" + http::UrlEscape(serverId) +
        "&token=" + http::UrlEscape(token);
    std::string pdata;
    int status = 0;
    if (!AtlasHttp("GET", base, nullptr, "", pdata, status)) {
        LogFormat("[NorthstarPS4] failed to make Atlas connect pdata request %s\n", token.c_str());
        return nullptr;
    }
    if (status != 200) {
        LogFormat("[NorthstarPS4] failed to make Atlas connect pdata request %s: response status %d\n", token.c_str(), status);
        return nullptr;
    }
    if (pdata.empty() || pdata.size() > kPersistenceMaxSize) {
        LogFormat("[NorthstarPS4] failed to make Atlas connect pdata request %s: pdata size %zu\n", token.c_str(), pdata.size());
        return nullptr;
    }
    LogFormat("[NorthstarPS4] accepting connection %s (uid=%llu username=%s) with %zu bytes of pdata\n", token.c_str(),
        static_cast<unsigned long long>(uid), username.c_str(), pdata.size());
    {
        Lock lock;
        g_records.push_back({token, uid, username, pdata, false});
        // Only the most recent records keep their pdata.
        if (g_records.size() > kMaxRecords) g_records.erase(g_records.begin());
        if (g_records.size() > kMaxPdataRecords) {
            g_records[g_records.size() - kMaxPdataRecords - 1].pdata.clear();
            g_records[g_records.size() - kMaxPdataRecords - 1].pdata.shrink_to_fit();
        }
    }
    std::string reply;
    if (!AtlasHttp("POST", base + "&reject=", nullptr, "", reply, status) || status != 200)
        LogFormat("[NorthstarPS4] failed to respond to Atlas connect request %s: response status %d\n", token.c_str(), status);
    return nullptr;
}

void HandleAtlasPacket(const std::uint8_t* data, std::size_t size) {
    const bool debug = ConVarInt(g_debugAtlasPacket, 0) != 0;
    std::string type, payload;
    atlas::ParseAtlasPacket(data, size, type, payload);
    if (type != "sigreq1") {
        if (debug) LogFormat("[NorthstarPS4] ignoring Atlas connectionless packet (size=%zu type=%s): unknown type\n", size, type.c_str());
        return;
    }
    if (payload.size() < atlas::kHmacSha256Length) {
        if (debug) LogFormat("[NorthstarPS4] ignoring Atlas connectionless packet: too short for signature\n");
        return;
    }
    const std::string signature = payload.substr(0, atlas::kHmacSha256Length);
    const std::string json = payload.substr(atlas::kHmacSha256Length);
    std::string key;
    {
        Lock lock;
        key = g_serverAuthToken;
    }
    if (key.empty()) {
        if (debug) LogFormat("[NorthstarPS4] ignoring Atlas connectionless packet: no masterserver token yet\n");
        return;
    }
    if (!atlas::VerifyHmacSha256(key, signature, json)) {
        if (!ConVarInt(g_debugAtlasPacketInsecure, 0)) {
            if (debug) LogFormat("[NorthstarPS4] ignoring Atlas connectionless packet: invalid signature\n");
            return;
        }
        LogFormat("[NorthstarPS4] processing Atlas connectionless packet with invalid signature due to "
                  "net_debug_atlas_packet_insecure\n");
    }
    if (debug) LogFormat("[NorthstarPS4] got Atlas connectionless packet (size=%zu type=%s data=%s)\n", size, type.c_str(), json.c_str());
    auto* work = new ConnectRequestWork{json};
    OrbisPthread thread{};
    if (StartRuntimeThread(&thread, ProcessSigreq1, work, "NSAtlasConnect") != 0) {
        delete work;
        return;
    }
    scePthreadDetach(thread);
}

// PC: the token/uid half of CheckAuthentication, at the client's connect
// request, before the engine sees it.
// Returns the uid and pdata of a request whose token matched, for the install
// once the engine has made the slot.
bool NoteConnectRequest(const std::uint8_t* data, std::size_t size, std::uint64_t& matchedUid,
    std::string& matchedPdata, bool& hostConnection) {
    atlas::ConnectRequest request;
    const bool parsed = atlas::ParseConnectRequest(data, size, request);
    if (ConVarInt(g_debugAtlasPacket, 0))
        LogFormat("[NorthstarPS4] connect request parsed=%d name=%s strings=%zu\n", parsed ? 1 : 0,
            parsed ? request.name.c_str() : "", request.strings.size());
    if (!parsed) return false;
    Lock lock;
    for (const auto& record : g_records) {
        if (record.uid == request.uid && !record.token.empty() && request.HasString(record.token)) {
            matchedUid = request.uid;
            matchedPdata = record.pdata;
            hostConnection = record.hostConnection;
            bool known = false;
            for (auto uid : g_authenticatedUids)
                if (uid == request.uid) known = true;
            if (!known) {
                g_authenticatedUids.push_back(request.uid);
                if (g_authenticatedUids.size() > kMaxRecords) g_authenticatedUids.erase(g_authenticatedUids.begin());
            }
            return !matchedPdata.empty();
        }
    }
    return false;
}

// When the slot could not be found at connect, the pdata waits for the slot's
// first persistence check instead (TakeRemotePdata).
void KeepPendingPdata(std::uint64_t uid, bool hostConnection, std::string pdata) {
    Lock lock;
    for (auto& pending : g_pendingPdata)
        if (pending.uid == uid && pending.hostConnection == hostConnection) {
            pending.pdata = std::move(pdata);
            return;
        }
    g_pendingPdata.push_back({uid, hostConnection, std::move(pdata)});
    if (g_pendingPdata.size() > kMaxPdataRecords) g_pendingPdata.erase(g_pendingPdata.begin());
}

// The host's own session (auth_with_self, runtime_server_join.inl): recorded
// like an accepted connect, so the host's connect with that token installs its
// save.
void AddSelfAuthRecord(std::uint64_t uid, const std::string& token, const std::string& pdata) noexcept {
    Lock lock;
    g_records.push_back({token, uid, "self", pdata, true});
    if (g_records.size() > kMaxRecords) g_records.erase(g_records.begin());
}

bool TakeRemotePdata(int client, std::uint64_t uid, std::string& pdata) noexcept {
    Lock lock;
    for (auto it = g_pendingPdata.begin(); it != g_pendingPdata.end(); ++it) {
        if (it->uid == uid && persistence::ScopeMatchesClient(it->hostConnection, client)) {
            pdata = std::move(it->pdata);
            g_pendingPdata.erase(it);
            return true;
        }
    }
    return false;
}

// PC: MasterServerManager::WritePlayerPersistentData, POST
// /accounts/write_persistence?id=<uid>&serverId=<id> with the pdata as a
// multipart file. On by default, as on PC (a PC player's save round-trips
// through a PS4 host); +ns_ps4_write_remote_persistence 0 in
// ns_startup_args.txt turns it off, and then what would be written is logged.
struct PdataWrite {
    int client;
    std::uint64_t uid;
    std::string pdata;
    std::string reason;
};

void* WritePdataWorker(void* argument) {
    auto* write = static_cast<PdataWrite*>(argument);
    std::string serverId;
    {
        Lock lock;
        serverId = g_serverId;
    }
    const std::string boundary = "NorthstarPS4Pdata5c1e0a7d";
    std::string body = "--" + boundary +
        "\r\nContent-Disposition: form-data; name=\"pdata\"; filename=\"file.pdata\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    body += write->pdata;
    body += "\r\n--" + boundary + "--\r\n";
    const std::string url = std::string(uiapi::MasterServerUrl()) + "/accounts/write_persistence?id=" +
        std::to_string(write->uid) + "&serverId=" + http::UrlEscape(serverId);
    std::string response;
    int status = 0;
    const std::string contentType = "multipart/form-data; boundary=" + boundary;
    const bool transported = AtlasHttp("POST", url, contentType.c_str(), body, response, status);
    const bool accepted = transported && status >= 200 && status < 300;
    LogFormat("[NorthstarPS4] wrote pdata for uid %llu (%s, %zu bytes): %s status %d\n",
        static_cast<unsigned long long>(write->uid), write->reason.c_str(), write->pdata.size(),
        accepted ? "accepted" : "failed", status);
    RemotePdataWriteCompleted(write->client, write->uid, write->pdata, accepted);
    delete write;
    --g_writesInFlight;
    return nullptr;
}

bool WriteRemotePdata(int client, std::uint64_t uid, const std::string& pdata, const char* reason) noexcept {
    if (!ConVarInt(g_writeRemotePersistence, 1)) {
        LogFormat("[NorthstarPS4] pdata for uid %llu (%s, %zu bytes) not written: ns_ps4_write_remote_persistence is 0\n",
            static_cast<unsigned long long>(uid), reason, pdata.size());
        return false;
    }
    auto* write = new PdataWrite{client, uid, pdata, reason};
    ++g_writesInFlight;
    OrbisPthread thread{};
    if (StartRuntimeThread(&thread, WritePdataWorker, write, "NSPdataWrite") != 0) {
        --g_writesInFlight;
        delete write;
        return false;
    }
    scePthreadDetach(thread);
    return true;
}

bool RemotePdataWriting() noexcept { return g_writesInFlight.load() > 0; }

// PC ServerLimitsManager::CheckConnectionlessPacketLimits (ns_limits.cpp): at
// most sv_querylimit_per_sec connectionless packets a second from one IP
// address, then a minute of silence for it. A packet starts with its sender's
// netadr_t. PC's is {type, ip[16], port} with NA_IP 2; the PS4 engine keeps
// Source's {type, ip[4], port} with NA_IP 3 (NA_BROADCAST is still 2), and the
// bytes after the port vary between packets, so only the IPv4 address is the
// key. Loopback (1), the host's own client, is not limited, as on PC. PC keeps
// an entry per address forever; this keeps the 256 most recently seen.
void* g_queryLimit = nullptr;
void* g_dataBlockEnabled = nullptr;
struct QueryLimit {
    std::uint8_t ip[4];
    std::uint64_t quotaStart;
    std::uint64_t timeoutEnd;
    std::uint64_t lastSeen;
    int count;
    bool used;
};
QueryLimit g_queryLimits[256];

bool CheckConnectionlessLimits(const void* packet, const std::uint8_t* data) noexcept {
    constexpr std::int32_t kNaIp = 3;
    const auto* address = static_cast<const std::uint8_t*>(packet);
    if (*reinterpret_cast<const std::int32_t*>(address) != kNaIp) return true;
    // Data-block packets ('N') are exempt while data blocks are enabled.
    if (data[4] == 'N' && ConVarInt(g_dataBlockEnabled, 0)) return true;
    const std::uint8_t* ip = address + 4;
    const std::uint64_t now = sceKernelGetProcessTime();
    QueryLimit* entry = nullptr;
    QueryLimit* oldest = &g_queryLimits[0];
    for (auto& candidate : g_queryLimits) {
        if (candidate.used && std::memcmp(candidate.ip, ip, 4) == 0) {
            entry = &candidate;
            break;
        }
        if (!candidate.used || (oldest->used && candidate.lastSeen < oldest->lastSeen)) oldest = &candidate;
    }
    if (!entry) {
        entry = oldest;
        *entry = QueryLimit{};
        std::memcpy(entry->ip, ip, 4);
        entry->used = true;
    }
    entry->lastSeen = now;
    if (now < entry->timeoutEnd) return false;
    if (now - entry->quotaStart >= 1000000) {
        entry->quotaStart = now;
        entry->count = 0;
    }
    const int limit = ConVarInt(g_queryLimit, 15);
    if (++entry->count >= limit) {
        LogFormat("[NorthstarPS4] client went over connectionless ratelimit of %d per sec with packet of type %c\n", limit,
            data[4] >= 0x20 && data[4] < 0x7f ? data[4] : '?');
        entry->timeoutEnd = now + 60ull * 1000000;
        return false;
    }
    return true;
}

bool RuntimeConnectionlessPacket(void* self, void* packet) noexcept {
    const auto bytes = static_cast<const char*>(packet);
    const auto data = *reinterpret_cast<const std::uint8_t* const*>(bytes + kPacketDataOffset);
    const int size = *reinterpret_cast<const std::int32_t*>(bytes + kPacketSizeOffset);
    if (data && size > 4) {
        if (ConVarInt(g_debugAtlasPacket, 0))
            LogFormat("[NorthstarPS4] connectionless packet type '%c' size %d\n", data[4] >= 0x20 && data[4] < 0x7f ? data[4] : '?', size);
        // 'T': Atlas, never the engine (PC: no rate limit, the packet is
        // authenticated before anything expensive happens).
        if (data[4] == 'T') {
            HandleAtlasPacket(data, static_cast<std::size_t>(size));
            return false;
        }
        if (!CheckConnectionlessLimits(packet, data)) return false;
        if (data[4] == 'A') {
            // PC: CBaseClient::Connect refuses a banned uid before anything
            // else ("Banned From Server.").
            atlas::ConnectRequest request;
            if (serverbans::g_rejectConnection && atlas::ParseConnectRequest(data, static_cast<std::size_t>(size), request) &&
                !serverbans::IsUidAllowed(request.uid)) {
                LogFormat("[NorthstarPS4] %s's (uid %llu) connection was rejected: \"Banned From Server.\"\n",
                    request.name.c_str(), static_cast<unsigned long long>(request.uid));
                serverbans::g_rejectConnection(self, *reinterpret_cast<const std::int32_t*>(static_cast<char*>(self) + 0xc),
                    packet, "Banned From Server.");
                return false;
            }
            std::uint64_t uid = 0;
            std::string pdata;
            bool hostConnection = false;
            const bool matched = NoteConnectRequest(data, static_cast<std::size_t>(size), uid, pdata, hostConnection);
            const bool result = g_originalConnectionless(self, packet);
            // PC: AuthenticatePlayer in CBaseClient::Connect, before signon.
            if (matched && !InstallRemoteSaveAtConnect(uid, pdata, hostConnection))
                KeepPendingPdata(uid, hostConnection, std::move(pdata));
            return result;
        }
    }
    return g_originalConnectionless(self, packet);
}

} // namespace atlasserver

// PC hooks CBaseClient::Disconnect to write a READY_REMOTE player's pdata
// before the slot is released. The first 13 bytes of engine+0xd75f0 (push rbp;
// mov rbp, rsp; push r15..rbx) move to this stub, which keeps every argument
// register (the function is variadic) while it calls the write.
extern "C" {
__attribute__((used)) std::uintptr_t g_disconnectResume = 0;
__attribute__((used)) void (*g_disconnectWrite)(void*) noexcept = nullptr;
}

__attribute__((naked)) void DisconnectWriteStub() {
    asm volatile(
        "pushq %rdi\n\t"
        "pushq %rsi\n\t"
        "pushq %rdx\n\t"
        "pushq %rcx\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %rax\n\t"
        "subq $128, %rsp\n\t"
        "movdqu %xmm0, 0(%rsp)\n\t"
        "movdqu %xmm1, 16(%rsp)\n\t"
        "movdqu %xmm2, 32(%rsp)\n\t"
        "movdqu %xmm3, 48(%rsp)\n\t"
        "movdqu %xmm4, 64(%rsp)\n\t"
        "movdqu %xmm5, 80(%rsp)\n\t"
        "movdqu %xmm6, 96(%rsp)\n\t"
        "movdqu %xmm7, 112(%rsp)\n\t"
        "callq *g_disconnectWrite(%rip)\n\t"
        "movdqu 0(%rsp), %xmm0\n\t"
        "movdqu 16(%rsp), %xmm1\n\t"
        "movdqu 32(%rsp), %xmm2\n\t"
        "movdqu 48(%rsp), %xmm3\n\t"
        "movdqu 64(%rsp), %xmm4\n\t"
        "movdqu 80(%rsp), %xmm5\n\t"
        "movdqu 96(%rsp), %xmm6\n\t"
        "movdqu 112(%rsp), %xmm7\n\t"
        "addq $128, %rsp\n\t"
        "popq %rax\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rcx\n\t"
        "popq %rdx\n\t"
        "popq %rsi\n\t"
        "popq %rdi\n\t"
        "pushq %rbp\n\t"
        "movq %rsp, %rbp\n\t"
        "pushq %r15\n\t"
        "pushq %r14\n\t"
        "pushq %r13\n\t"
        "pushq %r12\n\t"
        "pushq %rbx\n\t"
        "jmpq *g_disconnectResume(%rip)\n\t");
}

void InstallDisconnectWrite(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    constexpr std::uintptr_t kDisconnectVa = 0xd75f0;
    constexpr std::uint8_t prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,
        0x48, 0x81, 0xec, 0xe8, 0x04, 0x00, 0x00, 0x48, 0x89, 0xfb, 0x89};
    constexpr std::size_t kMoved = 13;
    const auto site = engineBase + kDisconnectVa;
    const auto distance = static_cast<std::int64_t>(Reachable(site + 5, reinterpret_cast<std::uintptr_t>(&DisconnectWriteStub))) -
        static_cast<std::int64_t>(site + 5);
    if (!ValidateEnginePreimage(engineBase, engineSize, kDisconnectVa, prologue, sizeof(prologue)) ||
        distance < -2147483648LL || distance > 2147483647LL) {
        LogFormat("[NorthstarPS4] disconnect pdata write refused: engine profile mismatch\n");
        return;
    }
    g_disconnectResume = site + kMoved;
    g_disconnectWrite = &WriteRemoteSaveOnDisconnect;
    std::uint8_t jump[kMoved];
    std::memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xe9;
    const auto rel = static_cast<std::int32_t>(distance);
    std::memcpy(jump + 1, &rel, sizeof(rel));
    if (!WriteEngineCode(site, jump, sizeof(jump))) {
        LogFormat("[NorthstarPS4] disconnect pdata write failed: mprotect\n");
        return;
    }
    LogFormat("[NorthstarPS4] disconnect pdata write installed\n");
}

// Host Options (Northstar.PS4 ui/ps4_host_options_menu.nut) saved by
// NSPS4_SetHostOption, read back at startup before the startup arguments,
// which can still override them.
constexpr const char* kHostOptionsFile = "/data/northstar_ps4/host_options.txt";

void LoadHostOptions() noexcept {
    char text[4096];
    std::size_t size = 0;
    if (!ReadFileIntoBuffer(kHostOptionsFile, text, sizeof(text), size)) return;
    for (const auto& option : hostoptions::Parse(std::string(text, size))) {
        void* convar = g_modConVarFindVar ? g_modConVarFindVar(g_modConVarCvar, option.first.c_str()) : nullptr;
        if (!convar) continue;
        const bool set = SetConVarString(convar, option.second.c_str());
        LogFormat("[NorthstarPS4] host option %s %s%s\n", option.first.c_str(),
            option.first.find("password") != std::string::npos ? "(value hidden)" : option.second.c_str(),
            set ? "" : " could not be set");
    }
}

bool SaveHostOption(const char* name, const char* value) noexcept {
    char existing[4096];
    std::size_t size = 0;
    auto values = ReadFileIntoBuffer(kHostOptionsFile, existing, sizeof(existing), size)
        ? hostoptions::Parse(std::string(existing, size))
        : std::vector<std::pair<std::string, std::string>>();
    hostoptions::Set(values, name, value);
    const std::string text = hostoptions::Serialize(values);
    const int fd = open(kHostOptionsFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        LogFormat("[NorthstarPS4] host options not saved: can't open %s\n", kHostOptionsFile);
        return false;
    }
    const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    close(fd);
    if (!written) LogFormat("[NorthstarPS4] host options not saved: write failed\n");
    return written;
}

namespace uiapi {

// NSPS4_UpdateServerPresence( string map, string playlist, int maxPlayers,
// int playerCount ), SERVER: see the top of this file.
int UpdateServerPresence(void* vm) {
    const char* map = TextArg(vm, 1);
    const char* playlist = TextArg(vm, 2);
    if (!map || !playlist || Arg(vm, 3).tag != kSqInteger || Arg(vm, 4).tag != kSqInteger)
        return Error(vm, "NSPS4_UpdateServerPresence expects string map, string playlist, int maxPlayers, int playerCount");
    {
        atlasserver::Lock lock;
        auto& presence = atlasserver::g_presence;
        presence.map = map;
        presence.playlist = playlist;
        presence.maxPlayers = static_cast<std::int32_t>(Arg(vm, 3).value);
        presence.playerCount = static_cast<std::int32_t>(Arg(vm, 4).value);
        presence.lastReport = atlasserver::NowUs();
        presence.alive = true;
    }
    bool expected = false;
    if (atlasserver::g_reporterStarted.compare_exchange_strong(expected, true)) {
        OrbisPthread thread{};
        if (StartRuntimeThread(&thread, atlasserver::PresenceReporter, nullptr, "NSServerPresence") == 0)
            scePthreadDetach(thread);
        else
            atlasserver::g_reporterStarted = false;
    }
    return 0;
}

// NSPS4_IsClientAtlasAuthenticated( int client ), SERVER: whether the client
// connected with a token Atlas issued for this server (see above).
int IsClientAtlasAuthenticated(void* vm) {
    if (Arg(vm, 1).tag != kSqInteger) return Error(vm, "NSPS4_IsClientAtlasAuthenticated expects int client");
    const char* uid = g_clientUid ? g_clientUid(static_cast<std::int32_t>(Arg(vm, 1).value)) : nullptr;
    bool authenticated = false;
    if (uid && *uid) {
        const std::uint64_t value = std::strtoull(uid, nullptr, 10);
        atlasserver::Lock lock;
        for (auto known : atlasserver::g_authenticatedUids)
            if (known == value) authenticated = true;
    }
    Boolean(vm, authenticated);
    return 1;
}

// NSPS4_IsServerListed(), UI: whether Atlas has accepted this server's
// listing (add_server succeeded and it has not been removed since).
int IsServerListed(void* vm) {
    bool listed;
    {
        atlasserver::Lock lock;
        listed = !atlasserver::g_serverId.empty();
    }
    Boolean(vm, listed);
    return 1;
}

// NSPS4_SetHostOption( string name, string value ), UI: sets one of the Host
// Options' console variables (host_options.h) and saves it. False for
// any other variable, or if it could not be set or saved.
int SetHostOption(void* vm) {
    const char* name = TextArg(vm, 1);
    const char* value = TextArg(vm, 2);
    if (!name || !value) return Error(vm, "NSPS4_SetHostOption expects string name, string value");
    void* convar = hostoptions::IsHostOption(name) && g_modConVarFindVar ? g_modConVarFindVar(g_modConVarCvar, name) : nullptr;
    const bool set = convar && SetConVarString(convar, hostoptions::OneLine(value).c_str());
    const bool saved = set && SaveHostOption(name, value);
    LogFormat("[NorthstarPS4] host option %s %s%s\n", name,
        std::strstr(name, "password") ? "(value hidden)" : value, !set ? ": not set" : saved ? "" : ": not saved");
    Boolean(vm, saved);
    return 1;
}

} // namespace uiapi

// PC: "+name value" launch arguments set console variables at startup, which
// is how PC servers are usually named (+ns_server_name "..."). Applied from
// ns_startup_args.txt once this file's convars exist; only existing convars
// are set, and other +commands are not run.
void ApplyStartupConVars() noexcept {
    for (const char* file : {kGameStartupArgsFile, kStartupArgsFile}) {
        char text[2048];
        std::size_t size = 0;
        if (!ReadFileIntoBuffer(file, text, sizeof(text), size)) continue;
        for (const auto& assignment : startup::ConVarAssignments(startup::SplitArgs(text))) {
            if (assignment.first == "ns_masterserver_hostname") continue;  // read by uiapi::MasterServerUrl()
            void* convar = g_modConVarFindVar ? g_modConVarFindVar(g_modConVarCvar, assignment.first.c_str()) : nullptr;
            const bool secret = assignment.first.find("password") != std::string::npos;
            if (!convar) {
                LogFormat("[NorthstarPS4] startup argument +%s: no such console variable\n", assignment.first.c_str());
                continue;
            }
            const bool set = SetConVarString(convar, assignment.second.c_str());
            LogFormat("[NorthstarPS4] startup argument +%s %s%s\n", assignment.first.c_str(),
                secret ? "(value hidden)" : assignment.second.c_str(), set ? "" : " could not be set");
        }
    }
}

void InstallNetMessageFixes(std::uintptr_t engineBase, std::size_t engineSize) noexcept;  // runtime_netmessage_fixes.inl

void InstallAtlasServer(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    InstallBans(engineBase, engineSize);
    InstallStringCommandGuard(engineBase, engineSize);
    InstallNetMessageFixes(engineBase, engineSize);
    using namespace atlasserver;
    alignas(16) static std::uint8_t storage[8][0x90]{};
    g_serverName = RegisterConVar(storage[0], "ns_server_name", "Unnamed Northstar Server", "This server's name");
    g_serverDescription = RegisterConVar(storage[1], "ns_server_desc", "Default server description", "This server's description");
    g_serverPassword = RegisterConVar(storage[2], "ns_server_password", "", "This server's password");
    g_reportServer = RegisterConVar(storage[3], "ns_report_server_to_masterserver", "1",
        "Whether we should report this server to the masterserver");
    g_reportSpServer = RegisterConVar(storage[4], "ns_report_sp_server_to_masterserver", "0",
        "Whether we should report this server to the masterserver, when started in singleplayer");
    g_presenceUpdateRate = RegisterConVar(storage[5], "ns_server_presence_update_rate", "5000",
        "How often we update our server's presence on server lists in ms");
    g_debugAtlasPacket = RegisterConVar(storage[6], "net_debug_atlas_packet", "0",
        "Whether to log detailed debugging information for Atlas connectionless packets");
    g_debugAtlasPacketInsecure = RegisterConVar(storage[7], "net_debug_atlas_packet_insecure", "0",
        "Whether to disable signature verification for Atlas connectionless packets (DANGEROUS: this allows anyone "
        "to impersonate Atlas)");
    if (g_modConVarCvar && g_modConVarFindVar) g_hostPort = g_modConVarFindVar(g_modConVarCvar, "hostport");
    alignas(16) static std::uint8_t queryLimitStorage[0x90]{};
    g_queryLimit = RegisterConVar(queryLimitStorage, "sv_querylimit_per_sec", "15", "");
    if (g_modConVarCvar && g_modConVarFindVar)
        g_dataBlockEnabled = g_modConVarFindVar(g_modConVarCvar, "net_data_block_enabled");
    alignas(16) static std::uint8_t writeStorage[0x90]{};
    g_writeRemotePersistence = RegisterConVar(writeStorage, "ns_ps4_write_remote_persistence", "1",
        "Whether this PS4 host writes Atlas-authenticated players' pdata back to Atlas");
    g_takeRemotePdata = TakeRemotePdata;
    g_addSelfAuthRecord = AddSelfAuthRecord;
    g_writeRemotePdata = WriteRemotePdata;
    g_remotePdataWriting = RemotePdataWriting;
    InstallDisconnectWrite(engineBase, engineSize);
    // PC runs Northstar.CustomServers' cfg/autoexec_ns_server.cfg whenever a
    // server starts a game (hoststate.cpp, listen servers included), and it sets
    // `everything_unlocked 1`. This port does not run that file, so a PS4 host
    // validated loadouts against real unlocks: a player's redline_sight failed
    // FailsItemLockedValidationCheck, sh_loadouts.nut reset it and kicked them
    // with "Resetting invalid loadout", and the reset was written to their
    // account. The file's other engine values already match on PS4 (tick
    // interval, update rate, snapshots), so this one is set the same way, before
    // the startup arguments, which can still override it.
    if (void* unlocked = g_modConVarFindVar ? g_modConVarFindVar(g_modConVarCvar, "everything_unlocked") : nullptr) {
        if (SetConVarString(unlocked, "1"))
            LogFormat("[NorthstarPS4] everything_unlocked 1 (Northstar's autoexec_ns_server.cfg)\n");
    }
    LoadHostOptions();
    ApplyStartupConVars();

    // PC: "patch to disable kicking based on incorrect serverfilter in
    // connectclient, since we repurpose it for use as an auth token" (engine.dll
    // 0x114655 -> EB). The inlined ConnectClient compares the client's
    // serverfilter with the server's own (engine+0xe7494) and otherwise rejects
    // with "Incoming server filter of ... doesn't match our server filter of
    // ...", which is what a PC joining through the browser saw. The je at
    // engine+0xe749b becomes a jmp.
    constexpr std::uint8_t filterCheckBytes[] = {0x48, 0x8b, 0x05, 0x1c, 0xe7, 0x36, 0x00, 0x48, 0x8d, 0x35, 0x50,
        0x5d, 0x26, 0x00, 0x48, 0x89, 0x95, 0x18, 0xfb, 0xff, 0xff, 0x48, 0x8b, 0x40, 0x48, 0x48, 0x85, 0xc0, 0x48, 0x0f,
        0x45, 0xf0, 0xeb, 0x0e, 0x48, 0x89, 0x95, 0x18, 0xfb, 0xff, 0xff, 0x48, 0x8d, 0x35, 0xd4, 0x55, 0x26, 0x00, 0x48,
        0x8d, 0xbd, 0xa0, 0xfd, 0xff, 0xff, 0xe8, 0x4f, 0x8f, 0xf1, 0xff, 0x85, 0xc0, 0x74, 0x23};
    constexpr std::uintptr_t kFilterCheckVa = 0xe745d;
    constexpr std::uint8_t jump = 0xeb;
    if (ValidateEnginePreimage(engineBase, engineSize, kFilterCheckVa, filterCheckBytes, sizeof(filterCheckBytes)) &&
        WriteEngineCode(engineBase + kFilterCheckVa + sizeof(filterCheckBytes) - 2, &jump, 1))
        LogFormat("[NorthstarPS4] server filter check disabled (the filter carries the Atlas token, as on PC)\n");
    else
        LogFormat("[NorthstarPS4] server filter check patch refused: engine profile mismatch\n");

    constexpr std::uint8_t handlerBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x81, 0xec, 0x08, 0x05, 0x00, 0x00, 0x4c, 0x8b, 0x25, 0x95, 0xb1, 0x2f, 0x00, 0x49, 0x89, 0xf3, 0x49,
        0x89, 0xfd, 0x49, 0x8b, 0x04, 0x24, 0x48, 0x89, 0x45, 0xd0};
    auto slot = reinterpret_cast<std::uintptr_t*>(engineBase + kConnectionlessSlotVa);
    if (!ValidateEnginePreimage(engineBase, engineSize, kConnectionlessHandlerVa, handlerBytes, sizeof(handlerBytes)) ||
        !AuthAddressReadable(slot) || *slot != engineBase + kConnectionlessHandlerVa) {
        LogFormat("[NorthstarPS4] connectionless packet hook refused: engine profile mismatch\n");
        return;
    }
    void* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(slot) & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 3) != 0) {
        LogFormat("[NorthstarPS4] connectionless packet hook failed: mprotect\n");
        return;
    }
    g_originalConnectionless = reinterpret_cast<ConnectionlessFn>(*slot);
    *slot = reinterpret_cast<std::uintptr_t>(&RuntimeConnectionlessPacket);
    LogFormat("[NorthstarPS4] connectionless packet hook installed; hostport %d\n", ConVarInt(g_hostPort, 0));
}
