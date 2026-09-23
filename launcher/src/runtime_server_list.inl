// Northstar server browser: fetching and exposing Atlas's server list.
//
// Included inside `uiapi`, after the Squirrel push helpers. Mirrors
// NorthstarLauncher's MasterServerManager::RequestServerList and the
// NSGetGameServers / NSGetServerCount / NSIsRequestingServerList natives in
// primedev/scripts/client/scriptserverbrowser.cpp. Parsing and merging are in
// northstar_ps4/server_list.h, which is host-tested against a live response.
//
// **Threading.** The browser script calls NSRequestServerList and then spins on
// NSIsRequestingServerList with WaitFrame, so the fetch has to happen off the
// UI thread, and the "requesting" flag has to turn true inside the request
// call itself - PC sets m_bScriptRequestingServerList before spawning its
// thread for the same reason. Otherwise the wait loop sees false on its first
// frame and reads an empty list.
//
// No lock is needed. The worker owns `fetched` and `fetchOk` while
// `fetchState` is kFetchRequesting and hands them over with a release store of
// kFetchReady; only the UI thread touches `servers`, and it takes `fetched`
// over after an acquire load sees kFetchReady. Every server-list native calls
// PublishFetchedServers first, so the handover happens on whichever of them the
// script calls next.
//
// **Transport.** GET <master>/client/servers over the module's sceHttp
// transport. The live list is ~120 KB for ~90 servers; the buffer allows four
// times that. HttpGet reports a full buffer as success, so a response that
// fills it is rejected here as truncated rather than handed to the parser.

// PC's default for `ns_masterserver_hostname`. This port registers no such
// convar yet, and nothing else here talks to the master server by URL.
constexpr const char* kMasterServerUrl = "https://northstar.tf";
constexpr std::size_t kServerListBufferSize = 512 * 1024;

constexpr int kFetchIdle = 0, kFetchRequesting = 1, kFetchReady = 2;
std::atomic<int> fetchState{kFetchIdle};
std::vector<RemoteServer> fetched;   // worker-owned while requesting
bool fetchOk = false;                // worker-owned while requesting
std::vector<RemoteServer> servers;   // UI thread only
bool fetchAttempted = false;         // UI thread only
bool lastFetchOk = false;            // UI thread only
char listBuffer[kServerListBufferSize];

void* ServerListWorker(void*) noexcept {
    char url[256];
    std::snprintf(url, sizeof(url), "%s/client/servers", kMasterServerUrl);
    LogFormat("[NorthstarPS4] requesting server list from %s\n", kMasterServerUrl);

    fetched.clear();
    fetchOk = false;
    int status = 0;
    const bool got = HttpGet(url, listBuffer, sizeof(listBuffer), status);
    const std::size_t length = std::strlen(listBuffer);
    if (!got || status != 200) {
        LogFormat("[NorthstarPS4] server list request failed: transport=%d status=%d\n", got ? 1 : 0, status);
    } else if (length >= sizeof(listBuffer) - 1) {
        LogFormat("[NorthstarPS4] server list request failed: response exceeds %zu bytes\n", sizeof(listBuffer));
    } else {
        std::size_t skipped = 0;
        const ServerListResult result = ParseServerList(listBuffer, fetched, skipped);
        fetchOk = result == ServerListResult::Ok;
        if (!fetchOk) fetched.clear();
        LogFormat("[NorthstarPS4] server list %s: %zu servers, %zu malformed skipped, %zu bytes\n",
            ServerListResultText(result), fetched.size(), skipped, length);
    }
    fetchState.store(kFetchReady, std::memory_order_release);
    return nullptr;
}

// UI thread. Takes over a finished fetch.
void PublishFetchedServers() {
    if (fetchState.load(std::memory_order_acquire) != kFetchReady) return;
    fetchAttempted = true;
    lastFetchOk = fetchOk;
    if (fetchOk) MergeServerList(servers, std::move(fetched));
    fetched.clear();
    fetchState.store(kFetchIdle, std::memory_order_release);
}

int RequestServers(void*) {
    PublishFetchedServers();
    int expected = kFetchIdle;
    // Already fetching: PC's thread waits for the previous one to finish; the
    // outcome for the script is the same single refreshed list.
    if (!fetchState.compare_exchange_strong(expected, kFetchRequesting)) return 0;
    // The transport is initialised here rather than on the worker, so two
    // threads never race through sceHttpInit.
    OrbisPthread thread{};
    if (!InitHttpTransport() ||
        scePthreadCreate(&thread, nullptr, ServerListWorker, nullptr, "NSServerList") != 0) {
        LogFormat("[NorthstarPS4] server list request failed: could not start the request\n");
        fetchOk = false;
        fetched.clear();
        fetchState.store(kFetchReady, std::memory_order_release);
        return 0;
    }
    scePthreadDetach(thread);
    return 0;
}

int IsRequestingServers(void* vm) {
    PublishFetchedServers();
    Boolean(vm, fetchState.load(std::memory_order_acquire) != kFetchIdle);
    return 1;
}

// PC's m_bSuccessfullyConnected: the browser shows "couldn't connect" when this
// is false after a request. Before any request it keeps its previous meaning
// here, whether an Atlas identity has been imported.
int MasterServerReachable(void* vm) {
    PublishFetchedServers();
    Boolean(vm, fetchAttempted ? lastFetchOk : AtlasIdentityReady());
    return 1;
}

int ServerCount(void* vm) {
    PublishFetchedServers();
    Integer(vm, static_cast<int>(servers.size()));
    return 1;
}

int ClearServers(void*) {
    PublishFetchedServers();
    servers.clear();
    return 0;
}

// Slot order is ServerInfo's field order in cl_northstar_client_init.nut, the
// same order PC seals them in.
int GameServers(void* vm) {
    PublishFetchedServers();
    Array(vm);
    for (std::size_t i = 0; i < servers.size(); ++i) {
        const auto& s = servers[i];
        Struct(vm, 11);
        Integer(vm, static_cast<int>(i));  Seal(vm, 0);
        String(vm, s.id.c_str());          Seal(vm, 1);
        String(vm, s.name.c_str());        Seal(vm, 2);
        String(vm, s.description.c_str()); Seal(vm, 3);
        String(vm, s.map.c_str());         Seal(vm, 4);
        String(vm, s.playlist.c_str());    Seal(vm, 5);
        Integer(vm, s.playerCount);        Seal(vm, 6);
        Integer(vm, s.maxPlayers);         Seal(vm, 7);
        Boolean(vm, s.requiresPassword);   Seal(vm, 8);
        String(vm, s.region.c_str());      Seal(vm, 9);
        Array(vm);
        for (const auto& mod : s.requiredMods) {
            Struct(vm, 2);
            String(vm, mod.name.c_str());    Seal(vm, 0);
            String(vm, mod.version.c_str()); Seal(vm, 1);
            Append(vm);
        }
        Seal(vm, 10);
        Append(vm);
    }
    return 1;
}
