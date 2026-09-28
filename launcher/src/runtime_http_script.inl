// Script HTTP requests: PC's scripts/scripthttprequesthandler.cpp.
//
// NS_InternalMakeHttpRequest (behind Northstar.CustomServers'
// sh_northstar_http_requests.gnut: NSHttpRequest, NSHttpGet, NSHttpPostBody...)
// starts a request on its own thread and returns a handle. The result reaches
// the calling VM later, as a call to NSHandleSuccessfulHttpRequest( handle,
// statusCode, body, headers ) or NSHandleFailedHttpRequest( handle, errorCode,
// errorMessage ), which PC queues with SquirrelManager::AsyncCall and runs from
// the host frame.
//
// Here the request goes through sceHttp, as the port's other HTTP does
// (runtime_http.inl); shadPS4 implements custom methods, request and response
// headers, bodies and timeouts. The result is queued for the context that made
// the request, and each VM runs its queue once a frame on its own thread:
// Northstar.PS4's ps4_async_calls.nut calls NSPS4_RunAsyncCalls() from a
// WaitFrame loop in the UI, CLIENT and SERVER VMs. The natives are registered
// once per context so each knows its queue without identifying the VM.
//
// Launch options, from ns_startup_args.txt as on PC:
//   -disablehttprequests  NSIsHttpEnabled() is false and requests return -1;
//   -allowlocalhttp       requests may go to private and loopback addresses;
//   -disablehttpssl       the certificate's host name is not checked
//                         (PC: CURLOPT_SSL_VERIFYHOST 0).
//
// Differences from PC's libcurl, all in failure details:
//   - errorCode/errorMessage for transport failures are curl's codes and
//     texts for the matching sceHttp error (resolve 6, connect 7, reply 8,
//     timeout 28, TLS 60, anything else 56);
//   - the private-network check resolves the host itself and sceHttp resolves
//     it again (PC pins the checked address with CURLOPT_RESOLVE);
//   - PC's address list has two typos (192.18/15 and 192.51.100/24 for the
//     benchmark and TEST-NET-2 ranges, 198.x); the actual ranges are used.
//   - the timeout (1-60 s) applies to each of resolve, connect, send and
//     receive rather than to the whole transfer.

namespace uiapi {

constexpr int kSceHttpHeaderOverwrite = 0;
constexpr int kSceHttpHeaderAdd = 1;
constexpr std::uint32_t kSceHttpsFlagCnCheck = 0x04;

struct ScriptHttpRequest {
    int context = 0;
    int handle = 0;
    int method = 0;
    std::string url;
    http::Parameters headers;
    http::Parameters query;
    std::string contentType;
    std::string body;
    int timeout = 60;
    std::string userAgent;
};

struct AsyncMessage {
    bool success = false;
    int handle = 0;
    int code = 0;
    std::string text;     // body, or the error message
    std::string headers;  // success only
};

std::atomic_flag g_asyncLock = ATOMIC_FLAG_INIT;
std::vector<AsyncMessage> g_asyncQueues[3];  // UI, CLIENT, SERVER
std::atomic<int> g_lastHttpHandle{0};
std::atomic<int> g_scriptHttpTemplate{-1};

int AsyncQueueIndex(int context) { return context == kCtxUi ? 0 : context == kCtxClient ? 1 : 2; }

void QueueAsyncMessage(int context, AsyncMessage message) {
    while (g_asyncLock.test_and_set(std::memory_order_acquire)) {}
    g_asyncQueues[AsyncQueueIndex(context)].push_back(std::move(message));
    g_asyncLock.clear(std::memory_order_release);
}

// PC reads these once, early, so a mod cannot change them later.
bool HttpDisabled() {
    static const bool disabled = StartupArgPresent("-disablehttprequests");
    return disabled;
}
bool LocalHttpAllowed() {
    static const bool allowed = StartupArgPresent("-allowlocalhttp");
    return allowed;
}
bool HttpSslHostCheckDisabled() {
    static const bool disabled = StartupArgPresent("-disablehttpssl");
    return disabled;
}

void QueueHttpFailure(const ScriptHttpRequest& request, int code, const char* message) {
    AsyncMessage failure;
    failure.handle = request.handle;
    failure.code = code;
    failure.text = message;
    QueueAsyncMessage(request.context, std::move(failure));
}

// PC's IsHttpDestinationHostAllowed: an IPv4 destination outside the private
// ranges. A host name is resolved first.
bool HttpDestinationAllowed(const http::UrlParts& parts) {
    if (parts.ipv6Literal) return false;
    std::uint8_t address[4];
    if (!http::ParseIPv4(parts.host, address)) {
        const OrbisNetId resolver = sceNetResolverCreate("NSHttpResolver", g_httpNetPool, 0);
        if (resolver < 0) return false;
        OrbisNetInAddr resolved{};
        const int result = sceNetResolverStartNtoa(resolver, parts.host.c_str(), &resolved, 10 * 1000 * 1000, 2, 0);
        sceNetResolverDestroy(resolver);
        if (result < 0) {
            LogFormat("[NorthstarPS4] http request: could not resolve %s (0x%x)\n", parts.host.c_str(),
                static_cast<unsigned>(result));
            return false;
        }
        std::memcpy(address, &resolved.s_addr, sizeof(address));  // network order
    }
    return !http::IsPrivateIPv4(address[0], address[1], address[2], address[3]);
}

void HttpErrorToCurl(int error, int& code, const char*& message) {
    const auto value = static_cast<std::uint32_t>(error);
    if ((value & 0xfffff000u) == 0x80436000u) {
        code = 6; message = "Couldn't resolve host name";
    } else if (value == 0x80431068u) {
        code = 28; message = "Timeout was reached";
    } else if (value >= 0x80431070u && value <= 0x8043107fu) {
        code = 60; message = "SSL peer certificate or SSH remote key was not OK";
    } else if (value == 0x80431061u) {
        code = 1; message = "Unsupported protocol";
    } else if (value == 0x80433060u) {
        code = 3; message = "URL using bad/illegal format or missing URL";
    } else if (value == 0x80431063u) {
        code = 7; message = "Couldn't connect to server";
    } else if (value == 0x80431064u) {
        code = 8; message = "Weird server reply";
    } else {
        code = 56; message = "Failure when receiving data from the peer";
    }
}

int ScriptHttpTemplate() {
    int current = g_scriptHttpTemplate.load(std::memory_order_acquire);
    if (current >= 0) return current;
    const int created = sceHttpCreateTemplate(g_httpCtx, g_httpUserAgent, kSceHttpVersion11, 1);
    if (created < 0) return created;
    if (HttpSslHostCheckDisabled()) sceHttpsDisableOption(created, kSceHttpsFlagCnCheck);
    int expected = -1;
    if (!g_scriptHttpTemplate.compare_exchange_strong(expected, created)) {
        sceHttpDeleteTemplate(created);
        return expected;
    }
    return created;
}

void RunScriptHttpRequest(ScriptHttpRequest& request) {
    static const char kPrivateRefused[] =
        "Cannot make HTTP requests to private network hosts without -allowlocalhttp. Check your console for more "
        "information.";
    const std::string url = http::WithDefaultScheme(request.url);
    http::UrlParts parts;
    const bool parsed = http::ParseUrl(url, parts);
    if (!LocalHttpAllowed() && (!parsed || !HttpDestinationAllowed(parts))) {
        LogFormat("[NorthstarPS4] HttpRequestHandler::MakeHttpRequest attempted to make a request to a private network. "
                  "This is only allowed when running the game with -allowlocalhttp.\n");
        QueueHttpFailure(request, 0, kPrivateRefused);
        return;
    }
    if (!parsed) {
        QueueHttpFailure(request, 3, "URL using bad/illegal format or missing URL");
        return;
    }
    if (parts.scheme != "http" && parts.scheme != "https") {
        QueueHttpFailure(request, 1, "Unsupported protocol");
        return;
    }

    const std::string full = http::BuildRequestUrl(url, request.method, request.body.empty(), request.query);
    const bool postLike = http::UsesPostOptions(request.method);
    const int templateId = ScriptHttpTemplate();
    if (templateId < 0) {
        QueueHttpFailure(request, 2, "Failed initialization");
        return;
    }
    const int connection = sceHttpCreateConnectionWithURL(templateId, full.c_str(), true);
    if (connection < 0) {
        int code; const char* message;
        HttpErrorToCurl(connection, code, message);
        LogFormat("[NorthstarPS4] http request %d: connection failed 0x%x\n", request.handle, static_cast<unsigned>(connection));
        QueueHttpFailure(request, code, message);
        return;
    }
    const int handle = sceHttpCreateRequestWithURL2(connection, http::MethodName(request.method), full.c_str(),
        postLike ? request.body.size() : 0);
    if (handle < 0) {
        int code; const char* message;
        HttpErrorToCurl(handle, code, message);
        LogFormat("[NorthstarPS4] http request %d: request failed 0x%x\n", request.handle, static_cast<unsigned>(handle));
        QueueHttpFailure(request, code, message);
        sceHttpDeleteConnection(connection);
        return;
    }

    const int seconds = request.timeout < 1 ? 1 : request.timeout > 60 ? 60 : request.timeout;
    const std::uint32_t usec = static_cast<std::uint32_t>(seconds) * 1000000u;
    sceHttpSetResolveTimeOut(handle, usec);
    sceHttpSetConnectTimeOut(handle, usec);
    sceHttpSetSendTimeOut(handle, usec);
    // Declared without parameters in this toolchain's Http.h.
    reinterpret_cast<int32_t (*)(int32_t, uint32_t)>(&sceHttpSetRecvTimeOut)(handle, usec);

    if (postLike && !request.body.empty())
        sceHttpAddRequestHeader(handle, "Content-Type", request.contentType.c_str(), kSceHttpHeaderOverwrite);
    for (const auto& header : request.headers)
        for (const auto& value : header.second)
            sceHttpAddRequestHeader(handle, header.first.c_str(), value.c_str(), kSceHttpHeaderAdd);
    if (!request.userAgent.empty())
        sceHttpAddRequestHeader(handle, "User-Agent", request.userAgent.c_str(), kSceHttpHeaderOverwrite);

    int result = 0;
    if (postLike) {
        sceHttpSetRequestContentLength(handle, request.body.size());
        result = sceHttpSendRequest(handle, request.body.empty() ? nullptr : request.body.data(), request.body.size());
    } else {
        result = sceHttpSendRequest(handle, nullptr, 0);
    }
    int status = 0;
    std::string body;
    std::string headers;
    if (result >= 0) result = sceHttpGetStatusCode(handle, &status);
    if (result >= 0) {
        char* raw = nullptr;
        std::size_t size = 0;
        if (sceHttpGetAllResponseHeaders(handle, &raw, &size) >= 0 && raw) headers.assign(raw, size);
        if (request.method != http::kHead) {
            static constexpr std::size_t kChunk = 16 * 1024;
            auto* chunk = new char[kChunk];
            for (;;) {
                const int read = sceHttpReadData(handle, chunk, kChunk);
                if (read < 0) { result = read; break; }
                if (read == 0) break;
                body.append(chunk, static_cast<std::size_t>(read));
            }
            delete[] chunk;
        }
    }
    sceHttpDeleteRequest(handle);
    sceHttpDeleteConnection(connection);

    if (result < 0) {
        int code; const char* message;
        HttpErrorToCurl(result, code, message);
        LogFormat("[NorthstarPS4] curl_easy_perform() failed with code %d, error: %s (sceHttp 0x%x)\n", code, message,
            static_cast<unsigned>(result));
        QueueHttpFailure(request, code, message);
        return;
    }
    AsyncMessage success;
    success.success = true;
    success.handle = request.handle;
    success.code = status;
    success.text = std::move(body);
    success.headers = std::move(headers);
    QueueAsyncMessage(request.context, std::move(success));
}

void* ScriptHttpWorker(void* argument) {
    auto* request = static_cast<ScriptHttpRequest*>(argument);
    RunScriptHttpRequest(*request);
    delete request;
    return nullptr;
}

// table<string, array<string> > from a native argument, in table order.
void ReadParameterTable(const Object& value, http::Parameters& out) {
    if (value.tag != kSqTable || !value.value) return;
    auto bytes = reinterpret_cast<char*>(value.value);
    auto nodes = *reinterpret_cast<char**>(bytes + 0x38);
    const auto count = *reinterpret_cast<std::int32_t*>(bytes + 0x40);
    for (std::int32_t i = 0; nodes && i < count; ++i) {
        const char* node = nodes + static_cast<std::size_t>(i) * 0x28;
        const auto& key = *reinterpret_cast<const Object*>(node + 0x10);
        const auto& entry = *reinterpret_cast<const Object*>(node);
        if (key.tag != kSqString || entry.tag != kSqArray) continue;
        std::vector<std::string> values;
        auto array = reinterpret_cast<char*>(entry.value);
        auto items = *reinterpret_cast<Object**>(array + 0x30);
        const auto used = *reinterpret_cast<std::int32_t*>(array + 0x38);
        for (std::int32_t j = 0; items && j < used; ++j)
            if (items[j].tag == kSqString) values.emplace_back(reinterpret_cast<const char*>(items[j].value + 0x30));
        out.emplace_back(reinterpret_cast<const char*>(key.value + 0x30), std::move(values));
    }
}

// int NS_InternalMakeHttpRequest( int method, string baseUrl, table headers,
// table queryParams, string contentType, string body, int timeout,
// string userAgent )
int MakeScriptHttpRequest(void* vm, int context) {
    if (HttpDisabled()) {
        LogFormat("[NorthstarPS4] NS_InternalMakeHttpRequest called while the game is running with "
                  "-disablehttprequests. Please check if requests are allowed using NSIsHttpEnabled() first.\n");
        Integer(vm, -1);
        return 1;
    }
    const char* url = TextArg(vm, 2);
    const char* contentType = TextArg(vm, 5);
    const char* body = TextArg(vm, 6);
    const char* userAgent = TextArg(vm, 8);
    if (Arg(vm, 1).tag != kSqInteger || !url || Arg(vm, 3).tag != kSqTable || Arg(vm, 4).tag != kSqTable ||
        !contentType || !body || Arg(vm, 7).tag != kSqInteger || !userAgent)
        return Error(vm, "NS_InternalMakeHttpRequest expects int method, string baseUrl, table headers, "
                         "table queryParams, string contentType, string body, int timeout, string userAgent");
    if (!InitHttpTransport()) {
        LogFormat("[NorthstarPS4] NS_InternalMakeHttpRequest: the HTTP transport is unavailable\n");
        Integer(vm, -1);
        return 1;
    }
    auto* request = new ScriptHttpRequest;
    request->context = context;
    request->handle = ++g_lastHttpHandle;
    request->method = static_cast<std::int32_t>(Arg(vm, 1).value);
    request->url = url;
    ReadParameterTable(Arg(vm, 3), request->headers);
    ReadParameterTable(Arg(vm, 4), request->query);
    request->contentType = contentType;
    request->body = body;
    request->timeout = static_cast<std::int32_t>(Arg(vm, 7).value);
    request->userAgent = userAgent;
    const int handle = request->handle;
    OrbisPthread thread{};
    if (scePthreadCreate(&thread, nullptr, ScriptHttpWorker, request, "NSHttpRequest") != 0) {
        LogFormat("[NorthstarPS4] NS_InternalMakeHttpRequest: could not start the request thread\n");
        delete request;
        Integer(vm, -1);
        return 1;
    }
    scePthreadDetach(thread);
    Integer(vm, handle);
    return 1;
}
int MakeScriptHttpRequestUi(void* vm) { return MakeScriptHttpRequest(vm, kCtxUi); }
int MakeScriptHttpRequestClient(void* vm) { return MakeScriptHttpRequest(vm, kCtxClient); }
int MakeScriptHttpRequestServer(void* vm) { return MakeScriptHttpRequest(vm, kCtxServer); }

int ScriptHttpEnabled(void* vm) { Boolean(vm, !HttpDisabled()); return 1; }
int ScriptLocalHttpAllowed(void* vm) { Boolean(vm, LocalHttpAllowed()); return 1; }

// Calls a global script function with an int, an int and one or two strings.
bool CallAsyncHandler(void* vm, const char* name, const AsyncMessage& message) {
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(0x685cf0)(vm, name, &function, nullptr) < 0) {
        LogFormat("[NorthstarPS4] ProcessMessageBuffer was unable to find function with name '%s'. Is it global?\n", name);
        return false;
    }
    auto push = At<void (*)(void*, std::uint64_t, void*)>(0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    Integer(vm, message.handle);
    Integer(vm, message.code);
    String(vm, message.text.c_str());
    int arguments = 4;
    if (message.success) {
        String(vm, message.headers.c_str());
        arguments = 5;
    }
    const int result = At<int (*)(void*, int, int, int)>(0x6876c0)(vm, arguments, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

// NSPS4_RunAsyncCalls(): runs the calls queued for this context, on its VM's
// thread (PC: SquirrelManager::ProcessMessageBuffer from the host frame).
int RunAsyncCalls(void* vm, int context) {
    std::vector<AsyncMessage> pending;
    while (g_asyncLock.test_and_set(std::memory_order_acquire)) {}
    pending.swap(g_asyncQueues[AsyncQueueIndex(context)]);
    g_asyncLock.clear(std::memory_order_release);
    for (const auto& message : pending)
        CallAsyncHandler(vm, message.success ? "NSHandleSuccessfulHttpRequest" : "NSHandleFailedHttpRequest", message);
    return 0;
}
int RunAsyncCallsUi(void* vm) { return RunAsyncCalls(vm, kCtxUi); }
int RunAsyncCallsClient(void* vm) { return RunAsyncCalls(vm, kCtxClient); }
int RunAsyncCallsServer(void* vm) { return RunAsyncCalls(vm, kCtxServer); }

} // namespace uiapi
