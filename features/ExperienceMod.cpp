#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/ghidra.h"
#include "headers/experience.h"

Ghidra::D2UnitStrc* pLastVictim;

REMOTEREF(DWORD, PlayerCountOverride, 0x883d70);
DWORD CALC_Experience = 0x57e3f0;
REMOTEFUNC(Ghidra::D2ClientStrc* __fastcall, SUNIT_GetClient, (Ghidra::D2UnitStrc* pUnit, const char* szFile, int nLine), 0x5531c0)
REMOTEFUNC(void __fastcall, SERVER_KillExperience, (Ghidra::D2GameStrc* pGame, Ghidra::D2UnitStrc* pAttacker, Ghidra::D2UnitStrc* pVictim), 0x57e990)

void __fastcall SERVER_KillExperience_intercept(Ghidra::D2GameStrc* pGame, Ghidra::D2UnitStrc* pAttacker, Ghidra::D2UnitStrc* pVictim) {
    pLastVictim = pVictim;
    return SERVER_KillExperience(pGame, pAttacker, pVictim);
}

void __fastcall SetPlayerCount(DWORD count) {
    PlayerCountOverride = count > 1 ? count : 1;
    gamelog << "Difficulty (players) set to " << count << " and saved." << std::endl;
    Settings["PlayerCountOverride"] = PlayerCountOverride;
    SaveSettings();
}

DWORD __stdcall ApplyExperienceBonus(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame) {
    if (Settings["xpMin"] <= 8000) {
        exp *= Settings["xpBonus"];

        if (exp && exp < Settings["xpMin"]) {
            exp = Settings["xpMin"];
        }
    }
    else {
        exp = 3520485254;
    }

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
            // Rewrite players count modification and leave to our handler - 42 bytes total
            MemoryPatch(0x47c4e4)
                << ASM::PUSHAD
                << ASM::MOV_ECX_EAX
                << CALL(SetPlayerCount)
                << ASM::POPAD
                << NOP_TO(0x47c50e);

            MemoryPatch(0x57e4fa) << CALL(ExperienceIntercept);

            // actual memory patches for feature
            MemoryPatch(0x5a4f12) << CALL(SERVER_KillExperience_intercept);

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
                PlayerCountOverride = Settings["PlayerCountOverride"];

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
