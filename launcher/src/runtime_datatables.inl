// Disk CSV datatables, matching NorthstarLauncher's scripts/scriptdatatables.cpp.
// Included inside namespace uiapi after the common Squirrel value helpers.

constexpr int kDatatableHandleBase = 0x44540000; // "DT"
constexpr int kDatatableHandleMask = 0xffff0000;
constexpr std::size_t kMaxDatatableBytes = 16 * 1024 * 1024;

struct CachedDatatable {
    std::string asset;
    DatatableCsv table;
};
std::vector<CachedDatatable> datatableCache;
std::atomic_flag datatableLock = ATOMIC_FLAG_INIT;

using DatatableNative = int (*)(void*);
struct DatatableOverride { const char* name; DatatableNative replacement; DatatableNative original; };

int GetDataTableCsv(void* vm);
int DataTableColumn(void* vm);
int DataTableRows(void* vm);
int DataTableString(void* vm);
int DataTableAsset(void* vm);
int DataTableInt(void* vm);
int DataTableFloat(void* vm);
int DataTableBool(void* vm);
int DataTableVector(void* vm);
enum class DatatableMatch { String, Asset, Int, Float, GreaterInt, LessInt, GreaterFloat, LessFloat };
template<DatatableMatch mode> int DataTableFind(void* vm);

DatatableOverride datatableOverrides[] = {
    {"GetDataTable", GetDataTableCsv, nullptr},
    {"GetDataTableColumnByName", DataTableColumn, nullptr},
    {"GetDatatableRowCount", DataTableRows, nullptr},
    {"GetDataTableString", DataTableString, nullptr},
    {"GetDataTableAsset", DataTableAsset, nullptr},
    {"GetDataTableInt", DataTableInt, nullptr},
    {"GetDataTableFloat", DataTableFloat, nullptr},
    {"GetDataTableBool", DataTableBool, nullptr},
    {"GetDataTableVector", DataTableVector, nullptr},
    {"GetDataTableRowMatchingStringValue", DataTableFind<DatatableMatch::String>, nullptr},
    {"GetDataTableRowMatchingAssetValue", DataTableFind<DatatableMatch::Asset>, nullptr},
    {"GetDataTableRowMatchingIntValue", DataTableFind<DatatableMatch::Int>, nullptr},
    {"GetDataTableRowMatchingFloatValue", DataTableFind<DatatableMatch::Float>, nullptr},
    {"GetDataTableRowGreaterThanOrEqualToIntValue", DataTableFind<DatatableMatch::GreaterInt>, nullptr},
    {"GetDataTableRowLessThanOrEqualToIntValue", DataTableFind<DatatableMatch::LessInt>, nullptr},
    {"GetDataTableRowGreaterThanOrEqualToFloatValue", DataTableFind<DatatableMatch::GreaterFloat>, nullptr},
    {"GetDataTableRowLessThanOrEqualToFloatValue", DataTableFind<DatatableMatch::LessFloat>, nullptr},
};

DatatableNative OriginalDatatable(const char* name) {
    for (const auto& entry : datatableOverrides)
        if (!std::strcmp(entry.name, name)) return entry.original;
    return nullptr;
}
int CallOriginalDatatable(void* vm, const char* name) {
    auto original = OriginalDatatable(name);
    return original ? original(vm) : Error(vm, "Original datatable native was not captured");
}
bool IsCustomDatatableArg(void* vm, int index = 1) {
    const auto& value = Arg(vm, index);
    return value.tag == kSqInteger &&
        (static_cast<int>(static_cast<std::uint32_t>(value.value)) & kDatatableHandleMask) == kDatatableHandleBase;
}

// PC Northstar intercepts RegisterSquirrelFunction before each built-in record
// is consumed. Do the same without detouring the registrar itself: rewrite
// only direct calls in client.prx's validated executable segment whose decoded
// target is the registrar, so our wrapper can retain the original record
// function and substitute the disk-CSV implementation before Titanfall inserts
// it into the VM.
using ClientRegistrar = void (*)(void*, void*, void*, int, int);
ClientRegistrar originalClientRegistrar = nullptr;
bool datatableRegistrarHooked = false;

void DatatableClientRegistrar(void* owner, void* rawRecord, void* unknown, int a, int b) {
    auto* record = static_cast<std::uint8_t*>(rawRecord);
    const char* name = record ? *reinterpret_cast<const char**>(record) : nullptr;
    if (name) for (auto& entry : datatableOverrides) {
        if (std::strcmp(name, entry.name)) continue;
        auto& function = *reinterpret_cast<DatatableNative*>(record + 0x60);
        if (!entry.original) entry.original = function;
        function = entry.replacement;
        LogFormat("[NorthstarPS4] datatable native replaced: %s original=%p\n",
            name, reinterpret_cast<void*>(entry.original));
        break;
    }
    originalClientRegistrar(owner, rawRecord, unknown, a, b);
}

bool InstallClientDatatableRegistrarHook() noexcept {
    if (datatableRegistrarHooked) return true;
    if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan,
            kClientRegisterSquirrelFuncVa, kClientRegisterSquirrelFuncPreimage,
            sizeof(kClientRegisterSquirrelFuncPreimage)) ||
        !ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan,
            kClientNativeRegistrationBlockVa, kClientNativeRegistrationBlockPreimage,
            sizeof(kClientNativeRegistrationBlockPreimage)) ||
        !ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan,
            kClientNativeRegistrationCallerVa, kClientNativeRegistrationCallerPreimage,
            sizeof(kClientNativeRegistrationCallerPreimage))) return false;
    originalClientRegistrar = reinterpret_cast<ClientRegistrar>(g_runtimeClientBase + kClientRegisterSquirrelFuncVa);
    // Built-ins are split across several generated registration functions;
    // GetDataTable itself is registered at client+0x312c6b, outside the block
    // that holds RunUIScript. Scan only the validated executable PT_LOAD and
    // rewrite calls whose decoded target is exactly the registrar.
    constexpr std::uintptr_t beginVa = 0;
    const std::uintptr_t endVa = std::min<std::uintptr_t>(g_runtimeClientSpan, 0x9dcc14);
    const std::uintptr_t replacement = reinterpret_cast<std::uintptr_t>(&DatatableClientRegistrar);
    std::size_t patched = 0;
    for (std::uintptr_t va = beginVa; va + 5 <= endVa; ++va) {
        auto* call = reinterpret_cast<std::uint8_t*>(g_runtimeClientBase + va);
        if (*call != 0xe8) continue;
        std::int32_t displacement;
        std::memcpy(&displacement, call + 1, sizeof(displacement));
        const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(call + 5) + displacement;
        if (target != g_runtimeClientBase + kClientRegisterSquirrelFuncVa) continue;
        const std::int64_t relative = static_cast<std::int64_t>(replacement) -
            static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(call + 5));
        if (relative < INT32_MIN || relative > INT32_MAX) return false;
        void* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(call) & ~std::uintptr_t(0x3fff));
        if (sceKernelMprotect(page, 0x4000, 7) != 0) return false;
        const auto newDisplacement = static_cast<std::int32_t>(relative);
        std::memcpy(call + 1, &newDisplacement, sizeof(newDisplacement));
        if (sceKernelMprotect(page, 0x4000, 5) != 0) return false;
        ++patched;
        va += 4;
    }
    datatableRegistrarHooked = patched != 0;
    LogFormat("[NorthstarPS4] datatable registrar calls patched=%zu\n", patched);
    return datatableRegistrarHooked;
}

struct DatatableGuard {
    DatatableGuard() { while (datatableLock.test_and_set(std::memory_order_acquire)) sceKernelUsleep(100); }
    ~DatatableGuard() { datatableLock.clear(std::memory_order_release); }
};

void ClearDatatableCache() noexcept {
    DatatableGuard guard;
    datatableCache.clear();
}

const char* AssetOrStringArg(void* vm, int index) {
    const auto& arg = Arg(vm, index);
    return (arg.tag == kSqAsset || arg.tag == kSqString)
        ? reinterpret_cast<const char*>(arg.value + 0x30) : nullptr;
}

bool ReadDatatableFile(const char* path, std::string& out) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return false; }
    const long end = std::ftell(file);
    if (end <= 0 || static_cast<std::size_t>(end) > kMaxDatatableBytes ||
        std::fseek(file, 0, SEEK_SET) != 0) { std::fclose(file); return false; }
    out.resize(static_cast<std::size_t>(end));
    const bool ok = std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    if (!ok) out.clear();
    return ok;
}

bool DatatableRelativePath(const char* asset, char* out, std::size_t capacity) {
    if (!asset || std::strncmp(asset, "datatable/", 10) != 0) return false;
    std::string path = std::string("scripts/") + asset;
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".rpak") == 0)
        path.replace(path.size() - 5, 5, ".csv");
    return NormalizeRequestedPath(path.c_str(), out, capacity) != 0;
}

CachedDatatable* TableArgLocked(void* vm, int index, const char* caller) {
    const auto& value = Arg(vm, index);
    if (value.tag != kSqInteger) { Error(vm, caller); return nullptr; }
    const int handle = static_cast<int>(static_cast<std::uint32_t>(value.value));
    if ((handle & kDatatableHandleMask) != kDatatableHandleBase) { Error(vm, caller); return nullptr; }
    const std::size_t slot = static_cast<std::uint16_t>(handle);
    if (slot >= datatableCache.size()) { Error(vm, "Datatable handle expired after mod reload"); return nullptr; }
    return &datatableCache[slot];
}

const std::string* DatatableCell(void* vm, CachedDatatable* cached, int rowArg = 2, int colArg = 3) {
    if (Arg(vm, rowArg).tag != kSqInteger || Arg(vm, colArg).tag != kSqInteger) {
        Error(vm, "Datatable row and column must be integers");
        return nullptr;
    }
    const int row = static_cast<std::int32_t>(Arg(vm, rowArg).value);
    const int column = static_cast<std::int32_t>(Arg(vm, colArg).value);
    const std::string* cell = cached->table.Cell(row, column);
    if (!cell) {
        char message[192];
        std::snprintf(message, sizeof(message), "Datatable %s row %d column %d is outside its bounds",
            cached->asset.c_str(), row, column);
        Error(vm, message);
    }
    return cell;
}

bool ParseIntCell(const std::string& text, int& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno || end == text.c_str() || *end || parsed < INT32_MIN || parsed > INT32_MAX) return false;
    value = static_cast<int>(parsed);
    return true;
}
bool ParseFloatCell(const std::string& text, float& value) {
    char* end = nullptr;
    errno = 0;
    value = std::strtof(text.c_str(), &end);
    return !errno && end != text.c_str() && *end == '\0';
}

int GetDataTableCsv(void* vm) {
    const char* asset = AssetOrStringArg(vm, 1);
    if (!asset) return Error(vm, "GetDataTable expects an asset path");
    char relative[256], absolute[512];
    if (!DatatableRelativePath(asset, relative, sizeof(relative)))
        return Error(vm, "Datatable asset must be a safe path beginning with datatable/");

    DatatableGuard guard;
    for (std::size_t i = 0; i < datatableCache.size(); ++i) if (datatableCache[i].asset == asset) {
        Integer(vm, kDatatableHandleBase | static_cast<int>(i));
        return 1;
    }
    if (datatableCache.size() >= 65536) return Error(vm, "Datatable cache is full");
    if (!ResolveModFile(relative, absolute, sizeof(absolute))) return CallOriginalDatatable(vm, "GetDataTable");
    std::string csv, parseError;
    DatatableCsv table;
    if (!ReadDatatableFile(absolute, csv)) return Error(vm, "Datatable CSV is empty, too large, or unreadable");
    if (!ParseDatatableCsv(csv, table, parseError)) {
        char message[320];
        std::snprintf(message, sizeof(message), "Datatable %s is invalid: %s", asset, parseError.c_str());
        return Error(vm, message);
    }
    const std::size_t slot = datatableCache.size();
    datatableCache.push_back({asset, std::move(table)});
    LogFormat("[NorthstarPS4] datatable CSV loaded: %s rows=%zu columns=%zu file=%s\n", asset,
        datatableCache.back().table.rows.size(), datatableCache.back().table.columns.size(), absolute);
    Integer(vm, kDatatableHandleBase | static_cast<int>(slot));
    return 1;
}

int DataTableColumn(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableColumnByName");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableColumnByName expects a PS4 CSV datatable");
    const char* name = TextArg(vm, 2);
    if (!table || !name) return table ? Error(vm, "GetDataTableColumnByName expects a string") : -1;
    Integer(vm, table->table.Column(name));
    return 1;
}
int DataTableRows(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDatatableRowCount");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDatatableRowCount expects a PS4 CSV datatable");
    if (!table) return -1;
    Integer(vm, static_cast<int>(table->table.rows.size()));
    return 1;
}
int DataTableString(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableString");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableString expects a PS4 CSV datatable");
    if (!table) return -1;
    const auto* cell = DatatableCell(vm, table);
    if (!cell) return -1;
    String(vm, cell->c_str());
    return 1;
}
int DataTableAsset(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableAsset");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableAsset expects a PS4 CSV datatable");
    if (!table) return -1;
    const auto* cell = DatatableCell(vm, table);
    if (!cell) return -1;
    Asset(vm, cell->c_str());
    return 1;
}
int DataTableInt(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableInt");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableInt expects a PS4 CSV datatable");
    if (!table) return -1;
    const auto* cell = DatatableCell(vm, table);
    int value = 0;
    if (!cell || !ParseIntCell(*cell, value)) return cell ? Error(vm, "Datatable cell is not an integer") : -1;
    Integer(vm, value);
    return 1;
}
int DataTableFloat(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableFloat");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableFloat expects a PS4 CSV datatable");
    if (!table) return -1;
    const auto* cell = DatatableCell(vm, table);
    float value = 0;
    if (!cell || !ParseFloatCell(*cell, value)) return cell ? Error(vm, "Datatable cell is not a float") : -1;
    Float(vm, value);
    return 1;
}
int DataTableBool(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableBool");
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "GetDataTableBool expects a PS4 CSV datatable");
    if (!table) return -1;
    const auto* cell = DatatableCell(vm, table);
    int value = 0;
    if (!cell || !ParseIntCell(*cell, value)) return cell ? Error(vm, "Datatable bool cell is not an integer") : -1;
    Boolean(vm, value != 0);
    return 1;
}

template<DatatableMatch mode> int DataTableFind(void* vm) {
    constexpr const char* names[] = {
        "GetDataTableRowMatchingStringValue", "GetDataTableRowMatchingAssetValue",
        "GetDataTableRowMatchingIntValue", "GetDataTableRowMatchingFloatValue",
        "GetDataTableRowGreaterThanOrEqualToIntValue", "GetDataTableRowLessThanOrEqualToIntValue",
        "GetDataTableRowGreaterThanOrEqualToFloatValue", "GetDataTableRowLessThanOrEqualToFloatValue",
    };
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, names[static_cast<int>(mode)]);
    DatatableGuard guard;
    auto* table = TableArgLocked(vm, 1, "Datatable row search expects a PS4 CSV datatable");
    if (!table || Arg(vm, 2).tag != kSqInteger) return table ? Error(vm, "Datatable search column must be an integer") : -1;
    const int column = static_cast<std::int32_t>(Arg(vm, 2).value);
    const char* text = nullptr;
    int integer = 0;
    float number = 0;
    if constexpr (mode == DatatableMatch::String) text = TextArg(vm, 3);
    if constexpr (mode == DatatableMatch::Asset) text = AssetOrStringArg(vm, 3);
    if constexpr (mode == DatatableMatch::Int || mode == DatatableMatch::GreaterInt || mode == DatatableMatch::LessInt) {
        if (Arg(vm, 3).tag != kSqInteger) return Error(vm, "Datatable search value must be an integer");
        integer = static_cast<std::int32_t>(Arg(vm, 3).value);
    }
    if constexpr (mode == DatatableMatch::Float || mode == DatatableMatch::GreaterFloat || mode == DatatableMatch::LessFloat)
        number = NumberArg(vm, 3);
    if constexpr (mode == DatatableMatch::String || mode == DatatableMatch::Asset)
        if (!text) return Error(vm, "Datatable search value has the wrong type");
    for (std::size_t row = 0; row < table->table.rows.size(); ++row) {
        const std::string* cell = table->table.Cell(static_cast<int>(row), column);
        if (!cell) return Error(vm, "Datatable search column is outside the row bounds");
        bool match = false;
        if constexpr (mode == DatatableMatch::String || mode == DatatableMatch::Asset) match = *cell == text;
        else if constexpr (mode == DatatableMatch::Int) { int value; match = ParseIntCell(*cell, value) && value == integer; }
        else if constexpr (mode == DatatableMatch::Float) { float value; match = ParseFloatCell(*cell, value) && value == number; }
        else if constexpr (mode == DatatableMatch::GreaterInt) { int value; match = ParseIntCell(*cell, value) && integer >= value; }
        else if constexpr (mode == DatatableMatch::LessInt) { int value; match = ParseIntCell(*cell, value) && integer <= value; }
        else if constexpr (mode == DatatableMatch::GreaterFloat) { float value; match = ParseFloatCell(*cell, value) && number >= value; }
        else if constexpr (mode == DatatableMatch::LessFloat) { float value; match = ParseFloatCell(*cell, value) && number <= value; }
        if (match) { Integer(vm, static_cast<int>(row)); return 1; }
    }
    Integer(vm, -1);
    return 1;
}

int DataTableVector(void* vm) {
    if (!IsCustomDatatableArg(vm)) return CallOriginalDatatable(vm, "GetDataTableVector");
    return Error(vm, "Custom CSV vector datatable cells are not implemented on PS4 yet");
}
