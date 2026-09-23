// DecodeJSON / EncodeJSON, included inside namespace uiapi.
//
// PC Northstar builds Squirrel values through the sq_ API and reads tables
// straight out of SQTable (scripts/scriptjson.cpp). The PS4 client exposes the
// same primitives at the addresses gated in RegisterRuntimeUiNatives, and the
// object layouts below are the ones the client's own code uses:
//   SQTable  +0x38 nodes, +0x40 node count, node stride 0x28 (val, key, next)
//   SQArray  +0x30 values, +0x38 used slots
//   SQString characters at +0x30
// verified against the client's table insert at VA 0x6ab480 and its array
// append at VA 0x683650.
constexpr std::uint64_t kSqNull = 0x1000001;
constexpr std::uint64_t kSqBool = 0x1000008;
constexpr std::uint64_t kSqInteger = 0x5000002;
constexpr std::uint64_t kSqFloat = 0x5000004;
constexpr std::uint64_t kSqString = 0x8000010;
constexpr std::uint64_t kSqAsset = 0x8000400;
constexpr std::uint64_t kSqArray = 0x8000040;
constexpr std::uint64_t kSqTable = 0xa000020;

void NewTable(void* vm) { At<void (*)(void*)>(0x683230)(vm); }
Object& Top(void* vm) {
    auto bytes = static_cast<char*>(vm);
    const auto top = *reinterpret_cast<std::uint32_t*>(bytes + 0x68);
    return (*reinterpret_cast<Object**>(bytes + 0x70))[top - 1];
}
void Float(void* vm, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    PushPrimitive(vm, kSqFloat, bits);
}
void Null(void* vm) { PushPrimitive(vm, kSqNull, 0); }

// Stores the value currently on top of the stack into `table` under `key`,
// then pops it.
//
// The insert (SQTable::NewSlot, VA 0x6ab3e0) takes its own reference to the
// value (0x6ab7af / 0x6ab7d3), so none is added here: the reference the client
// takes at 0x684555 belongs to its local SQObjectPtr and is dropped again at
// 0x6845ab. Adding one leaked every decoded string, array and table.
//
// Its bool is *not* success. It is false whenever the key ends up in an
// existing node, which includes every insert that grows the table: the key is
// written into its main position before the free-node search (0x6ab59f), so
// after Rehash the retry finds it and takes the replace path (0x6ab7e0). That
// is why a JSON object with more than a few members failed with "string could
// not be stored", and why the NS_VERSION_PATCH constant logs result=0 while
// working. Nothing on either path can fail short of allocation, so the result
// is ignored.
bool TableStoreTop(void* vm, void* table, const char* key) {
    auto shared = *reinterpret_cast<void**>(static_cast<char*>(vm) + 0x50);
    if (!shared || !table) return false;
    auto strings = *reinterpret_cast<void**>(static_cast<char*>(shared) + 0x4048);
    if (!strings) return false;
    auto intern = At<void* (*)(void*, const char*, std::int32_t)>(0x6a96a0);
    void* keyString = intern(strings, key, -1);
    if (!keyString) return false;
    *reinterpret_cast<void**>(static_cast<char*>(keyString) + 0x18) = shared;
    ++*reinterpret_cast<std::uint32_t*>(static_cast<char*>(keyString) + 8);
    const Object keyObject{kSqString, reinterpret_cast<std::uint64_t>(keyString)};
    At<bool (*)(void*, const void*, const void*)>(0x6ab3e0)(table, &keyObject, &Top(vm));
    Pop(vm, 1);
    return true;
}

// Builds Squirrel values as JsonParse walks the document. Every completed
// value leaves exactly one entry on the Squirrel stack, which its parent
// consumes; open containers stay on the stack so the collector cannot free
// them while their members are being filled.
struct SquirrelJsonBuilder {
    void* vm;
    struct Frame { bool isObject; void* table; std::string key; };
    std::vector<Frame> frames;
    bool rootIsObject = false;

    bool Attach() {
        if (frames.empty()) return true;               // the document root
        Frame& parent = frames.back();
        if (!parent.isObject) { Append(vm); return true; }
        return TableStoreTop(vm, parent.table, parent.key.c_str());
    }
    bool OnObjectBegin() {
        if (frames.empty()) rootIsObject = true;
        NewTable(vm);
        Object& table = Top(vm);
        if (table.tag != kSqTable) return false;
        frames.push_back({true, reinterpret_cast<void*>(table.value), std::string()});
        return true;
    }
    bool OnArrayBegin() {
        Array(vm);
        frames.push_back({false, nullptr, std::string()});
        return true;
    }
    bool OnObjectEnd() { frames.pop_back(); return Attach(); }
    bool OnArrayEnd() { frames.pop_back(); return Attach(); }
    bool OnKey(const char* text, std::size_t) { frames.back().key = text; return true; }
    bool OnString(const char* text, std::size_t) { String(vm, text); return Attach(); }
    bool OnInteger(int value) { Integer(vm, value); return Attach(); }
    bool OnFloat(float value) { Float(vm, value); return Attach(); }
    bool OnBool(bool value) { Boolean(vm, value); return Attach(); }
    // PC drops null members instead of storing them, because Squirrel tables
    // treat a null value as an absent key.
    bool OnNull() {
        if (frames.empty()) { Null(vm); return true; }
        if (frames.back().isObject) return true;
        Null(vm);
        return Attach();
    }
    void Unwind() { Pop(vm, static_cast<int>(frames.size())); frames.clear(); }
};

int DecodeJson(void* vm) {
    const char* text = TextArg(vm, 1);
    const bool fatal = Arg(vm, 2).tag == kSqBool && Arg(vm, 2).value != 0;
    SquirrelJsonBuilder builder{vm, {}, false};
    JsonParseResult result{false, 0, "expected a string"};
    if (text) result = JsonParse(text, builder);
    if (result.ok && !builder.rootIsObject) {
        // DecodeJSON is declared to return a table; a bare scalar or array
        // document has no table form, so PC's callers would see an empty one.
        Pop(vm, 1);
        result = {false, 0, "the document root is not a JSON object"};
    }
    if (!result.ok) {
        builder.Unwind();
        char message[256];
        std::snprintf(message, sizeof(message),
            "Failed parsing json file: encountered parse error \"%s\" at offset %zu",
            result.message ? result.message : "invalid document", result.offset);
        if (fatal) return Error(vm, message);
        LogFormat("[NorthstarPS4] %s\n", message);
        NewTable(vm);
        return 1;
    }
    return 1;
}

// Reads a Squirrel value back out as JSON text. Types PC cannot represent
// (structs, instances, entities) are skipped rather than guessed at, matching
// the comment in Northstar.Custom's own test script.
void EncodeValue(std::string& out, const Object& value, int depth);

void EncodeTable(std::string& out, void* table, int depth) {
    out.push_back('{');
    auto bytes = static_cast<char*>(table);
    auto nodes = *reinterpret_cast<char**>(bytes + 0x38);
    const auto count = *reinterpret_cast<std::int32_t*>(bytes + 0x40);
    bool first = true;
    for (std::int32_t i = 0; nodes && i < count; ++i) {
        const char* node = nodes + static_cast<std::size_t>(i) * 0x28;
        const auto& key = *reinterpret_cast<const Object*>(node + 0x10);
        const auto& value = *reinterpret_cast<const Object*>(node);
        if (key.tag != kSqString || value.tag == kSqNull) continue;
        const std::size_t mark = out.size();
        if (!first) out.push_back(',');
        const char* name = reinterpret_cast<const char*>(key.value + 0x30);
        JsonEscapeInto(out, name, std::strlen(name));
        out.push_back(':');
        const std::size_t before = out.size();
        EncodeValue(out, value, depth + 1);
        if (out.size() == before) { out.resize(mark); continue; } // unsupported type
        first = false;
    }
    out.push_back('}');
}

void EncodeArray(std::string& out, void* array, int depth) {
    out.push_back('[');
    auto bytes = static_cast<char*>(array);
    auto values = *reinterpret_cast<Object**>(bytes + 0x30);
    const auto used = *reinterpret_cast<std::int32_t*>(bytes + 0x38);
    bool first = true;
    for (std::int32_t i = 0; values && i < used; ++i) {
        const std::size_t mark = out.size();
        if (!first) out.push_back(',');
        const std::size_t before = out.size();
        EncodeValue(out, values[i], depth + 1);
        if (out.size() == before) { out.resize(mark); continue; }
        first = false;
    }
    out.push_back(']');
}

void EncodeValue(std::string& out, const Object& value, int depth) {
    if (depth > kJsonMaxDepth) return;
    switch (value.tag) {
    case kSqNull: out += "null"; return;
    case kSqBool: out += value.value ? "true" : "false"; return;
    case kSqInteger: {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(static_cast<std::int32_t>(value.value)));
        out += buffer;
        return;
    }
    case kSqFloat: {
        float number;
        const auto bits = static_cast<std::uint32_t>(value.value);
        std::memcpy(&number, &bits, sizeof(number));
        JsonAppendFloat(out, number);
        return;
    }
    case kSqString:
    case kSqAsset: {
        const char* text = reinterpret_cast<const char*>(value.value + 0x30);
        JsonEscapeInto(out, text, std::strlen(text));
        return;
    }
    case kSqTable: EncodeTable(out, reinterpret_cast<void*>(value.value), depth); return;
    case kSqArray: EncodeArray(out, reinterpret_cast<void*>(value.value), depth); return;
    default: return; // skipped, as PC skips what it cannot represent
    }
}

bool EncodeArgument(void* vm, int argument, std::string& out) {
    const auto& value = Arg(vm, argument);
    if (value.tag != kSqTable) return false;
    EncodeTable(out, reinterpret_cast<void*>(value.value), 0);
    return true;
}

int EncodeJson(void* vm) {
    std::string json;
    if (!EncodeArgument(vm, 1, json)) return Error(vm, "EncodeJSON expects a table");
    String(vm, json.c_str());
    return 1;
}

int SaveJsonFile(void* vm) {
    char directory[320], full[512];
    const char* relative = nullptr;
    int error = 0;
    if (!ResolveSavePath(vm, 0, 1, false, directory, sizeof(directory), full, sizeof(full), &relative, &error)) return error;
    std::string json;
    if (!EncodeArgument(vm, 2, json)) return Error(vm, "NSSaveJSONFile expects a file name and a table");
    if (!SaveContentsValid(json.data(), json.size()))
        return Error(vm, "File contents may not contain NUL characters! Make sure your strings are valid!");
    return WriteSaveFile(vm, directory, full, relative, json.data(), json.size());
}
