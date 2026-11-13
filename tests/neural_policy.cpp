#include "headers/neural_policy.h"
#include "headers/script_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
extern "C" {
#include "lualib.h"
}

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

namespace {

int Forged(lua_State* L) {
    // Same-sized foreign userdata, even carrying the authentic metatable.
    lua_getglobal(L, "model");
    const size_t bytes = lua_objlen(L, -1);
    const void* source = lua_touserdata(L, -1);
    void* destination = lua_newuserdata(L, bytes);
    std::memcpy(destination, source, bytes);
    lua_getmetatable(L, -2);
    lua_setmetatable(L, -2);
    return 1;
}

void RunScript(const char* path) {
    lua_State* L = luaL_newstate();
    assert(L);
    luaL_openlibs(L);
    for (const auto& install : Script::ModulesFor(Script::InGame)) install(L);
    const int status = luaL_dofile(L, path);
    if (status) fprintf(stderr, "%s: %s\n", path, lua_tostring(L, -1));
    assert(status == 0);
    lua_close(L);
}

}

int main() {
    NeuralPolicy::Model model = {};
    model.inputs = 24;
    model.hidden = 32;
    NeuralPolicy::Features features = {};
    model.w1[0] = 2;
    model.w1[1] = -1;
    model.b1[0] = -0.5;
    model.w2[0] = 3;
    model.b2 = 4;
    features[0] = 1;
    features[1] = 0.5;
    assert(NeuralPolicy::Evaluate(model, features.data()) == 7);
    features[0] = 0;
    assert(NeuralPolicy::Evaluate(model, features.data()) == 4);
    model.w1[model.inputs + 23] = 2;
    model.w2[1] = -2;
    features[23] = 0.5;
    assert(NeuralPolicy::Evaluate(model, features.data()) == 2);

    lua_State* L = luaL_newstate();
    assert(L);
    luaL_openlibs(L);
    assert(Script::ModulesFor(Script::OutOfGame).empty());
    for (const auto& install : Script::ModulesFor(Script::InGame)) install(L);
    lua_register(L, "forged", Forged);
    const char* test = R"lua(
        local function array(n, value)
            local result = {}
            for i = 1, n do result[i] = value or 0 end
            return result
        end
        local function weights()
            return {w1 = array(768), b1 = array(32), w2 = array(32), b2 = 4}
        end
        local function rejects(fn) assert(not pcall(fn)) end
        local w = weights()
        w.w1[1], w.w1[2], w.b1[1], w.w2[1] = 2, -1, -0.5, 3
        local inputs, hidden
        model, inputs, hidden = policy.compile(w)
        assert(type(model) == 'userdata' and inputs == 24 and hidden == 32)
        -- The shape comes from the tensors; a small model ranks rows of its own width.
        local small, small_inputs, small_hidden = policy.compile{
            features = {'x', 'y'}, w1 = {1, 0, 0, 1, 1, 1}, b1 = {0, 0, -1}, w2 = {1, 2, 10}, b2 = 0.5}
        assert(small_inputs == 2 and small_hidden == 3)
        local pick, sized = policy.rank(small, {{1, 0}, {0, 1}, {1, 1}})
        assert(pick == 3 and sized[1] == 1.5 and sized[2] == 2.5 and sized[3] == 13.5)
        rejects(function() policy.rank(small, {array(24)}) end)
        rejects(function() policy.rank(small, {{1}}) end)
        rejects(function() policy.compile{features = {'x'}, w1 = {1, 0}, b1 = {0}, w2 = {1}, b2 = 0} end)
        rejects(function() policy.compile{w1 = {1, 0, 1}, b1 = {0, 0}, w2 = {1, 1}, b2 = 0} end)
        rejects(function() policy.compile{w1 = {}, b1 = {}, w2 = {}, b2 = 0} end)
        rejects(function() policy.compile{w1 = array(65), b1 = {0}, w2 = {1}, b2 = 0} end)
        rejects(function() policy.compile{w1 = array(65), b1 = array(65), w2 = array(65), b2 = 0} end)
        local a, b = array(24), array(24)
        a[1], a[2] = 1, 0.5
        b[1] = 0.25
        local best, scores = policy.rank(model, {b, a, a})
        assert(best == 2 and #scores == 3 and scores[1] == 4 and scores[2] == 7 and scores[3] == 7)
        assert(policy.rank(model, {a, a}) == 1)
        w.w1[1], w.b1[1], w.w2[1], w.b2 = 0, 0, 0, 0
        local _, unchanged = policy.rank(model, {a})
        assert(unchanged[1] == 7)
        rejects(function() model.b2 = 0 end)
        rejects(function() setmetatable(model, {}) end)
        rejects(function() policy.rank({}, {a}) end)
        rejects(function() policy.rank(newproxy(true), {a}) end)
        local fake = newproxy(true)
        debug.setmetatable(fake, debug.getmetatable(model))
        rejects(function() policy.rank(fake, {a}) end)
        rejects(function() policy.rank(forged(), {a}) end)
        rejects(function() policy.rank(model, {}) end)
        local rows = {}
        for i = 1, 64 do rows[i] = a end
        local _, many = policy.rank(model, rows)
        assert(#many == 64)
        rows[65] = a
        rejects(function() policy.rank(model, rows) end)
        for _, key in ipairs({'w1', 'b1', 'w2'}) do
            local short = weights()
            table.remove(short[key])
            rejects(function() policy.compile(short) end)
            local long = weights()
            long[key][#long[key] + 1] = 0
            rejects(function() policy.compile(long) end)
            for _, bad in ipairs({'1', true, 0/0, math.huge, -math.huge, 1000.01, -1000.01}) do
                local invalid = weights()
                invalid[key][1] = bad
                rejects(function() policy.compile(invalid) end)
            end
        end
        for _, bad in ipairs({'1', true, 0/0, math.huge, -math.huge, 1000.01, -1000.01}) do
            local invalid = weights()
            invalid.b2 = bad
            rejects(function() policy.compile(invalid) end)
        end
        for _, bad in ipairs({'1', true, 0/0, math.huge, -math.huge, -0.01, 1.01}) do
            local row = array(24)
            row[1] = bad
            rejects(function() policy.rank(model, {row}) end)
        end
        rejects(function() policy.rank(model, {array(23)}) end)
        rejects(function() policy.rank(model, {array(25)}) end)
        rejects(function() policy.rank(model, {[2] = a}) end)
        rejects(function() policy.rank(model, {a, extra = a}) end)
        local hole = array(24)
        hole[10] = nil
        rejects(function() policy.rank(model, {hole}) end)
        local trap = setmetatable({}, {__index = function() error('metamethod called') end})
        rejects(function() policy.compile(trap) end)
        local invalid = weights()
        invalid.w1 = setmetatable({}, {__index = function() error('metamethod called') end})
        rejects(function() policy.compile(invalid) end)
        local limits = weights()
        limits.w1[1], limits.b1[1], limits.w2[1], limits.b2 = 1000, -1000, -1000, -1000
        local _, finite = policy.rank(policy.compile(limits), {array(24, 1)})
        assert(finite[1] == -1000)
        -- A ReLU gate changes slope at x = 0.5, unlike an affine scorer.
        local gate = weights()
        gate.b2, gate.w1[1], gate.b1[1], gate.w2[1] = 0, 1, -0.5, 2
        local x, y, z = array(24), array(24), array(24)
        x[1], y[1], z[1] = 0.25, 0.5, 0.75
        local _, gated = policy.rank(policy.compile(gate), {x, y, z})
        assert(gated[1] == 0 and gated[2] == 0 and gated[3] == 0.5)
    )lua";
    const int status = luaL_dostring(L, test);
    if (status) fprintf(stderr, "%s\n", lua_tostring(L, -1));
    assert(status == 0);
    const char* benchmark = R"lua(
        local function array(n)
            local result = {}
            for i = 1, n do result[i] = 1 end
            return result
        end
        local model = policy.compile{w1 = array(768), b1 = array(32), w2 = array(32), b2 = 1}
        local rows = {}
        for i = 1, 64 do rows[i] = array(24) end
        function benchmark_rank() policy.rank(model, rows) end
    )lua";
    assert(luaL_dostring(L, benchmark) == 0);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 256; ++i) {
        lua_getglobal(L, "benchmark_rank");
        assert(lua_pcall(L, 0, 0, 0) == 0);
    }
    const double microseconds = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - start).count() / 256;
    printf("policy.rank 64 candidates: %.1f us average (256 calls)\n", microseconds);
    lua_close(L);
    RunScript(CHARON_SOURCE_DIR "/tests/script_combat_weights.lua");
    RunScript(CHARON_SOURCE_DIR "/tests/script_neural_combat.lua");
    RunScript(CHARON_SOURCE_DIR "/tests/script_strategy.lua");
    puts("neural policy native and Lua binding checks passed");
}
