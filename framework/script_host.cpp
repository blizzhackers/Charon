/**
 * LuaJIT script host implementation. See headers/script_host.h.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "headers/script_host.h"
#include <cmath>
#include <cstdio>

extern "C" {
#include "lualib.h"
}

namespace Script {

namespace {

const char* HostKey = "charon.host";

uint64_t LastWriteTime(const std::wstring& file) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

std::wstring DirectoryOf(const std::wstring& file) {
    const size_t separator = file.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : file.substr(0, separator);
}

// Reads with a wide path so non-ASCII install directories work, unlike luaL_loadfile.
int LoadFile(lua_State* L, const std::wstring& file) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") || !fp) {
        lua_pushfstring(L, "cannot open %s", Narrow(file).c_str());
        return LUA_ERRFILE;
    }
    std::string source;
    char chunk[4096];
    size_t read;
    while ((read = fread(chunk, 1, sizeof(chunk), fp)) > 0) source.append(chunk, read);
    fclose(fp);
    const std::string chunkName = "@" + Narrow(file);
    return luaL_loadbuffer(L, source.data(), source.size(), chunkName.c_str());
}

int Traceback(lua_State* L) {
    const char* message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "(non-string error)", 1);
    return 1;
}

int Print(lua_State* L) {
    // Builds the line in a Lua buffer: tostring may raise, and no C++ object may be live then.
    const int count = lua_gettop(L);
    luaL_Buffer buffer;
    lua_getglobal(L, "tostring");
    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) luaL_addchar(&buffer, '\t');
        lua_pushvalue(L, count + 1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);
    if (ScriptHost* host = HostFor(L)) host->Log(lua_tostring(L, -1));
    return 0;
}

// package.loaders entry that searches package.path with wide-character file
// APIs (so non-ASCII install folders work) and records each module file, even
// one that fails to compile, so editing it triggers a reload.
int TrackedSearcher(lua_State* L) {
    const char* module = luaL_checkstring(L, 1);
    lua_pushstring(L, "package");
    lua_rawget(L, LUA_GLOBALSINDEX);
    if (!lua_istable(L, -1)) return 0;
    lua_pushstring(L, "path");
    lua_rawget(L, -2);
    const char* pathText = lua_tostring(L, -1);
    char tried[2048] = { 0 };
    int status = -1;
    {
        const std::string path = pathText ? pathText : "";
        std::string name = module;
        for (char& c : name) {
            if (c == '.') c = '\\';
        }
        std::wstring found;
        size_t start = 0;
        while (start <= path.size() && found.empty()) {
            size_t end = path.find(';', start);
            if (end == std::string::npos) end = path.size();
            std::string candidate = path.substr(start, end - start);
            start = end + 1;
            if (candidate.empty()) continue;
            for (size_t at = candidate.find('?'); at != std::string::npos; at = candidate.find('?', at + name.size())) {
                candidate.replace(at, 1, name);
            }
            const std::wstring wide = Widen(candidate);
            if (GetFileAttributesW(wide.c_str()) != INVALID_FILE_ATTRIBUTES) found = wide;
            else {
                strncat_s(tried, "\n\tno file '", _TRUNCATE);
                strncat_s(tried, candidate.c_str(), _TRUNCATE);
                strncat_s(tried, "'", _TRUNCATE);
            }
        }
        if (!found.empty()) {
            if (ScriptHost* host = HostFor(L)) host->TrackSource(found);
            status = LoadFile(L, found);
        }
    }
    if (status < 0) {
        lua_pushstring(L, tried);
        return 1;
    }
    if (status != 0) return luaL_error(L, "error loading module '%s':\n\t%s", module, lua_tostring(L, -1));
    return 1;
}

int StoreGet(lua_State* L) {
    const std::string key = ArgString(L, 1);
    auto& store = SharedStore();
    const auto found = store.find(key);
    PushValue(L, found == store.end() ? Value() : found->second);
    return 1;
}

int StoreSet(lua_State* L) {
    const std::string key = ArgString(L, 1);
    const Value value = ReadValue(L, 2);
    if (std::holds_alternative<std::monostate>(value)) SharedStore().erase(key);
    else SharedStore()[key] = value;
    return 0;
}

void SetPackagePath(lua_State* L, const std::wstring& scriptPath) {
    // The script's folder, then its parent: scripts/oog/x.lua can require "lib.fsm".
    const std::string dir = Narrow(DirectoryOf(scriptPath));
    const std::string path = dir + "\\?.lua;" + dir + "\\?\\init.lua;" + dir + "\\..\\?.lua;" + dir + "\\..\\?\\init.lua";
    lua_getglobal(L, "package");
    lua_pushstring(L, path.c_str());
    lua_setfield(L, -2, "path");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    // Insert after preload so `require` tracks every Lua module it loads.
    lua_getfield(L, -1, "loaders");
    const int count = (int)lua_objlen(L, -1);
    for (int i = count; i >= 2; --i) {
        lua_rawgeti(L, -1, i);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushcfunction(L, TrackedSearcher);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);
}

}

void RawField(lua_State* L, int index, const char* key) {
    if (index < 0 && index > LUA_REGISTRYINDEX) index = lua_gettop(L) + index + 1;
    lua_pushstring(L, key);
    lua_rawget(L, index);
}

int RaiseCaught(lua_State* L, const char* message) {
    return luaL_error(L, "%s", message);
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size);
    return out;
}

double ArgNumber(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TNUMBER) {
        throw Error("bad argument #" + std::to_string(index) + " (number expected, got " + luaL_typename(L, index) + ")");
    }
    return lua_tonumber(L, index);
}

int ArgInt(lua_State* L, int index) {
    const double value = ArgNumber(L, index);
    if (value != std::floor(value) || value < INT_MIN || value > INT_MAX) {
        throw Error("bad argument #" + std::to_string(index) + " (integer expected)");
    }
    return (int)value;
}

int OptInt(lua_State* L, int index, int fallback) {
    return lua_isnoneornil(L, index) ? fallback : ArgInt(L, index);
}

bool ArgBool(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TBOOLEAN) {
        throw Error("bad argument #" + std::to_string(index) + " (boolean expected, got " + luaL_typename(L, index) + ")");
    }
    return lua_toboolean(L, index) != 0;
}

bool OptBool(lua_State* L, int index, bool fallback) {
    return lua_isnoneornil(L, index) ? fallback : ArgBool(L, index);
}

std::string ArgString(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TSTRING) {
        throw Error("bad argument #" + std::to_string(index) + " (string expected, got " + luaL_typename(L, index) + ")");
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    return std::string(text, length);
}

std::string OptString(lua_State* L, int index, const std::string& fallback) {
    return lua_isnoneornil(L, index) ? fallback : ArgString(L, index);
}

void SetField(lua_State* L, const char* key, double value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }
void SetField(lua_State* L, const char* key, int value) { lua_pushinteger(L, value); lua_setfield(L, -2, key); }
void SetField(lua_State* L, const char* key, unsigned int value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }
void SetField(lua_State* L, const char* key, bool value) { lua_pushboolean(L, value); lua_setfield(L, -2, key); }
void SetField(lua_State* L, const char* key, const char* value) { lua_pushstring(L, value); lua_setfield(L, -2, key); }
void SetField(lua_State* L, const char* key, const std::string& value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

void PushValue(lua_State* L, const Value& value) {
    if (const bool* b = std::get_if<bool>(&value)) lua_pushboolean(L, *b);
    else if (const double* d = std::get_if<double>(&value)) lua_pushnumber(L, *d);
    else if (const std::string* s = std::get_if<std::string>(&value)) lua_pushlstring(L, s->data(), s->size());
    else lua_pushnil(L);
}

Value ReadValue(lua_State* L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNONE:
    case LUA_TNIL: return Value();
    case LUA_TBOOLEAN: return Value(lua_toboolean(L, index) != 0);
    case LUA_TNUMBER: return Value(lua_tonumber(L, index));
    case LUA_TSTRING: return Value(ArgString(L, index));
    default:
        throw Error(std::string("unsupported value type: ") + luaL_typename(L, index));
    }
}

std::map<std::string, Value>& SharedStore() {
    static std::map<std::string, Value> store;
    return store;
}

namespace {
std::vector<std::pair<int, ModuleInstaller>>& ModuleRegistry() {
    static std::vector<std::pair<int, ModuleInstaller>> registry;
    return registry;
}
}

void RegisterModule(int contexts, ModuleInstaller installer) {
    ModuleRegistry().emplace_back(contexts, std::move(installer));
}

std::vector<ModuleInstaller> ModulesFor(int context) {
    std::vector<ModuleInstaller> modules;
    for (const auto& [contexts, installer] : ModuleRegistry()) {
        if (contexts & context) modules.push_back(installer);
    }
    return modules;
}

namespace {
std::vector<std::pair<std::string, MemoryReporter>>& MemoryRegistry() {
    static std::vector<std::pair<std::string, MemoryReporter>> registry;
    return registry;
}
}

void RegisterMemoryUse(const std::string& name, MemoryReporter reporter) {
    MemoryRegistry().emplace_back(name, std::move(reporter));
}

std::vector<std::pair<std::string, size_t>> MemoryUse() {
    std::vector<std::pair<std::string, size_t>> use;
    for (const auto& [name, reporter] : MemoryRegistry()) use.emplace_back(name, reporter());
    return use;
}

ScriptHost* HostFor(lua_State* L) {
    RawField(L, LUA_REGISTRYINDEX, HostKey);
    ScriptHost* host = static_cast<ScriptHost*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return host;
}

ScriptHost::ScriptHost(std::string name, std::wstring path, LogSink log)
    : name(std::move(name)), path(std::move(path)), log(std::move(log)) { }

ScriptHost::~ScriptHost() { Unload(); }

void ScriptHost::AddModule(ModuleInstaller installer) { modules.push_back(std::move(installer)); }
void ScriptHost::SetAllowFFI(bool allow) { allowFFI = allow; }

void ScriptHost::Log(const std::string& line) const {
    if (log) log("[" + name + "] " + line);
}

size_t ScriptHost::HeapBytes() const {
    if (!L) return 0;
    return size_t(lua_gc(L, LUA_GCCOUNT, 0)) * 1024 + size_t(lua_gc(L, LUA_GCCOUNTB, 0));
}

void ScriptHost::Fail(const std::string& what) {
    failed = true;
    Log("error: " + what);
    Log("script stopped; it reloads when its files change");
}

bool ScriptHost::Load() {
    Unload();
    failed = false;
    sources.clear();
    events.clear();
    L = luaL_newstate();
    if (!L) {
        Fail("cannot create Lua state");
        return false;
    }
    luaL_openlibs(L);
    if (!allowFFI) {
        // FFI gives scripts unchecked memory access; it is opt-in (-luaffi).
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "preload");
        lua_pushnil(L);
        lua_setfield(L, -2, "ffi");
        lua_getfield(L, -2, "loaded");
        lua_pushnil(L);
        lua_setfield(L, -2, "ffi");
        lua_pop(L, 3);
    }
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, HostKey);
    lua_register(L, "print", Print);
    SetPackagePath(L, path);

    static const luaL_Reg storeFunctions[] = {
        { "get", Bind<StoreGet> },
        { "set", Bind<StoreSet> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, storeFunctions, 0);
    lua_setglobal(L, "store");

    for (const ModuleInstaller& install : modules) install(L);

    TrackSource(path);
    if (LoadFile(L, path) != 0) {
        Fail(lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (!ProtectedCall(0)) return false;
    Log("loaded " + Narrow(path));
    return true;
}

void ScriptHost::Unload() {
    if (L) {
        lua_close(L);
        L = nullptr;
    }
}

bool ScriptHost::ProtectedCall(int args) {
    const int base = lua_gettop(L) - args;
    lua_pushcfunction(L, Traceback);
    lua_insert(L, base);
    const int status = lua_pcall(L, args, 0, base);
    lua_remove(L, base);
    if (status != 0) {
        const char* message = lua_tostring(L, -1);
        Fail(message ? message : "(non-string error)");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool ScriptHost::CallGlobal(const char* function, const std::vector<Value>& args) {
    if (!Running()) return false;
    // Raw lookup: a global __index metamethod could raise outside lua_pcall.
    RawField(L, LUA_GLOBALSINDEX, function);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    for (const Value& value : args) PushValue(L, value);
    return ProtectedCall((int)args.size());
}

void ScriptHost::Queue(Event event) {
    // A stopped script must not accumulate events until it reloads.
    if (!Running() || events.size() >= 4096) return;
    events.push_back(std::move(event));
}

void ScriptHost::Tick(uint32_t frame, double ms) {
    if (!Running()) return;
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceCounter(&start);

    std::vector<Event> pending;
    pending.swap(events);
    for (const Event& event : pending) {
        if (!CallGlobal(("on_" + event.name).c_str(), event.args)) return;
    }
    if (!CallGlobal("tick", { Value(double(frame)), Value(ms) })) return;
    lua_gc(L, LUA_GCSTEP, 0);

    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    const double elapsed = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
    static ULONGLONG lastWarning = 0;
    if (elapsed > TickBudgetMs && GetTickCount64() - lastWarning > 5000) {
        lastWarning = GetTickCount64();
        char text[96];
        sprintf_s(text, "warning: frame took %.1f ms (budget %.1f ms)", elapsed, TickBudgetMs);
        Log(text);
    }
}

void ScriptHost::TrackSource(const std::wstring& file) {
    sources[file] = LastWriteTime(file);
}

bool ScriptHost::SourcesChanged() {
    for (const auto& [file, time] : sources) {
        if (LastWriteTime(file) != time) return true;
    }
    return false;
}

}
