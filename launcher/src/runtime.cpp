#if defined(NORTHSTAR_PS4_ENABLE_M6_FS_OVERLAY) && !defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
#define NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA 1
#endif
#include "northstar_ps4/runtime.h"
#include "northstar_ps4/mod_catalog.h"
#include "northstar_ps4/mod_settings.h"
#include "northstar_ps4/mod_callbacks.h"
#include "northstar_ps4/mod_savefiles.h"
#include "northstar_ps4/json_text.h"
#include "northstar_ps4/keyvalues.h"
#include "northstar_ps4/server_list.h"

#include <orbis/libkernel.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Http.h>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <algorithm>
#include <atomic>
#include "northstar_ps4/mod_vpks.h"
#include "northstar_ps4/mod_rpaks.h"
#include <vector>

namespace northstar::ps4 {
namespace {
constexpr std::size_t kMaxModules = 256;
constexpr std::uintptr_t kConVarConstructorVa = 0x205450;
constexpr std::uintptr_t kSvCheatsConstructorCallVa = 0x0dce52;
constexpr std::uint8_t kConVarConstructorPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x10,
    0xc5, 0xf8, 0x57, 0xc0, 0x48, 0x8d, 0x05, 0x2d,
};
constexpr std::uint8_t kSvCheatsCallPreimage[] = {
    0x48, 0x8d, 0x35, 0x92, 0xb3, 0x25, 0x00, 0x4c,
    0x8d, 0x05, 0x95, 0xb3, 0x25, 0x00, 0x4c, 0x8d,
};
constexpr std::uintptr_t kClientRunUiScriptVa = 0x767b10;
constexpr std::uintptr_t kClientUiVmLoadVa = 0x767b64;
constexpr std::uintptr_t kClientScriptOwnerGlobalVa = 0x1afbfb8;
constexpr std::uintptr_t kClientRegisterSquirrelFuncVa = 0x67a3c0;
constexpr std::uintptr_t kClientRunUiScriptRegistrationVa = 0x2f1554;
constexpr std::uintptr_t kClientNativeRegistrationBlockVa = 0x2e7ef0;
constexpr std::uintptr_t kClientNativeRegistrationCallerVa = 0x2e1a15;
constexpr std::uintptr_t kClientRegistrationVmGlobalVa = 0x19d4fe8;
constexpr std::uintptr_t kClientUiRegistrationTreeAVa = 0x1e42240;
constexpr std::uintptr_t kClientUiRegistrationTreeBVa = 0x1e422b0;
constexpr std::uintptr_t kClientRunUiScriptRecordVa = 0x19d3860;
constexpr std::uintptr_t kClientRunUiScriptNameVa = 0x8e673a;
constexpr std::uintptr_t kClientFindUiFunctionVa = 0x6799f0;
constexpr std::uintptr_t kClientUiRegistrationStartupVa = 0x31e23f;
constexpr std::int32_t kClientUiTreeBExpectedCountA = 135;
constexpr std::int32_t kClientUiTreeBExpectedCountB = 15;
constexpr std::uint8_t kClientRunUiScriptPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec,
};
constexpr std::uint8_t kClientUiVmLoadPreimage[] = {
    0x48, 0x8b, 0x05, 0x4d, 0x44, 0x39, 0x01, 0x31,
    0xd2, 0x31, 0xc9, 0x4c, 0x89, 0xfe, 0x48, 0x8b,
};
constexpr std::uint8_t kClientRegisterSquirrelFuncPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec,
};
constexpr std::uint8_t kClientRunUiScriptRegistrationPreimage[] = {
    0x48, 0x8d, 0x35, 0x05, 0x23, 0x6e, 0x01, 0x31,
    0xd2, 0xb9, 0x01, 0x00, 0x00, 0x00, 0x45, 0x31,
};
constexpr std::uint8_t kClientNativeRegistrationBlockPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8a,
};
constexpr std::uint8_t kClientNativeRegistrationCallerPreimage[] = {
    0x48, 0x8b, 0x3d, 0xcc, 0x35, 0x6f, 0x01, 0xe8,
    0xcf, 0x64, 0x00, 0x00, 0x4c, 0x8b, 0x35, 0xc0,
};
constexpr std::uint8_t kClientFindUiFunctionPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec,
};
constexpr std::uint8_t kClientUiRegistrationStartupPreimage[] = {
    0x48, 0x8d, 0x35, 0xfa, 0x3f, 0xb2, 0x01, 0xe8,
    0x95, 0xf8, 0x35, 0x00, 0x48, 0x8b, 0x3d, 0x66,
};
#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_PROBE)
constexpr std::uintptr_t kClientUiInitVa = 0x31dc50;
constexpr std::uintptr_t kClientRsonLoaderVa = 0x2f5310;
constexpr std::uintptr_t kClientScriptSystemGlobalVa = 0x1afbfc0;
constexpr std::uintptr_t kClientScriptCountTableVa = 0x1af4780;
constexpr std::uintptr_t kClientScriptPoolCurVa = 0x2564ea8;
constexpr std::uintptr_t kClientScriptPoolEndVa = 0x2564eb0;
constexpr std::uint8_t kClientUiInitPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec,
    0x18, 0x48, 0x8b, 0x0d, 0x18, 0x4b, 0x78, 0x00,
};
constexpr std::uint8_t kClientRsonLoaderPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec,
    0xb8, 0x40, 0x00, 0x00, 0x48, 0x8b, 0x05, 0x55,
};
// Candidate "CompileList(owner, ctx, paths, count)" used by the client's own
// boot-time RSON-driven script loader (references kClientScriptCountTableVa
// internally at VA 0x1af4780, confirmed by static disassembly). Never proven
// safe to call from a thread the engine did not create: see
// kClientCompileListGatePtrVa below and docs/TECHNICAL-NOTES.md run
// 20260806-214046.
constexpr std::uintptr_t kClientCompileListVa = 0x3153f0;
constexpr std::uint8_t kClientCompileListPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec,
};
// Global pointer-to-object dereferenced by CompileList before touching each
// path (client VA 0x315470 / 0x315500: `mov rdi,[this]; mov rax,[rdi];
// call [rax+0x58]`, args esi=-1 edx=0, matching a lock/wait-style virtual
// call). The object and its vtable are populated by the time the engine's
// own boot compile pass runs (152 real UI scripts already loaded by the time
// our probe polls), but vtable slot +0x58 was observed null when this same
// call executed on our own detached NorthstarPS4 thread instead of the
// thread the engine used for its own compile pass. Gate on the full object
// -> vtable -> slot chain and refuse rather than dereference a null call
// target.
constexpr std::uintptr_t kClientCompileListGatePtrVa = 0xb2f0d0;
constexpr std::uintptr_t kClientCompileListGateVtableSlot = 0x58;
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
// PS4 localize.prx (CUSA04013 2017-12-05 build). CLocalise::AddFile is called
// directly (this=rdi, fileName=rsi, pathId=rdx, includeFallbackSearchPaths=ecx);
// it resolves a %language% token internally and loads the file through the engine
// filesystem GAME search paths (which include /app0/r2, the staging overlay).
// The singleton instance pointer lives in .bss at kLocalizeInstanceVa and is
// returned by the trivial accessor at kLocalizeAccessorVa (`lea rax,[rip+...];
// ret`). Windows localize.dll AddFile (RVA 0x6D80) shares the same structure;
// see tools/NorthstarLauncher-reference/primedev/client/modlocalisation.cpp for
// the equivalent PC hook that calls AddFile(path, nullptr, false).
constexpr std::uintptr_t kLocalizeAddFileVa = 0x5c60;
constexpr std::uintptr_t kLocalizeInstanceVa = 0x1d280;
constexpr std::uintptr_t kLocalizeAccessorVa = 0x4f90;
constexpr std::uint8_t kLocalizeAddFilePreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xe4,
};
constexpr std::uint8_t kLocalizeAccessorPreimage[] = {
    0x48, 0x8d, 0x05, 0xe9, 0x82, 0x01, 0x00, 0xc3,
};
#endif



void LogFormat(const char* format, ...) noexcept {
    char buffer[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    sceKernelDebugOutText(0, buffer);
}

bool IsTarget(const char* name) noexcept {
    return name != nullptr &&
        (std::strstr(name, "engine.prx") != nullptr ||
         std::strstr(name, "engine.sprx") != nullptr ||
         std::strstr(name, "client.prx") != nullptr ||
         std::strstr(name, "client.sprx") != nullptr);
}

void LogModule(OrbisKernelModule handle, const OrbisKernelModuleInfo& info) noexcept {
    LogFormat("[NorthstarPS4] target module=%s handle=0x%x segments=%u\n",
        info.name, handle, info.segmentCount);
    const std::uint32_t count = info.segmentCount < 4 ? info.segmentCount : 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        LogFormat(
            "[NorthstarPS4]   segment[%u] address=%p size=0x%x prot=0x%x\n",
            i, info.segmentInfo[i].address, info.segmentInfo[i].size,
            info.segmentInfo[i].prot);
    }
}

void ProbeRegistrationExports(
    OrbisKernelModule engineHandle, OrbisKernelModule vstdlibHandle) noexcept {
    constexpr const char* candidates[] = {
        "ConVar_Register",
        "_ZN6ConVarC1EPKcS1_iS1_",
        "_ZN6ConVarC2EPKcS1_iS1_",
        "_ZN6ConVarC1EPKcS1_iS1_bfbfPFvPS_S1_fE",
        "_ZN6ConVarC2EPKcS1_iS1_bfbfPFvPS_S1_fE",
    };
    const struct {
        const char* name;
        OrbisKernelModule handle;
    } modules[] = {
        {"engine", engineHandle},
        {"vstdlib", vstdlibHandle},
    };

    for (const auto& module : modules) {
        for (const char* candidate : candidates) {
            void* address = nullptr;
            const int result = sceKernelDlsym(
                static_cast<int32_t>(module.handle), candidate, &address);
            LogFormat("[NorthstarPS4] registration export module=%s symbol=%s result=0x%x address=%p\n",
                module.name, candidate, result, address);
        }
    }
}
bool ValidateEnginePreimage(
    std::uintptr_t engineBase, std::size_t engineSize, std::uintptr_t va,
    const std::uint8_t* expected, std::size_t expectedSize) noexcept {
    if (engineBase == 0 || va > engineSize || expectedSize > engineSize - va) return false;
    return std::memcmp(reinterpret_cast<const void*>(engineBase + va), expected,
        expectedSize) == 0;
}

#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_UI_NATIVE)
alignas(16) std::uint8_t gDiagnosticUiRecords[(kClientUiTreeBExpectedCountA + 1) * 0x68]{};
void* gOriginalUiRecords = nullptr;
std::int32_t gOriginalUiRecordCount = 0;
bool gDiagnosticUiRecordInstalled = false;
std::uintptr_t gDiagnosticUiClientBase = 0;
void* gDiagnosticUiInternalVm = nullptr;
void* gDiagnosticUiTable = nullptr;

std::int64_t DiagnosticUiNative(void*) noexcept;

bool InsertDiagnosticNativeIntoVm(
    std::uintptr_t clientBase, void* uiVm) noexcept {
    if (uiVm == nullptr) return false;
    void* internalVm = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(uiVm) + 0x50);
    if (internalVm == nullptr) return false;
    void* stringTable = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(internalVm) + 0x4048);
    void* table = *reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(internalVm) + 0x40d0);
    if (stringTable == nullptr || table == nullptr) return false;

    using InternFn = void* (*)(void*, const char*, std::int32_t);
    using NewClosureFn = std::uintptr_t (*)(void*, void*, std::int32_t);
    using NewSlotFn = std::int32_t (*)(void*, const void*, const void*);
    auto intern = reinterpret_cast<InternFn>(clientBase + 0x6a96a0);
    auto newClosure = reinterpret_cast<NewClosureFn>(clientBase + 0x683730);
    auto newSlot = reinterpret_cast<NewSlotFn>(clientBase + 0x6ab3e0);

    static constexpr char name[] = "NSStage2Ping";
    void* const internedName = intern(stringTable, name, -1);
    if (internedName == nullptr) return false;
    const std::uintptr_t slotAddress =
        newClosure(uiVm, reinterpret_cast<void*>(&DiagnosticUiNative), 0);
    std::uintptr_t closureRecord = 0;
    if (slotAddress > 0x100000000ULL && slotAddress < 0x40000000000ULL) {
        closureRecord =
            *reinterpret_cast<const std::uintptr_t*>(slotAddress + 8);
    }
    if (closureRecord == 0) return false;
    const std::uint64_t key[2] = {
        0x8000010ULL, reinterpret_cast<std::uintptr_t>(internedName),
    };
    const std::uint64_t value[2] = { 0x8000200ULL, closureRecord };
    const std::int32_t result = newSlot(table, key, value);
    LogFormat("[NorthstarPS4] UI native ensure-in-vm uiVm=%p internal=%p t40d0=%p str=%p result=%d\n",
        uiVm, internalVm, table, internedName, result);
    return true;
}

std::int64_t DiagnosticUiNative(void*) noexcept {
    LogFormat("[NorthstarPS4] NSStage2Ping invoked\n");
    if (gDiagnosticUiClientBase != 0) {
        auto ownerSlot = reinterpret_cast<void**>(
            gDiagnosticUiClientBase + kClientScriptOwnerGlobalVa);
        void* owner = *ownerSlot;
        void* uiVm = owner != nullptr
            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(owner) + 8)
            : nullptr;
        void* internalVm = uiVm != nullptr
            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(uiVm) + 0x50)
            : nullptr;
        void* currentTable = internalVm != nullptr
            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(internalVm) + 0x40d0)
            : nullptr;
        LogFormat("[NorthstarPS4] UI native invoke context owner=%p uiVm=%p internal=%p table=%p handledInternal=%p handledTable=%p\n",
            owner, uiVm, internalVm, currentTable,
            gDiagnosticUiInternalVm, gDiagnosticUiTable);
        if (internalVm != nullptr &&
            (internalVm != gDiagnosticUiInternalVm || currentTable != gDiagnosticUiTable)) {
            LogFormat("[NorthstarPS4] UI native invoke saw new VM context, inserting\n");
            InsertDiagnosticNativeIntoVm(gDiagnosticUiClientBase, uiVm);
            gDiagnosticUiInternalVm = internalVm;
            gDiagnosticUiTable = currentTable;
        }
    }
    return 0;
}

bool InstallDiagnosticUiRecord(
    std::uintptr_t clientBase, std::size_t clientSpan) noexcept {
    const bool dispatcherMatches = ValidateEnginePreimage(
        clientBase, clientSpan, 0x67dae0,
        reinterpret_cast<const std::uint8_t*>("\x55\x48\x89\xe5\x41\x57\x41\x56"),
        8);
    const bool registerMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientRegisterSquirrelFuncVa,
        kClientRegisterSquirrelFuncPreimage,
        sizeof(kClientRegisterSquirrelFuncPreimage));
    const bool startupMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientUiRegistrationStartupVa,
        kClientUiRegistrationStartupPreimage,
        sizeof(kClientUiRegistrationStartupPreimage));
    LogFormat("[NorthstarPS4] UI native install gate dispatcher=%d register=%d startup=%d\n",
        dispatcherMatches ? 1 : 0, registerMatches ? 1 : 0,
        startupMatches ? 1 : 0);
    if (!dispatcherMatches || !registerMatches || !startupMatches ||
        kClientUiRegistrationTreeBVa + 0x60 > clientSpan)
        return false;

    auto ownerSlot = reinterpret_cast<void**>(
        clientBase + kClientScriptOwnerGlobalVa);
    auto tree = reinterpret_cast<std::uint8_t*>(
        clientBase + kClientUiRegistrationTreeBVa);
    auto recordsSlot = reinterpret_cast<void**>(tree + 0x20);
    auto countSlot = reinterpret_cast<std::int32_t*>(tree + 0x38);
    auto otherRecordsSlot = reinterpret_cast<void**>(tree + 0x40);
    auto otherCountSlot = reinterpret_cast<std::int32_t*>(tree + 0x58);
    std::int32_t lastStateCount = -1;
    void* lastStateRecords = reinterpret_cast<void*>(~std::uintptr_t{0});
    for (std::uint32_t attempt = 0; attempt < 600; ++attempt) {
        if (*ownerSlot != nullptr) {
            LogFormat("[NorthstarPS4] UI native install refused: UI owner already exists\n");
            return false;
        }
        const std::int32_t currentCount = *countSlot;
        void* const currentRecords = *recordsSlot;
        if (currentCount != lastStateCount || currentRecords != lastStateRecords) {
            LogFormat("[NorthstarPS4] UI native install state attempt=%u list=A count=%d records=%p otherList=B otherCount=%d otherRecords=%p\n",
                attempt, currentCount, currentRecords, *otherCountSlot,
                *otherRecordsSlot);
            lastStateCount = currentCount;
            lastStateRecords = currentRecords;
        }
        if (currentCount == kClientUiTreeBExpectedCountA)
            break;
        if (attempt == 599) {
            LogFormat("[NorthstarPS4] UI native install refused: tree B A-list not ready count=%d\n",
                currentCount);
            return false;
        }
        sceKernelUsleep(10000);
    }

    gOriginalUiRecords = *recordsSlot;
    gOriginalUiRecordCount = *countSlot;
    std::memcpy(gDiagnosticUiRecords, gOriginalUiRecords,
        static_cast<std::size_t>(gOriginalUiRecordCount) * 0x68);
    std::uint8_t* record =
        gDiagnosticUiRecords + gOriginalUiRecordCount * 0x68;
    std::memset(record, 0, 0x68);
    static constexpr char name[] = "NSStage2Ping";
    static constexpr char cppName[] = "Script_NSStage2Ping";
    static constexpr char help[] = "Northstar PS4 Stage 2 UI diagnostic";
    static constexpr char returnType[] = "void";
    static constexpr char argTypes[] = "";
    *reinterpret_cast<const void**>(record + 0x00) = name;
    *reinterpret_cast<const void**>(record + 0x08) = cppName;
    *reinterpret_cast<const void**>(record + 0x10) = help;
    *reinterpret_cast<const void**>(record + 0x18) = returnType;
    *reinterpret_cast<const void**>(record + 0x20) = argTypes;
    *reinterpret_cast<const void**>(record + 0x60) =
        reinterpret_cast<const void*>(&DiagnosticUiNative);
    LogFormat("[NorthstarPS4] UI native record uses exact zero-initialized PS4 shape\n");

    *recordsSlot = gDiagnosticUiRecords;
    *countSlot = gOriginalUiRecordCount + 1;
    gDiagnosticUiRecordInstalled = true;
    LogFormat("[NorthstarPS4] UI native record installed before owner creation original=%p count=%d replacement=%p count=%d\n",
        gOriginalUiRecords, gOriginalUiRecordCount, gDiagnosticUiRecords,
        gOriginalUiRecordCount + 1);
    if (gOriginalUiRecordCount > 1) {
        const std::uintptr_t first = *reinterpret_cast<const std::uintptr_t*>(
            gDiagnosticUiRecords);
        const std::uintptr_t lastReal = *reinterpret_cast<const std::uintptr_t*>(
            gDiagnosticUiRecords + (gOriginalUiRecordCount - 1) * 0x68);
        LogFormat("[NorthstarPS4] UI native records first=%p lastOriginal=%p our=%p name=%s\n",
            reinterpret_cast<void*>(first), reinterpret_cast<void*>(lastReal),
            reinterpret_cast<const void*>(*reinterpret_cast<const void**>(record + 0x00)),
            name);
    }
    return true;
}

void VerifyAndRestoreDiagnosticUiRecord(
    std::uintptr_t clientBase, std::size_t clientSpan, void* uiVm) noexcept {
    if (!gDiagnosticUiRecordInstalled) return;
    const bool registerSequenceMatches =
        ValidateEnginePreimage(clientBase, clientSpan, kClientUiVmLoadVa,
            kClientUiVmLoadPreimage, sizeof(kClientUiVmLoadPreimage)) &&
        ValidateEnginePreimage(clientBase, clientSpan, kClientRegisterSquirrelFuncVa,
            kClientRegisterSquirrelFuncPreimage,
            sizeof(kClientRegisterSquirrelFuncPreimage));
    if (!registerSequenceMatches) {
        LogFormat("[NorthstarPS4] UI native verify refused: register sequence preimage mismatch gate=0\n");
        return;
    }

    auto tree = reinterpret_cast<std::uint8_t*>(
        clientBase + kClientUiRegistrationTreeBVa);
    auto recordsSlot = reinterpret_cast<void**>(tree + 0x20);
    auto countSlot = reinterpret_cast<std::int32_t*>(tree + 0x38);
    auto otherRecordsSlot = reinterpret_cast<void**>(tree + 0x40);
    auto otherCountSlot = reinterpret_cast<std::int32_t*>(tree + 0x58);
    void* previousRecords = reinterpret_cast<void*>(~std::uintptr_t{0});
    std::int32_t previousCount = -1;

    auto logTree = [&](const char* tag) {
        const std::int32_t countA = *countSlot;
        void* const recordsA = *recordsSlot;
        const std::int32_t countB = *otherCountSlot;
        void* const recordsB = *otherRecordsSlot;
        LogFormat("[NorthstarPS4] UI native %s treeB recordsA=%p countA=%d recordsB=%p countB=%d ours=%d\n",
            tag, recordsA, countA, recordsB, countB,
            recordsA == reinterpret_cast<void*>(gDiagnosticUiRecords) ? 1 : 0);
    };

    sceKernelUsleep(50000);

    static constexpr char name[] = "NSStage2Ping";
    constexpr const char* probeTargets[] = {
        name, "SetImage", "RuiSetImage", "Hud_GetChild",
    };
    constexpr std::size_t targetCount =
        sizeof(probeTargets) / sizeof(probeTargets[0]);

    auto internalVm = reinterpret_cast<void*>(
        *reinterpret_cast<std::uintptr_t*>(
            reinterpret_cast<std::uintptr_t>(uiVm) + 0x50));
    auto vmField = [&](std::uintptr_t offset) -> void* {
        if (internalVm == nullptr) return nullptr;
        return *reinterpret_cast<void**>(
            reinterpret_cast<std::uintptr_t>(internalVm) + offset);
    };
    void* stringTable = vmField(0x4048);
    void* templateVm = vmField(0x4168);
    const struct {
        const char* tag;
        std::uintptr_t offset;
        void* table;
    } tables[] = {
        {"t40d0", 0x40d0, vmField(0x40d0)},
        {"t40e0", 0x40e0, vmField(0x40e0)},
        {"t40f0", 0x40f0, vmField(0x40f0)},
        {"t4120", 0x4120, vmField(0x4120)},
        {"t4180", 0x4180, vmField(0x4180)},
        {"t4188", 0x4188, vmField(0x4188)},
        {"t4190", 0x4190, vmField(0x4190)},
        {"t41b0", 0x41b0, vmField(0x41b0)},
    };
    LogFormat("[NorthstarPS4] UI native vm uiVm=%p internal=%p strings=%p t4168=%p\n",
        uiVm, internalVm, stringTable, templateVm);

    using InternFn = void* (*)(void*, const char*, std::int32_t);
    using NewClosureFn = std::uintptr_t (*)(void*, void*, std::int32_t);
    using NewSlotFn = std::int32_t (*)(void*, const void*, const void*);
    auto intern = reinterpret_cast<InternFn>(clientBase + 0x6a96a0);
    auto newClosure = reinterpret_cast<NewClosureFn>(clientBase + 0x683730);
    auto newSlot = reinterpret_cast<NewSlotFn>(clientBase + 0x6ab3e0);
    const std::uintptr_t callbackVa =
        reinterpret_cast<std::uintptr_t>(&DiagnosticUiNative);
    auto globalHashTable = reinterpret_cast<const std::uintptr_t*>(
        clientBase + 0x25859d0);

    auto isPlausiblePointer = [](std::uintptr_t value) -> bool {
        return value > 0x100000000ULL && value < 0x40000000000ULL;
    };
    auto globalHashScan = [&]() -> std::int32_t {
        std::int32_t hits = 0;
        for (std::int32_t i = 0; i < 0x2000; ++i) {
            if (globalHashTable[i * 2 + 1] == callbackVa) ++hits;
        }
        return hits;
    };
    struct TableLayout {
        bool valid;
        std::uint32_t numBuckets;
        std::uint32_t used;
        std::uintptr_t buckets;
    };
    auto tableLayout = [&](void* table) -> TableLayout {
        if (table == nullptr) return {false, 0, 0, 0};
        const auto fields = reinterpret_cast<const std::uintptr_t*>(table);
        const std::uintptr_t buckets = fields[7];
        const std::uint32_t numBuckets = static_cast<std::uint32_t>(fields[8]);
        const std::uint32_t used = static_cast<std::uint32_t>(fields[8] >> 32);
        const bool valid = numBuckets >= 1 && numBuckets <= 0x400000 &&
            used <= numBuckets && isPlausiblePointer(buckets);
        return {valid, numBuckets, used, buckets};
    };
    auto dumpTable = [&](const char* tag, void* table) {
        const TableLayout layout = tableLayout(table);
        LogFormat("[NorthstarPS4] UI native table %s=%p buckets=%p num=%u used=%u valid=%d\n",
            tag, table, reinterpret_cast<void*>(layout.buckets),
            layout.numBuckets, layout.used, layout.valid ? 1 : 0);
    };
    void* targetStrings[targetCount]{};
    for (std::size_t i = 0; i < targetCount; ++i) {
        if (intern != nullptr && stringTable != nullptr) {
            targetStrings[i] = intern(stringTable, probeTargets[i], -1);
        }
    }
    auto scanTable = [&](const char* tag, void* table) {
        const TableLayout layout = tableLayout(table);
        if (!layout.valid) {
            LogFormat("[NorthstarPS4] UI native scan %s invalid table=%p\n",
                tag, table);
            return;
        }
        std::int32_t bucketHits[targetCount]{};
        for (std::uint32_t i = 0; i < layout.numBuckets; ++i) {
            std::uintptr_t entry =
                layout.buckets + static_cast<std::uintptr_t>(i) * 0x28;
            for (std::int32_t depth = 0; depth < 64; ++depth) {
                if (!isPlausiblePointer(entry)) break;
                const std::int32_t keyType =
                    *reinterpret_cast<const std::int32_t*>(entry + 0x10);
                if (keyType == 0x8000010) {
                    const void* keyValue =
                        *reinterpret_cast<const void* const*>(entry + 0x18);
                    for (std::size_t t = 0; t < targetCount; ++t) {
                        if (targetStrings[t] != nullptr && keyValue == targetStrings[t])
                            ++bucketHits[t];
                    }
                }
                const std::uintptr_t next =
                    *reinterpret_cast<const std::uintptr_t*>(entry + 0x20);
                if (next == 0) break;
                entry = next;
            }
        }
        for (std::size_t t = 0; t < targetCount; ++t) {
            LogFormat("[NorthstarPS4] UI native scan %s name=%s hits=%d\n",
                tag, probeTargets[t], bucketHits[t]);
        }
    };
    auto dumpNodes = [&](const char* tag, void* table, std::uint32_t maxNodes) {
        const TableLayout layout = tableLayout(table);
        if (!layout.valid) return;
        std::uint32_t dumped = 0;
        for (std::uint32_t i = 0; i < layout.numBuckets && dumped < maxNodes; ++i) {
            std::uintptr_t entry =
                layout.buckets + static_cast<std::uintptr_t>(i) * 0x28;
            for (std::int32_t depth = 0; depth < 64; ++depth) {
                if (!isPlausiblePointer(entry)) break;
                const std::int32_t keyType =
                    *reinterpret_cast<const std::int32_t*>(entry + 0x10);
                const std::int32_t keyHash =
                    *reinterpret_cast<const std::int32_t*>(entry + 0x14);
                const std::uintptr_t keyValue =
                    *reinterpret_cast<const std::uintptr_t*>(entry + 0x18);
                const std::int32_t valueType =
                    *reinterpret_cast<const std::int32_t*>(entry + 0x00);
                const std::uintptr_t valuePtr =
                    *reinterpret_cast<const std::uintptr_t*>(entry + 0x08);
                char keyTextBuf[24]{};
                if (keyType == 0x8000010 && isPlausiblePointer(keyValue)) {
                    const char* src =
                        reinterpret_cast<const char*>(keyValue + 0x30);
                    for (std::size_t k = 0; k < sizeof(keyTextBuf) - 1; ++k) {
                        keyTextBuf[k] = src[k];
                        if (src[k] == '\0') break;
                    }
                }
                LogFormat("[NorthstarPS4] UI native node %s keyType=0x%x keyHash=0x%x key=%p keyText=%s valueType=0x%x value=%p\n",
                    tag, keyType, keyHash, reinterpret_cast<void*>(keyValue),
                    keyTextBuf, valueType, reinterpret_cast<void*>(valuePtr));
                ++dumped;
                const std::uintptr_t next =
                    *reinterpret_cast<const std::uintptr_t*>(entry + 0x20);
                if (next == 0) break;
                entry = next;
            }
        }
    };

    LogFormat("[NorthstarPS4] UI native probe tables (pre-insert):\n");
    for (const auto& entry : tables) {
        dumpTable(entry.tag, entry.table);
        scanTable(entry.tag, entry.table);
    }
    LogFormat("[NorthstarPS4] UI native node dump t4120 (pre-insert):\n");
    dumpNodes("t4120", vmField(0x4120), 48);
    LogFormat("[NorthstarPS4] UI native node dump t41b0 (pre-insert):\n");
    dumpNodes("t41b0", vmField(0x41b0), 48);

    void* const internedName =
        intern != nullptr && stringTable != nullptr
        ? intern(stringTable, name, -1)
        : nullptr;
    std::uintptr_t closureRecord = 0;
    std::uint32_t closureSlotType = 0;
    if (newClosure != nullptr) {
        const std::uintptr_t slotAddress =
            newClosure(uiVm, reinterpret_cast<void*>(&DiagnosticUiNative), 0);
        if (isPlausiblePointer(slotAddress)) {
            closureSlotType =
                *reinterpret_cast<const std::uint32_t*>(slotAddress);
            closureRecord =
                *reinterpret_cast<const std::uintptr_t*>(slotAddress + 8);
        }
        LogFormat("[NorthstarPS4] UI native newClosure slot=%p type=0x%x record=%p\n",
            reinterpret_cast<void*>(slotAddress), closureSlotType,
            reinterpret_cast<void*>(closureRecord));
    }
    const std::uint64_t key[2] = {
        0x8000010ULL,
        reinterpret_cast<std::uintptr_t>(internedName),
    };
    const std::uint64_t value[2] = { 0x8000200ULL, closureRecord };
    auto insertGlobal = [&](const char* tag, void* table) {
        const TableLayout layout = tableLayout(table);
        if (!layout.valid) {
            LogFormat("[NorthstarPS4] UI native insert %s skipped invalid table=%p\n",
                tag, table);
            return;
        }
        if (internedName == nullptr || closureRecord == 0) {
            LogFormat("[NorthstarPS4] UI native insert %s skipped missing name=%p record=%p\n",
                tag, internedName, reinterpret_cast<void*>(closureRecord));
            return;
        }
        const std::int32_t result = newSlot(table, key, value);
        LogFormat("[NorthstarPS4] UI native insert %s key=%s str=%p valueType=0x%x record=%p result=%d\n",
            tag, name, internedName, static_cast<std::uint32_t>(value[0]),
            reinterpret_cast<void*>(closureRecord), result);
    };
    LogFormat("[NorthstarPS4] UI native direct register start uiVm=%p name=%s\n",
        uiVm, name);
    insertGlobal("t40d0", vmField(0x40d0));
    gDiagnosticUiClientBase = clientBase;
    gDiagnosticUiInternalVm = internalVm;
    gDiagnosticUiTable = vmField(0x40d0);

    LogFormat("[NorthstarPS4] UI native probe tables (post-insert):\n");
    for (const auto& entry : tables) {
        scanTable(entry.tag, entry.table);
    }

    for (std::uint32_t attempt = 0; attempt < 60; ++attempt) {
        const std::int32_t countA = *countSlot;
        void* const recordsA = *recordsSlot;
        if (countA != previousCount || recordsA != previousRecords) {
            logTree("state");
            previousCount = countA;
            previousRecords = recordsA;
        }
        if (attempt % 20 == 0) {
            LogFormat("[NorthstarPS4] UI native periodic internal=%p strings=%p t4168=%p globalOurs=%d\n",
                internalVm, vmField(0x4048), vmField(0x4168),
                globalHashScan());
            for (const auto& entry : tables) {
                scanTable(entry.tag, vmField(entry.offset));
            }
        }
        sceKernelUsleep(500000);
    }
}
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA) && defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT)
// Bridges mod metadata discovery (parses each mod's Scripts[] entries whose
// RunOn is exactly "UI") to the M6 script-inject CompileList call, which
// otherwise only knows about one hardcoded probe path. Populated by
// ProbeModMetadata (runs first, from ProbeCvarInterface) and consumed by
// ProbeUiScriptSystem's M6_SCRIPT_INJECT block (runs later, both from
// ModuleTracker), so no cross-thread synchronization is needed.
constexpr std::size_t kMaxCollectedUiScripts = 64;
char gCollectedUiScripts[kMaxCollectedUiScripts][96]{};
std::int32_t gCollectedUiScriptCount = 0;
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
using namespace northstar::ps4::mods;
constexpr const char* kProfileRoot = "/app0/R2Northstar";
constexpr const char* kModsRoot = "/app0/R2Northstar/mods";

bool ReadFileIntoBuffer(const char* path, char* buffer,
    std::size_t capacity, std::size_t& sizeOut) noexcept {
    const int fd = open(path, 0);
    if (fd < 0) return false;
    std::size_t used = 0;
    bool ok = true;
    while (used < capacity) {
        const ssize_t bytesRead = read(fd, buffer + used, capacity - used);
        if (bytesRead < 0) {
            ok = false;
            break;
        }
        if (bytesRead == 0) break;
        used += static_cast<std::size_t>(bytesRead);
    }
    close(fd);
    if (!ok || used >= capacity) return false;
    if (used < capacity) buffer[used] = '\0';
    else buffer[capacity - 1] = '\0';
    sizeOut = used;
    return true;
}

bool ReadEnabledSettings(char* buffer, std::size_t capacity) noexcept {
    std::size_t size = 0;
    const char* paths[] = {"/data/northstar_ps4/enabledmods.json", "/app0/R2Northstar/enabledmods.json"};
    for (const char* path : paths) {
        struct stat info{};
        if (stat(path, &info) == 0) {
            if (!ReadFileIntoBuffer(path, buffer, capacity, size)) return false;
            const char* begin = JsonSkipWs(buffer);
            const char* end = JsonSkipValue(begin);
            return *begin == '{' && end > begin && end[-1] == '}' && *JsonSkipWs(end) == '\0';
        }
        if (errno != ENOENT) return false;
    }
    std::snprintf(buffer, capacity, "{}");
    return true;
}

void CollectModNames(ModDiscovery& discovery, bool includeDisabled = false) noexcept {
    discovery = ModDiscovery{};
    static char enabled[kModJsonBufferSize];
    std::size_t size = 0;
    char path[256]{};
    if (!ReadEnabledSettings(enabled, sizeof(enabled))) {
        LogFormat("[NorthstarPS4] refusing mods: enabled settings are invalid or unreadable\n");
        return;
    }
    auto collect = [&](const char* folder) {
        if (!IsModFolderName(folder)) return;
        std::snprintf(path, sizeof(path), "%s/%s/mod.json", kModsRoot, folder);
        static char json[kModJsonBufferSize];
        ModInfo mod{};
        if (!ReadFileIntoBuffer(path, json, sizeof(json), size) || !ParseModMetadata(json, mod)) {
            LogFormat("[NorthstarPS4] skipping invalid mod metadata: %s\n", path);
            return;
        }
        if (!includeDisabled && !IsModEnabled(enabled, mod)) {
            LogFormat("[NorthstarPS4] mod disabled: %s %s\n", mod.name, mod.version);
            return;
        }
        if (!InsertMod(discovery, folder, mod.loadPriority))
            LogFormat("[NorthstarPS4] mod catalog capacity exceeded: %s\n", folder);
    };
    DIR* const dir = opendir(kModsRoot);
    if (dir != nullptr) {
        while (struct dirent* entry = readdir(dir)) collect(entry->d_name);
        closedir(dir);
        return;
    }
    // Emulator fallback only. Normal installs discover folders at each boot.
    LogFormat("[NorthstarPS4] opendir failed: %s; using staging index\n", kModsRoot);
    static char buffer[kModJsonBufferSize];
    std::snprintf(path, sizeof(path), "%s/.ns_mod_manifest", kModsRoot);
    if (!ReadFileIntoBuffer(path, buffer, sizeof(buffer), size)) return;
    char* line = buffer;
    while (*line) {
        char* newline = std::strchr(line, '\n');
        if (newline) *newline = '\0';
        const std::size_t length = std::strlen(line);
        if (length && line[length - 1] == '\r') line[length - 1] = '\0';
        collect(line);
        if (!newline) break;
        line = newline + 1;
    }
}

using ModFindVarFn = void* (*)(void*, const char*);
using ModConVarConstructorFn = void (*)(
    void*, const char*, const char*, int, const char*, void*);

void RegisterModConVars(const ModInfo& mod, std::int32_t& poolIndex,
    void* cvar, ModFindVarFn findVar,
    ModConVarConstructorFn constructor) noexcept {
    alignas(16) static std::uint8_t objects[kMaxModConVars][0x90]{};
    static char names[kMaxModConVars][64]{};
    static char defaults[kMaxModConVars][64]{};
    static char help[kMaxModConVars][128]{};
    for (std::int32_t i = 0; i < mod.conVarCount &&
        poolIndex < static_cast<std::int32_t>(kMaxModConVars); ++i) {
        const ModConVarInfo& info = mod.conVars[i];
        const std::int32_t slot = poolIndex;
        std::strncpy(names[slot], info.name, sizeof(names[0]) - 1);
        names[slot][sizeof(names[0]) - 1] = '\0';
        std::strncpy(defaults[slot], info.defaultValue,
            sizeof(defaults[0]) - 1);
        defaults[slot][sizeof(defaults[0]) - 1] = '\0';
        std::snprintf(help[slot], sizeof(help[0]),
            "Northstar PS4 mod convar (%s)", mod.name);
        void* registered = findVar(cvar, names[slot]);
        if (registered == nullptr) {
            constructor(objects[slot], names[slot], defaults[slot], 0,
                help[slot], nullptr);
            registered = findVar(cvar, names[slot]);
        }
        LogFormat(
            "[NorthstarPS4] mod convar name=%s default=%s flags=%s result=%p success=%d\n",
            names[slot], defaults[slot], info.flags, registered,
            registered == objects[slot] ? 1 : 0);
        ++poolIndex;
    }
}

void ProbeModMetadata(void* cvar, ModFindVarFn findVar,
    std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    LogFormat("[NorthstarPS4] mod metadata probe start cvar=%p\n", cvar);
    ModDiscovery discovery{};
    CollectModNames(discovery);
    LogFormat("[NorthstarPS4] mod metadata discovered %d mod(s)\n",
        discovery.count);

    const bool constructorMatches = ValidateEnginePreimage(
        engineBase, engineSize, kConVarConstructorVa, kConVarConstructorPreimage,
        sizeof(kConVarConstructorPreimage));
    const bool callsiteMatches = ValidateEnginePreimage(
        engineBase, engineSize, kSvCheatsConstructorCallVa, kSvCheatsCallPreimage,
        sizeof(kSvCheatsCallPreimage));
    LogFormat("[NorthstarPS4] mod metadata gate constructor=%d callsite=%d\n",
        constructorMatches ? 1 : 0, callsiteMatches ? 1 : 0);
    if (!constructorMatches || !callsiteMatches) {
        LogFormat("[NorthstarPS4] mod metadata refused: profile preimage mismatch\n");
        return;
    }
    auto constructor = reinterpret_cast<ModConVarConstructorFn>(
        engineBase + kConVarConstructorVa);

    std::int32_t conVarSlot = 0;
    for (std::int32_t i = 0; i < discovery.count; ++i) {
        char path[160]{};
        std::snprintf(path, sizeof(path), "/app0/R2Northstar/mods/%s/mod.json",
            discovery.names[i]);
        static char jsonBuffer[kModJsonBufferSize];
        std::size_t jsonSize = 0;
        if (!ReadFileIntoBuffer(path, jsonBuffer,
                sizeof(jsonBuffer) - 1, jsonSize)) {
            LogFormat("[NorthstarPS4] mod metadata read failed: %s\n", path);
            continue;
        }
        ModInfo mod{};
        if (!ParseModMetadata(jsonBuffer, mod)) {
            LogFormat("[NorthstarPS4] mod metadata parse failed: %s\n", path);
            continue;
        }
        LogFormat(
            "[NorthstarPS4] mod %s version=%s priority=%d init=%s description=%s\n",
            mod.name, mod.version, mod.loadPriority, mod.initScript,
            mod.description);
        LogFormat("[NorthstarPS4] mod %s convars=%d scripts=%d uiScripts=%d\n",
            mod.name, mod.conVarCount, mod.scriptCount, mod.uiScriptCount);
        for (std::int32_t s = 0; s < mod.conVarCount; ++s) {
            LogFormat(
                "[NorthstarPS4] mod %s convar[%d] name=%s default=%s flags=%s\n",
                mod.name, s, mod.conVars[s].name,
                mod.conVars[s].defaultValue, mod.conVars[s].flags);
        }
        for (std::int32_t s = 0; s < mod.uiScriptCount; ++s) {
            LogFormat("[NorthstarPS4] mod %s uiScript[%d]=%s\n",
                mod.name, s, mod.uiScripts[s]);
        }
        RegisterModConVars(mod, conVarSlot, cvar, findVar, constructor);
#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT)
        // InitScript goes first: run 20260807-153420 showed
        // ui/menu_ns_modmenu.nut fail to compile with "Expected type, found
        // identifier ModInfo" because that struct is declared only in
        // Northstar.Client's InitScript (cl_northstar_client_init.nut, no
        // RunOn of its own). CompileList appears to build a single growing
        // symbol table across one call's path list, so compiling InitScript
        // first makes its struct/type declarations visible to the mod's UI
        // scripts compiled after it in the same call.
        if (mod.initScript[0] != '\0' &&
            gCollectedUiScriptCount < static_cast<std::int32_t>(kMaxCollectedUiScripts)) {
            std::strncpy(gCollectedUiScripts[gCollectedUiScriptCount], mod.initScript,
                sizeof(gCollectedUiScripts[0]) - 1);
            gCollectedUiScripts[gCollectedUiScriptCount][sizeof(gCollectedUiScripts[0]) - 1] = '\0';
            ++gCollectedUiScriptCount;
        }
        for (std::int32_t s = 0; s < mod.uiScriptCount &&
            gCollectedUiScriptCount < static_cast<std::int32_t>(kMaxCollectedUiScripts); ++s) {
            std::strncpy(gCollectedUiScripts[gCollectedUiScriptCount], mod.uiScripts[s],
                sizeof(gCollectedUiScripts[0]) - 1);
            gCollectedUiScripts[gCollectedUiScriptCount][sizeof(gCollectedUiScripts[0]) - 1] = '\0';
            ++gCollectedUiScriptCount;
        }
#endif
    }
    LogFormat("[NorthstarPS4] mod metadata probe complete convarsRegistered=%d\n",
        conVarSlot);
}
#endif
#include "runtime_auth.inl"

void ProbeCvarInterface(
    OrbisKernelModule vstdlibHandle, std::uintptr_t engineBase,
    std::size_t engineSize) noexcept {
    using CreateInterfaceFn = void* (*)(const char*, int*);
    using FindVarFn = void* (*)(void*, const char*);

    void* createInterfaceAddress = nullptr;
    const int dlsymResult =
        sceKernelDlsym(static_cast<int32_t>(vstdlibHandle), "CreateInterface",
            &createInterfaceAddress);
    LogFormat("[NorthstarPS4] cvar probe dlsymResult=0x%x CreateInterface=%p vstdlibHandle=0x%x\n",
        dlsymResult, createInterfaceAddress, vstdlibHandle);
    if (dlsymResult != 0 || createInterfaceAddress == nullptr) return;

    auto createInterface = reinterpret_cast<CreateInterfaceFn>(createInterfaceAddress);
    int interfaceResult = -1;
    void* cvar = createInterface("VEngineCvar007", &interfaceResult);
    LogFormat("[NorthstarPS4] cvar probe interface=%p result=%d\n",
        cvar, interfaceResult);
    if (cvar == nullptr) return;

    auto vtable = *reinterpret_cast<void***>(cvar);
    if (vtable == nullptr || vtable[16] == nullptr) {
        LogFormat("[NorthstarPS4] cvar probe invalid vtable\n");
        return;
    }

    auto findVar = reinterpret_cast<FindVarFn>(vtable[16]);
    void* svCheats = findVar(cvar, "sv_cheats");
    LogFormat("[NorthstarPS4] cvar probe vtable=%p FindVar[16]=%p sv_cheats=%p\n",
        vtable, vtable[16], svCheats);
    ProbeAuthConVars(cvar, findVar, engineBase, engineSize);
    ApplyAtlasIdentity(cvar, findVar);
    AllowMultiplayerMenu(cvar, findVar);
    CaptureServerFilter(cvar, findVar);
    // Always validated and resolved now: ns_allow_team_change and
    // ns_has_agreed_to_send_token below are unconditional, proven-required
    // registrations, not just the diagnostic/experimental ones.
    const bool constructorMatches = ValidateEnginePreimage(
        engineBase, engineSize, kConVarConstructorVa, kConVarConstructorPreimage,
        sizeof(kConVarConstructorPreimage));
    const bool callsiteMatches = ValidateEnginePreimage(
        engineBase, engineSize, kSvCheatsConstructorCallVa, kSvCheatsCallPreimage,
        sizeof(kSvCheatsCallPreimage));
    LogFormat("[NorthstarPS4] convar mutation gate base=%p size=0x%zx constructor=%d callsite=%d\n",
        reinterpret_cast<void*>(engineBase), engineSize,
        constructorMatches ? 1 : 0, callsiteMatches ? 1 : 0);
    if (!constructorMatches || !callsiteMatches) {
        LogFormat("[NorthstarPS4] convar mutation refused: profile preimage mismatch\n");
        return;
    }

    using ConVarConstructorFn = void (*)(
        void*, const char*, const char*, int, const char*, void*);
    auto constructor = reinterpret_cast<ConVarConstructorFn>(
        engineBase + kConVarConstructorVa);

#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_CONVAR)
    alignas(16) static std::uint8_t diagnosticConVar[0x90]{};
    void* registered = findVar(cvar, "ns_stage2_loaded");
    if (registered == nullptr) {
        constructor(diagnosticConVar, "ns_stage2_loaded", "1", 0,
            "Northstar PS4 Stage 2 diagnostic", nullptr);
        registered = findVar(cvar, "ns_stage2_loaded");
    }
    LogFormat("[NorthstarPS4] diagnostic convar registration result=%p expected=%p success=%d\n",
        registered, diagnosticConVar, registered == diagnosticConVar ? 1 : 0);
#else
    LogFormat("[NorthstarPS4] diagnostic convar disabled at build time\n");
#endif

    // ns_allow_team_change and ns_has_agreed_to_send_token are always
    // registered (not build-flag-gated) because they are proven-required
    // for basic UI functionality, not experimental: the shipped
    // ui/menu_ingame.nut and ui/menu_main.nut / ui/panel_mainmenu.nut
    // unconditionally call GetConVarBool()/GetConVarInt() on these, and the
    // engine throws a blocking "[UI] ConVar ... is not valid" dialog when
    // either is missing. Confirmed live 2026-08-07 (first
    // ns_allow_team_change, then ns_has_agreed_to_send_token) while testing
    // a real server connection -- see docs/GOALS.md Goal 8 and
    // docs/TECHNICAL-NOTES.md. `-EnableTeamChangesConVar` is kept as an
    // accepted but now-redundant build flag for compatibility with existing
    // scripts/docs.
    alignas(16) static std::uint8_t teamChangesConVar[0x90]{};
    void* teamChangesRegistered = findVar(cvar, "ns_allow_team_change");
    if (teamChangesRegistered == nullptr) {
        constructor(teamChangesConVar, "ns_allow_team_change", "0", 0,
            "Allow players to change teams", nullptr);
        teamChangesRegistered = findVar(cvar, "ns_allow_team_change");
    }
    LogFormat("[NorthstarPS4] team changes convar registration result=%p expected=%p success=%d default=0 flags=0\n",
        teamChangesRegistered, teamChangesConVar,
        teamChangesRegistered == teamChangesConVar ? 1 : 0);

    // ns_has_agreed_to_send_token: NorthstarLauncher registers this as an
    // int ConVar with string default "0" (== NOT_DECIDED_TO_SEND_TOKEN in
    // its client/clientauthhooks.cpp; 1 == agreed, 2 == disagreed), flag
    // FCVAR_ARCHIVE_PLAYERPROFILE on PC. Registered here with flags=0 until
    // PS4 flag-bit semantics are verified, matching the existing mod-convar
    // convention (see ProbeModMetadata's ARCHIVE_PLAYERPROFILE note).
    //
    // PS4-only default override (2026-08-07): defaulted to "1"
    // (NS_AGREED_TO_SEND_TOKEN) instead of upstream's "0". Live testing
    // showed ui/menu_main.nut's NorthstarMasterServerAuthDialog(), the
    // dialog shown when this convar is unset, does not respond to any
    // keyboard, mouse, or controller input on this port (confirmed: no
    // hover/click/keypress reaches it at all, including the native
    // toggleconsole bind that is otherwise input-independent of the UI
    // focus system) -- so the dialog is a hard blocker with no way to
    // dismiss it. Pre-agreeing here skips the dialog entirely (see
    // menu_main.nut's `if ( !GetConVarBool( "ns_has_agreed_to_send_token" ) )
    // NorthstarMasterServerAuthDialog()` gate) so players can get past the
    // main menu. The underlying dialog-input bug is still open -- see
    // docs/TECHNICAL-NOTES.md -- and this default should be revisited once
    // that's fixed, since it silently opts every player in to sending their
    // origin token to the Northstar masterserver without asking.
    alignas(16) static std::uint8_t agreedToSendTokenConVar[0x90]{};
    void* agreedToSendTokenRegistered = findVar(cvar, "ns_has_agreed_to_send_token");
    if (agreedToSendTokenRegistered == nullptr) {
        constructor(agreedToSendTokenConVar, "ns_has_agreed_to_send_token", "1", 0,
            "whether the user has agreed to send their origin token to the northstar masterserver",
            nullptr);
        agreedToSendTokenRegistered = findVar(cvar, "ns_has_agreed_to_send_token");
    }
    LogFormat("[NorthstarPS4] ns_has_agreed_to_send_token convar registration result=%p expected=%p success=%d default=1 flags=0\n",
        agreedToSendTokenRegistered, agreedToSendTokenConVar,
        agreedToSendTokenRegistered == agreedToSendTokenConVar ? 1 : 0);

    // ns_auth_allow_insecure: a server-side Northstar convar the *client* UI
    // also reads. `ui/atlas_auth.nut` and `ui/panel_mainmenu.nut` both call
    // `GetConVarBool("ns_auth_allow_insecure")`, and on this build that lookup
    // raised `SCRIPT ERROR: [UI] ConVar ns_auth_allow_insecure is not valid`.
    // It only began firing once master-server authentication started reporting
    // true, because until then the surrounding branches short-circuited before
    // reaching it. Registered with upstream's default of "0" so the client
    // behaves as an ordinary authenticated client rather than skipping checks.
    alignas(16) static std::uint8_t authAllowInsecureConVar[0x90]{};
    void* authAllowInsecureRegistered = findVar(cvar, "ns_auth_allow_insecure");
    if (authAllowInsecureRegistered == nullptr) {
        constructor(authAllowInsecureConVar, "ns_auth_allow_insecure", "0", 0,
            "Skip player authentication checks", nullptr);
        authAllowInsecureRegistered = findVar(cvar, "ns_auth_allow_insecure");
    }
    LogFormat("[NorthstarPS4] ns_auth_allow_insecure convar registration result=%p expected=%p success=%d default=0 flags=0\n",
        authAllowInsecureRegistered, authAllowInsecureConVar,
        authAllowInsecureRegistered == authAllowInsecureConVar ? 1 : 0);

#if defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
    ProbeModMetadata(cvar, findVar, engineBase, engineSize);
#endif
}
void ProbeUiRegistrationTree(
    std::uintptr_t clientBase, std::size_t clientSpan,
    std::uintptr_t treeVa, const char* label) noexcept {
    const std::uintptr_t imageEnd = clientBase + clientSpan;
    const std::uintptr_t target = clientBase + kClientRunUiScriptRecordVa;
    std::uintptr_t node = clientBase + treeVa;
    for (std::uint32_t depth = 0; depth < 32; ++depth) {
        if (node < clientBase || node > imageEnd - 0x60) {
            LogFormat("[NorthstarPS4] UI tree %s depth=%u invalid=%p\n",
                label, depth, reinterpret_cast<void*>(node));
            return;
        }
        auto fields = reinterpret_cast<const std::uintptr_t*>(node);
        const auto countA = *reinterpret_cast<const std::int32_t*>(node + 0x38);
        const auto countB = *reinterpret_cast<const std::int32_t*>(node + 0x58);
        const std::uintptr_t recordsA = fields[4];
        const std::uintptr_t recordsB = fields[8];
        const std::uintptr_t targetName = clientBase + kClientRunUiScriptNameVa;
        std::int32_t matchA = -1;
        std::int32_t matchB = -1;
        for (std::int32_t i = 0; recordsA != 0 && i < countA && i < 512; ++i) {
            if (*reinterpret_cast<const std::uintptr_t*>(recordsA + i * 0x68) == targetName)
                matchA = i;
        }
        for (std::int32_t i = 0; recordsB != 0 && i < countB && i < 512; ++i) {
            if (*reinterpret_cast<const std::uintptr_t*>(recordsB + i * 0x68) == targetName)
                matchB = i;
        }
        LogFormat("[NorthstarPS4] UI tree %s RunUIScript matchA=%d matchB=%d\n",
            label, matchA, matchB);
        const bool ownsA = countA > 0 && recordsA <= target &&
            target < recordsA + static_cast<std::uintptr_t>(countA) * 0x68;
        const bool ownsB = countB > 0 && recordsB <= target &&
            target < recordsB + static_cast<std::uintptr_t>(countB) * 0x68;
        LogFormat("[NorthstarPS4] UI tree %s depth=%u node=%p child=%p recordsA=%p countA=%d recordsB=%p countB=%d target=%d/%d\n",
            label, depth, reinterpret_cast<void*>(node),
            reinterpret_cast<void*>(fields[3]), reinterpret_cast<void*>(recordsA),
            countA, reinterpret_cast<void*>(recordsB), countB,
            ownsA ? 1 : 0, ownsB ? 1 : 0);
        if (ownsA || ownsB || fields[3] == 0) return;
        node = fields[3];
    }
    LogFormat("[NorthstarPS4] UI tree %s traversal limit reached\n", label);
}
void ProbeUiVm(std::uintptr_t clientBase, std::size_t clientSpan) noexcept {
    const bool runUiScriptMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientRunUiScriptVa,
        kClientRunUiScriptPreimage, sizeof(kClientRunUiScriptPreimage));
    const bool uiVmLoadMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientUiVmLoadVa,
        kClientUiVmLoadPreimage, sizeof(kClientUiVmLoadPreimage));
    LogFormat("[NorthstarPS4] UI VM probe gate base=%p span=0x%zx runUiScript=%d uiVmLoad=%d\n",
        reinterpret_cast<void*>(clientBase), clientSpan,
        runUiScriptMatches ? 1 : 0, uiVmLoadMatches ? 1 : 0);
    if (!runUiScriptMatches || !uiVmLoadMatches ||
        kClientScriptOwnerGlobalVa + sizeof(void*) > clientSpan) {
        LogFormat("[NorthstarPS4] UI VM probe refused: client profile mismatch\n");
        return;
    }

    void* previousOwner = reinterpret_cast<void*>(~std::uintptr_t{0});
    void* previousUiVm = reinterpret_cast<void*>(~std::uintptr_t{0});
    auto ownerSlot = reinterpret_cast<void**>(
        clientBase + kClientScriptOwnerGlobalVa);
    for (std::uint32_t attempt = 0; attempt < 600; ++attempt) {
        void* owner = *ownerSlot;
        void* uiVm = owner != nullptr
            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(owner) + 8)
            : nullptr;
        if (owner != previousOwner || uiVm != previousUiVm) {
            LogFormat("[NorthstarPS4] UI VM probe attempt=%u owner=%p uiVm=%p\n",
                attempt, owner, uiVm);
            previousOwner = owner;
            previousUiVm = uiVm;
        }
        if (uiVm != nullptr) {
            auto fields = reinterpret_cast<const std::uintptr_t*>(uiVm);
            for (std::uint32_t field = 0; field < 16; ++field) {
                LogFormat("[NorthstarPS4] UI VM candidate field[%u]=%p\n",
                    field, reinterpret_cast<void*>(fields[field]));
            }
            const bool registerMatches = ValidateEnginePreimage(
                clientBase, clientSpan, kClientRegisterSquirrelFuncVa,
                kClientRegisterSquirrelFuncPreimage,
                sizeof(kClientRegisterSquirrelFuncPreimage));
            const bool exemplarMatches = ValidateEnginePreimage(
                clientBase, clientSpan, kClientRunUiScriptRegistrationVa,
                kClientRunUiScriptRegistrationPreimage,
                sizeof(kClientRunUiScriptRegistrationPreimage));
            LogFormat("[NorthstarPS4] UI registration probe sqvm=%p register=%d exemplar=%d\n",
                reinterpret_cast<void*>(fields[10]), registerMatches ? 1 : 0,
                exemplarMatches ? 1 : 0);
            const bool blockMatches = ValidateEnginePreimage(
                clientBase, clientSpan, kClientNativeRegistrationBlockVa,
                kClientNativeRegistrationBlockPreimage,
                sizeof(kClientNativeRegistrationBlockPreimage));
            const bool callerMatches = ValidateEnginePreimage(
                clientBase, clientSpan, kClientNativeRegistrationCallerVa,
                kClientNativeRegistrationCallerPreimage,
                sizeof(kClientNativeRegistrationCallerPreimage));
            void* registrationVm = *reinterpret_cast<void**>(
                clientBase + kClientRegistrationVmGlobalVa);
            LogFormat("[NorthstarPS4] UI registration block vm=%p uiVm=%p same=%d block=%d caller=%d\n",
                registrationVm, uiVm, registrationVm == uiVm ? 1 : 0,
                blockMatches ? 1 : 0, callerMatches ? 1 : 0);

            ProbeUiRegistrationTree(clientBase, clientSpan, kClientUiRegistrationTreeAVa, "A");
            ProbeUiRegistrationTree(clientBase, clientSpan, kClientUiRegistrationTreeBVa, "B");

            // Read-only profile of internal_vm+0x40a0 and its neighbors
            // during a normal boot, before any CompileList injection is
            // attempted. This is the table CompileList reads (at its own
            // +0x38) that faulted when compiling a real mod script with a
            // typed struct parameter (ui/menu_ns_modmenu.nut, ModInfo) --
            // see docs/TECHNICAL-NOTES.md and docs/GOALS.md Goal 6. Pure
            // reads plus logging; no mutation, no dereference beyond one
            // plausibility-gated level.
            {
                auto internalVmForDump = reinterpret_cast<const std::uintptr_t*>(fields[10]);
                if (internalVmForDump != nullptr) {
                    constexpr std::uintptr_t kOffsetsToProbe[] = {
                        0x4090, 0x4098, 0x40a0, 0x40a8, 0x40b0,
                    };
                    for (std::uintptr_t off : kOffsetsToProbe) {
                        const std::uintptr_t value = internalVmForDump[off / 8];
                        LogFormat("[NorthstarPS4] UI VM internal+0x%zx=%p\n",
                            off, reinterpret_cast<void*>(value));
                    }
                    const std::uintptr_t table40a0 = internalVmForDump[0x40a0 / 8];
                    const bool plausible = table40a0 > 0x100000000ULL &&
                        table40a0 < 0x40000000000ULL;
                    if (plausible) {
                        auto tableBytes = reinterpret_cast<const std::uintptr_t*>(table40a0);
                        for (int i = 0; i < 8; ++i) {
                            LogFormat("[NorthstarPS4] UI VM internal+0x40a0[+0x%x]=%p\n",
                                i * 8, reinterpret_cast<void*>(tableBytes[i]));
                        }
                    } else {
                        LogFormat("[NorthstarPS4] UI VM internal+0x40a0 is not a plausible pointer (value=%p)\n",
                            reinterpret_cast<void*>(table40a0));
                    }
                }
            }

#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_UI_NATIVE)
            VerifyAndRestoreDiagnosticUiRecord(clientBase, clientSpan, uiVm);
#endif
            LogFormat("[NorthstarPS4] UI VM candidate discovered owner=%p uiVm=%p\n",
                owner, uiVm);
            return;
        }
        sceKernelUsleep(100000);
    }
    LogFormat("[NorthstarPS4] UI VM probe timed out\n");
}
#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_PROBE)
void ProbeUiScriptSystem(
    std::uintptr_t clientBase, std::size_t clientSpan,
    OrbisKernelModule fsHandle) noexcept {
    const bool uiInitMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientUiInitVa,
        kClientUiInitPreimage, sizeof(kClientUiInitPreimage));
    const bool rsonLoaderMatches = ValidateEnginePreimage(
        clientBase, clientSpan, kClientRsonLoaderVa,
        kClientRsonLoaderPreimage, sizeof(kClientRsonLoaderPreimage));
    const bool rangeOk =
        kClientScriptSystemGlobalVa + sizeof(void*) <= clientSpan &&
        kClientScriptCountTableVa + 8 * sizeof(std::int32_t) <= clientSpan;
    LogFormat("[NorthstarPS4] M6 script probe gate uiInit=%d rsonLoader=%d rangeOk=%d span=0x%zx\n",
        uiInitMatches ? 1 : 0, rsonLoaderMatches ? 1 : 0, rangeOk ? 1 : 0, clientSpan);
    if (!uiInitMatches || !rsonLoaderMatches || !rangeOk) {
        LogFormat("[NorthstarPS4] M6 script probe refused: client profile mismatch\n");
        return;
    }

    auto ownerSlot = reinterpret_cast<void**>(clientBase + kClientScriptOwnerGlobalVa);
    auto scriptSystemSlot = reinterpret_cast<void**>(clientBase + kClientScriptSystemGlobalVa);
    auto countTable = reinterpret_cast<std::int32_t*>(clientBase + kClientScriptCountTableVa);

    bool sawOwner = false;
    bool sawSystem = false;
    bool sawUiReady = false;
    for (std::uint32_t attempt = 0; attempt < 600; ++attempt) {
        void* owner = *ownerSlot;
        void* uiVm = owner != nullptr
            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(owner) + 8)
            : nullptr;
        std::int32_t contextType = owner != nullptr
            ? *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uintptr_t>(owner) + 0x3c)
            : -1;
        void* scriptSystem = *scriptSystemSlot;
        const std::int32_t counts[8] = {
            countTable[0], countTable[1], countTable[2], countTable[3],
            countTable[4], countTable[5], countTable[6], countTable[7],
        };
        const bool uiReady = scriptSystem != nullptr && countTable[2] > 0;
        const bool ownerNew = owner != nullptr && !sawOwner;
        const bool systemNew = scriptSystem != nullptr && !sawSystem;
        const bool readyNew = uiReady && !sawUiReady;
        if (ownerNew || systemNew || readyNew || (attempt % 50 == 0)) {
            LogFormat("[NorthstarPS4] M6 script probe attempt=%u owner=%p uiVm=%p ctx=%d system=%p counts=%d,%d,%d,%d,%d,%d,%d,%d\n",
                attempt, owner, uiVm, contextType, scriptSystem,
                counts[0], counts[1], counts[2], counts[3],
                counts[4], counts[5], counts[6], counts[7]);
        }
        if (owner != nullptr) sawOwner = true;
        if (scriptSystem != nullptr) sawSystem = true;
        if (uiReady) {
            sawUiReady = true;
            break;
        }
        sceKernelUsleep(150000);
    }

    const std::int32_t counts[8] = {
        countTable[0], countTable[1], countTable[2], countTable[3],
        countTable[4], countTable[5], countTable[6], countTable[7],
    };
    LogFormat("[NorthstarPS4] M6 script probe final owner=%p system=%p counts=%d,%d,%d,%d,%d,%d,%d,%d\n",
        *ownerSlot, *scriptSystemSlot, counts[0], counts[1], counts[2], counts[3],
        counts[4], counts[5], counts[6], counts[7]);

#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT)
    {
        void* const owner = *ownerSlot;
        if (owner == nullptr) {
            LogFormat("[NorthstarPS4] M6 script inject skipped: owner null\n");
        } else {
            std::int32_t prevCount = -1;
            std::uint32_t stableSamples = 0;
            std::uint32_t settleAttempts = 0;
            for (; settleAttempts < 120; ++settleAttempts) {
                const std::int32_t cur = countTable[2];
                if (cur == prevCount) {
                    ++stableSamples;
                } else {
                    stableSamples = 0;
                }
                prevCount = cur;
                if (stableSamples >= 20) break;
                sceKernelUsleep(250000);
            }
            LogFormat("[NorthstarPS4] M6 script inject settle attempts=%u stable=%u count=%d\n",
                settleAttempts, stableSamples, prevCount);
            if (stableSamples < 20) {
                LogFormat("[NorthstarPS4] M6 script inject skipped: UI script count unstable (race with engine loader)\n");
            } else {
                const bool findUiFunctionMatches = ValidateEnginePreimage(
                    clientBase, clientSpan, kClientFindUiFunctionVa,
                    kClientFindUiFunctionPreimage, sizeof(kClientFindUiFunctionPreimage));
                LogFormat("[NorthstarPS4] M6 script inject gate findUiFunction=%d\n",
                    findUiFunctionMatches ? 1 : 0);
                if (!findUiFunctionMatches) {
                    LogFormat("[NorthstarPS4] M6 script inject refused: findUiFunction preimage mismatch\n");
                } else {
                using FindUiFunctionFn = void* (*)(void*, const char*, std::int32_t, std::int32_t);
                auto findUiFunction =
                    reinterpret_cast<FindUiFunctionFn>(clientBase + kClientFindUiFunctionVa);
                void* const uiVm =
                    *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(owner) + 8);

                // Safe, read-only check first: does the scripts.rson manifest overlay
                // (New-Stage2R2Overlay.ps1 + Merge-Stage2ScriptsRson.ps1, staged under
                // r2/scripts/vscripts/scripts.rson) already cause the engine's own
                // boot-time compile pass to load and register ns_m6_probe.gnut? This
                // answers the actual Milestone 6 question (manifest-driven script
                // loading) without any risky call into the engine.
                void* const ctrlScript = findUiFunction(uiVm, "ui_main_menu", 0, 0);
                LogFormat("[NorthstarPS4] M6 script manifest verify FindUiFunction ui_main_menu=%p baseline=%d\n",
                    ctrlScript, ctrlScript != nullptr ? 1 : 0);
                void* const probeClosure = findUiFunction(uiVm, "NSM6ProbeMarker", 0, 0);
                LogFormat("[NorthstarPS4] M6 script manifest verify FindUiFunction NSM6ProbeMarker=%p viaManifest=%d\n",
                    probeClosure, probeClosure != nullptr ? 1 : 0);
                void* const probeScript = findUiFunction(uiVm, "ns_m6_probe", 0, 0);
                LogFormat("[NorthstarPS4] M6 script manifest verify FindUiFunction ns_m6_probe=%p viaManifest=%d\n",
                    probeScript, probeScript != nullptr ? 1 : 0);

                // Runtime compileList() injection candidate below. This previously
                // crashed with an unhandled exception at RIP=0x0 (run 20260806-214046):
                // CompileList's internal per-item call reads a global object's vtable
                // and calls the pointer at slot +0x58, which was null when invoked from
                // our own detached NorthstarPS4 thread rather than whatever thread the
                // engine uses for its own compile pass. Validate the exact dependency
                // chain and refuse instead of repeating that crash.
                const bool compileListPrologueMatches = ValidateEnginePreimage(
                    clientBase, clientSpan, kClientCompileListVa,
                    kClientCompileListPreimage, sizeof(kClientCompileListPreimage));
                void* compileListGateObject = nullptr;
                void* compileListGateVtable = nullptr;
                void* compileListGateSlot = nullptr;
                if (compileListPrologueMatches &&
                    kClientCompileListGatePtrVa + sizeof(void*) <= clientSpan) {
                    compileListGateObject = *reinterpret_cast<void**>(
                        clientBase + kClientCompileListGatePtrVa);
                    if (compileListGateObject != nullptr) {
                        compileListGateVtable =
                            *reinterpret_cast<void**>(compileListGateObject);
                    }
                    if (compileListGateVtable != nullptr) {
                        compileListGateSlot = *reinterpret_cast<void**>(
                            reinterpret_cast<std::uintptr_t>(compileListGateVtable) +
                            kClientCompileListGateVtableSlot);
                    }
                }
                const bool compileListDependencyReady = compileListGateSlot != nullptr;
                LogFormat("[NorthstarPS4] M6 script inject compileList gate prologue=%d object=%p vtable=%p slot58=%p ready=%d\n",
                    compileListPrologueMatches ? 1 : 0, compileListGateObject,
                    compileListGateVtable, compileListGateSlot,
                    compileListDependencyReady ? 1 : 0);
                if (!compileListPrologueMatches || !compileListDependencyReady) {
                    LogFormat("[NorthstarPS4] M6 script inject refused: compileList dependency not safe from this thread; relying on the manifest verify result above\n");
                } else {
                    void* const poolCur =
                        *reinterpret_cast<void**>(clientBase + kClientScriptPoolCurVa);
                    void* const poolEnd =
                        *reinterpret_cast<void**>(clientBase + kClientScriptPoolEndVa);
                    void* const vmObj =
                        uiVm != nullptr
                            ? *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(uiVm) + 0x50)
                            : nullptr;
                    std::int32_t vmPoolCounter = -1;
                    void* vmPoolPtr = nullptr;
                    if (vmObj != nullptr) {
                        vmPoolCounter =
                            *reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uintptr_t>(vmObj) + 0x4210);
                        vmPoolPtr =
                            *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(vmObj) + 0x4218);
                    }
                    LogFormat("[NorthstarPS4] M6 script inject pool cur=%p end=%p free=0x%zx vmObj=%p counter=%d vmPool=%p\n",
                        poolCur, poolEnd,
                        poolCur != nullptr && poolEnd != nullptr
                            ? reinterpret_cast<std::uintptr_t>(poolEnd) - reinterpret_cast<std::uintptr_t>(poolCur)
                            : 0,
                        vmObj, vmPoolCounter, vmPoolPtr);
                    {
                        static std::uint8_t sInjectPool[0x1400000];
                        *reinterpret_cast<void**>(clientBase + kClientScriptPoolCurVa) = sInjectPool;
                        *reinterpret_cast<void**>(clientBase + kClientScriptPoolEndVa) =
                            sInjectPool + sizeof(sInjectPool);
                        LogFormat("[NorthstarPS4] M6 script inject pool staged static=%p\n",
                            static_cast<void*>(sInjectPool));
                    }
                    using CompileListFn = bool (*)(void*, std::int32_t, const char* const*, std::int32_t);
                    auto compileList =
                        reinterpret_cast<CompileListFn>(clientBase + kClientCompileListVa);
                    static const char* const kProbeInjectPaths[] = {
                        "ns_m6_probe.gnut",
                    };
                    const char* const* injectPaths = kProbeInjectPaths;
                    std::int32_t injectCount = static_cast<std::int32_t>(
                        sizeof(kProbeInjectPaths) / sizeof(kProbeInjectPaths[0]));
#if defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
                    LogFormat("[NorthstarPS4] M6 script inject: %d mod UI script(s) collected (not injected unless -EnableM6ScriptInjectFromMods)\n",
                        gCollectedUiScriptCount);
#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT_FROM_MODS)
                    // Separately gated from mod discovery itself: run
                    // 20260807-154245 reproduced a real (non-null) crash
                    // compiling a real mod UI script (ui/menu_ns_modmenu.nut)
                    // this way -- a read of internal_vm+0x40a0 that faulted,
                    // most likely an internal type/struct registry not
                    // populated the way it would be for the engine's own
                    // boot-time compile pass. cl_northstar_client_init.nut
                    // alone compiled cleanly; menu_ns_modmenu.nut (which
                    // uses the ModInfo struct as a typed parameter) did not,
                    // even paired with only that one dependency. Do not
                    // enable this outside a deliberate, isolated experiment
                    // until that table dependency is understood -- see
                    // docs/TECHNICAL-NOTES.md and docs/GOALS.md (Goal 6).
                    static const char* modInjectPaths[kMaxCollectedUiScripts];
                    if (gCollectedUiScriptCount > 0) {
                        for (std::int32_t i = 0; i < gCollectedUiScriptCount; ++i) {
                            modInjectPaths[i] = gCollectedUiScripts[i];
                        }
                        injectPaths = modInjectPaths;
                        injectCount = gCollectedUiScriptCount;
                        LogFormat("[NorthstarPS4] M6 script inject using %d mod-discovered UI script(s) (EXPERIMENTAL, known to crash on some real scripts)\n",
                            injectCount);
                    } else {
                        LogFormat("[NorthstarPS4] M6 script inject: no mod UI scripts collected, falling back to probe path\n");
                    }
#endif
#endif
                    const std::int32_t before = countTable[2];
                    const bool compileOk =
                        compileList(owner, 2, injectPaths, injectCount);
                    const std::int32_t after = countTable[2];
                    LogFormat("[NorthstarPS4] M6 script inject ctx=2 count=%d result=%d owner=%p before=%d after=%d\n",
                        injectCount, compileOk ? 1 : 0, owner, before, after);
                    for (std::int32_t i = 0; i < injectCount; ++i) {
                        LogFormat("[NorthstarPS4] M6 script inject path[%d]=%s\n", i, injectPaths[i]);
                    }
                    void* const ctrlScript2 = findUiFunction(uiVm, "ui_main_menu", 0, 0);
                    LogFormat("[NorthstarPS4] M6 script inject FindUiFunction ui_main_menu=%p ctrl=%d\n",
                        ctrlScript2, ctrlScript2 != nullptr ? 1 : 0);
                    void* const probeClosure2 = findUiFunction(uiVm, "NSM6ProbeMarker", 0, 0);
                    LogFormat("[NorthstarPS4] M6 script inject FindUiFunction NSM6ProbeMarker=%p verified=%d\n",
                        probeClosure2, probeClosure2 != nullptr ? 1 : 0);
                    void* const probeScript2 = findUiFunction(uiVm, "ns_m6_probe", 0, 0);
                    LogFormat("[NorthstarPS4] M6 script inject FindUiFunction ns_m6_probe=%p verified=%d\n",
                        probeScript2, probeScript2 != nullptr ? 1 : 0);
                }
                }
            }
        }
        sceKernelUsleep(20000000);
    }
#endif

    if (fsHandle == static_cast<OrbisKernelModule>(-1)) {
        LogFormat("[NorthstarPS4] M6 script probe fs skipped: filesystem_stdio unavailable\n");
        return;
    }

    void* createInterfaceAddress = nullptr;
    const int dlsymResult =
        sceKernelDlsym(static_cast<std::int32_t>(fsHandle), "CreateInterface",
            &createInterfaceAddress);
    if (dlsymResult != 0 || createInterfaceAddress == nullptr) {
        LogFormat("[NorthstarPS4] M6 script probe fs dlsym=0x%x addr=%p\n",
            dlsymResult, createInterfaceAddress);
        return;
    }
    using CreateInterfaceFn = void* (*)(const char*, int*);
    using OpenFn = void* (*)(void**, const char*, const char*, const char*, std::int64_t);
    using ReadFn = std::int32_t (*)(void**, void*, std::int32_t, void*);
    using CloseFn = void (*)(void*, void*);
    auto createInterface = reinterpret_cast<CreateInterfaceFn>(createInterfaceAddress);
    int interfaceResult = -1;
    void* const fs = createInterface("VFileSystem017", &interfaceResult);
    if (fs == nullptr) {
        LogFormat("[NorthstarPS4] M6 script probe fs interface failed result=%d\n",
            interfaceResult);
        return;
    }
    auto vtable2 = *reinterpret_cast<void***>(
        reinterpret_cast<std::uintptr_t>(fs) + 8);
    if (vtable2 == nullptr) {
        LogFormat("[NorthstarPS4] M6 script probe fs invalid vtable2=%p\n",
            vtable2);
        return;
    }
    auto open = reinterpret_cast<OpenFn>(vtable2[2]);
    auto read = reinterpret_cast<ReadFn>(vtable2[0]);
    auto close = reinterpret_cast<CloseFn>(vtable2[3]);
    void* const fsFieldAddr =
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(fs) + 8);
    static std::uint8_t buffer[32768];

    const struct { const char* tag; const char* file; } targets[] = {
        { "scripts.rson", "scripts/vscripts/scripts.rson" },
        { "probe.gnut", "scripts/vscripts/ns_m6_probe.gnut" },
        { "init.nut", "scripts/vscripts/init.nut" },
    };
    for (const auto& target : targets) {
        void* const handle = open(
            reinterpret_cast<void**>(fsFieldAddr), target.file, "rb", "GAME", 0);
        if (handle == nullptr) {
            LogFormat("[NorthstarPS4] M6 script probe open %-12s failed\n",
                target.tag);
            continue;
        }
        const std::int32_t bytesRead =
            read(reinterpret_cast<void**>(fsFieldAddr), buffer,
                static_cast<std::int32_t>(sizeof(buffer)), handle);
        close(fs, handle);
        if (bytesRead <= 0) {
            LogFormat("[NorthstarPS4] M6 script probe read %-12s bytes=%d\n",
                target.tag, bytesRead);
            continue;
        }
        const std::int32_t bytes =
            bytesRead > static_cast<std::int32_t>(sizeof(buffer))
                ? static_cast<std::int32_t>(sizeof(buffer)) : bytesRead;
        LogFormat("[NorthstarPS4] M6 script probe read %-12s bytes=%d\n",
            target.tag, bytes);
        const std::int32_t head = bytes < 64 ? bytes : 64;
        LogFormat("[NorthstarPS4] M6 script probe %s head=%.*s\n",
            target.tag, head, reinterpret_cast<const char*>(buffer));
        const std::int32_t tailStart = bytes > 64 ? bytes - 64 : 0;
        LogFormat("[NorthstarPS4] M6 script probe %s tail=%.*s\n",
            target.tag, bytes - tailStart,
            reinterpret_cast<const char*>(buffer + tailStart));
        int markerHits = 0;
        for (std::int32_t i = 0; i + 15 <= bytes; ++i) {
            if (std::memcmp(buffer + i, "M6 script probe", 15) == 0) ++markerHits;
        }
        int probeGnutHits = 0;
        for (std::int32_t i = 0; i + 14 <= bytes; ++i) {
            if (std::memcmp(buffer + i, "ns_m6_probe.gnut", 14) == 0) ++probeGnutHits;
        }
        LogFormat("[NorthstarPS4] M6 script probe %s markerM6=%d probeGnut=%d\n",
            target.tag, markerHits, probeGnutHits);
    }
    LogFormat("[NorthstarPS4] M6 script probe done\n");
}
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_FS_OVERLAY)
namespace {
using FsOpenFn = void* (*)(void*, const char*, const char*, const char*, std::int64_t);
using FsReadFn = std::int32_t (*)(void*, void*, std::int32_t, void*);
using FsCloseFn = void (*)(void*, void*);

// Mod search-path overlay. The game's IBaseFileSystem secondary vtable lives
// at fs+8 and the engine dispatches open/read/close through it (slot 2 = open,
// 0 = read, 3 = close, proven by the probe below). We copy the whole vtable
// into writable memory, swap slot 2 for a resolution hook, and repoint fs+8.
// The hook serves mod files from their own /app0/R2Northstar/mods/<Name>/mod dir first
// (highest LoadPriority wins, mirroring PC AddSearchPath
// semantics where the last-registered path wins), then falls through to the
// engine's original search paths (loose /app0/r2 + VPK mounts). This replaces
// the old "dump mod content into r2" staging: mods now live and load from
// their own folder exactly like PC R2Northstar/mods/<Name>/mod.
constexpr std::size_t kFsVtableCopySlots = 256;
constexpr std::size_t kMaxModRoots = kMaxModNames;
constexpr std::size_t kModRootCapacity = 128;

std::uintptr_t g_fsVtableCopy[kFsVtableCopySlots]{};
char g_modRoots[kMaxModRoots][kModRootCapacity]{};
std::int32_t g_modRootCount = 0;

// Every file under every mod root, indexed once per boot.
//
// The overlay used to find mod files by trying `open()` on each mod root, for
// every file the engine asked for. Nearly every request is for a stock file
// no mod ships, so nearly every probe failed: in one session that joined a
// server, 55,513 of 55,590 failed opens (99.9%) were these probes, 19,280 of
// them between connecting and the "Connection to server timed out" - each a
// host syscall plus two log lines, piled onto shadPS4's shader compiles in the
// window where the client has to keep up with the server. PC Northstar
// indexes mod files up front for the same reason (ModManager's file map).
//
// Mods cannot change mid-session (enabling one already requires a restart), so
// one walk at overlay install is enough. Keys are lowercased because the host
// filesystem is case-insensitive and the probe it replaces matched that way.
// A sorted vector rather than a map: this module's static constructors never
// run, and a zeroed vector is a valid empty one.
//
// 800 files across 6 roots: failed opens per session 55,590 -> 103, boot to the
// Northstar lobby 70-74 s -> 52-57 s.
//
// It was disabled while shadPS4 5b92da8 was current: there the index turned the
// emulator's level-transition crash (VCRUNTIME140+0x1cca7 on
// GpuSchedPriorityPendingOpsRunner) from occasional into certain on the first
// map load, 3/3. That crash is gone in shadPS4 ca89b01 (buffer manager rewrite,
// #5047), so the index is on; older emulator builds should keep it off.
constexpr bool kModFileIndexEnabled = true;
struct ModFileEntry { std::string key; std::int32_t root; };
std::vector<ModFileEntry> g_modFileIndex;
std::atomic<bool> g_modFileIndexReady{false};

std::string ModFileKey(const char* normalized) {
    std::string key(normalized);
    for (auto& c : key)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return key;
}

void IndexModDirectory(std::int32_t root, const std::string& absolute, const std::string& relative,
    int depth) noexcept {
    if (depth > 16) return;
    DIR* dir = opendir(absolute.c_str());
    if (!dir) return;
    while (dirent* entry = readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        const std::string childAbsolute = absolute + "/" + entry->d_name;
        const std::string childRelative = relative.empty() ? std::string(entry->d_name)
                                                           : relative + "/" + entry->d_name;
        struct stat info{};
        if (stat(childAbsolute.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) IndexModDirectory(root, childAbsolute, childRelative, depth + 1);
        else g_modFileIndex.push_back({ModFileKey(childRelative.c_str()), root});
    }
    closedir(dir);
}

void BuildModFileIndex() noexcept {
    g_modFileIndex.clear();
    for (std::int32_t root = 0; root < g_modRootCount; ++root)
        IndexModDirectory(root, g_modRoots[root], std::string(), 0);
    // Highest root (priority) first within each key, then keep only that one.
    std::sort(g_modFileIndex.begin(), g_modFileIndex.end(),
        [](const ModFileEntry& a, const ModFileEntry& b) {
            return a.key != b.key ? a.key < b.key : a.root > b.root;
        });
    g_modFileIndex.erase(std::unique(g_modFileIndex.begin(), g_modFileIndex.end(),
        [](const ModFileEntry& a, const ModFileEntry& b) { return a.key == b.key; }),
        g_modFileIndex.end());
    g_modFileIndexReady.store(true, std::memory_order_release);
    LogFormat("[NorthstarPS4] mod file index: %zu files across %d roots\n",
        g_modFileIndex.size(), g_modRootCount);
}

// The highest-priority mod root that ships `normalized`, or -1. Falls back to
// the old per-root probe if the index was never built, so a failure here can
// only cost speed, never mods.
std::int32_t FindModFileRoot(const char* normalized) noexcept {
    if (!g_modFileIndexReady.load(std::memory_order_acquire)) {
        for (std::int32_t i = g_modRootCount - 1; i >= 0; --i) {
            char candidate[384];
            const int n = std::snprintf(candidate, sizeof(candidate), "%s/%s", g_modRoots[i], normalized);
            if (n < 0 || static_cast<std::size_t>(n) >= sizeof(candidate)) continue;
            const int fd = open(candidate, O_RDONLY);
            if (fd < 0) continue;
            close(fd);
            return i;
        }
        return -1;
    }
    const std::string key = ModFileKey(normalized);
    auto it = std::lower_bound(g_modFileIndex.begin(), g_modFileIndex.end(), key,
        [](const ModFileEntry& entry, const std::string& k) { return entry.key < k; });
    return (it != g_modFileIndex.end() && it->key == key) ? it->root : -1;
}

bool ModCandidate(std::int32_t root, const char* normalized, char* out, std::size_t capacity) noexcept {
    const int n = std::snprintf(out, capacity, "%s/%s", g_modRoots[root], normalized);
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}
FsOpenFn g_originalFsOpen = nullptr;
using FsOpenExFn = void* (*)(void*, const char*, const char*, std::uint32_t, const char*, char**);
FsOpenExFn g_originalFsOpenEx = nullptr;
using FsReadCacheFn = bool (*)(void*, const char*, void*);
FsReadCacheFn g_originalFsReadCache = nullptr;
std::uintptr_t g_primaryFsTable[168]{};
bool g_fsHookInstalled = false;


std::size_t NormalizeRequestedPath(const char* in, char* out, std::size_t capacity) noexcept {
    if (capacity == 0) return 0;
    out[0] = '\0';
    if (!in || *in == '/' || *in == '\\' || std::strchr(in, ':')) return 0;
    std::size_t n = 0;
    for (; *in; ++in) {
        if (n + 1 >= capacity) { out[0] = '\0'; return 0; }
        out[n++] = *in == '\\' ? '/' : *in;
    }
    out[n] = '\0';
    const char* segment = out;
    while (*segment) {
        const char* end = std::strchr(segment, '/');
        const std::size_t length = end ? static_cast<std::size_t>(end - segment) : std::strlen(segment);
        if (length == 0 || (length == 1 && segment[0] == '.') ||
            (length == 2 && segment[0] == '.' && segment[1] == '.')) {
            out[0] = '\0'; return 0;
        }
        if (!end) break;
        segment = end + 1;
    }
    return n;
}

bool ModReadFromCache(void* self, const char* fileName, void* result) noexcept {
    char normalized[256]{};
    if (NormalizeRequestedPath(fileName, normalized, sizeof(normalized))) {
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
        if (!std::strcmp(normalized, "cfg/server/persistent_player_data_version_929.pdef")) return false;
#endif
        if (FindModFileRoot(normalized) >= 0) {
            LogFormat("[NorthstarPS4] bypass cached mod file: %s\n", normalized);
            return false;
        }
    }
    return g_originalFsReadCache(self, fileName, result);
}

#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
constexpr const char* kRuntimeRson = "/data/northstar_ps4/scripts.rson";
FsReadFn g_originalFsRead = nullptr;
FsCloseFn g_originalFsClose = nullptr;
using FsSizeFn = std::uint64_t (*)(void*, const char*, const char*);
FsSizeFn g_originalFsSize = nullptr;

std::uintptr_t g_runtimeClientBase = 0;
std::size_t g_runtimeClientSpan = 0;
bool g_runtimeManifestGenerated = false;

// Defined in runtime_http.inl, which is included further down under a superset
// of this block's guards. The server-list natives in the UI API need them.
bool InitHttpTransport() noexcept;
bool HttpGet(const char* url, char* out, std::size_t capacity, int& status) noexcept;
bool HttpPost(const char* url, char* out, std::size_t capacity, int& status) noexcept;
#include "runtime_ui_api.inl"
#include "runtime_script_print.inl"
#include "runtime_ui_callbacks.inl"

bool RegisterRuntimeConstants(void* owner, int context) noexcept {
    const auto base = g_runtimeClientBase;
    constexpr std::uint8_t internBytes[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
    if (!ValidateEnginePreimage(base, g_runtimeClientSpan, 0x6a96a0, internBytes, sizeof(internBytes))) return false;
    constexpr std::uint8_t insertBytes[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec};
    if (!ValidateEnginePreimage(base, g_runtimeClientSpan, 0x6ab3e0, insertBytes, sizeof(insertBytes))) return false;
    constexpr std::uint8_t constTableBytes[] = {0x49,0x8b,0x55,0x08,0x8b,0x4a,0x68,0x48,0x8b,0x42,0x50,0x8d,0x71,0x01,0x48,0xc1,0xe1,0x04,0x89,0x72,0x68,0x48,0x8b,0x72,0x70,0xf6,0x80,0xdb,0x40,0x00,0x00,0x08};
    if (!ValidateEnginePreimage(base, g_runtimeClientSpan, 0x6759d2, constTableBytes, sizeof(constTableBytes))) return false;
    if (kClientScriptOwnerGlobalVa + sizeof(void*) > g_runtimeClientSpan) return false;
    void* vm = owner ? *reinterpret_cast<void**>(reinterpret_cast<char*>(owner) + 8) : nullptr;
    void* shared = vm ? *reinterpret_cast<void**>(reinterpret_cast<char*>(vm) + 0x50) : nullptr;
    if (!shared) { LogFormat("[NorthstarPS4] constants not ready owner=%p vm=%p\n", owner, vm); return false; }

    // Install the script print sink before anything else can fail: script
    // output is the only diagnostic channel mods have, and it is worth having
    // even when constant or native registration below goes wrong.
    InstallScriptPrint(shared, context == uiapi::kCtxUi ? "UI" : "CLIENT");

    void* strings = *reinterpret_cast<void**>(reinterpret_cast<char*>(shared) + 0x4048);
    void* constants = *reinterpret_cast<void**>(reinterpret_cast<char*>(shared) + 0x40e0);
    const auto tag = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(shared) + 0x40d8);
    LogFormat("[NorthstarPS4] constants table vm=%p shared=%p table=%p tag=%x\n", vm, shared, constants, tag);
    if (!strings || !constants || tag != 0xa000020) return false;
    using InternFn = void* (*)(void*, const char*, std::int32_t);
    using InsertFn = bool (*)(void*, const void*, const void*);
    auto intern = reinterpret_cast<InternFn>(base + 0x6a96a0);
    auto insert = reinterpret_cast<InsertFn>(base + 0x6ab3e0);
    const struct { const char* name; std::int64_t value; } values[] = {
        {"VANILLA", 0}, {"NS_VERSION_MAJOR", 0}, {"NS_VERSION_MINOR", 1},
        {"NS_VERSION_PATCH", 0}, {"NS_VERSION_DEV", 1}
    };
    for (const auto& entry : values) {
        void* keyString = intern(strings, entry.name, -1);
        if (!keyString) return false;
        // SQString::Create (0x6a96a0) never writes the shared-state back-pointer
        // at string+0x18, and it does not take a reference. Every engine caller
        // does both itself immediately afterwards (sq_pushstring 0x682e00, the
        // script-function lookup 0x685cf0, the native registrar 0x684630).
        // Omitting it leaves the key unowned with a garbage +0x18, and the
        // table's release path at VM teardown dereferences string+0x18 and then
        // sharedState+0x4048, faulting inside SQString release at VA 0x69f6e4.
        // Latent until a VM was actually destroyed: the UI VM outlives the
        // session, the CLIENT VM is torn down on leaving a map.
        *reinterpret_cast<void**>(static_cast<char*>(keyString) + 0x18) = shared;
        ++*reinterpret_cast<std::uint32_t*>(static_cast<char*>(keyString) + 8);
        const std::uint64_t key[2] = {0x8000010, reinterpret_cast<std::uintptr_t>(keyString)};
        const std::uint64_t value[2] = {0x5000002, static_cast<std::uint64_t>(entry.value)};
        // 1 for a new slot, 0 when the key landed in an existing node - including
        // after a rehash, so 0 is not a failure (see TableStoreTop).
        const int result = insert(constants, key, value);
        LogFormat("[NorthstarPS4] runtime constant name=%s value=%lld result=%d\n", entry.name, static_cast<long long>(entry.value), result);
    }
    if (!RegisterRuntimeUiNatives(owner, context) || !InstallRuntimeUiCallbacks()) return false;
    return true;
}

#include "runtime_vm_lifecycle.inl"

bool BuildRuntimeManifest(void* self) noexcept {
    void* source = g_originalFsOpenEx(self, "scripts/vscripts/scripts.rson", "rb", 0, "GAME", nullptr);
    if (!source) return false;
    std::string original;
    char chunk[4096];
    int count = 0;
    do {
        count = g_originalFsRead(reinterpret_cast<char*>(self) + 8, chunk, sizeof(chunk), source);
        if (count > 0) original.append(chunk, count);
    } while (count == sizeof(chunk) && original.size() < 1024 * 1024);
    g_originalFsClose(reinterpret_cast<char*>(self) + 8, source);
    if (count < 0 || original.empty() || original.size() >= 1024 * 1024) return false;
    std::string initBlocks, modBlocks;
    ModDiscovery discovery{};
    CollectModNames(discovery);
    // Collected rather than appended directly, so a script path declared by
    // more than one mod is resolved instead of emitted twice. The engine treats
    // a repeat as fatal:
    //
    //   FatalError: Script "_custom_codecallbacks_client.gnut" is being loaded
    //               more than once from "scripts/vscripts/scripts.rson"
    //
    // which is exactly what an override looks like - Northstar.PS4 ships its
    // own copy of a script Northstar.Client also ships. The file overlay
    // already resolves that by walking mod roots backwards so the highest
    // LoadPriority wins; the manifest has to agree, or the overlay serves one
    // file while the manifest asks for it twice.
    //
    // `discovery` is ordered by ascending LoadPriority, so last-wins matches
    // both the overlay and PC's AddSearchPath semantics. The first occurrence
    // keeps its position, because scripts.rson order is load order and an
    // override should not reorder anything around it.
    std::vector<std::pair<std::string, std::string>> scripts;  // normalized path, RunOn
    for (int i = 0; i < discovery.count; ++i) {
        char metadataPath[256]{};
        std::snprintf(metadataPath, sizeof(metadataPath), "%s/%s/mod.json", kModsRoot, discovery.names[i]);
        static char json[kModJsonBufferSize];
        std::size_t size = 0;
        static ModInfo info;
        if (!ReadFileIntoBuffer(metadataPath, json, sizeof(json), size) || !ParseModMetadata(json, info)) return false;
        if (info.initScript[0]) {
            // RuntimeVmInit compiles each mod's InitScript directly at VM
            // creation for every context it hooks, so the manifest must only
            // declare the contexts it does not hook. Declaring a hooked context
            // here compiles the InitScript twice in that VM, and the second pass
            // fails ("Redefinition of enumeration ..."). SERVER lives in
            // server.prx and is still unhooked; once it is, this block goes away
            // entirely rather than gaining another context.
            initBlocks += g_runtimeVmInitHooked ? "When: \"SERVER\"\nScripts:\n[\n" : "When: \"SERVER || CLIENT || UI\"\nScripts:\n[\n";
            initBlocks += info.initScript;
            initBlocks += "\n]\n";
        }
        const char* entries = JsonFindMember(json, "Scripts");
        if (!entries || *JsonSkipWs(entries) != '[') continue;
        entries = JsonSkipWs(entries + 1);
        while (*entries && *entries != ']') {
            const char* pathValue = JsonFindMember(entries, "Path");
            const char* whenValue = JsonFindMember(entries, "RunOn");
            char path[256]{}, when[512]{}, normalized[256]{};
            if (!pathValue || !whenValue || !JsonExtractString(pathValue, path, sizeof(path)) ||
                !JsonExtractString(whenValue, when, sizeof(when)) ||
                !NormalizeRequestedPath(path[0] == '/' ? path + 1 : path, normalized, sizeof(normalized)) ||
                std::strpbrk(when, "\"\r\n") || std::strpbrk(path, "\"\r\n[]")) { LogFormat("[NorthstarPS4] manifest rejected mod=%s script=%s\n", info.name, path); return false; }
            auto existing = scripts.end();
            for (auto it = scripts.begin(); it != scripts.end(); ++it)
                if (it->first == normalized) { existing = it; break; }
            if (existing != scripts.end()) {
                LogFormat("[NorthstarPS4] manifest override mod=%s script=%s\n", info.name, normalized);
                existing->second = when;
            } else {
                scripts.emplace_back(normalized, when);
            }
            entries = JsonSkipWs(JsonSkipValue(entries));
            if (*entries != ',') break;
            entries = JsonSkipWs(entries + 1);
        }
    }
    for (const auto& script : scripts) {
        modBlocks += "When: \"";
        modBlocks += script.second;
        modBlocks += "\"\nScripts:\n[\n";
        modBlocks += script.first;
        modBlocks += "\n]\n";
    }
    const int scriptCount = static_cast<int>(scripts.size());
    // Runtime compiled cache, like PC Northstar. Retail archives and mod sources
    // are only read. Init declarations precede scripts that reference their types.
    mkdir("/data/northstar_ps4", 0777);
    FILE* output = std::fopen(kRuntimeRson, "wb");
    if (!output) { LogFormat("[NorthstarPS4] runtime manifest cache is not writable\n"); return false; }
    const std::string combined = initBlocks + "\n" + original + "\n" + modBlocks;
    bool ok = std::fwrite(combined.data(), 1, combined.size(), output) == combined.size();
    if (std::fclose(output) != 0) ok = false;
    LogFormat("[NorthstarPS4] runtime manifest generated mods=%d scripts=%d bytes=%zu success=%d\n", discovery.count, scriptCount, combined.size(), ok ? 1 : 0);
    return ok;
}

#include "runtime_keyvalues.inl"
#endif

#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
// IBaseFileSystem::Size(fileName, pathID), primary vtable slot 135. A patched
// file must report the length of the merged copy that Open will hand back, or
// the engine allocates for the original and truncates the rest.
std::uint64_t ModSize(void* self, const char* fileName, const char* pathID) noexcept {
    char normalized[256]{};
    if (kKeyValuesMergeEnabled && NormalizeRequestedPath(fileName, normalized, sizeof(normalized)) &&
        IsKeyValuePatched(normalized)) {
        std::uint64_t size = 0;
        if (KeyValuesServedSize(self, normalized, size)) {
            LogFormat("[NorthstarPS4] keyvalues size %s = %llu\n",
                normalized, static_cast<unsigned long long>(size));
            return size;
        }
    }
    return g_originalFsSize(self, fileName, pathID);
}
#endif

#include "runtime_vpks.inl"
#include "runtime_rpaks.inl"
#include "runtime_server_vm.inl"
#include "runtime_console.inl"
#include "runtime_http.inl"

// IBaseFileSystem::ReadFile - secondary slot 14, filesystem_stdio+0xc3d0.
//
// Some loaders read a whole file in one call instead of opening it, and
// ReadFile opens internally without going through OpenEx, so none of the mod
// overlay above applies to it. That is how the server's per-level AI init
// loads its behaviour definitions (server.prx 0x2fc2c4:
// `call [rax+0x70]` on fs+8 with pathID "game"), and it is why hosting a
// match died on
//
//   FatalError: Couldn't read scripts/aibehavior/behaviors.txt!
//
// even though Northstar.CustomServers ships that file: the stock game keeps it
// only in the single-player archives, on PS4 and PC alike, and PC hosts serve
// it from the mod.
//
// Mod roots are tried in priority order, as OpenEx does; a hit is read by
// handing ReadFile the absolute mod path. Files OpenEx rewrites rather than
// overlays (the runtime scripts.rson, the generated pdef, KeyValues merges)
// keep ReadFile's stock behaviour, which is what they had before this hook
// existed, and are logged so a loader that reads them this way is noticed.
using FsReadFileFn = bool (*)(void*, const char*, const char*, void*, int, int, void*);
FsReadFileFn g_originalFsReadFile = nullptr;
constexpr std::size_t kSecondaryFsTableSlots = 64;
std::uintptr_t g_secondaryFsTable[kSecondaryFsTableSlots + 2]{};

bool PathIdIsGame(const char* pathID) noexcept {
    if (!pathID) return true;
    const char* game = "game";
    for (; *pathID && *game; ++pathID, ++game)
        if ((*pathID | 0x20) != *game) return false;
    return *pathID == '\0' && *game == '\0';
}

bool RewrittenByOpenEx(const char* normalized) noexcept {
    if (!std::strcmp(normalized, "scripts/vscripts/scripts.rson")) return true;
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    if (kKeyValuesMergeEnabled && IsKeyValuePatched(normalized)) return true;
#endif
    return false;
}

bool ModReadFile(void* self, const char* fileName, const char* pathID, void* buffer,
    int maxBytes, int startingByte, void* alloc) noexcept {
    char normalized[256]{};
    if (PathIdIsGame(pathID) && NormalizeRequestedPath(fileName, normalized, sizeof(normalized))) {
        if (RewrittenByOpenEx(normalized)) {
            LogFormat("[NorthstarPS4] ReadFile of rewritten file left stock: %s\n", normalized);
        } else {
            const std::int32_t root = FindModFileRoot(normalized);
            char candidate[384];
            if (root >= 0 && ModCandidate(root, normalized, candidate, sizeof(candidate))) {
                if (g_originalFsReadFile(self, candidate, pathID, buffer, maxBytes, startingByte, alloc)) {
                    LogFormat("[NorthstarPS4] mod file read: %s\n", candidate);
                    return true;
                }
                LogFormat("[NorthstarPS4] mod file read failed, using stock: %s\n", candidate);
            }
        }
    }
    return g_originalFsReadFile(self, fileName, pathID, buffer, maxBytes, startingByte, alloc);
}

void* ModOpenEx(void* self, const char* fileName, const char* mode,
    std::uint32_t flags, const char* pathID, char** resolved) noexcept {
    // server.prx arrives mid-map-load; this is the earliest hot path that sees it.
    TryInstallServerVm();
    // Catch preload archives when the engine mounted its initial stock set
    // before our interface hook was installed. Run on the engine reader thread.
    if (fileName && std::strstr(fileName, "scripts/vscripts/scripts.rson"))
        MountModVpks(self, nullptr, nullptr);
    const bool readOnly = mode && std::strchr(mode, 'r') &&
        !std::strchr(mode, '+') && !std::strchr(mode, 'w') && !std::strchr(mode, 'a');
    char normalized[256]{};
    const bool trace = fileName && (std::strstr(fileName, "scripts.rson") ||
        std::strstr(fileName, "_menus.nut") || std::strstr(fileName, "panel_mainmenu.nut"));
    if (trace) LogFormat("[NorthstarPS4] OpenEx requested=%s pathID=%s\n", fileName, pathID ? pathID : "(null)");
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    if (readOnly && NormalizeRequestedPath(fileName, normalized, sizeof(normalized)) &&
        std::strcmp(normalized, "scripts/vscripts/scripts.rson") == 0) {
        if (!g_runtimeManifestGenerated) g_runtimeManifestGenerated = BuildRuntimeManifest(self);
        if (g_runtimeManifestGenerated) {
            void* handle = g_originalFsOpenEx(self, kRuntimeRson, mode, flags, pathID, resolved);
            LogFormat("[NorthstarPS4] runtime manifest served handle=%p\n", handle);
            if (handle) return handle;
        }
    }
#endif
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    // The persistence definition needs no special case: Northstar.PS4 ships a
    // pre-generated 929 file (PC 231 plus the console's black market; see
    // scripts/pdef/build_ps4_pdef.py) and the ordinary mod overlay below
    // serves it, Northstar.PS4 having the highest LoadPriority.
    // KeyValues patches. A patched file is merged on its first request and the
    // single complete result is served in its place; later requests reuse it.
    // If the merge fails for any reason this falls through to the stock file,
    // so a bad patch degrades to vanilla rather than failing to boot.
    if (kKeyValuesMergeEnabled && readOnly &&
        NormalizeRequestedPath(fileName, normalized, sizeof(normalized)) &&
        IsKeyValuePatched(normalized) &&
        (KeyValuesAlreadyBuilt(normalized) || BuildKeyValuesPatch(self, normalized))) {
        char candidate[512];
        if (KeyValuesOutputPath(normalized, candidate, sizeof(candidate))) {
            void* handle = g_originalFsOpenEx(self, candidate, mode, flags, pathID, resolved);
            if (handle) {
                LogFormat("[NorthstarPS4] keyvalues served: %s\n", normalized);
                return handle;
            }
            LogFormat("[NorthstarPS4] keyvalues merged file would not open: %s\n", candidate);
        }
    }
#endif
    if (readOnly && (!pathID || std::strcmp(pathID, "GAME") == 0) &&
        NormalizeRequestedPath(fileName, normalized, sizeof(normalized))) {
        const std::int32_t root = FindModFileRoot(normalized);
        char candidate[384];
        if (root >= 0 && ModCandidate(root, normalized, candidate, sizeof(candidate))) {
            void* handle = g_originalFsOpenEx(self, candidate, mode, flags, pathID, resolved);
            if (handle) {
                LogFormat("[NorthstarPS4] mod file served: %s\n", candidate);
                return handle;
            }
        }
    }
    return g_originalFsOpenEx(self, fileName, mode, flags, pathID, resolved);
}

void* ModSearchPathOpen(void* self, const char* fileName, const char* mode,
    const char* pathID, std::int64_t flags) noexcept {
    // TEMP DIAGNOSTIC (2026-08-17): tracing a case where fixed mod content
    // (verified correct on disk in both the VPK and the /app0/R2Northstar/mods overlay)
    // still isn't what CLIENT-context script compilation reads at connect
    // time. Logs every open attempt whose requested path mentions
    // "codecallbacks" -- remove once root-caused.
    const bool traceThis = fileName != nullptr &&
        (std::strstr(fileName, "codecallbacks") != nullptr ||
         std::strstr(fileName, "scripts.rson") != nullptr ||
         std::strstr(fileName, "_menus.nut") != nullptr ||
         std::strstr(fileName, "panel_mainmenu.nut") != nullptr);
    if (traceThis) {
        LogFormat("[NorthstarPS4] modtrace requested fileName=%s mode=%s pathID=%s\n",
            fileName, mode != nullptr ? mode : "(null)",
            pathID != nullptr ? pathID : "(null)");
    }
    const bool readOnly = mode != nullptr &&
        std::strchr(mode, 'w') == nullptr && std::strchr(mode, 'a') == nullptr &&
        std::strchr(mode, '+') == nullptr;
    if (readOnly && fileName != nullptr &&
        (pathID == nullptr || std::strcmp(pathID, "GAME") == 0)) {
        char normalized[256]{};
        const std::size_t pathLength =
            NormalizeRequestedPath(fileName, normalized, sizeof(normalized));
        if (traceThis) {
            LogFormat("[NorthstarPS4] modtrace normalized=%s length=%zu modRootCount=%d\n",
                normalized, pathLength, g_modRootCount);
        }
        if (pathLength > 0) {
            for (std::int32_t i = g_modRootCount - 1; i >= 0; --i) {
                const std::size_t rootLength = std::strlen(g_modRoots[i]);
                if (rootLength + 1 + pathLength >= 384) continue;
                char candidate[384]{};
                std::memcpy(candidate, g_modRoots[i], rootLength);
                candidate[rootLength] = '/';
                std::memcpy(candidate + rootLength + 1, normalized, pathLength);
                candidate[rootLength + 1 + pathLength] = '\0';
                // NOTE: access() is a shadPS4 stub that always returns 0, so
                // probe existence with open/close (real kernel FS) instead.
                const int fd = open(candidate, O_RDONLY);
                if (traceThis) {
                    LogFormat("[NorthstarPS4] modtrace candidate[%d]=%s fd=%d\n",
                        i, candidate, fd);
                }
                if (fd < 0) continue;
                close(fd);
                if (traceThis) {
                    LogFormat("[NorthstarPS4] modtrace SERVING candidate=%s\n", candidate);
                }
                void* handle = g_originalFsOpen(self, candidate, mode, pathID, flags);
                if (handle != nullptr) return handle;
            }
        }
    }
    if (traceThis) {
        LogFormat("[NorthstarPS4] modtrace FALLTHROUGH to original open fileName=%s\n",
            fileName);
    }
    return g_originalFsOpen(self, fileName, mode, pathID, flags);
}
} // namespace

void ProbeFilesystemInterface(OrbisKernelModule fsHandle) noexcept {
    if (g_fsHookInstalled) return;
    using CreateInterfaceFn = void* (*)(const char*, int*);

    void* createInterfaceAddress = nullptr;
    const int dlsymResult =
        sceKernelDlsym(static_cast<std::int32_t>(fsHandle), "CreateInterface",
            &createInterfaceAddress);
    LogFormat("[NorthstarPS4] fs overlay dlsymResult=0x%x CreateInterface=%p fsHandle=0x%x\n",
        dlsymResult, createInterfaceAddress, fsHandle);
    if (dlsymResult != 0 || createInterfaceAddress == nullptr) return;

    auto createInterface = reinterpret_cast<CreateInterfaceFn>(createInterfaceAddress);
    int interfaceResult = -1;
    void* const fs = createInterface("VFileSystem017", &interfaceResult);
    LogFormat("[NorthstarPS4] fs overlay interface=%p result=%d\n",
        fs, interfaceResult);
    if (fs == nullptr) return;

    auto vtable = *reinterpret_cast<void***>(fs);
    auto vtable2 = *reinterpret_cast<void***>(
        reinterpret_cast<std::uintptr_t>(fs) + 8);
    if (vtable == nullptr || vtable2 == nullptr) {
        LogFormat("[NorthstarPS4] fs overlay invalid vtable=%p vtable2=%p\n",
            vtable, vtable2);
        return;
    }
    LogFormat("[NorthstarPS4] fs overlay vtable=%p vtable2=%p\n", vtable, vtable2);
    for (std::int32_t slot = 0; slot < 17; ++slot) {
        LogFormat("[NorthstarPS4] fs overlay vtable[%d]=%p\n",
            slot, vtable[slot]);
    }
    // All of IBaseFileSystem, Read (0) through UnzipFile (16). Slot 14 is
    // ReadFile, hooked below.
    for (std::int32_t slot = 0; slot < 17; ++slot) {
        LogFormat("[NorthstarPS4] fs overlay vtable2[%d]=%p\n",
            slot, vtable2[slot]);
    }
    void* const fsFieldAddr =
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(fs) + 8);
    auto originalOpen = reinterpret_cast<FsOpenFn>(vtable2[2]);
    auto originalRead = reinterpret_cast<FsReadFn>(vtable2[0]);
    auto originalClose = reinterpret_cast<FsCloseFn>(vtable2[3]);
    LogFormat("[NorthstarPS4] fs overlay vtable2[0]=%p vtable2[2]=%p vtable2[3]=%p vtable2[10]=%p\n",
        vtable2[0], vtable2[2], vtable2[3], vtable2[10]);

    // Discover mods and install the mod search-path overlay (only when at
    // least one mod root exists; otherwise the game's open dispatch is left
    // untouched).
    ModDiscovery discovery{};
    CollectModNames(discovery);
    g_modRootCount = 0;
    for (std::int32_t i = 0; i < discovery.count && g_modRootCount < kMaxModRoots; ++i) {
        std::snprintf(g_modRoots[g_modRootCount], sizeof(g_modRoots[0]),
            "/app0/R2Northstar/mods/%s/mod", discovery.names[i]);
        LogFormat("[NorthstarPS4] fs overlay mod root[%d]=%s\n",
            g_modRootCount, g_modRoots[g_modRootCount]);
        ++g_modRootCount;
    }
    if (g_modRootCount == 0) return;
    if (kModFileIndexEnabled) BuildModFileIndex();
    OrbisKernelModuleInfo fsInfo{};
    fsInfo.size = sizeof(fsInfo);
    if (sceKernelGetModuleInfo(fsHandle, &fsInfo) != 0 || fsInfo.segmentCount == 0) return;
    const auto base = reinterpret_cast<std::uintptr_t>(fsInfo.segmentInfo[0].address);
    // filesystem_stdio.prx SHA256 4d6b7b653c1d9cde01b2f0634d11a06d8b986dded4374b87ca330100e4969266.
    // Read-only disassembly: secondary Open at d480 adjusts this by -8 and
    // jumps through primary slot 0x260/8 (76), OpenEx at d4a0.
    constexpr std::uint8_t openExPreimage[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x58,0x02,0x00,0x00};
    constexpr std::uint8_t cachePreimage[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x81,0xec,0x28,0x02,0x00,0x00};
    // Size(fileName, pathID): secondary slot 7 is a `this -= 8` thunk straight
    // into this same implementation, so the primary slot is the one to hook.
    constexpr std::uint8_t sizePreimage[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xa8,0x01,0x00,0x00};
    const bool matches = reinterpret_cast<std::uintptr_t>(vtable) == base + 0x70eb0 &&
        reinterpret_cast<std::uintptr_t>(vtable2) == base + 0x713f0 &&
        reinterpret_cast<std::uintptr_t>(vtable[135]) == base + 0xde20 &&
        ValidateEnginePreimage(base, fsInfo.segmentInfo[0].size, 0xde20, sizePreimage, sizeof(sizePreimage)) &&
        reinterpret_cast<std::uintptr_t>(vtable[76]) == base + 0xd4a0 &&
        reinterpret_cast<std::uintptr_t>(vtable[97]) == base + 0x67f0 &&
        ValidateEnginePreimage(base, fsInfo.segmentInfo[0].size, 0x67f0, cachePreimage, sizeof(cachePreimage)) &&
        reinterpret_cast<std::uintptr_t>(vtable2[2]) == base + 0xd480 &&
        ValidateEnginePreimage(base, fsInfo.segmentInfo[0].size, 0xd4a0, openExPreimage, sizeof(openExPreimage));
    LogFormat("[NorthstarPS4] OpenEx profile gate base=%p match=%d\n", reinterpret_cast<void*>(base), matches ? 1 : 0);
    if (!matches) return;
    g_originalFsOpen = originalOpen;
    g_originalFsOpenEx = reinterpret_cast<FsOpenExFn>(vtable[76]);
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    g_originalFsRead = originalRead;
    g_originalFsClose = originalClose;
#endif
    // Include the offset-to-top and RTTI entries preceding the vtable.
    for (std::int32_t i = -2; i < 166; ++i)
        g_primaryFsTable[i + 2] = reinterpret_cast<std::uintptr_t>(vtable[i]);
    g_primaryFsTable[78] = reinterpret_cast<std::uintptr_t>(&ModOpenEx);
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    g_originalFsSize = reinterpret_cast<FsSizeFn>(vtable[135]);
    g_primaryFsTable[137] = reinterpret_cast<std::uintptr_t>(&ModSize);
#endif
    g_originalFsReadCache = reinterpret_cast<FsReadCacheFn>(vtable[97]);
    g_primaryFsTable[99] = reinterpret_cast<std::uintptr_t>(&ModReadFromCache);
    constexpr std::uint8_t mountBytes[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x28,0x02,0x00,0x00};
    if (reinterpret_cast<std::uintptr_t>(vtable[113]) == base + 0xa900 &&
        ValidateEnginePreimage(base, fsInfo.segmentInfo[0].size, 0xa900, mountBytes, sizeof(mountBytes))) {
        DiscoverModVpks();
        g_originalMountVpk = reinterpret_cast<FsMountVpkFn>(vtable[113]);
        g_primaryFsTable[115] = reinterpret_cast<std::uintptr_t>(&ModMountVpk);
        g_modVpkHookReady = true;
        LogFormat("[NorthstarPS4] MountVPK hook installed archives=%zu\n", g_modVpks.size());
    } else LogFormat("[NorthstarPS4] MountVPK profile mismatch; mod VPK mounting disabled\n");
    *reinterpret_cast<void**>(fs) = g_primaryFsTable + 2;
    // ReadFile lives only in the secondary table (fs+8): slot 14 points
    // straight at the implementation, not at a thunk into the primary table,
    // so it gets its own copied table. Gated separately; a mismatch leaves
    // ReadFile stock and everything above in place.
    constexpr std::uint8_t readFilePreimage[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x49,0x89,0xcf,0x48,0x89,0xfb,0x4c,0x89,0x4d,0xc8,0x49,0x89,0xd1,0x49,0x89};
    if (reinterpret_cast<std::uintptr_t>(vtable2[14]) == base + 0xc3d0 &&
        ValidateEnginePreimage(base, fsInfo.segmentInfo[0].size, 0xc3d0, readFilePreimage, sizeof(readFilePreimage))) {
        // Offset-to-top and RTTI come across with the table, as for the primary.
        for (std::size_t i = 0; i < kSecondaryFsTableSlots + 2; ++i)
            g_secondaryFsTable[i] = reinterpret_cast<std::uintptr_t>(vtable2[static_cast<std::ptrdiff_t>(i) - 2]);
        g_originalFsReadFile = reinterpret_cast<FsReadFileFn>(vtable2[14]);
        g_secondaryFsTable[14 + 2] = reinterpret_cast<std::uintptr_t>(&ModReadFile);
        *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(fs) + 8) = g_secondaryFsTable + 2;
        LogFormat("[NorthstarPS4] ReadFile hook installed\n");
    } else {
        LogFormat("[NorthstarPS4] ReadFile profile mismatch; whole-file reads bypass mods\n");
    }
    g_fsHookInstalled = true;
    LogFormat("[NorthstarPS4] OpenEx hook installed roots=%d\n", g_modRootCount);
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    CollectKeyValuePatches();
#endif

    // Probe through the repointed (hooked) interface so results reflect mod
    // search-path resolution instead of the pre-hook function pointer.
    auto activeVtable2 = *reinterpret_cast<void***>(
        reinterpret_cast<std::uintptr_t>(fs) + 8);
    auto open = reinterpret_cast<FsOpenFn>(activeVtable2[2]);
    auto tryOpen = [&](const char* tag, const char* fileName) {
        void* const handle = open(fsFieldAddr, fileName, "rb", "GAME", 0);
        if (handle == nullptr) {
            LogFormat("[NorthstarPS4] fs overlay open %-12s %-40s failed\n",
                tag, fileName);
            return;
        }
        char buffer[80]{};
        const std::int32_t bytesRead =
            originalRead(fsFieldAddr, buffer, sizeof(buffer) - 1, handle);
        buffer[sizeof(buffer) - 1] = '\0';
        LogFormat("[NorthstarPS4] fs overlay open %-12s %-40s handle=%p read=%d bytes=%.*s\n",
            tag, fileName, handle, bytesRead,
            bytesRead > 0 ? bytesRead : 0, buffer);
        originalClose(fsFieldAddr, handle);
    };

    const struct { const char* tag; const char* file; } probePaths[] = {
        { "client-init", "scripts/vscripts/cl_northstar_client_init.nut" },
        { "client-loc-eng",
            "resource/northstar_client_localisation_english.txt" },
        { "client-cfg", "cfg/autoexec_ns_client.cfg" },
        { "custom-nut", "scripts/vscripts/_disallowed_tacticals.gnut" },
        { "base-ui-menus", "scripts/vscripts/ui/_menus.nut" },
        { "abs-init",
            "/app0/R2Northstar/mods/Northstar.Client/mod/scripts/vscripts/cl_northstar_client_init.nut" },
    };
    for (const auto& probe : probePaths) {
        tryOpen(probe.tag, probe.file);
    }
}
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
// Loads each mod's Localisation[] array through the game's native localise
// interface (CLocalise::AddFile) so mod tokens resolve exactly like base game
// localisation instead of depending on the VPK bake. Mirrors the PC hook in
// modlocalisation.cpp: AddFile(instance, path, nullptr, false), where path
// keeps the %language% token and the game substitutes the current language.
void ProbeLocaliseInterface(OrbisKernelModule localizeHandle,
    std::uintptr_t localizeBase, std::size_t localizeSize) noexcept {
    using AddFileFn = bool (*)(void*, const char*, const char*, bool);
    LogFormat("[NorthstarPS4] localise probe start handle=0x%x base=%p size=0x%zx\n",
        localizeHandle, reinterpret_cast<void*>(localizeBase), localizeSize);

    const bool addFileMatches = ValidateEnginePreimage(localizeBase,
        localizeSize, kLocalizeAddFileVa, kLocalizeAddFilePreimage,
        sizeof(kLocalizeAddFilePreimage));
    const bool accessorMatches = ValidateEnginePreimage(localizeBase,
        localizeSize, kLocalizeAccessorVa, kLocalizeAccessorPreimage,
        sizeof(kLocalizeAccessorPreimage));
    LogFormat("[NorthstarPS4] localise gate addFile=%d accessor=%d\n",
        addFileMatches ? 1 : 0, accessorMatches ? 1 : 0);
    if (!addFileMatches || !accessorMatches) {
        LogFormat("[NorthstarPS4] localise refused: profile preimage mismatch\n");
        return;
    }
    if (kLocalizeInstanceVa > localizeSize) {
        LogFormat("[NorthstarPS4] localise refused: instance VA out of range\n");
        return;
    }
    // The singleton object lives in .bss at localizeBase + kLocalizeInstanceVa;
    // its first qword is the vptr installed by the module's own init code and
    // points to the relocated vtable in the rodata segment. AddFile is reached
    // through vtable slot 9 (+0x48), which the loader relocates to a file VA
    // inside the module (observed to be AddFile itself, 0x5c60).
    const std::uintptr_t thisAddr = localizeBase + kLocalizeInstanceVa;
    const std::uintptr_t vptr = *reinterpret_cast<const std::uintptr_t*>(thisAddr);
    const bool vptrInModule = vptr >= localizeBase &&
        vptr - localizeBase < localizeSize;
    bool slotInModule = false;
    std::uintptr_t addFileSlot = 0;
    if (vptrInModule) {
        addFileSlot = *reinterpret_cast<const std::uintptr_t*>(vptr + 0x48);
        slotInModule = addFileSlot >= localizeBase &&
            addFileSlot - localizeBase < localizeSize;
        LogFormat("[NorthstarPS4] localise singleton this=%p vptr=%p vtable[9]=%p this+0x48=%u\n",
            reinterpret_cast<void*>(thisAddr), reinterpret_cast<void*>(vptr),
            reinterpret_cast<void*>(addFileSlot),
            *reinterpret_cast<const std::uint8_t*>(thisAddr + 0x48));
    }
    if (!vptrInModule || !slotInModule) {
        LogFormat("[NorthstarPS4] localise refused: vtable chain invalid vptrInModule=%d slotInModule=%d\n",
            vptrInModule ? 1 : 0, slotInModule ? 1 : 0);
        return;
    }
    const auto addFile = reinterpret_cast<AddFileFn>(addFileSlot);

    // AddFile routes through vtable slot 9 (its own address) when this+0x48 is
    // zero; force the field to 1 so the direct path is taken, then restore it.
    std::uint8_t* const fallbackField =
        reinterpret_cast<std::uint8_t*>(thisAddr + 0x48);
    const std::uint8_t savedFallback = *fallbackField;
    *fallbackField = 1;

    ModDiscovery discovery{};
    CollectModNames(discovery);
    std::int32_t totalFiles = 0;
    std::int32_t loadedFiles = 0;
    for (std::int32_t i = 0; i < discovery.count; ++i) {
        char path[160]{};
        std::snprintf(path, sizeof(path), "/app0/R2Northstar/mods/%s/mod.json",
            discovery.names[i]);
        static char jsonBuffer[kModJsonBufferSize];
        std::size_t jsonSize = 0;
        if (!ReadFileIntoBuffer(path, jsonBuffer,
                sizeof(jsonBuffer) - 1, jsonSize)) {
            LogFormat("[NorthstarPS4] localise mod metadata read failed: %s\n", path);
            continue;
        }
        ModInfo mod{};
        if (!ParseModMetadata(jsonBuffer, mod)) {
            LogFormat("[NorthstarPS4] localise mod metadata parse failed: %s\n", path);
            continue;
        }
        for (std::int32_t f = 0; f < mod.localisationCount; ++f) {
            const bool ok = addFile(reinterpret_cast<void*>(thisAddr),
                mod.localisationFiles[f], nullptr, false);
            LogFormat("[NorthstarPS4] localise %s file=%s result=%d\n",
                mod.name, mod.localisationFiles[f], ok ? 1 : 0);
            ++totalFiles;
            if (ok) ++loadedFiles;
        }
    }
    {
        const bool ok = addFile(reinterpret_cast<void*>(thisAddr),
            "resource/northstar_client_localisation_english.txt", nullptr, false);
        LogFormat("[NorthstarPS4] localise self-test vanilla english result=%d\n",
            ok ? 1 : 0);
    }
    *fallbackField = savedFallback;
    LogFormat("[NorthstarPS4] localise probe complete files=%d loaded=%d\n",
        totalFiles, loadedFiles);
}
#endif
void* ModuleTracker(void*) noexcept {

    bool engineSeen = false;
    bool clientSeen = false;
    std::size_t previousAvailable = static_cast<std::size_t>(-1);
    bool loggedHandles[kMaxModules]{};
    OrbisKernelModule vstdlibHandle = static_cast<OrbisKernelModule>(-1);
    OrbisKernelModule engineHandle = static_cast<OrbisKernelModule>(-1);
    OrbisKernelModule fsHandle = static_cast<OrbisKernelModule>(-1);
    OrbisKernelModule rtechHandle = static_cast<OrbisKernelModule>(-1);
#if defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
    OrbisKernelModule localizeHandle = static_cast<OrbisKernelModule>(-1);
    std::uintptr_t localizeBase = 0;
    std::size_t localizeSize = 0;
#endif
    std::uintptr_t engineBase = 0;
    std::uintptr_t clientBase = 0;
    std::size_t clientSpan = 0;
    std::size_t engineSize = 0;
    for (std::uint32_t attempt = 0; attempt < 600 && !(engineSeen && clientSeen); ++attempt) {
        OrbisKernelModule handles[kMaxModules]{};
        std::size_t available = 0;
        const int listResult = sceKernelGetModuleList(handles, sizeof(handles), &available);
        if (attempt == 0 || available != previousAvailable) {
            LogFormat("[NorthstarPS4] module scan attempt=%u result=0x%x available=%zu\n",
                attempt, listResult, available);
            previousAvailable = available;
        }
        if (listResult == 0) {
            const std::size_t count = available < kMaxModules ? available : kMaxModules;
            for (std::size_t i = 0; i < count; ++i) {
                OrbisKernelModuleInfo info{};
                info.size = sizeof(info);
                const int infoResult = sceKernelGetModuleInfo(handles[i], &info);
                if (attempt == 0) {
                    LogFormat("[NorthstarPS4] initial[%zu] handle=0x%x infoResult=0x%x name=%s segments=%u\n",
                        i, handles[i], infoResult, infoResult == 0 ? info.name : "<unavailable>",
                        infoResult == 0 ? info.segmentCount : 0);
                    if (infoResult == 0) {
                        const std::uint32_t initialSegments =
                            info.segmentCount < 4 ? info.segmentCount : 4;
                        for (std::uint32_t segment = 0; segment < initialSegments; ++segment) {
                            LogFormat("[NorthstarPS4]   initial[%zu].segment[%u] address=%p size=0x%x prot=0x%x\n",
                                i, segment, info.segmentInfo[segment].address,
                                info.segmentInfo[segment].size, info.segmentInfo[segment].prot);
                        }
                    }
                }
                if (infoResult == 0 && handles[i] < kMaxModules && !loggedHandles[handles[i]]) {
                    loggedHandles[handles[i]] = true;
                    LogFormat("[NorthstarPS4] discovered module=%s handle=0x%x segments=%u\n",
                        info.name, handles[i], info.segmentCount);
                }
                if (infoResult == 0 && std::strcmp(info.name, "vstdlib.sprx") == 0) {
                    vstdlibHandle = handles[i];
                }
                if (infoResult == 0 &&
                    std::strstr(info.name, "filesystem_stdio") != nullptr) {
                    fsHandle = handles[i];
                }
                if (infoResult == 0 && std::strstr(info.name, "rtech_game") != nullptr) {
                    rtechHandle = handles[i];
                }
#if defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
                if (infoResult == 0 && std::strstr(info.name, "localize") != nullptr) {
                    localizeHandle = handles[i];
                    if (info.segmentCount > 0) {
                        localizeBase = reinterpret_cast<std::uintptr_t>(
                            info.segmentInfo[0].address);
                        for (std::uint32_t segment = 0;
                            segment < info.segmentCount && segment < 4; ++segment) {
                            const auto segmentAddress = reinterpret_cast<std::uintptr_t>(
                                info.segmentInfo[segment].address);
                            const auto segmentEnd =
                                segmentAddress + info.segmentInfo[segment].size;
                            if (segmentEnd > localizeBase &&
                                segmentEnd - localizeBase > localizeSize) {
                                localizeSize = segmentEnd - localizeBase;
                            }
                        }
                    }
                }
#endif
                if (infoResult != 0 || !IsTarget(info.name)) continue;
                const bool isEngine = std::strstr(info.name, "engine.prx") != nullptr ||
                    std::strstr(info.name, "engine.sprx") != nullptr;
                const bool isClient = std::strstr(info.name, "client.prx") != nullptr ||
                    std::strstr(info.name, "client.sprx") != nullptr;
                if (isEngine) {
                    engineHandle = handles[i];
                    if (info.segmentCount > 0) {
                        engineBase = reinterpret_cast<std::uintptr_t>(info.segmentInfo[0].address);
                        engineSize = info.segmentInfo[0].size;
                    }
                }
                if (isClient && info.segmentCount > 0) {
                    clientBase = reinterpret_cast<std::uintptr_t>(info.segmentInfo[0].address);
                    for (std::uint32_t segment = 0; segment < info.segmentCount && segment < 4; ++segment) {
                        const auto segmentAddress =
                            reinterpret_cast<std::uintptr_t>(info.segmentInfo[segment].address);
                        const auto segmentEnd = segmentAddress + info.segmentInfo[segment].size;
                        if (segmentEnd > clientBase && segmentEnd - clientBase > clientSpan) {
                            clientSpan = segmentEnd - clientBase;
                        }
                    }
                }
                if ((isEngine && engineSeen) || (isClient && clientSeen)) continue;
                LogModule(handles[i], info);
                engineSeen = engineSeen || isEngine;
                clientSeen = clientSeen || isClient;
            }
        } else if (attempt == 0) {
            LogFormat(
                "[NorthstarPS4] sceKernelGetModuleList failed: 0x%x\n", listResult);
        }
        if (!(engineSeen && clientSeen)) sceKernelUsleep(100000);
    }
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST)
    g_runtimeClientBase = clientBase;
    g_runtimeClientSpan = clientSpan;
    InstallRuntimeVmInit();
#endif
#if defined(NORTHSTAR_PS4_ENABLE_M6_FS_OVERLAY)
    if (fsHandle != static_cast<OrbisKernelModule>(-1)) {
        ProbeFilesystemInterface(fsHandle);
    } else {
        LogFormat("[NorthstarPS4] fs overlay skipped: filesystem_stdio handle unavailable\n");
    }
    // After the filesystem overlay, so mod discovery has already run.
    if (rtechHandle != static_cast<OrbisKernelModule>(-1)) {
        InstallModRpakHook(rtechHandle);
    } else {
        LogFormat("[NorthstarPS4] mod rpak hook skipped: rtech_game handle unavailable\n");
    }
#endif
#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_UI_NATIVE)
    if (clientBase != 0) InstallDiagnosticUiRecord(clientBase, clientSpan);
#endif
    if (vstdlibHandle != static_cast<OrbisKernelModule>(-1)) {
        ProbeCvarInterface(vstdlibHandle, engineBase, engineSize);
        if (engineHandle != static_cast<OrbisKernelModule>(-1)) {
            ProbeRegistrationExports(engineHandle, vstdlibHandle);
            ProbeEngineClientInterface(engineHandle, engineBase, engineSize);
        }
    } else {
        LogFormat("[NorthstarPS4] cvar probe skipped: vstdlib handle unavailable\n");
    }

    LogFormat(
        "[NorthstarPS4] module tracker complete engine=%d client=%d\n",
        engineSeen ? 1 : 0, clientSeen ? 1 : 0);
#if defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
    if (localizeHandle != static_cast<OrbisKernelModule>(-1) && localizeBase != 0) {
        ProbeLocaliseInterface(localizeHandle, localizeBase, localizeSize);
    } else {
        LogFormat("[NorthstarPS4] localise probe skipped: localize module unavailable\n");
    }
#endif
    if (clientBase != 0) {
        ProbeUiVm(clientBase, clientSpan);
    }
#if defined(NORTHSTAR_PS4_ENABLE_M6_SCRIPT_PROBE)
    if (clientBase != 0) {
        ProbeUiScriptSystem(clientBase, clientSpan, fsHandle);
    }
#endif
    return nullptr;
}
} // namespace

bool Initialize(InitStage stage) noexcept {
    if (stage != InitStage::ModuleLoaded) return false;
    OrbisPthread thread{};
    const int result = scePthreadCreate(&thread, nullptr, ModuleTracker, nullptr, "NorthstarPS4");
    if (result != 0) {
        LogFormat("[NorthstarPS4] tracker thread creation failed: 0x%x\n", result);
        return false;
    }
    scePthreadDetach(thread);
    LogFormat("[NorthstarPS4] module tracker started\n");
    return true;
}

void Log(const char* message) noexcept {
    LogFormat("%s", message);
}
} // namespace northstar::ps4