// Compiled particles manifest.
//
// Mods extend `mod/particles/particles_manifest.txt`. Serving only the normal
// overlay winner drops the retail entries and every lower-priority mod, so PC
// Northstar extracts each root body and writes one combined manifest. Do the
// same in writable storage and route every filesystem entry point to it.
constexpr const char* kParticleManifestRelative = "particles/particles_manifest.txt";
constexpr const char* kParticleManifestOutput = "/data/northstar_ps4/particles/particles_manifest.txt";
constexpr std::size_t kMaxParticleManifestSize = 2 * 1024 * 1024;
bool g_particleManifestGenerated = false;

bool ParticleManifestPath(const char* normalized) noexcept {
    if (!normalized) return false;
    const char* expected = kParticleManifestRelative;
    while (*normalized && *expected) {
        if (std::tolower(static_cast<unsigned char>(*normalized)) !=
            std::tolower(static_cast<unsigned char>(*expected))) return false;
        ++normalized;
        ++expected;
    }
    return !*normalized && !*expected;
}

bool ReadOriginalParticleManifest(void* self, std::string& out) noexcept {
    void* handle = g_originalFsOpenEx(self, kParticleManifestRelative, "rb", 0, "GAME", nullptr);
    if (!handle) return false;
    char chunk[4096];
    int count = 0;
    do {
        count = g_originalFsRead(reinterpret_cast<char*>(self) + 8, chunk, sizeof(chunk), handle);
        if (count > 0) out.append(chunk, count);
    } while (count == static_cast<int>(sizeof(chunk)) && out.size() < kMaxParticleManifestSize);
    g_originalFsClose(reinterpret_cast<char*>(self) + 8, handle);
    return count >= 0 && !out.empty() && out.size() < kMaxParticleManifestSize;
}

bool BuildParticlesManifest(void* self) noexcept {
    std::string original, originalBody, error;
    if (!ReadOriginalParticleManifest(self, original) ||
        !northstar::ps4::mods::ExtractParticleManifestBody(original, originalBody, error)) {
        LogFormat("[NorthstarPS4] particles manifest base parse failed: %s\n", error.c_str());
        return false;
    }

    std::vector<std::pair<std::string, std::string>> bodies;
    const ModOverlay* overlay = CurrentModOverlay();
    for (std::size_t i = 0; overlay && i < overlay->roots.size(); ++i) {
        char path[512];
        const int n = std::snprintf(path, sizeof(path), "%s/%s",
            overlay->roots[i].c_str(), kParticleManifestRelative);
        if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(path)) continue;
        std::string text, body;
        if (!ReadModKeyValues(path, text)) continue;
        if (!northstar::ps4::mods::ExtractParticleManifestBody(text, body, error)) {
            LogFormat("[NorthstarPS4] particles manifest ignored %s: %s\n", path, error.c_str());
            continue;
        }
        bodies.emplace_back(overlay->dirs[i], std::move(body));
        LogFormat("[NorthstarPS4] particles manifest merged: %s\n", path);
    }

    const std::string output = northstar::ps4::mods::BuildParticleManifest(originalBody, bodies);
    if (output.size() >= kMaxParticleManifestSize || !MakeKeyValueDirectories(kParticleManifestOutput)) {
        LogFormat("[NorthstarPS4] particles manifest output refused: %zu bytes\n", output.size());
        return false;
    }
    FILE* file = std::fopen(kParticleManifestOutput, "wb");
    if (!file) return false;
    bool ok = std::fwrite(output.data(), 1, output.size(), file) == output.size();
    if (std::fclose(file) != 0) ok = false;
    LogFormat("[NorthstarPS4] particles manifest built mods=%zu bytes=%zu success=%d\n",
        bodies.size(), output.size(), ok ? 1 : 0);
    return ok;
}

bool ParticleManifestReady(void* self) noexcept {
    if (!g_particleManifestGenerated) g_particleManifestGenerated = BuildParticlesManifest(self);
    return g_particleManifestGenerated;
}

bool ParticleManifestSize(void* self, std::uint64_t& size) noexcept {
    if (!ParticleManifestReady(self)) return false;
    struct stat info{};
    if (stat(kParticleManifestOutput, &info) != 0) return false;
    size = static_cast<std::uint64_t>(info.st_size);
    return true;
}
