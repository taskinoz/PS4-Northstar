// HTTP transport, for talking to Atlas.
//
// Everything the master server needs - authenticating, the server browser,
// authorising a connection - is HTTP against `ns_masterserver_hostname`. This
// port has never had a transport, which is why those natives all return
// explicit unavailable states.
//
// The PS4 SDK's `sceHttp` is the way to get one. shadPS4 does implement part of
// it: the game itself reaches `sceHttpInit` twice at boot and gets back two
// contexts, and creates an epoll. What no captured log has ever shown is a
// request actually being *sent*, so whether `sceHttpSendRequest` works here is
// unproven, and that is what this probe answers before anything is built on it.
//
// The libraries are linked as toolchain stubs; shadPS4 provides the functions
// by HLE rather than loading a real module, which is why `libSceHttp.sprx`
// never appears in the module list even though the calls succeed.
//
// Init order is the SDK's: net, then a net memory pool, then SSL, then HTTP.
constexpr int kSceHttpMethodGet = 0;
constexpr int kSceHttpVersion11 = 2;
constexpr std::size_t kHttpNetPoolSize = 16 * 1024;
constexpr std::size_t kHttpSslPoolSize = 96 * 1024;
constexpr std::size_t kHttpPoolSize = 64 * 1024;
constexpr const char* kHttpUserAgent = "NorthstarPS4";
constexpr const char* kHttpProbeFile = "/data/northstar_ps4/http_probe.txt";

int g_httpNetPool = -1;
int g_httpSslCtx = -1;
int g_httpCtx = -1;
int g_httpTemplate = -1;
bool g_httpReady = false;

// One-time setup. Failure at any step leaves the transport unavailable rather
// than half-initialised, and every step's result is logged because which one
// fails is the whole answer when it does.
bool InitHttpTransport() noexcept {
    if (g_httpReady) return true;

    const int netResult = sceNetInit();
    // Already-initialised is not a failure: the game gets there first.
    LogFormat("[NorthstarPS4] http sceNetInit=0x%x\n", netResult);

    g_httpNetPool = sceNetPoolCreate("NorthstarPS4", static_cast<int>(kHttpNetPoolSize), 0);
    LogFormat("[NorthstarPS4] http sceNetPoolCreate=%d\n", g_httpNetPool);
    if (g_httpNetPool < 0) return false;

    g_httpSslCtx = sceSslInit(kHttpSslPoolSize);
    LogFormat("[NorthstarPS4] http sceSslInit=%d\n", g_httpSslCtx);
    if (g_httpSslCtx < 0) return false;

    g_httpCtx = sceHttpInit(g_httpNetPool, g_httpSslCtx, kHttpPoolSize);
    LogFormat("[NorthstarPS4] http sceHttpInit=%d\n", g_httpCtx);
    if (g_httpCtx < 0) return false;

    g_httpTemplate = sceHttpCreateTemplate(g_httpCtx, kHttpUserAgent, kSceHttpVersion11, 1);
    LogFormat("[NorthstarPS4] http sceHttpCreateTemplate=%d\n", g_httpTemplate);
    if (g_httpTemplate < 0) return false;

    g_httpReady = true;
    return true;
}

// A bounded GET. `out` always ends NUL-terminated; `status` is the HTTP status
// when the request completed at all.
bool HttpGet(const char* url, char* out, std::size_t capacity, int& status) noexcept {
    status = 0;
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    if (!InitHttpTransport() || !url) return false;

    const int connection = sceHttpCreateConnectionWithURL(g_httpTemplate, url, 1);
    if (connection < 0) {
        LogFormat("[NorthstarPS4] http connect failed 0x%x\n", connection);
        return false;
    }
    const int request = sceHttpCreateRequestWithURL(connection, kSceHttpMethodGet, url, 0);
    if (request < 0) {
        LogFormat("[NorthstarPS4] http request create failed 0x%x\n", request);
        sceHttpDeleteConnection(connection);
        return false;
    }

    bool ok = false;
    const int sent = sceHttpSendRequest(request, nullptr, 0);
    if (sent < 0) {
        LogFormat("[NorthstarPS4] http send failed 0x%x\n", sent);
    } else if (sceHttpGetStatusCode(request, &status) < 0) {
        LogFormat("[NorthstarPS4] http status unavailable\n");
    } else {
        std::size_t total = 0;
        for (;;) {
            const int read = sceHttpReadData(request, out + total,
                static_cast<std::uint32_t>(capacity - 1 - total));
            if (read <= 0) {
                ok = read == 0;
                break;
            }
            total += static_cast<std::size_t>(read);
            if (total >= capacity - 1) { ok = true; break; }
        }
        out[total] = '\0';
    }

    sceHttpDeleteRequest(request);
    sceHttpDeleteConnection(connection);
    return ok;
}

// Reads a URL from a file and fetches it once, so the transport can be pointed
// at a local server first and only then at the real master server, without
// rebuilding. Absent file, no probe.
void RunHttpProbe() noexcept {
    static bool done = false;
    if (done) return;
    static char url[512];
    std::size_t size = 0;
    if (!ReadFileIntoBuffer(kHttpProbeFile, url, sizeof(url), size)) return;
    done = true;
    // Trim whitespace and newlines a text editor will have left behind.
    while (size > 0 && (url[size - 1] == '\n' || url[size - 1] == '\r' ||
        url[size - 1] == ' ' || url[size - 1] == '\t')) url[--size] = '\0';
    if (size == 0) return;

    static char body[8192];
    int status = 0;
    const bool ok = HttpGet(url, body, sizeof(body), status);
    LogFormat("[NorthstarPS4] http probe url=%s ok=%d status=%d bytes=%zu\n",
        url, ok ? 1 : 0, status, std::strlen(body));
    if (body[0]) {
        // First line only: a server list is large and the point is whether a
        // response arrived at all.
        char preview[200]{};
        std::size_t n = 0;
        while (n < sizeof(preview) - 1 && body[n] && body[n] != '\n' && body[n] != '\r') {
            preview[n] = body[n];
            ++n;
        }
        preview[n] = '\0';
        LogFormat("[NorthstarPS4] http probe body: %s\n", preview);
    }
}
