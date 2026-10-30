#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "headers/script_host.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

namespace {

std::vector<std::string> logLines;
int liveGuards = 0;

struct Guard {
    Guard() { ++liveGuards; }
    ~Guard() { --liveGuards; }
};

int ThrowingBinding(lua_State* L) {
    Guard guard;
    const int value = Script::ArgInt(L, 1);
    if (value < 0) throw Script::Error("negative value");
    lua_pushinteger(L, value * 2);
    return 1;
}

void WriteFile(const std::wstring& path, const char* text) {
    FILE* fp = nullptr;
    assert(!_wfopen_s(&fp, path.c_str(), L"wb") && fp);
    fputs(text, fp);
    fclose(fp);
}

bool Logged(const char* fragment) {
    for (const std::string& line : logLines) {
        if (line.find(fragment) != std::string::npos) return true;
    }
    return false;
}

Script::ScriptHost MakeHost(const std::wstring& path) {
    return Script::ScriptHost("test", path, [](const std::string& line) {
        logLines.push_back(line);
    });
}

}

int main() {
    wchar_t temp[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, temp));
    const std::wstring root = std::wstring(temp) + L"charon_script_host_test";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + L"\\lib").c_str(), nullptr);
    CreateDirectoryW((root + L"\\game").c_str(), nullptr);

    WriteFile(root + L"\\lib\\counter.lua", "return { step = 1 }\n");
    WriteFile(root + L"\\game\\main.lua",
        "local counter = require 'lib.counter'\n"
        "ticks, seen = 0, {}\n"
        "function on_hello(a, b) seen[#seen + 1] = a .. ':' .. tostring(b) end\n"
        "function tick(frame, ms)\n"
        "  ticks = ticks + counter.step\n"
        "  if ticks == 2 then\n"
        "    assert(seen[1] == 'x:true' and seen[2] == 'y:3', 'events out of order')\n"
        "    assert(twice(21) == 42)\n"
        "    local ok, err = pcall(twice, -1)\n"
        "    assert(not ok and err:find('negative value'), tostring(err))\n"
        "    ok, err = pcall(twice, 'text')\n"
        "    assert(not ok and err:find('number expected'), tostring(err))\n"
        "    assert(not pcall(require, 'ffi'), 'ffi must be disabled by default')\n"
        "    store.set('handoff', 'Alice')\n"
        "    print('tick', frame)\n"
        "  end\n"
        "  if ticks == 3 then error('boom') end\n"
        "end\n");

    {
        Script::ScriptHost host = MakeHost(root + L"\\game\\main.lua");
        host.AddModule([](lua_State* L) {
            lua_register(L, "twice", Script::Bind<ThrowingBinding>);
        });
        assert(host.Load());
        host.Tick(1, 0);
        host.Queue({ "hello", { Script::Value(std::string("x")), Script::Value(true) } });
        host.Queue({ "hello", { Script::Value(std::string("y")), Script::Value(3.0) } });
        host.Tick(2, 16);
        assert(host.Running());
        assert(liveGuards == 0 && "C++ destructors must run before a Lua error unwinds");
        assert(Logged("[test] tick\t2"));
        assert(std::get<std::string>(Script::SharedStore()["handoff"]) == "Alice");

        host.Tick(3, 32);
        assert(host.Failed() && !host.Running());
        assert(Logged("boom") && Logged("stack traceback"));
        host.Tick(4, 48); // Stopped scripts must not run or throw.

        assert(!host.SourcesChanged());
        Sleep(20);
        WriteFile(root + L"\\lib\\counter.lua", "return { step = 10 }\n");
        assert(host.SourcesChanged() && "required modules are tracked for reload");
        assert(host.Load() && host.Running());
    }

    {
        WriteFile(root + L"\\game\\ffi.lua", "local ffi = require 'ffi'\nassert(ffi.sizeof('int') == 4)\n");
        Script::ScriptHost host = MakeHost(root + L"\\game\\ffi.lua");
        host.SetAllowFFI(true);
        assert(host.Load());
    }

    {
        WriteFile(root + L"\\game\\syntax.lua", "function tick(\n");
        Script::ScriptHost host = MakeHost(root + L"\\game\\syntax.lua");
        assert(!host.Load() && host.Failed());
    }

    std::puts("script host checks passed");
    return 0;
}
