/**
 * Runs the Lua automation scripts given on the command line:
 *   -oogscript <file>   ticks out of game for the whole session
 *   -gamescript <file>  loads fresh for each game and ticks while in game
 *   -luaffi             lets scripts require LuaJIT's FFI (unchecked memory access)
 * In game, `/lua reload` and `/lua stop` control the game script and `/lua mem`
 * reports the Lua heaps, the bindings' caches and Game.exe's private bytes.
 * Ticks run at the game's 25 Hz logic rate. Relative paths resolve from Game.exe's
 * folder. Scripts reload when their files change.
 */
#include "headers/feature.h"
#include "headers/common.h"
#include "headers/remote.h"
#include "headers/script_game.h"
#include "headers/script_host.h"
#include <psapi.h>
#include <shellapi.h>
#include <share.h>
#include <cstdio>
#include <memory>

namespace Scripting {

namespace {

std::wstring oogPath, gamePath;
bool allowFFI = false, gameActive = false, gameStopped = false;
// Chat commands are applied from the guarded loop, never inside a Lua call.
enum class ChatRequest { None, Reload, Stop } chatRequest = ChatRequest::None;
std::unique_ptr<Script::ScriptHost> oogHost, gameHost;
uint32_t oogFrame = 0, gameFrame = 0;
double oogNextTick = 0, gameNextTick = 0;
// Script ticks follow the game's 25 Hz logic rate; the raw loops spin far faster.
const double TickIntervalMs = 40.0;
FILE* logFile = nullptr;
LARGE_INTEGER clockStart, clockFrequency;
const char* currentContext = "oog";
// Some menu handlers run nested message pumps that call the loops again.
bool inTick = false;

struct TickScope {
    TickScope() { inTick = true; }
    ~TickScope() { inTick = false; }
};

double NowMs() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - clockStart.QuadPart) * 1000.0 / double(clockFrequency.QuadPart);
}

std::wstring ResolvePath(const std::wstring& path) {
    std::wstring joined = path;
    const bool absolute = path.size() > 1 && (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\'));
    if (!absolute) {
        wchar_t exe[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (length && length < MAX_PATH) {
            std::wstring dir(exe, length);
            joined = dir.substr(0, dir.find_last_of(L"\\/") + 1) + path;
        }
    }
    wchar_t full[MAX_PATH];
    const DWORD length = GetFullPathNameW(joined.c_str(), MAX_PATH, full, nullptr);
    return length && length < MAX_PATH ? std::wstring(full, length) : joined;
}

void WriteLog(const std::string& line) {
    const bool error = line.find("] error: ") != std::string::npos;
    gamelog << COLOR(error ? 1 : 0) << Script::Widen(line) << std::endl;
    if (!logFile) return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    fprintf(logFile, "%02d:%02d:%02d.%03d [%lu] %s\n", time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, GetCurrentProcessId(), line.c_str());
    fflush(logFile);
}

// Logs hardware faults with enough context to find them: faulting address as
// module+offset, data address, and the last script binding entered (resolve
// Charon offsets with Charon.map). Only logs; the game's handlers still run.
LONG CALLBACK CrashTracer(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const bool fault = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION
        || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW
        || code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        || code == 0xC0000409;
    static LONG reports = 0;
    if (!fault || !logFile || InterlockedIncrement(&reports) > 8) return EXCEPTION_CONTINUE_SEARCH;
    auto describe = [](void* address, char* out, size_t size) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(address), &module) && module) {
            GetModuleFileNameA(module, name, MAX_PATH);
            const char* base = strrchr(name, '\\');
            sprintf_s(out, size, "%s+0x%X", base ? base + 1 : name, unsigned(uintptr_t(address) - uintptr_t(module)));
        }
        else sprintf_s(out, size, "0x%p", address);
    };
    char where[MAX_PATH + 32], binding[MAX_PATH + 32];
    describe(info->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    if (Script::LastBinding) describe(Script::LastBinding, binding, sizeof(binding));
    else strcpy_s(binding, "none");
    const ULONG_PTR* params = info->ExceptionRecord->ExceptionInformation;
    fprintf(logFile, "[%lu] fault 0x%08lX at %s (Game.exe VA 0x%08lX), %s 0x%08lX; in tick: %s; last binding %s\n",
        GetCurrentProcessId(), code, where, (unsigned long)info->ContextRecord->Eip,
        info->ExceptionRecord->NumberParameters > 1 ? (params[0] ? "writing" : "reading") : "data",
        info->ExceptionRecord->NumberParameters > 1 ? (unsigned long)params[1] : 0ul,
        inTick ? currentContext : "no", binding);
    // Thread, registers and code addresses found on the stack (likely return
    // addresses): enough to tell the game's worker threads apart and to see
    // who called into the faulting function.
    const CONTEXT* c = info->ContextRecord;
    fprintf(logFile, "[%lu]   thread %lu, eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX\n",
        GetCurrentProcessId(), GetCurrentThreadId(), c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(c->Esp);
    const uintptr_t* top = reinterpret_cast<const uintptr_t*>(tib->StackBase);
    int found = 0;
    for (int i = 0; i < 512 && slot + i < top && found < 12; ++i) {
        const uintptr_t value = slot[i];
        HMODULE module = nullptr;
        if (value > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(value), &module) && module) {
            char frame[MAX_PATH + 32];
            describe(reinterpret_cast<void*>(value), frame, sizeof(frame));
            fprintf(logFile, "[%lu]   stack +0x%03X: %s\n", GetCurrentProcessId(), unsigned(i * 4), frame);
            ++found;
        }
    }
    fflush(logFile);
    return EXCEPTION_CONTINUE_SEARCH;
}

std::unique_ptr<Script::ScriptHost> CreateHost(const char* name, const std::wstring& path, int context) {
    auto host = std::make_unique<Script::ScriptHost>(name, path, WriteLog);
    host->SetAllowFFI(allowFFI);
    for (const Script::ModuleInstaller& installer : Script::ModulesFor(context)) host->AddModule(installer);
    return host;
}

bool TickDue(double& next) {
    const double now = NowMs();
    if (now < next) return false;
    // After a stall, resume the cadence instead of replaying missed ticks.
    next = now - next > TickIntervalMs ? now + TickIntervalMs : next + TickIntervalMs;
    return true;
}

// Reloads a changed script about twice a second.
void ReloadIfChanged(Script::ScriptHost& host, uint32_t frame) {
    if (frame % 12 == 0 && host.SourcesChanged()) {
        host.Log("source changed, reloading");
        host.Load();
    }
}

int CharonContext(lua_State* L) {
    lua_pushstring(L, currentContext);
    return 1;
}

int CharonFrame(lua_State* L) {
    lua_pushnumber(L, strcmp(currentContext, "game") == 0 ? gameFrame : oogFrame);
    return 1;
}

int CharonNow(lua_State* L) {
    lua_pushnumber(L, NowMs());
    return 1;
}

// Game.exe's private bytes: everything it has committed for itself, game and Charon alike.
size_t ProcessBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return 0;
    return counters.PrivateUsage;
}

// charon.memory() -> { oog, game (Lua heap bytes, absent without that script), map, ..., process }
int CharonMemory(lua_State* L) {
    lua_newtable(L);
    if (oogHost && oogHost->HeapBytes()) Script::SetField(L, "oog", double(oogHost->HeapBytes()));
    if (gameHost && gameHost->HeapBytes()) Script::SetField(L, "game", double(gameHost->HeapBytes()));
    for (const auto& [name, bytes] : Script::MemoryUse()) Script::SetField(L, name.c_str(), double(bytes));
    Script::SetField(L, "process", double(ProcessBytes()));
    return 1;
}

std::string Megabytes(size_t bytes) {
    char text[32];
    sprintf_s(text, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return text;
}

void ReportMemory() {
    std::string line = "memory: Lua";
    line += " oog " + (oogHost && oogHost->HeapBytes() ? Megabytes(oogHost->HeapBytes()) : std::string("-"));
    line += ", game " + (gameHost && gameHost->HeapBytes() ? Megabytes(gameHost->HeapBytes()) : std::string("-"));
    for (const auto& [name, bytes] : Script::MemoryUse()) line += "; " + name + " " + Megabytes(bytes);
    line += "; Game.exe private " + Megabytes(ProcessBytes());
    WriteLog(line);
}

int CharonLog(lua_State* L) {
    const std::string text = Script::ArgString(L, 1);
    if (Script::ScriptHost* host = Script::HostFor(L)) host->Log(text);
    return 0;
}

const bool registered = (Script::RegisterModule(Script::AnyContext, [](lua_State* L) {
    static const luaL_Reg functions[] = {
        { "context", Script::Bind<CharonContext> },
        { "frame", Script::Bind<CharonFrame> },
        { "now", Script::Bind<CharonNow> },
        { "log", Script::Bind<CharonLog> },
        { "memory", Script::Bind<CharonMemory> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    Script::SetField(L, "version", Script::Narrow(version));
    lua_setglobal(L, "charon");
}), true);

}

class : public Feature {
public:
    void init() {
        QueryPerformanceCounter(&clockStart);
        QueryPerformanceFrequency(&clockFrequency);

        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int c = 0; argv && c < argc; c++) {
            if (!_wcsicmp(argv[c], L"-oogscript") && c + 1 < argc) oogPath = ResolvePath(argv[++c]);
            else if (!_wcsicmp(argv[c], L"-gamescript") && c + 1 < argc) gamePath = ResolvePath(argv[++c]);
            else if (!_wcsicmp(argv[c], L"-luaffi")) allowFFI = true;
        }
        LocalFree(argv);

        if (oogPath.empty() && gamePath.empty()) return;
        const std::wstring logPath = std::wstring(saveDir) + L"\\lua.log";
        logFile = _wfsopen(logPath.c_str(), L"a", _SH_DENYNO);
        AddVectoredExceptionHandler(1, CrashTracer);
        if (!oogPath.empty()) oogHost = CreateHost("oog", oogPath, Script::OutOfGame);
        if (!gamePath.empty()) gameHost = CreateHost("game", gamePath, Script::InGame);
    }

    void oogLoop() {
        if (inTick) return;
        TickScope scope;
        currentContext = "oog";
        if (gameActive) {
            gameActive = false;
            gameHost->Log("game ended");
            gameHost->Unload();
        }
        if (!oogHost || !TickDue(oogNextTick)) return;
        if (!oogFrame++) oogHost->Load();
        else ReloadIfChanged(*oogHost, oogFrame);
        oogHost->Tick(oogFrame, NowMs());
    }

    void gameLoop() {
        if (inTick || !gameHost || !TickDue(gameNextTick)) return;
        // The message loop also runs while a game loads and shuts down: tick only
        // once the player stands in a level, and stop once a leave is under way.
        if (!Game::Player() || !Game::UnitLevelId(Game::Player()) || Game::LeavingGame()) return;
        TickScope scope;
        currentContext = "game";
        if (chatRequest == ChatRequest::Stop) {
            gameStopped = true;
            gameHost->Unload();
            gameHost->Log("stopped; '/lua reload' starts it again");
        }
        else if (chatRequest == ChatRequest::Reload) {
            gameStopped = false;
            gameHost->Load();
        }
        chatRequest = ChatRequest::None;
        if (!gameActive) {
            gameActive = true;
            gameStopped = false;
            gameFrame = 0;
            gameHost->Load();
        }
        else if (!gameStopped) ReloadIfChanged(*gameHost, gameFrame);
        gameHost->Tick(++gameFrame, NowMs());
    }

    bool chatInput(InputStream msg) {
        std::wstring command, action;
        msg >> command >> action;
        if (command != L"lua") return true;
        if (action == L"mem") {
            ReportMemory();
        }
        else if (!gameHost) {
            gamelog << COLOR(1) << L"No -gamescript given." << std::endl;
        }
        else if (action == L"reload") {
            chatRequest = ChatRequest::Reload;
        }
        else if (action == L"stop") {
            chatRequest = ChatRequest::Stop;
        }
        else {
            gamelog << COLOR(4) << L"Usage: /lua reload | /lua stop | /lua mem" << std::endl;
        }
        return false;
    }
} feature;

}
