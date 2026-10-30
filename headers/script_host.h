/**
 * LuaJIT script host: one Lua state per script, driven by per-frame ticks.
 *
 * Free of game dependencies so it can be tested standalone. Game APIs register
 * themselves through ScriptHost::AddModule before Load.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace Script {

// Thrown by bindings; converted to a Lua error after C++ destructors have run.
// LuaJIT on x86 Windows unwinds with longjmp, so bindings must never let a Lua
// error escape while C++ objects with destructors are alive.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

int RaiseCaught(lua_State* L, const char* message);

// The binding most recently entered, for crash reports (see Scripting.cpp).
inline void* LastBinding = nullptr;

template <int (*F)(lua_State*)> int Bind(lua_State* L) {
    char message[512] = { 0 };
    LastBinding = reinterpret_cast<void*>(F);
    try {
        return F(L);
    }
    catch (const std::exception& e) {
        strncpy_s(message, e.what(), _TRUNCATE);
    }
    catch (...) {
        strncpy_s(message, "unknown C++ exception", _TRUNCATE);
    }
    return RaiseCaught(L, message);
}

// Argument readers throw Script::Error instead of raising Lua errors.
double ArgNumber(lua_State* L, int index);
int ArgInt(lua_State* L, int index);
int OptInt(lua_State* L, int index, int fallback);
bool ArgBool(lua_State* L, int index);
bool OptBool(lua_State* L, int index, bool fallback);
std::string ArgString(lua_State* L, int index);
std::string OptString(lua_State* L, int index, const std::string& fallback);

// Pushes t[key] for the table at `index` without metamethods, which could
// raise a Lua error across C++ destructors.
void RawField(lua_State* L, int index, const char* key);

void SetField(lua_State* L, const char* key, double value);
void SetField(lua_State* L, const char* key, int value);
void SetField(lua_State* L, const char* key, unsigned int value);
void SetField(lua_State* L, const char* key, bool value);
void SetField(lua_State* L, const char* key, const char* value);
void SetField(lua_State* L, const char* key, const std::string& value);

using Value = std::variant<std::monostate, bool, double, std::string>;
void PushValue(lua_State* L, const Value& value);
Value ReadValue(lua_State* L, int index);

// Data shared by every script for the whole process (for example OOG -> game handoff).
std::map<std::string, Value>& SharedStore();

struct Event {
    std::string name;
    std::vector<Value> args;
};

using LogSink = std::function<void(const std::string& line)>;
using ModuleInstaller = std::function<void(lua_State* L)>;

class ScriptHost {
public:
    ScriptHost(std::string name, std::wstring path, LogSink log);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Installers run on every (re)load, after the standard libraries.
    void AddModule(ModuleInstaller installer);
    void SetAllowFFI(bool allow);

    bool Load();
    void Unload();
    bool Running() const { return L != nullptr && !failed; }
    bool Failed() const { return failed; }
    const std::string& Name() const { return name; }
    const std::wstring& Path() const { return path; }

    // Calls global `on_<name>` handlers for queued events, then `tick(frame, ms)`.
    void Tick(uint32_t frame, double ms);
    void Queue(Event event);
    // Calls a global function if present. Returns false when it raised an error.
    bool CallGlobal(const char* function, const std::vector<Value>& args = {});

    // True when any loaded script file changed since Load (polled for hot reload).
    bool SourcesChanged();
    void TrackSource(const std::wstring& file);

    void Log(const std::string& line) const;
    // Bytes the Lua state's allocator holds (live objects plus garbage not yet collected); 0 when unloaded.
    size_t HeapBytes() const;
    double TickBudgetMs = 8.0;

private:
    std::string name;
    std::wstring path;
    LogSink log;
    std::vector<ModuleInstaller> modules;
    std::vector<Event> events;
    std::map<std::wstring, uint64_t> sources;
    lua_State* L = nullptr;
    bool failed = false;
    bool allowFFI = false;

    bool ProtectedCall(int args);
    void Fail(const std::string& what);
};

// API modules register at static-init time; the Scripting feature installs them per context.
enum ContextFlags { OutOfGame = 1, InGame = 2, AnyContext = OutOfGame | InGame };
void RegisterModule(int contexts, ModuleInstaller installer);
std::vector<ModuleInstaller> ModulesFor(int context);

// Memory the bindings hold outside the Lua heaps (layout caches and the like),
// for `/lua mem` and charon.memory(). Reporters register at static-init time.
using MemoryReporter = std::function<size_t()>;
void RegisterMemoryUse(const std::string& name, MemoryReporter reporter);
std::vector<std::pair<std::string, size_t>> MemoryUse();

ScriptHost* HostFor(lua_State* L);
std::string Narrow(const std::wstring& text);
std::wstring Widen(const std::string& text);

}
