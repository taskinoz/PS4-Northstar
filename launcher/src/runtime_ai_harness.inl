// UI-only local development mailbox. No network listener or arbitrary file paths.
// Included within uiapi after the catalog and script marshalling helpers.
bool HarnessAllowed(void* vm) {
    char folder[64]{};
    if (!CallingModFolder(vm, 0, folder, sizeof(folder)) || std::strcmp(folder, "AI.Harness")) return false;
    LoadCatalog();
    for (const auto& mod : catalog)
        if (mod.enabled && !std::strcmp(mod.info.name, "AI.Harness")) return true;
    return false;
}
constexpr const char* kHarnessRequest = "/data/northstar_ps4/ai_harness/request.json";
constexpr const char* kHarnessClaim = "/data/northstar_ps4/ai_harness/claimed.json";
int HarnessRead(void* vm) {
    if (!HarnessAllowed(vm)) return Error(vm, "AI.Harness must be enabled and own the calling script");
    // Claim before execution; a crash never silently replays a command.
    if (std::rename(kHarnessRequest, kHarnessClaim) != 0) { String(vm, ""); return 1; }
    FILE* f = std::fopen(kHarnessClaim, "rb");
    if (!f) return Error(vm, "Cannot read claimed harness request");
    char data[16385]{};
    const auto count = std::fread(data, 1, sizeof(data), f);
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    std::remove(kHarnessClaim);
    if (failed || count >= sizeof(data) || std::memchr(data, 0, count)) return Error(vm, "Invalid or oversized harness request");
    String(vm, data); return 1;
}
int HarnessReply(void* vm) {
    if (!HarnessAllowed(vm)) return Error(vm, "AI.Harness access denied");
    const char* data = TextArg(vm, 1);
    if (!data || std::strlen(data) > 16384) return Error(vm, "Invalid harness reply");
    constexpr const char* temp = "/data/northstar_ps4/ai_harness/response.tmp";
    constexpr const char* path = "/data/northstar_ps4/ai_harness/response.json";
    FILE* f = std::fopen(temp, "wb");
    if (!f) return Error(vm, "Cannot create harness reply; create the host mailbox directory first");
    bool ok = std::fwrite(data, 1, std::strlen(data), f) == std::strlen(data);
    if (std::fflush(f) != 0) ok = false;
    if (std::fclose(f) != 0) ok = false;
    if (!ok || std::rename(temp, path) != 0) return Error(vm, "Cannot publish harness reply");
    return 0;
}

int HarnessField(void* vm) {
    if (!HarnessAllowed(vm)) return Error(vm, "AI.Harness access denied");
    const char* json = TextArg(vm, 1);
    const char* key = TextArg(vm, 2);
    char value[16385]{};
    const char* member = json && key ? JsonFindMember(json, key) : nullptr;
    if (!member || !JsonExtractString(member, value, sizeof(value))) return Error(vm, "Missing or invalid harness string field");
    String(vm, value); return 1;
}
