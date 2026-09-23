// Server-side persistence availability for a locally hosted lobby.
//
// With the lobby finally reaching script, the first thing it does is write a
// persistent var, and the SERVER VM stops on:
//
//   SCRIPT ERROR: [SERVER] Persistent data not available for client #0.
//    -> player.SetPersistentVar( "privateMatchState", 0 ) // disable spectator
//   Lobby_OnClientConnectionStarted()        lobby/_lobby.gnut line [24]
//   CodeCallback_OnClientConnectionStarted() mp/_base_gametype_mp.gnut line [79]
//
// On PC this never happens, because NorthstarLauncher marks persistence ready
// natively before any script runs. `ServerAuthenticationManager::Authenticate-
// Player` sets `m_iPersistenceReady = ePersistenceReady::READY_INSECURE` when
// `ns_auth_allow_insecure` is set, with the comment *"actual placeholder
// persistent data is populated in script with InitPersistentData()"*, and
// Northstar.CustomServers calls `InitPersistentData( player )` from
// `CodeCallback_OnClientConnectionCompleted`. The ordering matters: the script
// above runs at connection *Started*, before that initialisation, so the flag
// has to already be set by the engine side.
//
// This port hooks no server authentication at all, so nothing ever sets it and
// the flag stays at NOT_READY. That is what this file supplies, as close to
// PC's insecure path as the port can get.
//
// **The gate.** Both persistence accessors in server.prx consult the same
// engine interface through the same vtable slot before touching a var:
//
//   004810a2  movsx ebx, di                  ; entity index
//   004810a5  mov   rdi, [rip + 0x63c1dc]    ; interface singleton -> 0xabd288
//   004810ac  dec   ebx                      ; entity index - 1 = client #0
//   004810b3  call  qword ptr [rax + 0x5c8]  ; IsPersistentDataAvailable(client)
//   004810b9  test  al, al
//   004810bb  je    0x481174                 ; -> "Persistent data not available"
//
// The second accessor at 0x481319 loads the same singleton (0x63bf68 from
// 0x481320 is also 0xabd288) and calls the same slot, so hooking the slot
// covers both, and any other caller, rather than patching two call sites and
// hoping there is no third.
//
// The vtable is replaced the way the filesystem overlay does it: copy the
// table into module memory, swap the one entry, and repoint the object. No
// page protection is touched, because the object's vtable pointer is ordinary
// writable data.
//
// **What the hook does, and why not simply return true.** The first version
// forced the answer to true. The lobby came up, and then the game crashed
// (access violation at engine+0x2dbd6b) once the lobby shut down. The engine
// function behind the slot, engine.prx 0x2dba90, is:
//
//   if (client >= clientCount)                  return false  ; engine+0x38188c0
//   c = &clients[client]                        ; engine+0x3818680, stride 0x2d738
//   if (c.signonState   (+0x4f0) != 8)          return false  ; not fully connected
//   return c.persistenceReady (+0x6f0) > 2                    ; READY_INSECURE = 3
//
// and the persistence readers repeat that check inline: when it fails they
// take a NULL buffer instead of `client + 0x74a` and read through it. Forcing
// the vtable answer made the callers believe a buffer existed that the readers
// then refused to hand out. That is exactly what happened after the lobby
// shut down and the client slot emptied.
//
// So the hook now does what PC's AuthenticatePlayer does: it sets the client's
// own `persistenceReady` field to READY_INSECURE, and only for a slot that is
// fully connected, then lets the engine's function answer. Every reader
// (through the vtable or inlined) sees one consistent state, a disconnected
// slot still reports unavailable, and a slot the engine has loaded real
// persistence for is left alone.
//
// PC's layout for comparison: stride 0x2d728, m_iPersistenceReady at 0x4a0,
// buffer at 0x4fa. The console build is laid out differently, so these
// offsets come from the function above and are gated on its exact bytes.
constexpr std::uintptr_t kEnginePersistenceAvailableVa = 0x2dba90;
constexpr std::uintptr_t kEngineClientCountVa = 0x38188c0;
constexpr std::uintptr_t kEngineClientArrayVa = 0x3818680;
constexpr std::size_t kEngineClientStride = 0x2d738;
constexpr std::size_t kClientSignonStateOffset = 0x4f0;
constexpr std::size_t kClientPersistenceReadyOffset = 0x6f0;
constexpr std::int32_t kSignonStateFull = 8;
constexpr std::int32_t kPersistenceReadyInsecure = 3;
constexpr std::uintptr_t kServerPersistenceInterfaceVa = 0xabd288;
constexpr std::uintptr_t kServerPersistenceGateVa = 0x4810a5;
constexpr std::uintptr_t kServerPersistenceGate2Va = 0x481319;
// 0x5c8 / sizeof(void*). The two leading entries are the offset-to-top and
// RTTI pointers that sit in front of every Itanium-ABI vtable and have to be
// carried across with it.
constexpr std::size_t kPersistenceAvailableSlot = 0x5c8 / sizeof(void*);
constexpr std::size_t kPersistenceVtableSlots = 288;

using PersistenceAvailableFn = bool (*)(void*, int);
PersistenceAvailableFn g_originalPersistenceAvailable = nullptr;
std::uintptr_t g_persistenceVtable[kPersistenceVtableSlots + 2]{};
bool g_persistenceHookInstalled = false;
std::uintptr_t g_engineBaseForPersistence = 0;

bool RuntimePersistenceAvailable(void* self, int client) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (engine && client >= 0 &&
        client < *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa)) {
        auto slot = reinterpret_cast<char*>(engine + kEngineClientArrayVa) +
            static_cast<std::size_t>(client) * kEngineClientStride;
        auto ready = reinterpret_cast<std::int32_t*>(slot + kClientPersistenceReadyOffset);
        const auto signon = *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset);
        // Only a fully connected client, and only if the engine has not
        // already marked it ready itself.
        if (signon == kSignonStateFull && *ready < kPersistenceReadyInsecure) {
            *ready = kPersistenceReadyInsecure;
            LogFormat("[NorthstarPS4] client #%d persistence marked READY_INSECURE "
                "(PC AuthenticatePlayer equivalent)\n", client);
        }
    }
    return g_originalPersistenceAvailable(self, client);
}

bool InstallRuntimePersistence() noexcept {
    if (g_persistenceHookInstalled) return true;
    const auto base = g_runtimeServerBase;
    if (base == 0 || g_runtimeServerSpan == 0) return false;

    struct Gate { std::uintptr_t va; const char* bytes; std::size_t size; };
    const Gate gates[] = {
        {kServerPersistenceGateVa,
         "\x48\x8b\x3d\xdc\xc1\x63\x00\xff\xcb\x89\xde\x48\x8b\x07\xff\x90\xc8\x05\x00\x00\x84\xc0\x0f\x84\xb3\x00\x00\x00", 28},
        {kServerPersistenceGate2Va,
         "\x48\x8b\x3d\x68\xbf\x63\x00\x0f\xbf\xdb\xff\xcb\x89\xde\x48\x8b\x07\xff\x90\xc8\x05\x00\x00\x84\xc0\x0f\x84\xe0\x00\x00\x00", 31},
    };
    for (const auto& gate : gates) {
        if (!ValidateEnginePreimage(base, g_runtimeServerSpan, gate.va,
                reinterpret_cast<const std::uint8_t*>(gate.bytes), gate.size)) {
            LogFormat("[NorthstarPS4] persistence gate mismatch va=%lx; lobby persistence unchanged\n", gate.va);
            return false;
        }
    }

    // The singleton is constructed while the server starts, so this can be
    // reached before it exists; the caller retries on the next VM creation.
    void* object = *reinterpret_cast<void**>(base + kServerPersistenceInterfaceVa);
    if (!object) {
        LogFormat("[NorthstarPS4] persistence interface not constructed yet\n");
        return false;
    }
    auto vtable = *reinterpret_cast<void***>(object);
    if (!vtable || !AuthAddressReadable(vtable) ||
        !AuthAddressReadable(vtable + kPersistenceVtableSlots - 1)) {
        LogFormat("[NorthstarPS4] persistence vtable unreadable object=%p vtable=%p\n", object, vtable);
        return false;
    }
    auto original = reinterpret_cast<PersistenceAvailableFn>(vtable[kPersistenceAvailableSlot]);
    if (!original) {
        LogFormat("[NorthstarPS4] persistence vtable slot %zu is null\n", kPersistenceAvailableSlot);
        return false;
    }
    // The implementation lives in engine.prx. Its base is recovered from the
    // function's own address and then proven by matching every byte of it:
    // the client offsets this hook writes through are only valid for exactly
    // this function, so a mismatch leaves persistence untouched.
    constexpr std::uint8_t implementation[] = {
        0x39,0x35,0x2a,0xce,0x53,0x03,0x7e,0x27,0x48,0x63,0xc6,0x48,0x8d,0x0d,
        0xde,0xcb,0x53,0x03,0x48,0x69,0xc0,0x38,0xd7,0x02,0x00,0x83,0xbc,0x08,
        0xf0,0x04,0x00,0x00,0x08,0x75,0x0f,0x83,0xbc,0x08,0xf0,0x06,0x00,0x00,
        0x02,0x0f,0x9f,0xc0,0xc3,0x31,0xc0,0xc3,0x31,0xc0,0xc3,
    };
    constexpr std::size_t kEngineTextSpan = 0x398000;
    const auto engine = reinterpret_cast<std::uintptr_t>(original) - kEnginePersistenceAvailableVa;
    if (!ValidateEnginePreimage(engine, kEngineTextSpan, kEnginePersistenceAvailableVa,
            implementation, sizeof(implementation)) ||
        !AuthAddressReadable(reinterpret_cast<const void*>(engine + kEngineClientCountVa))) {
        LogFormat("[NorthstarPS4] persistence implementation mismatch at %p; lobby persistence unchanged\n",
            reinterpret_cast<void*>(original));
        return false;
    }
    g_engineBaseForPersistence = engine;

    for (std::size_t i = 0; i < kPersistenceVtableSlots + 2; ++i)
        g_persistenceVtable[i] = reinterpret_cast<std::uintptr_t>(vtable[i - 2]);
    g_originalPersistenceAvailable = original;
    g_persistenceVtable[kPersistenceAvailableSlot + 2] =
        reinterpret_cast<std::uintptr_t>(&RuntimePersistenceAvailable);
    *reinterpret_cast<void**>(object) = g_persistenceVtable + 2;

    g_persistenceHookInstalled = true;
    LogFormat("[NorthstarPS4] persistence availability hook installed object=%p original=%p slot=%zu\n",
        object, reinterpret_cast<void*>(original), kPersistenceAvailableSlot);
    return true;
}
