// Runtime mod.pdiff compilation, matching NorthstarLauncher's BuildPdef while
// retaining the PS4 929 filename and Northstar.PS4's PC-231-compatible base.

constexpr const char* kRuntimePdefLogical = "cfg/server/persistent_player_data_version_929.pdef";
constexpr const char* kRuntimePdefOutput = "/data/northstar_ps4/persistent_player_data_version_929.pdef";
constexpr std::size_t kRuntimePdefBufferLimit = 56781;
constexpr std::size_t kRuntimePdefFileLimit = 0xd000;
constexpr std::size_t kRuntimePdiffReadLimit = 1024 * 1024;

std::atomic_flag g_runtimePdefLock = ATOMIC_FLAG_INIT;
bool g_runtimePdefAttempted = false;
bool g_runtimePdefGenerated = false;
std::size_t g_runtimePdefGeneratedSize = 0;

bool RuntimePdefPath(const char* normalized) noexcept {
    return normalized && !std::strcmp(normalized, kRuntimePdefLogical);
}

bool ReadRuntimePdefText(const std::string& path, std::size_t limit, std::string& out) noexcept {
    out.clear();
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return false; }
    const long length = std::ftell(file);
    if (length < 0 || static_cast<std::size_t>(length) > limit || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file); return false;
    }
    out.resize(static_cast<std::size_t>(length));
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    if (!ok) out.clear();
    return ok;
}

bool WriteRuntimePdef(const std::string& text) noexcept {
    mkdir("/data/northstar_ps4", 0777);
    const char* temporary = "/data/northstar_ps4/persistent_player_data_version_929.pdef.tmp";
    FILE* file = std::fopen(temporary, "wb");
    if (!file) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (std::fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    return ok && std::rename(temporary, kRuntimePdefOutput) == 0;
}

bool BuildRuntimePdef() noexcept {
    const ModOverlay* overlay = CurrentModOverlay();
    if (!overlay) { LogFormat("[NorthstarPS4] pdef refused: mod overlay unavailable\n"); return false; }

    std::string basePath;
    for (std::size_t i = 0; i < overlay->dirs.size(); ++i) {
        std::string json;
        if (!ReadRuntimePdefText(overlay->dirs[i] + "/mod.json", kModJsonBufferSize, json)) continue;
        ModInfo info{};
        if (ParseModMetadata(json.c_str(), info) && !std::strcmp(info.name, "Northstar.PS4")) {
            basePath = overlay->roots[i] + "/" + kRuntimePdefLogical;
            break;
        }
    }
    std::string merged, error;
    if (basePath.empty() || !ReadRuntimePdefText(basePath, kRuntimePdefFileLimit, merged)) {
        LogFormat("[NorthstarPS4] pdef refused: Northstar.PS4 base is missing or too large\n");
        return false;
    }
    std::size_t dataBytes = 0;
    if (!PersistenceDefinitionSize(merged, dataBytes, error) ||
        dataBytes > kRuntimePdefBufferLimit || merged.size() > kRuntimePdefFileLimit) {
        LogFormat("[NorthstarPS4] pdef refused: invalid base: %s data=%zu file=%zu\n",
            error.c_str(), dataBytes, merged.size());
        return false;
    }

    std::size_t applied = 0;
    for (std::size_t i = 0; i < overlay->dirs.size(); ++i) {
        const std::string pdiffPath = overlay->dirs[i] + "/mod.pdiff";
        std::string pdiff;
        if (!ReadRuntimePdefText(pdiffPath, kRuntimePdiffReadLimit, pdiff)) continue;
        std::string json;
        ModInfo info{};
        if (!ReadRuntimePdefText(overlay->dirs[i] + "/mod.json", kModJsonBufferSize, json) ||
            !ParseModMetadata(json.c_str(), info)) {
            LogFormat("[NorthstarPS4] pdef refused: metadata unreadable for %s\n", pdiffPath.c_str());
            return false;
        }
        std::string candidate = merged;
        if (!ApplyPdiff(candidate, pdiff, info.name, error) ||
            !PersistenceDefinitionSize(candidate, dataBytes, error)) {
            LogFormat("[NorthstarPS4] pdef refused: mod=%s error=%s\n", info.name, error.c_str());
            return false;
        }
        if (dataBytes > kRuntimePdefBufferLimit || candidate.size() > kRuntimePdefFileLimit) {
            LogFormat("[NorthstarPS4] pdef refused: mod=%s exceeds limits data=%zu/%zu file=%zu/%zu\n",
                info.name, dataBytes, kRuntimePdefBufferLimit, candidate.size(), kRuntimePdefFileLimit);
            return false;
        }
        if (candidate != merged) ++applied;
        merged.swap(candidate);
    }
    if (!WriteRuntimePdef(merged)) {
        LogFormat("[NorthstarPS4] pdef refused: atomic output write failed\n");
        return false;
    }
    g_runtimePdefGeneratedSize = merged.size();
    LogFormat("[NorthstarPS4] pdef generated mods=%zu data=%zu file=%zu base=%s\n",
        applied, dataBytes, merged.size(), basePath.c_str());
    return true;
}

bool RuntimePdefReady() noexcept {
    while (g_runtimePdefLock.test_and_set(std::memory_order_acquire)) sceKernelUsleep(100);
    if (!g_runtimePdefAttempted) {
        g_runtimePdefAttempted = true;
        g_runtimePdefGenerated = BuildRuntimePdef();
    }
    const bool ready = g_runtimePdefGenerated;
    g_runtimePdefLock.clear(std::memory_order_release);
    return ready;
}

bool RuntimePdefSize(std::uint64_t& size) noexcept {
    if (!RuntimePdefReady()) return false;
    size = static_cast<std::uint64_t>(g_runtimePdefGeneratedSize);
    return size != 0;
}

void ResetRuntimePdef() noexcept {
    while (g_runtimePdefLock.test_and_set(std::memory_order_acquire)) sceKernelUsleep(100);
    g_runtimePdefAttempted = false;
    g_runtimePdefGenerated = false;
    g_runtimePdefGeneratedSize = 0;
    g_runtimePdefLock.clear(std::memory_order_release);
}
