#include <headers/feature.h>
#include <headers/hook.h>
#include <headers/remote.h>

namespace RoomInit {

    ASMPTR SUNITINACTIVE_RoomInit_Original = 0x542b40, RoomInit_Rejoin = 0x542b46;

    __declspec(naked) void
    __fastcall RoomInit_Relocated(D2::Types::IncompleteGameData *pGame, D2::Types::Room1 *pRoom1) {
        __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x18
        jmp RoomInit_Rejoin
        }
    }

    void __fastcall RoomInit_Hook(D2::Types::IncompleteGameData *pGame, D2::Types::Room1 *pRoom1) {
        RoomInit_Relocated(pGame, pRoom1);

        for (Feature *f = Features; f; f = f->next) {
            f->roomInit(pGame, pRoom1);
        }
    }

    class : public Feature {
    public:
        void init() override {
            MemoryPatch(SUNITINACTIVE_RoomInit_Original) << JUMP(RoomInit_Hook);
        }
    } feature;
}