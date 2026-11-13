#include "headers/neural_policy.h"
#include "headers/script_host.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

const char* ModelMetatable = "charon.policy.model";

struct CompiledModel {
    uintptr_t identity;
    NeuralPolicy::Model model;
};

// Count keys as well as values: Lua's length is ambiguous for sparse arrays.
int DenseLength(lua_State* L, int index, int maximum) {
    if (lua_type(L, index) != LUA_TTABLE) throw Script::Error("policy: expected a dense array");
    if (index < 0) index = lua_gettop(L) + index + 1;
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) != LUA_TNUMBER) throw Script::Error("policy: array keys must be integers");
        const double key = lua_tonumber(L, -2);
        if (!std::isfinite(key) || key < 1 || key > maximum || key != std::floor(key))
            throw Script::Error("policy: array key out of range");
        if (++count > maximum) throw Script::Error("policy: array too long");
        lua_pop(L, 1);
    }
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(L, index, i);
        const bool missing = lua_isnil(L, -1);
        lua_pop(L, 1);
        if (missing) throw Script::Error("policy: array has a hole");
    }
    return count;
}

double Number(lua_State* L, int index, double minimum, double maximum) {
    if (lua_type(L, index) != LUA_TNUMBER) throw Script::Error("policy: expected a number");
    const double value = lua_tonumber(L, index);
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw Script::Error("policy: number out of range or nonfinite");
    return value;
}

// Reads a dense array of 1..maximum numbers and returns its length.
size_t ReadNumbers(lua_State* L, int index, double* values, size_t maximum, double minimum, double limit) {
    const int count = DenseLength(L, index, int(maximum));
    if (index < 0) index = lua_gettop(L) + index + 1;
    for (int i = 0; i < count; ++i) {
        lua_rawgeti(L, index, i + 1);
        values[i] = Number(L, -1, minimum, limit);
        lua_pop(L, 1);
    }
    return size_t(count);
}

// The shape comes from the tensors: hidden = #b1 = #w2, inputs = #w1 / hidden.
int Compile(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) throw Script::Error("policy.compile: expected weights table");
    // Build in place; the metatable that makes it a model is attached last.
    void* memory = lua_newuserdata(L, sizeof(CompiledModel));
    CompiledModel* compiled = new (memory) CompiledModel{};
    NeuralPolicy::Model& model = compiled->model;
    const double limit = NeuralPolicy::MaxWeight;
    Script::RawField(L, 1, "b1");
    model.hidden = ReadNumbers(L, -1, model.b1.data(), NeuralPolicy::MaxHidden, -limit, limit);
    lua_pop(L, 1);
    if (!model.hidden) throw Script::Error("policy.compile: b1 is empty");
    Script::RawField(L, 1, "w2");
    if (ReadNumbers(L, -1, model.w2.data(), NeuralPolicy::MaxHidden, -limit, limit) != model.hidden)
        throw Script::Error("policy.compile: w2 must have one weight per hidden unit");
    lua_pop(L, 1);
    Script::RawField(L, 1, "w1");
    const size_t weights = ReadNumbers(L, -1, model.w1.data(), model.w1.size(), -limit, limit);
    lua_pop(L, 1);
    model.inputs = weights / model.hidden;
    if (!weights || weights % model.hidden || model.inputs > NeuralPolicy::MaxInputs)
        throw Script::Error("policy.compile: w1 must hold hidden x inputs weights");
    Script::RawField(L, 1, "b2");
    model.b2 = Number(L, -1, -limit, limit);
    lua_pop(L, 1);
    // An artifact that names its inputs must name exactly as many as it has.
    Script::RawField(L, 1, "features");
    if (!lua_isnil(L, -1) && size_t(DenseLength(L, -1, int(NeuralPolicy::MaxInputs))) != model.inputs)
        throw Script::Error("policy.compile: features must name every input");
    lua_pop(L, 1);
    compiled->identity = reinterpret_cast<uintptr_t>(memory);
    luaL_getmetatable(L, ModelMetatable);
    lua_setmetatable(L, -2);
    lua_pushinteger(L, lua_Integer(model.inputs));
    lua_pushinteger(L, lua_Integer(model.hidden));
    return 3;
}

const NeuralPolicy::Model& RequireModel(lua_State* L) {
    if (lua_type(L, 1) != LUA_TUSERDATA || lua_objlen(L, 1) != sizeof(CompiledModel))
        throw Script::Error("policy.rank: expected compiled model");
    if (!lua_getmetatable(L, 1)) throw Script::Error("policy.rank: expected compiled model");
    luaL_getmetatable(L, ModelMetatable);
    const bool matches = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    void* memory = lua_touserdata(L, 1);
    uintptr_t identity = 0;
    std::memcpy(&identity, memory, sizeof(identity));
    if (!matches || identity != reinterpret_cast<uintptr_t>(memory))
        throw Script::Error("policy.rank: invalid compiled model");
    return static_cast<const CompiledModel*>(memory)->model;
}

int Rank(lua_State* L) {
    const NeuralPolicy::Model& model = RequireModel(L);
    const int count = DenseLength(L, 2, int(NeuralPolicy::MaxCandidates));
    if (!count) throw Script::Error("policy.rank: expected at least one candidate");
    std::array<double, NeuralPolicy::MaxCandidates> scores = {};
    int best = 0;
    for (int i = 0; i < count; ++i) {
        NeuralPolicy::Features features = {};
        lua_rawgeti(L, 2, i + 1);
        if (ReadNumbers(L, -1, features.data(), features.size(), 0.0, 1.0) != model.inputs)
            throw Script::Error("policy.rank: each candidate needs one value per model input");
        lua_pop(L, 1);
        scores[i] = NeuralPolicy::Evaluate(model, features.data());
        if (scores[i] > scores[best]) best = i;
    }
    lua_pushinteger(L, best + 1);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        lua_pushnumber(L, scores[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

const bool registered = (Script::RegisterModule(Script::InGame, [](lua_State* L) {
    luaL_newmetatable(L, ModelMetatable);
    lua_pushliteral(L, "immutable policy model");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    static const luaL_Reg functions[] = {
        { "compile", Script::Bind<Compile> },
        { "rank", Script::Bind<Rank> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "policy");
}), true);

}
