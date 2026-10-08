// Signing in from a PC (atlas_refresh.h, token-helper/, NorthstarPS4TokenHelper.exe).
//
// The PC token helper gets the Atlas token. To hand it over without copying
// files, this runtime listens on TCP kSignInPort for the helper's one-time
// push: POST kSignInPath with uid, playerToken, refreshUrl, refreshKey and the
// code the menu shows. A push from this machine (shadPS4 on the helper's own
// PC) needs no code, and neither does one from the helper already paired
// (same refreshKey). After that the console asks the helper for new tokens by
// itself (RefreshAtlasToken). An accepted push takes effect at once: the
// identity is applied and saved, and no restart is needed.
//
// GET kHelloPath lets the helper find a running game; it reveals nothing.
//
// Both sockets are non-blocking and polled. shadPS4 holds a socket's lock for
// the whole of a blocking accept or recv, and its sceNetGetSockInfo takes the
// lock of every socket, so a listener waiting in accept could stall any game
// thread that enumerates sockets.
// `-nopcsignin` in ns_startup_args.txt turns the listener off. The pairing
// code is logged, as it is shown on screen anyway; the token and key are not.
namespace signin {

constexpr int kNetSolSocket = 0xffff;
constexpr int kNetSoReuseAddr = 0x4;
constexpr int kNetSoSndTimeo = 0x1105;  // microseconds
constexpr int kNetSoNbio = 0x1200;
constexpr int kNetCtlInfoIpAddress = 14;

struct SockaddrIn {
    std::uint8_t len;
    std::uint8_t family;
    std::uint16_t port;    // network order
    std::uint32_t address; // network order
    char zero[8];
};
static_assert(sizeof(SockaddrIn) == 16, "sockaddr_in layout");

std::string g_code;
int g_wrongCodes = 0;

std::uint32_t RandomCode() noexcept {
    std::uint64_t x = sceKernelGetProcessTimeCounter() ^ reinterpret_cast<std::uintptr_t>(&g_code) ^
        (static_cast<std::uint64_t>(sceKernelGetProcessTime()) << 21);
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>(x ^ (x >> 31));
}

void Reply(int socket, int status, const char* reason, const std::string& body) noexcept {
    char head[160];
    const int length = std::snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, reason, body.size());
    if (length > 0) sceNetSend(socket, head, static_cast<std::size_t>(length), 0);
    if (!body.empty()) sceNetSend(socket, body.data(), body.size(), 0);
}

std::string ErrorBody(const std::string& message) { return "{\"error\":\"" + message + "\"}"; }

// The identity becomes this console's at once, and is saved for later boots.
void ApplyPushedIdentity(const atlas::IdentityFields& fields) noexcept {
    while (uiapi::g_atlasRefreshBusy.test_and_set(std::memory_order_acquire)) sceKernelUsleep(1000);
    std::snprintf(g_atlasUid, sizeof(g_atlasUid), "%s", fields.uid.c_str());
    std::snprintf(g_atlasToken, sizeof(g_atlasToken), "%s", fields.token.c_str());
    std::snprintf(g_atlasRefreshUrl, sizeof(g_atlasRefreshUrl), "%s", fields.refreshUrl.c_str());
    std::snprintf(g_atlasRefreshKey, sizeof(g_atlasRefreshKey), "%s", fields.refreshKey.c_str());
    g_atlasIdentityState.store(AtlasIdentityState::Ready);
    const std::string json = atlas::BuildIdentityJson({fields.uid, fields.token, fields.refreshUrl, fields.refreshKey});
    FILE* file = std::fopen(uiapi::kAtlasIdentityTemp, "wb");
    const bool written = file && std::fwrite(json.data(), 1, json.size(), file) == json.size();
    if (file && std::fclose(file) != 0) file = nullptr;
    if (!written || !file || std::rename(uiapi::kAtlasIdentityTemp, kAtlasIdentityFile) != 0)
        LogFormat("[NorthstarPS4] PC sign-in applied, but atlas_identity.json was not saved\n");
    uiapi::g_atlasRefreshBusy.clear(std::memory_order_release);
}

void Serve(int socket, std::uint32_t peer) noexcept {
    const int on = 1, timeout = 3 * 1000 * 1000;
    sceNetSetsockopt(socket, kNetSolSocket, kNetSoNbio, &on, sizeof(on));
    sceNetSetsockopt(socket, kNetSolSocket, kNetSoSndTimeo, &timeout, sizeof(timeout));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&peer);
    char peerText[16];
    std::snprintf(peerText, sizeof(peerText), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    const bool fromThisMachine = bytes[0] == 127;

    std::string request;
    char chunk[1024];
    std::size_t headEnd = std::string::npos;
    atlas::RequestHead head;
    // Polled for up to 3 s: a client that stalls is dropped.
    for (int waits = 0;;) {
        const int got = sceNetRecv(socket, chunk, sizeof(chunk), 0);
        if (got == 0) return;
        if (got < 0) {
            if (++waits > 150) return;
            sceKernelUsleep(20 * 1000);
            continue;
        }
        request.append(chunk, static_cast<std::size_t>(got));
        if (request.size() > atlas::kMaxSignInRequest) return Reply(socket, 413, "Too Large", ErrorBody("too large"));
        if (headEnd == std::string::npos) {
            headEnd = request.find("\r\n\r\n");
            if (headEnd == std::string::npos) continue;
            if (!atlas::ParseRequestHead(request.substr(0, headEnd), head))
                return Reply(socket, 400, "Bad Request", ErrorBody("bad request"));
        }
        if (request.size() >= headEnd + 4 + head.contentLength) break;
    }
    // `paired` answers only whether the helper's own key matches, so the helper
    // can leave out the code box for a console it is already paired with.
    if (head.method == "GET" && head.path == atlas::kHelloPath)
        return Reply(socket, 200, "OK", std::string("{\"app\":\"NorthstarPS4\",\"signedIn\":") +
            (AtlasIdentityReady() ? "true" : "false") + ",\"paired\":" +
            (atlas::KeyMatches(head.key, g_atlasRefreshKey) ? "true}" : "false}"));
    if (head.method != "POST" || head.path != atlas::kSignInPath)
        return Reply(socket, 404, "Not Found", ErrorBody("not found"));

    const std::string body = request.substr(headEnd + 4, head.contentLength);
    atlas::IdentityFields fields;
    if (!atlas::ParseIdentity(body.c_str(), fields)) return Reply(socket, 400, "Bad Request", ErrorBody("bad request"));
    const std::string refusal = atlas::CheckSignInPush(fields, fromThisMachine, g_code, g_atlasRefreshKey, g_wrongCodes);
    if (!refusal.empty()) {
        LogFormat("[NorthstarPS4] PC sign-in from %s refused: %s\n", peerText, refusal.c_str());
        return Reply(socket, 403, "Forbidden", ErrorBody(refusal));
    }
    ApplyPushedIdentity(fields);
    LogFormat("[NorthstarPS4] PC sign-in from %s: account %s, token helper at %s\n", peerText, fields.uid.c_str(),
        fields.refreshUrl.c_str());
    Reply(socket, 200, "OK", "{\"ok\":true}");
}

void* Listener(void*) noexcept {
    sceNetInit();
    sceNetCtlInit();
    OrbisNetCtlInfo info{};
    char address[16] = "";
    if (sceNetCtlGetInfo(kNetCtlInfoIpAddress, &info) == 0) std::snprintf(address, sizeof(address), "%s", info.ip_address);

    const int listener = sceNetSocket("NSPcSignIn", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if (listener < 0) {
        LogFormat("[NorthstarPS4] PC sign-in unavailable: socket failed 0x%x\n", listener);
        return nullptr;
    }
    const int on = 1;
    sceNetSetsockopt(listener, kNetSolSocket, kNetSoReuseAddr, &on, sizeof(on));
    if (sceNetSetsockopt(listener, kNetSolSocket, kNetSoNbio, &on, sizeof(on)) < 0) {
        LogFormat("[NorthstarPS4] PC sign-in unavailable: non-blocking mode refused\n");
        sceNetSocketClose(listener);
        return nullptr;
    }
    SockaddrIn bindAddress{};
    bindAddress.len = sizeof(bindAddress);
    bindAddress.family = ORBIS_NET_AF_INET;
    bindAddress.port = sceNetHtons(atlas::kSignInPort);
    int result = sceNetBind(listener, reinterpret_cast<OrbisNetSockaddr*>(&bindAddress), sizeof(bindAddress));
    if (result >= 0) result = sceNetListen(listener, 4);
    if (result < 0) {
        LogFormat("[NorthstarPS4] PC sign-in unavailable: port %d could not be opened (0x%x)\n", atlas::kSignInPort,
            result);
        sceNetSocketClose(listener);
        return nullptr;
    }

    g_code = atlas::FormatSignInCode(RandomCode());
    if (address[0] && std::strcmp(address, "127.0.0.1") != 0)
        std::snprintf(g_pcSignInHint, sizeof(g_pcSignInHint),
            "On a PC signed in to the EA app, run the NorthstarPS4 token helper and enter %s %s when it asks",
            address, g_code.c_str());
    else
        std::snprintf(g_pcSignInHint, sizeof(g_pcSignInHint),
            "On a PC signed in to the EA app, run the NorthstarPS4 token helper and enter this console's IP "
            "address and the code %s when it asks", g_code.c_str());
    g_pcSignInReady.store(true, std::memory_order_release);
    LogFormat("[NorthstarPS4] PC sign-in: listening on %s:%d, code %s\n", address[0] ? address : "?",
        atlas::kSignInPort, g_code.c_str());

    for (;;) {
        SockaddrIn peer{};
        OrbisNetSocklen_t peerLength = sizeof(peer);
        const int client = sceNetAccept(listener, reinterpret_cast<OrbisNetSockaddr*>(&peer), &peerLength);
        if (client < 0) {  // nothing waiting
            sceKernelUsleep(250 * 1000);
            continue;
        }
        Serve(client, peer.address);
        sceNetSocketClose(client);
    }
}

} // namespace signin

void StartPcSignIn() noexcept {
    if (StartupArgPresent("-nopcsignin")) {
        LogFormat("[NorthstarPS4] PC sign-in turned off (-nopcsignin)\n");
        return;
    }
    OrbisPthread thread{};
    if (StartRuntimeThread(&thread, signin::Listener, nullptr, "NSPcSignIn") != 0)
        LogFormat("[NorthstarPS4] PC sign-in unavailable: thread not started\n");
}
