#include "northstar_ps4/runtime.h"

#include <orbis/libkernel.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

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
constexpr std::size_t kMaxModConVars = 32;
constexpr std::size_t kMaxModNames = 16;
constexpr std::size_t kModJsonBufferSize = 16 * 1024;

const char* JsonSkipWs(const char* p) noexcept {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

const char* JsonSkipString(const char* p) noexcept {
    if (*p != '"') return p;
    ++p;
    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '"') return p + 1;
        ++p;
    }
    return p;
}

const char* JsonSkipValue(const char* p) noexcept {
    p = JsonSkipWs(p);
    if (*p == '"') return JsonSkipString(p);
    if (*p == '{') {
        ++p;
        for (;;) {
            p = JsonSkipWs(p);
            if (*p == '}') return p + 1;
            if (*p != '"') return p;
            p = JsonSkipString(p);
            p = JsonSkipWs(p);
            if (*p != ':') return p;
            p = JsonSkipValue(p + 1);
            p = JsonSkipWs(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return (*p == '}') ? p + 1 : p;
        }
    }
    if (*p == '[') {
        ++p;
        for (;;) {
            p = JsonSkipWs(p);
            if (*p == ']') return p + 1;
            p = JsonSkipValue(p);
            p = JsonSkipWs(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return (*p == ']') ? p + 1 : p;
        }
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') ++p;
    return p;
}

const char* JsonFindMember(const char* object, const char* key) noexcept {
    if (object == nullptr) return nullptr;
    const char* p = JsonSkipWs(object);
    if (*p != '{') return nullptr;
    ++p;
    for (;;) {
        p = JsonSkipWs(p);
        if (*p != '"') return nullptr;
        const char* const keyStart = p + 1;
        const char* keyEnd = keyStart;
        while (*keyEnd != '\0' && *keyEnd != '"') {
            if (*keyEnd == '\\' && keyEnd[1] != '\0') ++keyEnd;
            ++keyEnd;
        }
        if (*keyEnd != '"') return nullptr;
        const std::size_t keyLength =
            static_cast<std::size_t>(keyEnd - keyStart);
        const bool matches = std::strlen(key) == keyLength &&
            std::memcmp(keyStart, key, keyLength) == 0;
        p = keyEnd + 1;
        p = JsonSkipWs(p);
        if (*p != ':') return nullptr;
        p = JsonSkipWs(p + 1);
        if (matches) return p;
        p = JsonSkipValue(p);
        p = JsonSkipWs(p);
        if (*p != ',') return nullptr;
        ++p;
    }
}

bool JsonExtractString(const char* p, char* out, std::size_t capacity) noexcept {
    p = JsonSkipWs(p);
    if (*p != '"') return false;
    ++p;
    std::size_t used = 0;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
            char decoded = *p;
            switch (*p) {
                case 'b': decoded = '\b'; break;
                case 'f': decoded = '\f'; break;
                case 'n': decoded = '\n'; break;
                case 'r': decoded = '\r'; break;
                case 't': decoded = '\t'; break;
                default: break;
            }
            if (used + 1 < capacity) out[used++] = decoded;
            ++p;
            continue;
        }
        if (used + 1 < capacity) out[used++] = *p;
        ++p;
    }
    if (capacity > 0) out[capacity - 1] = '\0';
    return *p == '"';
}

long long JsonExtractInteger(const char* p) noexcept {
    p = JsonSkipWs(p);
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    long long value = 0;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    return negative ? -value : value;
}

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
    if (!ok) return false;
    if (used < capacity) buffer[used] = '\0';
    else buffer[capacity - 1] = '\0';
    sizeOut = used;
    return true;
}

constexpr std::size_t kMaxModUiScripts = 32;

struct ModConVarInfo {
    char name[64];
    char defaultValue[64];
    char flags[32];
};

struct ModInfo {
    char name[64];
    char description[128];
    char version[32];
    char initScript[96];
    std::int32_t loadPriority = 0;
    std::int32_t scriptCount = 0;
    std::int32_t conVarCount = 0;
    ModConVarInfo conVars[kMaxModConVars];
    // Subset of Scripts[] whose "RunOn" is exactly "UI" (the only CompileList
    // context proven safe so far, VA-verified as countTable index 2 by
    // ProbeUiScriptSystem). CLIENT/SERVER/compound RunOn expressions are
    // counted in scriptCount but intentionally not collected here until
    // their CompileList context index is identified the same way.
    std::int32_t uiScriptCount = 0;
    char uiScripts[kMaxModUiScripts][96];
};

struct ModDiscovery {
    char names[kMaxModNames][64];
    std::int32_t count = 0;
};

bool ParseModMetadata(const char* json, ModInfo& out) noexcept {
    out = ModInfo{};
    const char* const nameValue = JsonFindMember(json, "Name");
    if (nameValue != nullptr) {
        JsonExtractString(nameValue, out.name, sizeof(out.name));
    }
    const char* const descriptionValue = JsonFindMember(json, "Description");
    if (descriptionValue != nullptr) {
        JsonExtractString(descriptionValue, out.description,
            sizeof(out.description));
    }
    const char* const versionValue = JsonFindMember(json, "Version");
    if (versionValue != nullptr) {
        JsonExtractString(versionValue, out.version, sizeof(out.version));
    }
    const char* const initScriptValue = JsonFindMember(json, "InitScript");
    if (initScriptValue != nullptr) {
        JsonExtractString(initScriptValue, out.initScript,
            sizeof(out.initScript));
    }
    const char* const priorityValue = JsonFindMember(json, "LoadPriority");
    if (priorityValue != nullptr) {
        out.loadPriority =
            static_cast<std::int32_t>(JsonExtractInteger(priorityValue));
    }

    const char* const conVarsValue = JsonFindMember(json, "ConVars");
    if (conVarsValue != nullptr) {
        const char* elem = JsonSkipWs(conVarsValue);
        if (*elem == '[') elem = JsonSkipWs(elem + 1);
        while (elem != nullptr && *elem != ']' &&
            out.conVarCount < static_cast<std::int32_t>(kMaxModConVars)) {
            if (*elem == '{') {
                ModConVarInfo& info = out.conVars[out.conVarCount];
                const char* const nv = JsonFindMember(elem, "Name");
                if (nv != nullptr &&
                    JsonExtractString(nv, info.name, sizeof(info.name))) {
                    const char* const dv =
                        JsonFindMember(elem, "DefaultValue");
                    if (dv != nullptr) {
                        JsonExtractString(dv, info.defaultValue,
                            sizeof(info.defaultValue));
                    }
                    const char* const fv = JsonFindMember(elem, "Flags");
                    if (fv != nullptr) {
                        JsonExtractString(fv, info.flags, sizeof(info.flags));
                    }
                    ++out.conVarCount;
                }
            }
            elem = JsonSkipWs(JsonSkipValue(elem));
            if (*elem == ',') elem = JsonSkipWs(elem + 1);
            else break;
        }
    }

    const char* const scriptsValue = JsonFindMember(json, "Scripts");
    if (scriptsValue != nullptr) {
        const char* elem = JsonSkipWs(scriptsValue);
        if (*elem == '[') elem = JsonSkipWs(elem + 1);
        while (elem != nullptr && *elem != ']') {
            ++out.scriptCount;
            if (*elem == '{') {
                const char* const pathValue = JsonFindMember(elem, "Path");
                const char* const runOnValue = JsonFindMember(elem, "RunOn");
                char path[96]{};
                char runOn[64]{};
                const bool havePath = pathValue != nullptr &&
                    JsonExtractString(pathValue, path, sizeof(path));
                const bool haveUiRunOn = runOnValue != nullptr &&
                    JsonExtractString(runOnValue, runOn, sizeof(runOn)) &&
                    std::strcmp(runOn, "UI") == 0;
                if (havePath && haveUiRunOn &&
                    out.uiScriptCount < static_cast<std::int32_t>(kMaxModUiScripts)) {
                    std::strncpy(out.uiScripts[out.uiScriptCount], path,
                        sizeof(out.uiScripts[0]) - 1);
                    out.uiScripts[out.uiScriptCount][sizeof(out.uiScripts[0]) - 1] = '\0';
                    ++out.uiScriptCount;
                }
            }
            elem = JsonSkipWs(JsonSkipValue(elem));
            if (*elem == ',') elem = JsonSkipWs(elem + 1);
            else break;
        }
    }
    return out.name[0] != '\0';
}

void CollectModNames(ModDiscovery& discovery) noexcept {
    DIR* const dir = opendir("/app0/mods");
    if (dir != nullptr) {
        LogFormat("[NorthstarPS4] mod metadata opendir /app0/mods ok\n");
        while (struct dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            if (discovery.count >= static_cast<std::int32_t>(kMaxModNames)) break;
            std::strncpy(discovery.names[discovery.count], entry->d_name,
                sizeof(discovery.names[0]) - 1);
            discovery.names[discovery.count][sizeof(discovery.names[0]) - 1] = '\0';
            ++discovery.count;
        }
        closedir(dir);
        return;
    }
    LogFormat("[NorthstarPS4] mod metadata opendir failed, reading manifest\n");
    static char buffer[kModJsonBufferSize];
    std::size_t size = 0;
    if (!ReadFileIntoBuffer("/app0/mods/.ns_mod_manifest",
            buffer, sizeof(buffer) - 1, size)) {
        LogFormat("[NorthstarPS4] mod metadata manifest read failed\n");
        return;
    }
    char* line = buffer;
    while (line != nullptr && *line != '\0' &&
        discovery.count < static_cast<std::int32_t>(kMaxModNames)) {
        char* const newline = std::strchr(line, '\n');
        if (newline != nullptr) *newline = '\0';
        if (line[0] != '\0' && line[0] != '\r') {
            std::strncpy(discovery.names[discovery.count], line,
                sizeof(discovery.names[0]) - 1);
            discovery.names[discovery.count][sizeof(discovery.names[0]) - 1] = '\0';
            ++discovery.count;
        }
        if (newline == nullptr) break;
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
        std::snprintf(path, sizeof(path), "/app0/mods/%s/mod.json",
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
#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_CONVAR) || defined(NORTHSTAR_PS4_ENABLE_TEAM_CHANGES_CONVAR)
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
#endif

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

#if defined(NORTHSTAR_PS4_ENABLE_TEAM_CHANGES_CONVAR)
    alignas(16) static std::uint8_t teamChangesConVar[0x90]{};
    void* teamChangesRegistered = findVar(cvar, "ns_allow_team_changes");
    if (teamChangesRegistered == nullptr) {
        constructor(teamChangesConVar, "ns_allow_team_changes", "0", 0,
            "Allow players to change teams", nullptr);
        teamChangesRegistered = findVar(cvar, "ns_allow_team_changes");
    }
    LogFormat("[NorthstarPS4] team changes convar registration result=%p expected=%p success=%d default=0 flags=0\n",
        teamChangesRegistered, teamChangesConVar,
        teamChangesRegistered == teamChangesConVar ? 1 : 0);
#else
    LogFormat("[NorthstarPS4] team changes convar disabled at build time\n");
#endif

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
void ProbeFilesystemInterface(OrbisKernelModule fsHandle) noexcept {
    using CreateInterfaceFn = void* (*)(const char*, int*);
    using OpenFn = void* (*)(void**, const char*, const char*, const char*, std::int64_t);
    using ReadFn = std::int32_t (*)(void**, void*, std::int32_t, void*);
    using CloseFn = void (*)(void*, void*);

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

    auto open = reinterpret_cast<OpenFn>(vtable2[2]);
    auto read = reinterpret_cast<ReadFn>(vtable2[0]);
    auto close = reinterpret_cast<CloseFn>(vtable2[3]);
    LogFormat("[NorthstarPS4] fs overlay vtable2[0]=%p vtable2[2]=%p vtable2[3]=%p vtable2[10]=%p\n",
        vtable2[0], vtable2[2], vtable2[3], vtable2[10]);
    for (std::int32_t slot = 0; slot < 12; ++slot) {
        LogFormat("[NorthstarPS4] fs overlay vtable2[%d]=%p\n",
            slot, vtable2[slot]);
    }
    void* const fsFieldAddr =
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(fs) + 8);
    auto tryOpen = [&](const char* tag, const char* fileName) {
        void* const handle = open(reinterpret_cast<void**>(fsFieldAddr),
            fileName, "rb", "GAME", 0);
        if (handle == nullptr) {
            LogFormat("[NorthstarPS4] fs overlay open %-12s %-40s failed\n",
                tag, fileName);
            return;
        }
        char buffer[80]{};
        const std::int32_t bytesRead =
            read(reinterpret_cast<void**>(fsFieldAddr), buffer,
                sizeof(buffer) - 1, handle);
        buffer[sizeof(buffer) - 1] = '\0';
        LogFormat("[NorthstarPS4] fs overlay open %-12s %-40s handle=%p read=%d bytes=%.*s\n",
            tag, fileName, handle, bytesRead,
            bytesRead > 0 ? bytesRead : 0, buffer);
        close(fs, handle);
    };

    const struct { const char* tag; const char* file; } probePaths[] = {
        { "client-init", "scripts/vscripts/cl_northstar_client_init.nut" },
        { "client-loc-eng",
            "resource/northstar_client_localisation_english.txt" },
        { "client-cfg", "cfg/autoexec_ns_client.cfg" },
        { "custom-nut", "scripts/vscripts/_disallowed_tacticals.gnut" },
    };
    for (const auto& probe : probePaths) {
        tryOpen(probe.tag, probe.file);
    }
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
#if defined(NORTHSTAR_PS4_ENABLE_DIAGNOSTIC_UI_NATIVE)
    if (clientBase != 0) InstallDiagnosticUiRecord(clientBase, clientSpan);
#endif
    if (vstdlibHandle != static_cast<OrbisKernelModule>(-1)) {
        ProbeCvarInterface(vstdlibHandle, engineBase, engineSize);
        if (engineHandle != static_cast<OrbisKernelModule>(-1)) {
            ProbeRegistrationExports(engineHandle, vstdlibHandle);
        }
    } else {
        LogFormat("[NorthstarPS4] cvar probe skipped: vstdlib handle unavailable\n");
    }
#if defined(NORTHSTAR_PS4_ENABLE_M6_FS_OVERLAY)
    if (fsHandle != static_cast<OrbisKernelModule>(-1)) {
        ProbeFilesystemInterface(fsHandle);
    } else {
        LogFormat("[NorthstarPS4] fs overlay skipped: filesystem_stdio handle unavailable\n");
    }
#endif
    LogFormat(
        "[NorthstarPS4] module tracker complete engine=%d client=%d\n",
        engineSeen ? 1 : 0, clientSeen ? 1 : 0);
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