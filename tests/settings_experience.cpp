// Build with assertions enabled; this exercises the same helpers used by gameplay.
#include "../headers/settings_data.h"
#include "../headers/experience_math.h"
#include <cassert>
#include <cstdio>
#include <iterator>

#ifdef NDEBUG
#error Build this self-check with assertions enabled.
#endif

static CharonSettings::Values Load(const char* text) {
    std::istringstream file(text);
    return CharonSettings::Load(file);
}

static std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main() {
    const auto defaults = Load("");
    for (const auto& definition : CharonSettings::Definitions)
        assert(defaults.at(definition.key) == definition.initial);
    assert(defaults.at("xpBonus") == 1);
    assert(defaults.at("PlayerCountOverride") == 1);
    assert(defaults.at("openGLFullscreen") == 1);

    auto values = Load("xpBonus: 3\nrevealLevel: 1\nxpBonus: nope\nshowItems:\n"
        "xpMin: -1\nTargetFPS: 4294967296\nsojSaleTime: 20junk\n"
        ": 20\nno colon\nfuture: 4294967295\nxpBonus:");
    assert(values.at("xpBonus") == 3); // Invalid and truncated duplicates cannot replace a good value.
    assert(values.at("revealLevel") == 1 && values.at("showItems") == 0);
    assert(values.at("xpMin") == 0 && values.at("TargetFPS") == 25);
    assert(values.at("sojSaleTime") == 150 && values.at("future") == MAXDWORD);
    assert(values.find("") == values.end());
    assert(Load("xpBonus:").at("xpBonus") == 1);
    assert(Load("xpBonus: 4").at("xpBonus") == 4);
    assert(Load(" xpBonus : 4 \r\n").at("xpBonus") == 4);

    values = Load("xpBonus: 0\nxpMin: 4294967295\nTargetFPS: 0\nsojSaleTime: 0\n"
        "PlayerCountOverride: 20\nrevealLevel: 9\ndebugModeType: 9\n");
    assert(values.at("xpBonus") == 1 && values.at("xpMin") == 8250);
    assert(values.at("TargetFPS") == 25 && values.at("sojSaleTime") == 150);
    assert(values.at("PlayerCountOverride") == 20); // Preserve the intentional /players uncap.
    assert(values.at("revealLevel") == 1 && values.at("debugModeType") == 2);
    assert(Load("sojSaleTime: 4294967295\n").at("sojSaleTime") == MAXLONG / 25);
    assert(Load("xpBonus: 4294967295\n").at("xpBonus") == 16384);

    assert(Load("openGLFilter: 2\n").at("openGLFilter") == static_cast<DWORD>(CharonGL::Filter::Xbrz));
    assert(Load("openGLFilter: 4\nopenGLFilterVersion: 1\n").at("openGLFilter") == static_cast<DWORD>(CharonGL::Filter::Crt));
    assert(Load("openGLFilter: 7\nopenGLFilterVersion: 2\n").at("openGLFilter") == static_cast<DWORD>(CharonGL::Filter::Lcd));

    const auto directory = std::filesystem::temp_directory_path() /
        (L"charon-settings-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    assert(std::filesystem::create_directory(directory));
    const auto path = directory / L"settings.txt";
    values = defaults;
    values["xpBonus"] = 4;
    values["future"] = MAXDWORD;
    assert(CharonSettings::Save(path, values));
    { std::ifstream file(path); assert(CharonSettings::Load(file) == values); }
    const auto original = Read(path);

    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    values["xpBonus"] = 8;
    assert(!CharonSettings::Save(path, values));
    assert(CloseHandle(locked));
    assert(Read(path) == original);
    assert(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()) == 1);
    assert(CharonSettings::Save(path, values));
    { std::ifstream file(path); assert(CharonSettings::Load(file) == values); }
    const auto updated = Read(path);
    values["bad\nkey"] = 1;
    assert(!CharonSettings::Save(path, values));
    assert(Read(path) == updated);
    assert(!CharonSettings::Save(directory / L"missing" / L"settings.txt", defaults));
    assert(std::filesystem::remove(path));
    assert(std::filesystem::remove(directory));

    using CharonExperience::Award;
    using CharonExperience::MaximumExperience;
    using CharonExperience::MaximumMercenaryAward;
    assert(Award(100, defaults.at("xpBonus"), defaults.at("xpMin"), 0, true) == 100);
    assert(Award(100, 3, 0, 1000, true) == 300);
    assert(Award(0, 3, 250, 1000, true) == 0);
    assert(Award(1, 3, 250, 1000, true) == 250);
    assert(Award(1000000, 16384, 0, 0, true) == MaximumExperience);
    assert(Award(MAXDWORD, MAXDWORD, 0, 0, true) == MaximumExperience);
    assert(Award(MaximumExperience, 1, 0, 0, true) == MaximumExperience);
    assert(Award(MaximumExperience - 1, 1, 0, 0, true) == MaximumExperience - 1);
    assert(Award(0, 1, 8250, 1000000, true) == MaximumExperience - 1000000);
    assert(Award(1, 1, 8000, 0, true) == 8000);
    assert(Award(1000, 3, 250, MaximumExperience - 100, true) == 100);
    assert(Award(MAXDWORD, MAXDWORD, 8250, MaximumExperience, true) == 0);
    assert(Award(MAXDWORD, MAXDWORD, 8250, MAXDWORD, true) == 0);
    assert(Award(MAXDWORD, MAXDWORD, 0, 0, false) == MaximumMercenaryAward);
    assert(Award(0, 1, 8250, 0, false) == MaximumMercenaryAward);
    assert(Award(MaximumMercenaryAward - 1, 1, 0, 0, false) == MaximumMercenaryAward - 1);
    assert(Award(MAXDWORD, 16384, 0, MaximumExperience - 1, false) == 1);
    assert(std::uint64_t(MaximumMercenaryAward) * 86 <= 0x7fffffffu);
    assert(std::uint64_t(MaximumMercenaryAward + 1) * 86 > 0x7fffffffu);
    std::puts("Settings and XP: defaults, validation, migration, atomic save, and award math passed.");
}
