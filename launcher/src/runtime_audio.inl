// Custom audio: mods replace sound events' samples with their own .wav files
// (PC: client/audio.cpp; the rules are in audio_override.h).
//
// Miles 10.0.10 is linked into client.prx on PS4 (mileswin64.dll on PC), and
// the two functions PC hooks have the same shape here:
//   - client+0x22450 plays an event: its second argument holds the event's name
//     at +0x30 (PC mileswin64+0x294C0, a2+0x30). It is reached from three call
//     sites (0x162c4, 0x2bd0f, 0x2cc35), which now go through AudioEventStub to
//     note the name first.
//   - client+0x99b0 is Miles' LoadSampleMetadata(sample, buffer, length, type)
//     (PC mileswin64+0xF110; the same "Unknown File Type" and "Bink Audio
//     decoder has not been registered." messages). Its four call sites (0x9fbf,
//     0xa40a, 0x133a1, 0x13d1c) go to AudioLoadSampleMetadata, which swaps the
//     buffer for the mod's sample and asks Miles to detect its type (64), as PC
//     does. The sample keeps its buffer at +0xe8 and its length at +0xf0 on both
//     platforms (the callers load them from there).
// shadPS4 can't give this module an executable trampoline, so the functions
// themselves are left alone and only their call sites are patched.
//
// Samples are read the first time they play and then kept for the session:
// Miles plays from the buffer it is given, so a buffer is never freed, not
// even when a reload replaces the overrides.

extern "C" {
__attribute__((used)) const char* volatile g_audioEventName = nullptr;
__attribute__((used)) std::uintptr_t g_audioEventTarget = 0;
}

// At a call to the event function: rdi/rsi hold its arguments, rax is free.
__attribute__((naked)) void AudioEventStub() {
    asm volatile(
        "movq 0x30(%rsi), %rax\n\t"
        "movq %rax, g_audioEventName(%rip)\n\t"
        "jmpq *g_audioEventTarget(%rip)\n\t");
}

namespace customaudio {

using LoadSampleMetadataFn = int (*)(void* sample, void* buffer, unsigned length, int type);
LoadSampleMetadataFn g_loadSampleMetadata = nullptr;
void* g_printPlayedSounds = nullptr;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};
audio::Registry* g_registry = nullptr; // replaced, never freed, on reload
struct Sample { std::uint8_t* data; unsigned size; };
// By path, never freed. A pointer: this module runs no static constructors, and
// a zero-filled unordered_map aborts on its first insert (max load factor 0).
std::unordered_map<std::string, Sample>* g_samples = nullptr;
unsigned g_random = 0x2545f491;

void ListWavFiles(const std::string& directory, std::vector<std::string>& out) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) return;
    std::vector<std::string> subdirectories;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = directory + "/" + name;
        if (IsDirectory(path.c_str())) subdirectories.push_back(path);
        else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".wav") == 0) out.push_back(path);
    }
    closedir(dir);
    std::sort(subdirectories.begin(), subdirectories.end());
    for (const auto& subdirectory : subdirectories) ListWavFiles(subdirectory, out);
}

// PC ModManager: every enabled mod's audio/*.json, in load order.
audio::Registry* BuildRegistry() noexcept {
    auto* registry = new audio::Registry();
    static ModDiscovery discovery;
    CollectModNames(discovery);
    for (std::int32_t i = 0; i < discovery.count; ++i) {
        const std::string audioDir = std::string(discovery.dirs[i]) + "/audio";
        DIR* dir = opendir(audioDir.c_str());
        if (!dir) continue;
        std::vector<std::string> definitions;
        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) definitions.push_back(name);
        }
        closedir(dir);
        std::sort(definitions.begin(), definitions.end());
        for (const auto& name : definitions) {
            const std::string path = audioDir + "/" + name;
            static char json[kModJsonBufferSize];
            std::size_t size = 0;
            auto entry = std::make_unique<audio::Override>();
            entry->mod = discovery.names[i];
            entry->definitionPath = path;
            std::string error;
            if (!ReadFileIntoBuffer(path.c_str(), json, sizeof(json), size) ||
                !audio::ParseDefinition(json, entry->definition, error)) {
                LogFormat("[NorthstarPS4] audio: failed reading audio override file %s: %s\n", path.c_str(),
                    error.empty() ? "unreadable" : error.c_str());
                continue;
            }
            const std::string samplesDir = path.substr(0, path.size() - 5);
            if (!IsDirectory(samplesDir.c_str())) {
                LogFormat("[NorthstarPS4] audio: failed reading audio override file %s: samples folder doesn't exist; "
                    "should be named the same as the definition file without JSON extension.\n", path.c_str());
                continue;
            }
            std::vector<std::string> samples;
            ListWavFiles(samplesDir, samples);
            for (const auto& sample : samples) {
                if (registry->IsClaimed(audio::SampleEventFolder(sample))) {
                    LogFormat("[NorthstarPS4] audio: %s couldn't be loaded because %s event has already been overrided, "
                        "skipping.\n", sample.c_str(), audio::SampleEventFolder(sample).c_str());
                    continue;
                }
                entry->samples.push_back(sample);
            }
            if (entry->samples.empty())
                LogFormat("[NorthstarPS4] audio: override %s has no valid samples! Sounds will not play for this event.\n",
                    path.c_str());
            LogFormat("[NorthstarPS4] audio: loaded audio override file %s (%zu events, %zu regexes, %zu samples)\n",
                path.c_str(), entry->definition.eventIds.size(), entry->definition.eventRegexes.size(),
                entry->samples.size());
            for (const auto& line : registry->Add(std::move(entry)))
                LogFormat("[NorthstarPS4] audio: %s\n", line.c_str());
        }
    }
    return registry;
}

bool ReadSample(const std::string& path, Sample& out) noexcept {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    // Sized with lseek: fstat is a stub under shadPS4 (runtime_mod_download.inl).
    const off_t end = lseek(fd, 0, SEEK_END);
    if (end <= 0 || end > 256 * 1024 * 1024 || lseek(fd, 0, SEEK_SET) != 0) {
        close(fd);
        return false;
    }
    const auto length = static_cast<std::size_t>(end);
    auto* data = new (std::nothrow) std::uint8_t[length];
    std::size_t done = 0;
    while (data && done < length) {
        const ssize_t got = read(fd, data + done, length - done);
        if (got <= 0) break;
        done += static_cast<std::size_t>(got);
    }
    close(fd);
    if (!data || done != length) {
        delete[] data;
        return false;
    }
    out = {data, static_cast<unsigned>(done)};
    return true;
}

// The sample to play instead, or false to keep the event's own.
bool PickReplacement(const char* eventName, const std::uint8_t*& data, unsigned& size) noexcept {
    std::string path;
    {
        Lock lock;
        if (!g_registry || g_registry->Empty()) return false;
        audio::Override* entry = g_registry->Find(eventName);
        if (!entry || !audio::ShouldOverride(eventName, entry->definition.eventIds)) return false;
        g_random = g_random * 1103515245u + 12345u;
        const int index = audio::PickSample(*entry, g_random >> 8);
        if (index < 0) {
            data = audio::kEmptyWave;
            size = sizeof(audio::kEmptyWave);
            return true;
        }
        path = entry->samples[static_cast<std::size_t>(index)];
        auto cached = g_samples->find(path);
        if (cached != g_samples->end()) {
            data = cached->second.data;
            size = cached->second.size;
            return true;
        }
    }
    // Read outside the lock: Miles' other threads keep playing meanwhile.
    Sample sample{};
    if (!ReadSample(path, sample)) {
        LogFormat("[NorthstarPS4] audio: failed reading audio sample %s; using the original\n", path.c_str());
        return false;
    }
    Lock lock;
    auto inserted = g_samples->emplace(path, sample);
    if (!inserted.second) delete[] sample.data; // another thread read it first
    data = inserted.first->second.data;
    size = inserted.first->second.size;
    return true;
}

} // namespace customaudio

// PC h_LoadSampleMetadata.
int AudioLoadSampleMetadata(void* sample, void* buffer, unsigned length, int type) noexcept {
    using namespace customaudio;
    // Raw source: voice data only.
    if (type == 0) return g_loadSampleMetadata(sample, buffer, length, type);
    char eventName[256];
    const char* name = g_audioEventName;
    if (!name) return g_loadSampleMetadata(sample, buffer, length, type);
    std::size_t n = 0;
    while (n + 1 < sizeof(eventName) && name[n]) {
        eventName[n] = name[n];
        ++n;
    }
    eventName[n] = '\0';
    if (g_printPlayedSounds && *reinterpret_cast<const std::int32_t*>(static_cast<char*>(g_printPlayedSounds) +
                                   kConVarIntValueOffset) > 0)
        LogFormat("[NorthstarPS4] [AUDIO] Playing event %s\n", eventName);
    const std::uint8_t* data = nullptr;
    unsigned size = 0;
    if (!PickReplacement(eventName, data, size)) return g_loadSampleMetadata(sample, buffer, length, type);
    *reinterpret_cast<const void**>(static_cast<char*>(sample) + 0xe8) = data;
    *reinterpret_cast<unsigned*>(static_cast<char*>(sample) + 0xf0) = size;
    // 64: detect the type.
    const int result = g_loadSampleMetadata(sample, const_cast<std::uint8_t*>(data), size, 64);
    if (!result) LogFormat("[NorthstarPS4] audio: Miles could not read the replacement for %s\n", eventName);
    else if (g_printPlayedSounds && *reinterpret_cast<const std::int32_t*>(static_cast<char*>(g_printPlayedSounds) +
                                        kConVarIntValueOffset) > 0)
        LogFormat("[NorthstarPS4] [AUDIO] Replaced event %s (%u bytes)\n", eventName, size);
    return result;
}

// Called by ReloadModState: the new enabled set's overrides.
void ReloadAudioOverrides() noexcept {
    if (!customaudio::g_loadSampleMetadata) return;
    audio::Registry* registry = customaudio::BuildRegistry();
    customaudio::Lock lock;
    customaudio::g_registry = registry; // the old one stays allocated: a sound may still be picking from it
}

void RegisterAudioConVars() noexcept {
    alignas(16) static std::uint8_t storage[0x90]{};
    if (!g_modConVarCvar || !g_modConVarFindVar) return;
    customaudio::g_printPlayedSounds = g_modConVarFindVar(g_modConVarCvar, "ns_print_played_sounds");
    if (!customaudio::g_printPlayedSounds && g_modConVarConstructor) {
        g_modConVarConstructor(storage, "ns_print_played_sounds", "0", 0, "", nullptr);
        customaudio::g_printPlayedSounds = g_modConVarFindVar(g_modConVarCvar, "ns_print_played_sounds");
    }
}

void InstallCustomAudio(std::uintptr_t clientBase, std::size_t clientSpan) noexcept {
    constexpr std::uint8_t metadataBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x83, 0xec, 0x58, 0x41, 0x89, 0xcf, 0x41, 0x89, 0xd6, 0x49, 0x89, 0xf5, 0x49, 0x89, 0xfc, 0x41,
        0x83, 0xff, 0x40};
    constexpr std::uint8_t eventBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x81, 0xec, 0x98, 0x04, 0x00, 0x00, 0x48, 0x8d, 0x05, 0x55, 0x1a, 0xaa, 0x00, 0x80, 0x4e, 0x38,
        0x01};
    constexpr std::uintptr_t kLoadSampleMetadataVa = 0x99b0;
    constexpr std::uintptr_t kPlayEventVa = 0x22450;
    if (!ValidateEnginePreimage(clientBase, clientSpan, kLoadSampleMetadataVa, metadataBytes, sizeof(metadataBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, kPlayEventVa, eventBytes, sizeof(eventBytes))) {
        LogFormat("[NorthstarPS4] custom audio refused: client profile mismatch\n");
        return;
    }
    struct Site { std::uintptr_t va; std::uint8_t bytes[5]; };
    constexpr Site eventSites[] = {{0x162c4, {0xe8, 0x87, 0xc1, 0x00, 0x00}}, {0x2bd0f, {0xe8, 0x3c, 0x67, 0xff, 0xff}},
        {0x2cc35, {0xe8, 0x16, 0x58, 0xff, 0xff}}};
    constexpr Site metadataSites[] = {{0x9fbf, {0xe8, 0xec, 0xf9, 0xff, 0xff}}, {0xa40a, {0xe8, 0xa1, 0xf5, 0xff, 0xff}},
        {0x133a1, {0xe8, 0x0a, 0x66, 0xff, 0xff}}, {0x13d1c, {0xe8, 0x8f, 0x5c, 0xff, 0xff}}};
    for (const auto& site : eventSites)
        if (!ValidateEnginePreimage(clientBase, clientSpan, site.va, site.bytes, 5)) {
            LogFormat("[NorthstarPS4] custom audio refused: call site %lx differs\n", site.va);
            return;
        }
    for (const auto& site : metadataSites)
        if (!ValidateEnginePreimage(clientBase, clientSpan, site.va, site.bytes, 5)) {
            LogFormat("[NorthstarPS4] custom audio refused: call site %lx differs\n", site.va);
            return;
        }
    customaudio::g_loadSampleMetadata = reinterpret_cast<customaudio::LoadSampleMetadataFn>(clientBase + kLoadSampleMetadataVa);
    g_audioEventTarget = clientBase + kPlayEventVa;
    customaudio::g_samples = new std::unordered_map<std::string, customaudio::Sample>();
    customaudio::g_registry = customaudio::BuildRegistry();
    // The event name first, so a replacement never sees a stale one.
    for (const auto& site : eventSites)
        if (!PatchCallSite(site.va, site.bytes, reinterpret_cast<void*>(&AudioEventStub), "audio event")) return;
    for (const auto& site : metadataSites)
        if (!PatchCallSite(site.va, site.bytes, reinterpret_cast<void*>(&AudioLoadSampleMetadata), "audio sample"))
            return;
    LogFormat("[NorthstarPS4] custom audio installed (%zu override files)\n", customaudio::g_registry->Size());
}
