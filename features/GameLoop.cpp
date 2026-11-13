/**
 * Enables throttle and game loop hooks.
 */

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/frame_wait.h"
#include <iostream>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

void _postInit() {
    for (Feature* f = Features; f; f = f->next) {
        f->postInit();
    }
}

ASMPTR _init_rejoin = 0x405e36, SOUND_Initialize = 0x514530;

__declspec(naked) void _postInitHook() {
    __asm {
        cmp byte ptr[esi + 0x220], 0x0
        mov eax, [0x74c704]
        jnz skip
        cmp eax, 2
        jz skip
        movzx edx, byte ptr[esi + 0x224]
        mov ecx, dword ptr[esi]
        call SOUND_Initialize
        mov eax, [0x74c704]
        mov dword ptr[ebp + -0x4], 0x1
        skip:
        pushad
        call _postInit
        popad
        jmp _init_rejoin
    }
}


void _gameLoop() {
    for (Feature* f = Features; f; f = f->next) {
        f->gameLoop();
    }
}

// The single player frame gate in SrvProcessAllGames (0x52fc20): a server
// frame runs once (timeGetTime() & 0x7fffffff) - LastUpdateTick reaches
// Frames_TimeBetweenFrames (signed compare at 0x52fc62).
// Frames_TimeBetweenFrames: milliseconds per frame, written by
// GAME_SetFrameRate (0x52df94) from the "Target Single Player FPS" setting.
// LastUpdateTick: read at 0x52fc3e, written at 0x52fc83 when a frame runs.
REMOTEREF(int, Frames_TimeBetweenFrames, 0x883d60);
REMOTEREF(int, LastUpdateTick, 0x883d58);

// Replaces the game's Sleep(10) after every idle pass of MessageGameLoop
// (0x451c42). The game does not raise the timer resolution, so that sleep took
// about 16 ms: with the server stepping in this same loop (CLIENT_GameLoopFrame,
// 0x44efa0), single player stayed under some 45 frames a second whatever the
// target, and frames at the stock rate ran up to 16 ms late. In single player
// the wait now ends when the gate opens (headers/frame_wait.h).
void _idle() {
    static int seen = 0;
    const int type = D2::eD2GSGameType_007a0610; // 0 single player, 1 the same uncapped (0x44efb7)
    const int last = LastUpdateTick;
    const unsigned wait = FrameWait::Milliseconds(type == 0 || type == 1, Frames_TimeBetweenFrames, last,
        int(timeGetTime() & 0x7fffffff), last != seen);
    seen = last;
    if (wait) Sleep(wait);
}

// Stands in for GetTickCount in the client's sender (headers/frame_wait.h).
DWORD __stdcall _senderClock() {
    static FrameWait::SenderClock clock;
    const int type = D2::eD2GSGameType_007a0610;
    return clock.Read(GetTickCount(), type == 0 || type == 1, Frames_TimeBetweenFrames);
}

void _oogLoop() {
    // Out of game logic goes here.
    for (Feature* f = Features; f; f = f->next) {
        f->oogLoop();
    }
}

void __stdcall _serverLoop(D2::Types::IncompleteGameData* pGame) {
    // to test it ups by one every loop, every tick this function is called for ever hosted game the client does
    //int gameFrame = pGame->unk5[46];

    for (Feature* f = Features; f; f = f->next) {
        f->gameServerLoop(pGame);
    }
}

REMOTEFUNC(int, FreeUnusedItems,(void), 0x52d310)
__declspec(naked) void _serverLoopIntercept() {
    __asm {
        // safety, we dont know what the function/gameloops messup in terms of ptrs
        pushad

        push edi // pGame
        call _serverLoop

        popad
        // since we jump to this intercept as a call, the return value is set
        JMP FreeUnusedItems
    }
}


// This feature class registers itself.
class : public Feature {
public:
    void init() {
        MemoryPatch(0x405e09) << JUMP(_postInitHook);
        // The game imports timeGetTime but never timeBeginPeriod: without this
        // a sleep ends on a 15.6 ms boundary and _idle cannot hit a frame.
        timeBeginPeriod(1);
        // Patch MessageGameLoop @ 0x451C2A:
        //   call _gameLoop                       (5 bytes)
        //   call _idle                           (5 bytes)
        //   NOP out the other 22 bytes: the conditional (skip-Sleep-when-mouse-evt-or-host)
        //   and the original `push 0xA; call ds:[Sleep]` at 0x451C42
        // so the sleep after the feature callbacks is _idle's to give or withhold.
        // The section ends at `test edi, edi` (0x451C4A); the loop keeps its
        // state in EBX, ESI and EDI, which both calls preserve.
        MemoryPatch(0x451C2A)
            << CALL(_gameLoop)
            << CALL(_idle)
            << BYTES(ASM::NOP, 22);

        // NET_D2GS_CLIENT_Send @ 0x478385: `call ds:[GetTickCount]` (6 bytes),
        // the clock of its duplicate-request window. Same contract: no
        // arguments, the tick in EAX.
        MemoryPatch(0x478385)
            << CALL(_senderClock)
            << BYTES(ASM::NOP, 1);

        // override the entire sleepy section - 23 bytes long
        MemoryPatch(0x4FA663)
            << CALL(_oogLoop)
            << BYTES(ASM::NOP, 18);

        // overriding FreeUnusedItems function call
        MemoryPatch(0x52d96c) << CALL(_serverLoopIntercept);

    }
} feature;
