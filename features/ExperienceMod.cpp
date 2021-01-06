#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/ghidra.h"
#include "headers/CustomPacket.h"

Ghidra::D2UnitStrc* pLastVictim;

REMOTEREF(DWORD, PlayerCountOverride, 0x883d70);
REMOTEFUNC(DWORD __fastcall, CALC_Experience, (Ghidra::D2UnitStrc* pUnit, int dummy, Ghidra::D2GameStrc* pGame, unsigned int param_3), 0x57e3f0)
REMOTEFUNC(Ghidra::D2ClientStrc* __stdcall, PLAYER_GetClientFromUnitData, (Ghidra::D2UnitStrc* pUnit), 0x5531c0)
REMOTEFUNC(void __fastcall, SERVER_KillExperience, (Ghidra::D2GameStrc* pGame, Ghidra::D2UnitStrc* pAttacker, Ghidra::D2UnitStrc* pVictim), 0x57e990)
REMOTEFUNC(unsigned int __stdcall, TXT_Experience_GetLineLevel, (int eClassId, int nLevel), 0x611800)
REMOTEFUNC(int __stdcall, UNIT_GetUnitStat, (Ghidra::D2UnitStrc* pUnit, int eUnitStat, short nLayer), 0x625480)

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

DWORD __fastcall ExperienceIntercept(Ghidra::D2UnitStrc* pUnit, int dummy, Ghidra::D2GameStrc* pGame, unsigned int param_3) {
    DWORD exp = CALC_Experience(pUnit, dummy, pGame, param_3);

    if (Settings["xpMin"] <= 8000) {
        exp *= Settings["xpBonus"];

        if (exp && exp < Settings["xpMin"]) {
            exp = Settings["xpMin"];
        }
    }
    else {
        unsigned int maxExp = TXT_Experience_GetLineLevel(pUnit->eUnitType, 98);
        unsigned int curExp = UNIT_GetUnitStat(pUnit, 0x0D, 0);
        exp = maxExp - curExp;
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
            Ghidra::D2ClientStrc* pClient = PLAYER_GetClientFromUnitData(pUnit);

            FlyingTextPacket packet{ 0x3D, static_cast<unsigned char>(pLastVictim->eUnitType), (DWORD)pLastVictim->nUnitGUID, 2, static_cast<int>(exp) };
            FlyingTextPacketHandler->sendPacket(pClient, &packet);
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
