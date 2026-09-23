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

std::atomic<int> joinState{kFetchIdle};
std::string joinServerId, joinPassword;  // set before the worker starts
ServerAuthResponse joinResult;           // worker-owned while requesting
char joinBuffer[8 * 1024];

// UI thread only. The address and token from the last successful auth, spent
// by NSConnectToAuthedServer.
struct PendingConnection { bool ready = false; std::string ip, authToken; int port = 0; };
PendingConnection pendingConnection;

void* ServerJoinWorker(void*) noexcept {
    const std::string url = std::string(kMasterServerUrl) + "/client/auth_with_server?id=" +
        PercentEncode(g_atlasUid) + "&playerToken=" + PercentEncode(g_atlasToken) +
        "&server=" + PercentEncode(joinServerId) + "&password=" + PercentEncode(joinPassword);
    LogFormat("[NorthstarPS4] authenticating with server %s\n", joinServerId.c_str());

    joinResult = ServerAuthResponse{};
    int status = 0;
    const bool got = HttpPost(url.c_str(), joinBuffer, sizeof(joinBuffer), status);
    if (!got) {
        joinResult.failureReason = "Could not reach the Northstar master server";
    } else if (std::strlen(joinBuffer) >= sizeof(joinBuffer) - 1) {
        joinResult.failureReason = "Authentication Failed";
        LogFormat("[NorthstarPS4] server auth response exceeds %zu bytes\n", sizeof(joinBuffer));
    } else {
        // Parsed whatever the status: Atlas puts the reason for a 4xx/5xx in
        // the body.
        ParseServerAuthResponse(joinBuffer, joinResult);
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
        scePthreadCreate(&thread, nullptr, ServerJoinWorker, nullptr, "NSServerJoin") != 0) {
        joinResult = ServerAuthResponse{};
        joinResult.failureReason = "Could not start the authentication request";
        joinState.store(kFetchReady, std::memory_order_release);
        return 0;
    }
    scePthreadDetach(thread);
    return 0;
}

int IsAuthenticating(void* vm) {
    PublishJoinResult();
    Boolean(vm, joinState.load(std::memory_order_acquire) != kFetchIdle);
    return 1;
}

// Calls a global UI script function taking one string, the same way
// DispatchLoadResult calls NSHandleLoadResult.
bool CallScriptWithString(void* vm, const char* name, const char* text) noexcept {
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(0x685cf0)(vm, name, &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    String(vm, text);
    const int result = At<int (*)(void*, int, int, int)>(0x6876c0)(vm, 2, 0, 1);
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
    // Both parts were validated by ParseServerAuthResponse (dotted IPv4, port
    // 1-65535), so nothing but an address reaches the command line.
    char command[64];
    std::snprintf(command, sizeof(command), "connect %s:%d", connection.ip.c_str(), connection.port);
    if (!CallScriptWithString(vm, "NSPS4_ClientCommand", command))
        return Error(vm, "Cannot issue connect: NSPS4_ClientCommand is missing (Northstar.PS4 not loaded?)");
    LogFormat("[NorthstarPS4] connecting to %s:%d\n", connection.ip.c_str(), connection.port);
    return 0;
}
