#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/ghidra.h"
#include "headers/experience.h"
#include "headers/experience_math.h"
#include "headers/player_count.h"
#include "headers/player_count_patch.h"

namespace {
    CharonPlayers::State configuredPlayers;
}

CharonPlayers::State CharonPlayers::Configured() { return configuredPlayers; }

REMOTEREF(DWORD, PlayerCountOverride, 0x883d70);
DWORD CALC_Experience = 0x57e3f0;
REMOTEFUNC(Ghidra::D2ClientStrc* __fastcall, SUNIT_GetClient, (Ghidra::D2UnitStrc* pUnit, const char* szFile, int nLine), 0x5531c0)

void __fastcall SetPlayerCount(DWORD count) {
    const DWORD configured = count > 1 ? count : 1;
    PlayerCountOverride = configured;
    configuredPlayers.Accept(configured);
    gamelog << "Difficulty (players) set to " << count << " and saved." << std::endl;
    Settings["PlayerCountOverride"] = configured;
    SaveSettings();
}

DWORD __stdcall ApplyExperienceBonus(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame) {
    const DWORD currentExperience = D2::STATSEX_GetCurrentStat(reinterpret_cast<D2::Types::UnitAny*>(pUnit), 13 /* experience */, 0);
    exp = CharonExperience::Award(exp, Settings.at("xpBonus"), Settings.at("xpMin"), currentExperience,
        pUnit->eUnitType == Ghidra::UNIT_PLAYER);

    for (Feature* f = Features; f; f = f->next) {
        f->serverExpAward(exp, pUnit, pGame);
    }

    return exp;
}

namespace ExperienceMod {

    class : public Feature {
        bool ingame = false;
    public:
        void init() {
            configuredPlayers.ApplySaved(Settings["PlayerCountOverride"]);
            // Rewrite players count modification and leave to our handler - 42 bytes total
            CharonPlayers::InstallCommandPatch(0x47c4e4, SetPlayerCount);

            // Scale the engine-calculated award through the original unsigned XP ABI.
            MemoryPatch(0x57e4fa) << CALL(ExperienceIntercept);

            AutomapInfoHooks.push_back([]() -> std::wstring {
                wchar_t ret[256] = L"Players 1";

                if (PlayerCountOverride > 1) {
                    swprintf_s(ret, L"Players %d", PlayerCountOverride);
                }

                return ret;
            });
        }

        void serverExpAward(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame) {
            if (pUnit->eUnitType == Ghidra::UNIT_PLAYER && Settings["infoPopups"]) {
                Ghidra::D2ClientStrc* pClient = SUNIT_GetClient(pUnit, __FILE__, __LINE__);

                if (pClient != nullptr) {
                    FlyingTextPacket packet;
                    D2::Types::UnitAny* unit = (D2::Types::UnitAny*)pUnit;

                    packet.pos = unit->pos();
                    packet.color = 2;
                    packet.value = static_cast<int>(exp);

                    ServerSendCustomData(pClient, packet);
                }
            }
        }

        void gameLoop() {
            if (!ingame || PlayerCountOverride != Settings["PlayerCountOverride"]) {
                const DWORD configured = Settings["PlayerCountOverride"];
                PlayerCountOverride = configured;
                configuredPlayers.ApplySaved(configured);

                if (PlayerCountOverride > 1) {
                    gamelog << "Difficulty (players) set to " << PlayerCountOverride << "." << std::endl;
                }
            }

            ingame = true;
        }

        void oogLoop() {
            ingame = false;
        }
    } feature;

}
