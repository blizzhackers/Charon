#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <list>

REMOTEFUNC(void __fastcall, PrintGameString, (const wchar_t* wMessage, int nColor), 0x49E3A0)
REMOTEFUNC(void __fastcall, PrintPartyString, (const wchar_t* wMessage, int nColor), 0x49E5C0) // Updated 1.14d //0049E5C0-BASE
REMOTEREF(DWORD, PanelState, 0x07a5210);

struct GameLogMessage {
    DWORD timestamp;
    std::wstring text;
};

std::list<GameLogMessage> GameLogMessages;
GameOutput gamelog;

bool ingame = false;

int GameOutput::GameOutputBuffer::sync() {
    std::wstring::size_type start = 0;
    std::wstring::size_type pos = this->str().find(L"\n");

    while (pos != std::wstring::npos) {
        std::wstring tmp = this->str().substr(start, pos - start);
        GameLogMessages.push_back({ GetTickCount(), tmp });
        start = pos + 1;
        pos = this->str().find(L"\n", start);
    }

    if (start > 0) {
        this->str(this->str().substr(start));
    }

    return 0;
}

GameOutput::GameOutput() : std::wostream(&buf) { }

std::wstring COLOR(BYTE color) {
    wchar_t ret[]{ 0xFF, 0x63, 0x30, 0 };
    ret[2] += (color & 0xf);
    return ret;
}

void __fastcall PrintGameStringHook(const wchar_t* wMessage, int nColor) {
    gamelog << COLOR((BYTE)nColor) << wMessage << std::endl;
}

ASMPTR GameStringInterceptExit = 0x49e3a9;

void __declspec(naked) PrintGameStringIntercept() {
    __asm {
        // preserve the registers around our call
        pushad
        call PrintGameStringHook
        popad

        // mimic instructions we've replaced
        push ebp
        mov ebp, esp
        sub esp, 0x110

        // resume the original function
        jmp GameStringInterceptExit
    }
}

void ClearChatMessages() {
    while (!GameLogMessages.empty()) {
        GameLogMessages.pop_front();
    }
}

namespace GameLog {

    class : public Feature {
        DWORD start = 0;
    public:
        void init() {
            //gamelog << COLOR(4) << "Game log installed..." << std::endl;
            MemoryPatch(0x49dcb8) << NOP_TO(0x49ddc9);
            MemoryPatch(0x49e3a0) << JUMP(PrintGameStringIntercept) << NOP_TO(GameStringInterceptExit);
            MemoryPatch(0x4a01fa) << JUMP(ClearChatMessages);
        }

        void showMessages(bool lower) {
            int top = lower ? 0x5f : 10, left = 15;
            DWORD height = 0, width = 0, fontno = 13;
            int space = -8;

            if (PanelState == 2) {
                left += D2::ScreenWidth / 2;
            }

            if (!start) {
                start = GetTickCount();
            }

            if (GetTickCount() - start < 300) {
                return;
            }

            while (!GameLogMessages.empty() && GameLogMessages.front().timestamp + 10000 < GetTickCount()) {
                GameLogMessages.pop_front();
            }

            D2::SetFont(fontno);

            for (GameLogMessage message : GameLogMessages) {
                height = D2::GetTextSize(message.text.c_str(), &width, &fontno);
                D2::DrawGameText(message.text.c_str(), left, top += (height + space), 0, 0);
            }
        }

        void gamePostDraw() {
            if (!D2::GetUiFlag(0x18)) {
                showMessages(D2::GetUiFlag(0x13));
            }
        }

        void oogPostDraw() {
            showMessages(false);
        }
    } feature;

}
