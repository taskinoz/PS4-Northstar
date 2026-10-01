// Mod auto-download: NSFetchVerifiedModsManifesto, NSIsModDownloadable,
// NSDownloadMod, NSGetModInstallState and NSCancelModDownload.
//
// Included inside `uiapi`. Mirrors NorthstarLauncher's ModDownloader
// (primedev/mods/autodownload/moddownloader.cpp); the verified-mods list,
// SHA-256, zip reading and inflate are in northstar_ps4/mod_download.h and
// mod_archive.h, host-tested in tests/mod_download.cpp.
//
// The server browser (menu_ns_serverbrowser.nut) fetches the verified list
// when a server needs a mod this install lacks, checks NSIsModDownloadable,
// then calls NSDownloadMod and polls NSGetModInstallState every frame until
// the state reaches DONE or an error (menu_ns_moddownload.nut). The mod is
// installed disabled, as on PC, and the join then enables it and calls
// NSReloadMods (runtime_mod_reload.inl).
//
// Differences from PC, all forced by the platform:
//   - Downloads go to /data/northstar_ps4/runtime/remote/mods (PC:
//     R2Northstar/runtime/remote/mods). /app0 is read-only on hardware.
//   - The archive is hashed while it downloads, so CHECKSUMING is brief.
//   - Under shadPS4, sceHttp buffers the whole response before the first
//     read, so download progress jumps from 0 to 100%; hardware streams.
//
// Threading follows the server list: a worker owns the transfer and publishes
// through atomics; the verified list is published as a new vector (the old
// one is leaked: it is a few KiB and replaced a handful of times at most).
// Catalog invalidation after an install happens on the UI thread, in
// NSGetModInstallState, because the catalog is UI-thread state.

std::atomic<int> modInstallState{kModManifestoFetching};  // PC's MOD_STATE{} default
std::atomic<std::uint32_t> modInstallProgress{0};
std::atomic<std::uint32_t> modInstallTotal{0};
std::atomic<std::uint32_t> modInstallRatioBits{0};
std::atomic<bool> modDownloadCancel{false};
std::atomic<bool> modWorkerBusy{false};
std::atomic<bool> modInstalledPending{false};
std::atomic<const std::vector<VerifiedMod>*> verifiedMods{nullptr};

void SetInstallProgress(std::uint64_t progress, std::uint64_t total) noexcept {
    modInstallProgress.store(static_cast<std::uint32_t>(progress), std::memory_order_relaxed);
    modInstallTotal.store(static_cast<std::uint32_t>(total), std::memory_order_relaxed);
    float ratio = total ? static_cast<float>(progress) * 100.0f / static_cast<float>(total) : 0.0f;
    ratio = static_cast<float>(static_cast<int>(ratio + 0.5f));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &ratio, sizeof(bits));
    modInstallRatioBits.store(bits, std::memory_order_relaxed);
}

bool StartModWorker(void* (*entry)(void*), void* argument, const char* name) noexcept {
    bool expected = false;
    if (!modWorkerBusy.compare_exchange_strong(expected, true)) return false;
    OrbisPthread thread{};
    if (!InitHttpTransport() || scePthreadCreate(&thread, nullptr, entry, argument, name) != 0) {
        modWorkerBusy.store(false);
        return false;
    }
    scePthreadDetach(thread);
    return true;
}

void* VerifiedModsWorker(void*) noexcept {
    constexpr std::size_t kCapacity = 1024 * 1024;
    auto* body = new char[kCapacity];
    int status = 0;
    const bool ok = HttpGet(kVerifiedModsUrl, body, kCapacity, status);
    auto* list = new std::vector<VerifiedMod>;
    if (!ok || status != 200 || std::strlen(body) >= kCapacity - 1) {
        LogFormat("[NorthstarPS4] verified mods fetch failed ok=%d status=%d\n", ok ? 1 : 0, status);
    } else if (!ParseVerifiedMods(body, *list)) {
        LogFormat("[NorthstarPS4] verified mods list format is unrecognized, not loading it\n");
        list->clear();
    } else {
        LogFormat("[NorthstarPS4] verified mods list loaded: %zu mod(s)\n", list->size());
    }
    delete[] body;
    verifiedMods.store(list, std::memory_order_release);
    // PC reports DONE whether or not the fetch worked; the browser then finds
    // the mod unverified and says so.
    modInstallState.store(kModDone, std::memory_order_release);
    modWorkerBusy.store(false, std::memory_order_release);
    return nullptr;
}

int FetchVerifiedMods(void*) {
    modInstallState.store(kModManifestoFetching, std::memory_order_release);
    if (!StartModWorker(VerifiedModsWorker, nullptr, "NSVerifiedMods")) {
        LogFormat("[NorthstarPS4] verified mods fetch not started: another transfer is running\n");
        modInstallState.store(kModDone, std::memory_order_release);
    }
    return 0;
}

const VerifiedModVersion* FindVerified(const char* name, const char* version) {
    const auto* list = verifiedMods.load(std::memory_order_acquire);
    return list && name && version ? FindVerifiedModVersion(*list, name, version) : nullptr;
}

int IsModDownloadable(void* vm) {
    Boolean(vm, FindVerified(TextArg(vm, 1), TextArg(vm, 2)) != nullptr);
    return 1;
}

// Removes a directory tree under the remote mods root, and nothing else.
void RemoveModTree(const std::string& path) noexcept {
    if (path.compare(0, std::strlen(kRemoteModsRoot) + 1, std::string(kRemoteModsRoot) + "/") != 0) return;
    if (DIR* dir = opendir(path.c_str())) {
        while (dirent* entry = readdir(dir)) {
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            const std::string child = path + "/" + entry->d_name;
            struct stat info{};
            if (stat(child.c_str(), &info) != 0) continue;
            if (S_ISDIR(info.st_mode)) RemoveModTree(child);
            else unlink(child.c_str());
        }
        closedir(dir);
        rmdir(path.c_str());
    } else {
        unlink(path.c_str());
    }
}

struct DownloadJob {
    // The archive is extracted into `staging` and moved to `destination` only
    // when every entry is in, so a failed or cancelled download never touches
    // a copy that is already installed.
    std::string name, version, archive, destination, staging;
    VerifiedModVersion verified;
};

struct DownloadSink {
    int fd = -1;
    Sha256 sha;
    std::uint64_t written = 0;
    const std::uint64_t* length = nullptr;  // HttpStream sets it before the first chunk
    bool writeFailed = false;
};

bool DownloadChunk(const void* data, std::size_t size, void* user) {
    auto& sink = *static_cast<DownloadSink*>(user);
    if (write(sink.fd, data, size) != static_cast<ssize_t>(size)) { sink.writeFailed = true; return false; }
    sink.sha.Update(data, size);
    sink.written += size;
    const std::uint64_t total = sink.length && *sink.length > sink.written ? *sink.length : sink.written;
    SetInstallProgress(sink.written, total);
    return true;
}

// Compressed bytes of one zip entry, read through a small buffer.
struct EntrySource {
    int fd;
    std::uint64_t remaining;
    std::uint8_t buffer[16 * 1024];
    std::size_t used = 0, at = 0;
    int Next() {
        if (at == used) {
            if (!remaining) return -1;
            const std::size_t want = remaining < sizeof(buffer) ? static_cast<std::size_t>(remaining) : sizeof(buffer);
            const ssize_t got = read(fd, buffer, want);
            if (got <= 0) return -1;
            used = static_cast<std::size_t>(got);
            at = 0;
            remaining -= used;
        }
        return buffer[at++];
    }
};

struct EntrySink {
    int fd;
    std::uint64_t* progress;
    std::uint64_t total;
    bool Write(const std::uint8_t* data, std::size_t size) {
        if (modDownloadCancel.load(std::memory_order_relaxed)) return false;
        if (write(fd, data, size) != static_cast<ssize_t>(size)) return false;
        *progress += size;
        SetInstallProgress(*progress, total);
        return true;
    }
};

bool ReadAt(int fd, std::uint64_t offset, void* out, std::size_t size) noexcept {
    if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) return false;
    auto* bytes = static_cast<std::uint8_t*>(out);
    while (size) {
        const ssize_t got = read(fd, bytes, size);
        if (got <= 0) return false;
        bytes += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
}

// PC's ExtractMod. Returns the final state.
int ExtractModArchive(const DownloadJob& job) noexcept {
    const int fd = open(job.archive.c_str(), O_RDONLY);
    if (fd < 0) return kModFailedReadingArchive;
    std::vector<ZipEntry> entries;
    ZipDirectoryLocation location;
    // Sized with lseek: fstat is an unimplemented stub under shadPS4 (it
    // returns 0 and leaves the struct untouched).
    const off_t end = lseek(fd, 0, SEEK_END);
    bool readable = end > 0;
    if (readable) {
        const std::uint64_t size = static_cast<std::uint64_t>(end);
        const std::size_t tail = size < 65557 ? static_cast<std::size_t>(size) : 65557;
        std::vector<std::uint8_t> bytes(tail);
        readable = ReadAt(fd, size - tail, bytes.data(), tail) &&
            FindZipDirectory(bytes.data(), tail, size, location) && location.size <= 16 * 1024 * 1024;
        if (readable) {
            bytes.resize(location.size);
            readable = ReadAt(fd, location.offset, bytes.data(), location.size) &&
                ParseZipDirectory(bytes.data(), location.size, location.entries, entries);
        }
    }
    std::string root;
    if (readable && job.verified.platform == VerifiedModPlatform::ModWorkshop && !FindModWorkshopRoot(entries, root))
        readable = false;
    if (!readable) {
        close(fd);
        LogFormat("[NorthstarPS4] mod archive unreadable: %s\n", job.archive.c_str());
        return kModFailedReadingArchive;
    }

    std::uint64_t total = 0, progress = 0;
    for (const auto& entry : entries)
        if (!ZipInstallPath(entry, root).empty()) total += entry.size;
    modInstallState.store(kModExtracting, std::memory_order_release);
    SetInstallProgress(0, total);

    RemoveModTree(job.staging);
    if (!MakeDirectories(job.staging.c_str())) { close(fd); return kModFailedWritingToDisk; }
    int result = kModDone;
    for (const auto& entry : entries) {
        if (modDownloadCancel.load(std::memory_order_relaxed)) { result = kModAborted; break; }
        const std::string relative = ZipInstallPath(entry, root);
        if (relative.empty()) continue;
        if ((entry.flags & 1) || (entry.method != 0 && entry.method != 8)) {
            LogFormat("[NorthstarPS4] mod archive entry not supported (method %u): %s\n", entry.method, entry.name.c_str());
            result = kModFailedReadingArchive;
            break;
        }
        std::uint8_t header[30];
        std::uint64_t dataOffset = 0;
        if (!ReadAt(fd, entry.localHeaderOffset, header, sizeof(header)) || !ZipDataOffset(header, entry, dataOffset) ||
            lseek(fd, static_cast<off_t>(dataOffset), SEEK_SET) < 0) {
            result = kModFailedReadingArchive;
            break;
        }
        const std::string target = job.staging + "/" + relative;
        const std::size_t slash = target.find_last_of('/');
        if (!MakeDirectories(target.substr(0, slash).c_str())) { result = kModFailedWritingToDisk; break; }
        const int out = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out < 0) { result = kModFailedWritingToDisk; break; }
        auto* source = new EntrySource{fd, entry.compressedSize};
        EntrySink sink{out, &progress, total};
        std::uint32_t crc = 0;
        std::uint64_t written = 0;
        bool entryOk = true;
        if (entry.method == 0) {
            constexpr std::size_t kChunk = 16 * 1024;
            auto* chunk = new std::uint8_t[kChunk];
            int byte = 0;
            std::size_t used = 0;
            while (entryOk && (byte = source->Next()) >= 0) {
                chunk[used++] = static_cast<std::uint8_t>(byte);
                if (used == kChunk) {
                    crc = Crc32Update(crc, chunk, used);
                    entryOk = sink.Write(chunk, used);
                    written += used;
                    used = 0;
                }
            }
            if (entryOk && used) {
                crc = Crc32Update(crc, chunk, used);
                entryOk = sink.Write(chunk, used);
                written += used;
            }
            delete[] chunk;
        } else {
            auto* inflater = new Inflater<EntrySource, EntrySink>(*source, sink);
            entryOk = inflater->Run() == InflateStatus::Ok;
            crc = inflater->Crc();
            written = inflater->Written();
            delete inflater;
        }
        delete source;
        if (close(out) != 0) entryOk = false;
        if (modDownloadCancel.load(std::memory_order_relaxed)) { result = kModAborted; break; }
        if (!entryOk || written != entry.size || crc != entry.crc) {
            LogFormat("[NorthstarPS4] mod archive entry failed: %s (%llu of %u bytes, crc %08x/%08x)\n",
                entry.name.c_str(), static_cast<unsigned long long>(written), entry.size, crc, entry.crc);
            result = kModFailedReadingArchive;
            break;
        }
    }
    close(fd);
    return result;
}

// Moves the extracted mod into place. An existing copy is moved aside first
// and restored if the move fails; it is deleted only once the new one is in.
bool CommitModInstall(const DownloadJob& job) noexcept {
    const std::string replaced = std::string(kRemoteModsRoot) + "/.replaced";
    RemoveModTree(replaced);
    struct stat info{};
    const bool existed = stat(job.destination.c_str(), &info) == 0;
    if (existed && rename(job.destination.c_str(), replaced.c_str()) != 0) {
        LogFormat("[NorthstarPS4] mod install: could not move the existing copy aside: %s\n", job.destination.c_str());
        return false;
    }
    if (rename(job.staging.c_str(), job.destination.c_str()) != 0) {
        LogFormat("[NorthstarPS4] mod install: could not move the new copy into place: %s\n", job.destination.c_str());
        if (existed) rename(replaced.c_str(), job.destination.c_str());
        return false;
    }
    if (existed) RemoveModTree(replaced);
    return true;
}

void* ModDownloadWorker(void* argument) noexcept {
    auto* job = static_cast<DownloadJob*>(argument);
    int result = kModFailed;
    const std::string& temporary = job->archive;
    LogFormat("[NorthstarPS4] downloading mod %s %s from %s\n", job->name.c_str(), job->version.c_str(),
        job->verified.downloadLink.c_str());
    if (!MakeDirectories(kRemoteModsRoot)) {
        result = kModFailedWritingToDisk;
    } else {
        DownloadSink sink;
        sink.fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (sink.fd < 0) {
            result = kModFailedWritingToDisk;
        } else {
            std::uint64_t length = 0;
            int status = 0;
            sink.length = &length;
            const bool fetched = HttpStream(job->verified.downloadLink.c_str(), DownloadChunk, &sink, length,
                status, &modDownloadCancel);
            if (close(sink.fd) != 0) sink.writeFailed = true;
            if (modDownloadCancel.load()) {
                result = kModAborted;
            } else if (sink.writeFailed) {
                result = kModFailedWritingToDisk;
            } else if (!fetched || status != 200 || (length && sink.written != length)) {
                LogFormat("[NorthstarPS4] mod download failed ok=%d status=%d bytes=%llu of %llu\n", fetched ? 1 : 0,
                    status, static_cast<unsigned long long>(sink.written), static_cast<unsigned long long>(length));
                result = kModFetchingFailed;
            } else {
                modInstallState.store(kModChecksuming, std::memory_order_release);
                const std::string hash = sink.sha.FinishHex();
                if (hash != job->verified.checksum) {
                    LogFormat("[NorthstarPS4] mod archive checksum mismatch: %s, expected %s\n", hash.c_str(),
                        job->verified.checksum.c_str());
                    result = kModCorrupted;
                } else {
                    result = ExtractModArchive(*job);
                }
            }
        }
    }
    unlink(temporary.c_str());
    if (result == kModDone && !CommitModInstall(*job)) result = kModFailedWritingToDisk;
    if (result != kModDone) RemoveModTree(job->staging);
    else modInstalledPending.store(true, std::memory_order_release);
    LogFormat("[NorthstarPS4] mod download %s %s finished: state %d%s\n", job->name.c_str(), job->version.c_str(),
        result, result == kModDone ? (" -> " + job->destination).c_str() : "");
    // A cancel already reported ABORTED; keep it rather than a later state.
    if (!(modDownloadCancel.load() && result != kModDone)) modInstallState.store(result, std::memory_order_release);
    delete job;
    modWorkerBusy.store(false, std::memory_order_release);
    return nullptr;
}

int DownloadMod(void* vm) {
    const char* name = TextArg(vm, 1);
    const char* version = TextArg(vm, 2);
    const VerifiedModVersion* verified = FindVerified(name, version);
    if (!verified) {
        LogFormat("[NorthstarPS4] tried to download a mod that is not verified, aborting\n");
        modInstallState.store(kModAborted, std::memory_order_release);
        return 0;
    }
    if (verified->platform == VerifiedModPlatform::Unknown) {
        modInstallState.store(kModUnknownPlatform, std::memory_order_release);
        return 0;
    }
    const std::string folder = ModInstallFolder(ModArchiveName(verified->downloadLink), version, verified->platform);
    const std::string destination = std::string(kRemoteModsRoot) + "/" + folder;
    if (folder.empty() || destination.size() >= kModDirCapacity - 64) {
        LogFormat("[NorthstarPS4] mod download refused: no usable folder name for %s\n", verified->downloadLink.c_str());
        modInstallState.store(kModFailedWritingToDisk, std::memory_order_release);
        return 0;
    }
    // Beside the installs; the leading dot keeps discovery from treating it as a mod.
    auto* job = new DownloadJob{name, version, std::string(kRemoteModsRoot) + "/.download.zip", destination,
        std::string(kRemoteModsRoot) + "/.staging", *verified};
    modDownloadCancel.store(false);
    SetInstallProgress(0, 0);
    modInstallState.store(kModDownloading, std::memory_order_release);
    if (!StartModWorker(ModDownloadWorker, job, "NSModDownload")) {
        delete job;
        LogFormat("[NorthstarPS4] mod download not started: another transfer is running\n");
        modInstallState.store(kModFailed, std::memory_order_release);
    }
    return 0;
}

int CancelModDownload(void*) {
    // PC reports ABORTED at once; the worker notices between chunks and
    // removes what it wrote.
    modDownloadCancel.store(true);
    modInstallState.store(kModAborted, std::memory_order_release);
    return 0;
}

int GetModInstallState(void* vm) {
    if (modInstalledPending.exchange(false, std::memory_order_acq_rel)) {
        catalog.clear();
        catalogReady = false;
    }
    Struct(vm, 4);
    Integer(vm, modInstallState.load(std::memory_order_acquire)); Seal(vm, 0);
    Integer(vm, static_cast<int>(modInstallProgress.load(std::memory_order_relaxed))); Seal(vm, 1);
    Integer(vm, static_cast<int>(modInstallTotal.load(std::memory_order_relaxed))); Seal(vm, 2);
    PushPrimitive(vm, 0x5000004, modInstallRatioBits.load(std::memory_order_relaxed)); Seal(vm, 3);
    return 1;
}
