// Joining a Northstar server from the browser.
//
// Included inside `uiapi` right after runtime_server_list.inl, whose `servers`
// list the index argument refers to. Mirrors NorthstarLauncher's
// NSTryAuthWithServer / NSIsAuthenticatingWithServer / NSConnectToAuthedServer
// (primedev/scripts/client/scriptserverbrowser.cpp) and
// MasterServerManager::AuthenticateWithServer (masterserver.cpp).
//
// The browser's sequence is: NSTryAuthWithServer(index, password), spin while
// NSIsAuthenticatingWithServer(), then NSWasAuthSuccessful() - showing
// NSGetAuthFailReason() in a dialog on failure - and, after reconciling
// client-required mods, NSConnectToAuthedServer(). So, as with the server list,
// "authenticating" turns true inside the request call and the POST runs on a
// worker, with the same lock-free handover: the worker owns `joinResult` while
// `joinState` is kFetchRequesting and publishes it with a release store.
//
// A failure is reported through NSWasAuthSuccessful and the reason, as on PC,
// not raised as a script error. The one exception is PC's own: an index past
// the end of the list raises.
//
// Nothing secret is logged: the player token and the per-connection auth token
// stay out of the log, the server id and address are fine.

// Atlas token refresh through the PC token helper (atlas_refresh.h,
// token-helper/, NorthstarPS4TokenHelper.exe). PC re-authenticates with Origin when
// Atlas refuses its token; a PS4 cannot, so when atlas_identity.json names a
// helper, the refused token is replaced by one the helper mints and the
// request is tried once more. The helper's key and both tokens stay out of
// the log. `usedToken` is the token the caller just had refused: when another
// worker has already replaced it, that replacement is used instead.
std::atomic_flag g_atlasRefreshBusy = ATOMIC_FLAG_INIT;
constexpr const char* kAtlasIdentityTemp = "/data/northstar_ps4/atlas_identity.json.tmp";

bool RefreshAtlasToken(const std::string& usedToken, std::string& reason) noexcept {
    if (!g_atlasRefreshUrl[0] || !g_atlasRefreshKey[0]) {
        reason = "no token helper is set up";
        return false;
    }
    while (g_atlasRefreshBusy.test_and_set(std::memory_order_acquire)) sceKernelUsleep(1000);
    bool ok = false;
    if (usedToken != g_atlasToken) {
        ok = true;  // already refreshed by another request
    } else {
        char reply[1024];
        int status = 0;
        LogFormat("[NorthstarPS4] atlas token refused; asking the token helper at %s\n", g_atlasRefreshUrl);
        if (!HttpGetWithHeader(g_atlasRefreshUrl, "X-NorthstarPS4-Key", g_atlasRefreshKey, reply, sizeof(reply),
                status)) {
            reason = std::string("the token helper could not be reached at ") + g_atlasRefreshUrl;
        } else if (status != 200) {
            std::string message;
            if (!DecodeJsonString(JsonFindMember(reply, "error"), message) || message.empty())
                message = "status " + std::to_string(status);
            reason = "the token helper reported: " + message;
        } else {
            atlas::IdentityFields fields;
            if (!atlas::ParseIdentity(reply, fields)) {
                reason = "the token helper sent an unreadable reply";
            } else if (atlas::AcceptRefreshedToken(fields, g_atlasUid, usedToken, reason)) {
                std::snprintf(g_atlasToken, sizeof(g_atlasToken), "%s", fields.token.c_str());
                ok = true;
                // Kept on disk, so a restart starts with the new token.
                const std::string json = atlas::BuildIdentityJson({g_atlasUid, fields.token, g_atlasRefreshUrl,
                    g_atlasRefreshKey});
                FILE* file = std::fopen(kAtlasIdentityTemp, "wb");
                const bool written = file && std::fwrite(json.data(), 1, json.size(), file) == json.size();
                if (file && std::fclose(file) != 0) file = nullptr;
                if (!written || !file || std::rename(kAtlasIdentityTemp, kAtlasIdentityFile) != 0)
                    LogFormat("[NorthstarPS4] atlas token refreshed, but atlas_identity.json was not updated\n");
                LogFormat("[NorthstarPS4] atlas token refreshed through the token helper\n");
            }
        }
        if (!ok) LogFormat("[NorthstarPS4] atlas token refresh failed: %s\n", reason.c_str());
    }
    g_atlasRefreshBusy.clear(std::memory_order_release);
    return ok;
}

// What to tell the player when Atlas refuses the token and no refresh helped.
std::string AtlasTokenAdvice(const std::string& refreshReason) {
    if (!g_atlasRefreshUrl[0]) {
        if (g_pcSignInReady.load(std::memory_order_acquire)) return std::string(". ") + g_pcSignInHint + ".";
        return ". On a PC signed in to the EA app, run the NorthstarPS4 token helper to renew it.";
    }
    // The address and code only when the helper no longer knows this console:
    // a paired one is renewed by its key, and the code would not help.
    std::string advice = ". Token refresh failed: " + refreshReason + ".";
    if (refreshReason.find("could not be reached") != std::string::npos)
        advice += " Start the NorthstarPS4 token helper on your PC and try again.";
    else if (refreshReason.find("not paired") != std::string::npos && g_pcSignInReady.load(std::memory_order_acquire))
        advice += std::string(" ") + g_pcSignInHint + ".";
    else
        advice += " Check the NorthstarPS4 token helper on your PC and try again.";
    return advice;
}

std::atomic<int> joinState{kFetchIdle};
std::string joinServerId, joinPassword;  // set before the worker starts
ServerAuthResponse joinResult;           // worker-owned while requesting
char joinBuffer[8 * 1024];

// UI thread only. The address and token from the last successful auth, spent
// by NSConnectToAuthedServer.
struct PendingConnection { bool ready = false; std::string ip, authToken; int port = 0; };
PendingConnection pendingConnection;

void* ServerJoinWorker(void*) noexcept {
    LogFormat("[NorthstarPS4] authenticating with server %s\n", joinServerId.c_str());
    std::string refreshReason;
    int status = 0;
    bool got = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string usedToken = g_atlasToken;
        const std::string url = std::string(MasterServerUrl()) + "/client/auth_with_server?id=" +
            PercentEncode(g_atlasUid) + "&playerToken=" + PercentEncode(usedToken) +
            "&server=" + PercentEncode(joinServerId) + "&password=" + PercentEncode(joinPassword);
        joinResult = ServerAuthResponse{};
        status = 0;
        got = HttpPost(url.c_str(), joinBuffer, sizeof(joinBuffer), status);
        if (!got || attempt == 1 || std::strlen(joinBuffer) >= sizeof(joinBuffer) - 1) break;
        ServerAuthResponse probe;
        ParseServerAuthResponse(joinBuffer, probe);
        if (probe.success || probe.errorEnum != "INVALID_MASTERSERVER_TOKEN") break;
        if (!RefreshAtlasToken(usedToken, refreshReason)) break;
    }
    if (!got) {
        joinResult.failureReason = "Could not reach the Northstar master server";
    } else if (std::strlen(joinBuffer) >= sizeof(joinBuffer) - 1) {
        joinResult.failureReason = "Authentication Failed";
        LogFormat("[NorthstarPS4] server auth response exceeds %zu bytes\n", sizeof(joinBuffer));
    } else {
        // Parsed whatever the status: Atlas puts the reason for a 4xx/5xx in
        // the body.
        ParseServerAuthResponse(joinBuffer, joinResult);
        // PC answers these by re-authenticating with Origin and retrying; the
        // PS4 has only the imported token, so the dialog says how to renew it.
        if (joinResult.errorEnum == "INVALID_MASTERSERVER_TOKEN" || joinResult.errorEnum == "PLAYER_NOT_FOUND")
            joinResult.failureReason += AtlasTokenAdvice(refreshReason);
    }
    if (joinResult.success)
        LogFormat("[NorthstarPS4] server auth succeeded: %s:%d\n", joinResult.ip.c_str(), joinResult.port);
    else
        LogFormat("[NorthstarPS4] server auth failed (status %d): %s\n", status, joinResult.failureReason.c_str());
    joinState.store(kFetchReady, std::memory_order_release);
    return nullptr;
}

void PublishJoinResult() {
    if (joinState.load(std::memory_order_acquire) != kFetchReady) return;
    authSucceeded = joinResult.success;
    authFailure = joinResult.success ? std::string() : joinResult.failureReason;
    if (joinResult.success)
        pendingConnection = {true, joinResult.ip, joinResult.authToken, joinResult.port};
    joinResult = ServerAuthResponse{};
    joinState.store(kFetchIdle, std::memory_order_release);
}

int TryRemoteAuth(void* vm) {
    PublishFetchedServers();
    PublishJoinResult();
    // PC: "dont wait, just stop if we're trying to do 2 auth requests at once".
    if (joinState.load(std::memory_order_acquire) != kFetchIdle) return 0;

    const auto& indexArg = Arg(vm, 1);
    const int index = static_cast<std::int32_t>(indexArg.value);
    if (indexArg.tag != 0x5000002 || index < 0 || static_cast<std::size_t>(index) >= servers.size()) {
        char message[128];
        std::snprintf(message, sizeof(message),
            "Tried to auth with server index %d when only %zu servers are available",
            index, servers.size());
        return Error(vm, message);
    }

    authSucceeded = false;
    authFailure = "Authentication Failed";
    pendingConnection.ready = false;
    if (!AtlasIdentityReady()) {
        authFailure = AtlasIdentityMessage();
        return 0;
    }
    const char* password = TextArg(vm, 2);
    joinServerId = servers[static_cast<std::size_t>(index)].id;
    joinPassword = password ? password : "";

    int expected = kFetchIdle;
    if (!joinState.compare_exchange_strong(expected, kFetchRequesting)) return 0;
    OrbisPthread thread{};
    if (!InitHttpTransport() ||
        StartRuntimeThread(&thread, ServerJoinWorker, nullptr, "NSServerJoin") != 0) {
        joinResult = ServerAuthResponse{};
        joinResult.failureReason = "Could not start the authentication request";
        joinState.store(kFetchReady, std::memory_order_release);
        return 0;
    }
    scePthreadDetach(thread);
    return 0;
}

// The host's own Atlas session: PC MasterServerManager::AuthenticateWithOwnServer.
// "Launch Northstar" calls NSTryAuthWithLocalServer, waits for
// NSIsAuthenticatingWithServer to go false, then on NSWasAuthSuccessful runs
// NSCompleteAuthWithLocalServer, `setplaylist tdm` and `map mp_lobby`.
//
// POST /client/auth_with_self?id=&playerToken= returns the host's save and a
// token. The save goes into the hosted server's Atlas records (see
// runtime_atlas_server.inl), and NSCompleteAuthWithLocalServer puts the token
// in serverfilter, so the host's own connect installs its save as READY_REMOTE
// and the host's progress is written back to its account, as on PC.
//
// PC does not start the lobby when this fails. With only an imported identity
// that expires, that would lock a PS4 player out of the lobby until they
// re-export it, so here the lobby starts anyway with the local placeholder
// save and the reason is logged.
//
// Except when Atlas refuses the token itself and the token helper cannot
// renew it: the lobby would then start on the local save with no word to the
// player, who may not know that signing in to Northstar elsewhere with the
// same EA account replaced the PS4's sign-in (2026-10-08). That fails here as
// on PC, with the reason and what to do in the error dialog.
std::atomic<int> selfAuthState{kFetchIdle};
SelfAuthResponse selfAuthResult;  // worker-owned while requesting
bool selfAuthTokenRefused = false; // worker-owned while requesting
std::string selfAuthToken;        // UI thread: set by a successful attempt

void* SelfAuthWorker(void*) noexcept {
    LogFormat("[NorthstarPS4] authenticating with own server\n");
    constexpr std::size_t kCapacity = 1 << 20;  // the save arrives as a JSON array of bytes (~225 KB)
    auto* buffer = new char[kCapacity];
    int status = 0;
    bool got = false;
    std::string refreshReason;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string usedToken = g_atlasToken;
        const std::string url = std::string(MasterServerUrl()) + "/client/auth_with_self?id=" +
            PercentEncode(g_atlasUid) + "&playerToken=" + PercentEncode(usedToken);
        selfAuthResult = SelfAuthResponse{};
        status = 0;
        got = HttpPost(url.c_str(), buffer, kCapacity, status);
        if (!got || attempt == 1 || std::strlen(buffer) >= kCapacity - 1) break;
        SelfAuthResponse probe;
        ParseSelfAuthResponse(buffer, probe);
        if (probe.success || probe.errorEnum != "INVALID_MASTERSERVER_TOKEN") break;
        if (!RefreshAtlasToken(usedToken, refreshReason)) break;
    }
    if (!got) {
        selfAuthResult.failureReason = "Could not reach the Northstar master server";
    } else if (std::strlen(buffer) >= kCapacity - 1) {
        selfAuthResult.failureReason = "Authentication Failed";
    } else if (ParseSelfAuthResponse(buffer, selfAuthResult) && selfAuthResult.id != g_atlasUid) {
        selfAuthResult.success = false;
        selfAuthResult.failureReason = "Master server returned a different account";
    }
    delete[] buffer;
    selfAuthTokenRefused = !selfAuthResult.success && selfAuthResult.errorEnum == "INVALID_MASTERSERVER_TOKEN";
    if (selfAuthTokenRefused) {
        selfAuthResult.failureReason = "Your Northstar sign-in has run out, or was replaced: signing in to Northstar "
            "somewhere else with the same EA account replaces it" + AtlasTokenAdvice(refreshReason);
    }
    if (selfAuthResult.success) {
        if (g_addSelfAuthRecord)
            g_addSelfAuthRecord(std::strtoull(g_atlasUid, nullptr, 10), selfAuthResult.authToken, selfAuthResult.pdata);
        LogFormat("[NorthstarPS4] own server auth succeeded (%zu bytes of pdata)\n", selfAuthResult.pdata.size());
    } else {
        LogFormat("[NorthstarPS4] own server auth failed (status %d): %s; the lobby uses the local save\n", status,
            selfAuthResult.failureReason.c_str());
    }
    selfAuthState.store(kFetchReady, std::memory_order_release);
    return nullptr;
}

void PublishSelfAuthResult() {
    if (selfAuthState.load(std::memory_order_acquire) != kFetchReady) return;
    // The lobby starts either way, unless the token was refused; see above.
    authSucceeded = !selfAuthTokenRefused;
    authFailure = selfAuthResult.success ? std::string() : selfAuthResult.failureReason;
    selfAuthToken = selfAuthResult.success ? selfAuthResult.authToken : std::string();
    selfAuthResult = SelfAuthResponse{};
    selfAuthState.store(kFetchIdle, std::memory_order_release);
}

int TryLocalAuth(void*) {
    PublishSelfAuthResult();
    if (selfAuthState.load(std::memory_order_acquire) != kFetchIdle) return 0;
    selfAuthToken.clear();
    if (!AtlasIdentityReady()) {
        authSucceeded = false;
        authFailure = AtlasIdentityMessage();
        LogFormat("[NorthstarPS4] local server auth refused: %s\n", authFailure.c_str());
        return 0;
    }
    authSucceeded = false;
    authFailure = "Authentication Failed";
    int expected = kFetchIdle;
    if (!selfAuthState.compare_exchange_strong(expected, kFetchRequesting)) return 0;
    OrbisPthread thread{};
    if (!InitHttpTransport() || StartRuntimeThread(&thread, SelfAuthWorker, nullptr, "NSSelfAuth") != 0) {
        selfAuthResult = SelfAuthResponse{};
        selfAuthTokenRefused = false;
        selfAuthResult.failureReason = "Could not start the authentication request";
        selfAuthState.store(kFetchReady, std::memory_order_release);
        return 0;
    }
    scePthreadDetach(thread);
    return 0;
}

// Distinct from CompleteAuth: raising here would abort the script between the
// success branch and the `map mp_lobby` that follows it.
int CompleteLocalAuth(void* vm) {
    PublishSelfAuthResult();
    if (!authSucceeded) return Error(vm, AtlasIdentityMessage());
    // PC: "literally just set serverfilter". Cleared when there is no token,
    // so a token left over from a server join is not sent to the local server.
    if (g_serverFilterConVar) SetConVarString(g_serverFilterConVar, selfAuthToken.c_str());
    if (!selfAuthToken.empty()) EnsureConnectUid();
    LogFormat("[NorthstarPS4] local server auth completed (%s); lobby launch follows\n",
        selfAuthToken.empty() ? "local save" : "Atlas save");
    return 0;
}

int IsAuthenticating(void* vm) {
    PublishJoinResult();
    PublishSelfAuthResult();
    Boolean(vm, joinState.load(std::memory_order_acquire) != kFetchIdle ||
        selfAuthState.load(std::memory_order_acquire) != kFetchIdle);
    return 1;
}

// Calls a global UI script function taking one string, the same way
// DispatchLoadResult calls NSHandleLoadResult.
bool CallScriptWithString(void* vm, const char* name, const char* text) noexcept {
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(vm, 0x685cf0)(vm, name, &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(vm, 0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    String(vm, text);
    const int result = At<int (*)(void*, int, int, int)>(vm, 0x6876c0)(vm, 2, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

int CompleteAuth(void* vm) {
    PublishJoinResult();
    if (!pendingConnection.ready)
        return Error(vm, "Tried to connect to authed server before any pending connection info was available");
    PendingConnection connection = pendingConnection;
    pendingConnection = PendingConnection{};
    if (!g_serverFilterConVar || !SetConVarString(g_serverFilterConVar, connection.authToken.c_str()))
        return Error(vm, "Cannot set serverfilter for the connection");
    // The other half the server checks: the uid in the connect packet must be
    // the one Atlas issued this token for. See EnsureConnectUid.
    if (!EnsureConnectUid())
        return Error(vm, "Cannot set the connect uid (platform_user_id)");
    // Both parts were validated by ParseServerAuthResponse (dotted IPv4, port
    // 1-65535), so nothing but an address reaches the command line.
    char command[64];
    std::snprintf(command, sizeof(command), "connect %s:%d", connection.ip.c_str(), connection.port);
    if (!CallScriptWithString(vm, "NSPS4_ClientCommand", command))
        return Error(vm, "Cannot issue connect: NSPS4_ClientCommand is missing (Northstar.PS4 not loaded?)");
    LogFormat("[NorthstarPS4] connecting to %s:%d\n", connection.ip.c_str(), connection.port);
    return 0;
}
