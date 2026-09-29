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
// Its pdata is not installed: PC and PS4 lay persistence out differently
// (G02), so it plays with placeholder data like an insecure player.
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
std::vector<std::pair<std::uint64_t, std::string>> g_pendingPdata;
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
    if (result < 0) LogFormat("[NorthstarPS4] atlas %s failed 0x%x\n", method, static_cast<unsigned>(result));
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
    const std::string url = std::string(uiapi::kMasterServerUrl) + (id.empty() ? "/server/add_server?" : "/server/update_values?") +
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
    AtlasHttp("DELETE", std::string(uiapi::kMasterServerUrl) + "/server/remove_server?id=" + http::UrlEscape(id), nullptr, "",
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
        if (!ConVarInt(g_reportServer, 1)) continue;
        if (singleplayer && !ConVarInt(g_reportSpServer, 0)) continue;
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
    std::string serverId;
    {
        Lock lock;
        serverId = g_serverId;
    }
    LogFormat("[NorthstarPS4] getting pdata for connection %s (uid=%llu username=%s)\n", token.c_str(),
        static_cast<unsigned long long>(uid), username.c_str());
    const std::string base = std::string(uiapi::kMasterServerUrl) + "/server/connect?serverId=" + http::UrlEscape(serverId) +
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
        g_records.push_back({token, uid, username, pdata});
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
    if (scePthreadCreate(&thread, nullptr, ProcessSigreq1, work, "NSAtlasConnect") != 0) {
        delete work;
        return;
    }
    scePthreadDetach(thread);
}

// PC: the token/uid half of CheckAuthentication, at the client's connect
// request, before the engine sees it.
void NoteConnectRequest(const std::uint8_t* data, std::size_t size) {
    atlas::ConnectRequest request;
    const bool parsed = atlas::ParseConnectRequest(data, size, request);
    if (ConVarInt(g_debugAtlasPacket, 0))
        LogFormat("[NorthstarPS4] connect request parsed=%d name=%s strings=%zu\n", parsed ? 1 : 0,
            parsed ? request.name.c_str() : "", request.strings.size());
    if (!parsed) return;
    Lock lock;
    for (const auto& record : g_records) {
        if (record.uid == request.uid && !record.token.empty() && request.HasString(record.token)) {
            if (!record.pdata.empty()) {
                bool replaced = false;
                for (auto& pending : g_pendingPdata)
                    if (pending.first == request.uid) {
                        pending.second = record.pdata;
                        replaced = true;
                    }
                if (!replaced) g_pendingPdata.emplace_back(request.uid, record.pdata);
                if (g_pendingPdata.size() > kMaxPdataRecords) g_pendingPdata.erase(g_pendingPdata.begin());
            }
            for (auto uid : g_authenticatedUids)
                if (uid == request.uid) return;
            g_authenticatedUids.push_back(request.uid);
            if (g_authenticatedUids.size() > kMaxRecords) g_authenticatedUids.erase(g_authenticatedUids.begin());
            return;
        }
    }
}

bool TakeRemotePdata(std::uint64_t uid, std::string& pdata) noexcept {
    Lock lock;
    for (auto it = g_pendingPdata.begin(); it != g_pendingPdata.end(); ++it) {
        if (it->first == uid) {
            pdata = std::move(it->second);
            g_pendingPdata.erase(it);
            return true;
        }
    }
    return false;
}

// PC: MasterServerManager::WritePlayerPersistentData, POST
// /accounts/write_persistence?id=<uid>&serverId=<id> with the pdata as a
// multipart file. Off unless ns_ps4_write_remote_persistence is 1: the PS4
// layout has been checked against 231 byte for byte, but writes change real
// players' Northstar accounts, so they wait for in-game confirmation that
// installed saves read correctly. When off, what would be written is logged.
struct PdataWrite {
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
    const std::string url = std::string(uiapi::kMasterServerUrl) + "/accounts/write_persistence?id=" +
        std::to_string(write->uid) + "&serverId=" + http::UrlEscape(serverId);
    std::string response;
    int status = 0;
    const std::string contentType = "multipart/form-data; boundary=" + boundary;
    const bool sent = AtlasHttp("POST", url, contentType.c_str(), body, response, status);
    LogFormat("[NorthstarPS4] wrote pdata for uid %llu (%s, %zu bytes): %s status %d\n",
        static_cast<unsigned long long>(write->uid), write->reason.c_str(), write->pdata.size(),
        sent ? "sent" : "not sent", status);
    delete write;
    --g_writesInFlight;
    return nullptr;
}

void WriteRemotePdata(std::uint64_t uid, const std::string& pdata, const char* reason) noexcept {
    if (!ConVarInt(g_writeRemotePersistence, 0)) {
        LogFormat("[NorthstarPS4] pdata for uid %llu (%s, %zu bytes) not written: ns_ps4_write_remote_persistence is 0\n",
            static_cast<unsigned long long>(uid), reason, pdata.size());
        return;
    }
    auto* write = new PdataWrite{uid, pdata, reason};
    ++g_writesInFlight;
    OrbisPthread thread{};
    if (scePthreadCreate(&thread, nullptr, WritePdataWorker, write, "NSPdataWrite") != 0) {
        --g_writesInFlight;
        delete write;
        return;
    }
    scePthreadDetach(thread);
}

bool RemotePdataWriting() noexcept { return g_writesInFlight.load() > 0; }

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
        if (data[4] == 'A') NoteConnectRequest(data, static_cast<std::size_t>(size));
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
    const auto distance = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&DisconnectWriteStub)) -
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
        if (scePthreadCreate(&thread, nullptr, atlasserver::PresenceReporter, nullptr, "NSServerPresence") == 0)
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

void InstallAtlasServer(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
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
    alignas(16) static std::uint8_t writeStorage[0x90]{};
    g_writeRemotePersistence = RegisterConVar(writeStorage, "ns_ps4_write_remote_persistence", "0",
        "Whether this PS4 host writes Atlas-authenticated players' pdata back to Atlas");
    g_takeRemotePdata = TakeRemotePdata;
    g_writeRemotePdata = WriteRemotePdata;
    g_remotePdataWriting = RemotePdataWriting;
    InstallDisconnectWrite(engineBase, engineSize);
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
