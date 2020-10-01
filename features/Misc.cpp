/**
 * Small patches and features that don't belong elsewhere.
 */

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/dialog.h"
#include <iostream>
#include <cwchar>
#include <vector>
#include <string>

REMOTEREF(int, DrawAutoMapStatsOffsetY, 0x7A51BC);
REMOTEREF(D2::Types::UnitAny*, CurrentTooltipItem, 0x7BCBF4);
REMOTEFUNC(BYTE __stdcall, GetMaxSocketCount, (D2::Types::UnitAny *pItem), 0x62BC20);
REMOTEFUNC(int, FUN_0047a560, (), 0x47a560);

Dialog splashDialog;

void ClearSplash() {
    splashDialog.hide();
    D2::MainMenuForm();
}

void SplashScreenDialogSetup() {
    int width = 375, top = 0, height = 15;
    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, 25);
        elem->setFont(5);
        elem->setText(L"Charon");
        elem->setOrientation(Orientation::CENTER);
        elem->setFrame(0x2, 0xFF, 0xD);
        elem->setTextOffset(0, -6);
        elem->show();
        splashDialog.addChild(elem);

        top += 25 + height;
    }

    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(0);
        elem->setText(L"This is an experiment and utility for Diablo II.");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -5);
        elem->show();
        splashDialog.addChild(elem);
        top += height;
    }

    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(0);
        elem->setText(L"It is intended for single player and TCP/IP game");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -5);
        elem->show();
        splashDialog.addChild(elem);
        top += height;
    }

    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(0);
        elem->setText(L"use, so connections to Battle.net are disabled.");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -5);
        elem->show();
        splashDialog.addChild(elem);
        top += height;
    }

    int height2 = 12;
    top += height2;
 
    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(12);
        elem->setText(L"\"\u00FFc5I see him there at the oars of his little boat in the lake,");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -3);
        elem->show();
        splashDialog.addChild(elem);
        top += height2;
    }

    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(12);
        elem->setText(L"\u00FFc5the ferryman of the dead, Charon, with his hand upon the oar");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -3);
        elem->show();
        splashDialog.addChild(elem);
        top += height2;
    }

    {
        TextElement* elem = new TextElement();
        elem->setPos(0, top);
        elem->setDimensions(width, height);
        elem->setFont(12);
        elem->setText(L"\u00FFc5and he calls me now.\" ~Alcestis(from Alcestis by Euripides)");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(0, -3);
        elem->show();
        splashDialog.addChild(elem);
        top += height;
    }

    {
        int w = 30, h = 24;
        TextElement* elem = new TextElement();
        elem->setPos((width - w) / 2, top + 4);
        elem->setDimensions(w, h);
        elem->setFont(0);
        elem->setText(L"OK");
        elem->setOrientation(Orientation::CENTER);
        elem->setTextOffset(1, -3);
        elem->setFrame(0x3, 0xFF, 0xD);
        elem->show();

        elem->onClick([](MouseButton button, bool down) -> void {
            ClearSplash();
        });

        splashDialog.addChild(elem);
        top += h + height;
    }

    splashDialog.setDimensions(width, top);
    splashDialog.setPos(-width / 2, -top / 2);

    // bind the enter key to the dialog
    splashDialog.onKey([](DWORD keyCode, bool down, DWORD flags) -> bool {
        if (keyCode == 0x0D /* enter */) {
            ClearSplash();
        }

        return false; // Block keypresses to D2 and dialogs behind this one (windowMessage and keyEvent still work regardless)
    });

}

// Replaces the automatic splash screen timeout
void SplashScreenHook() {
    splashDialog.show();
}

static DWORD automapTextOffset = 0;
void _drawAutoMapInfo(DWORD size) {
    DWORD width = 0, height = 0, fileno = 1;
    height = D2::GetTextSize(L"test", &width, &fileno);
    DWORD bottom = DrawAutoMapStatsOffsetY - height;
    for (AutomapInfoCallback func : AutomapInfoHooks) {
        std::wstring msg = func();
        bottom += D2::GetTextSize(msg.c_str(), &width, &fileno);
        D2::DrawGameText(msg.c_str(), D2::ScreenWidth - 16 - width, bottom, 4, 0);
    }
    automapTextOffset = bottom;
}

void __fastcall DrawRepairImageAlertIntercept(void *pImage, bool isArrow) {
    int x = D2::ScreenWidth - (isArrow ? 0x38 : 0x3C), y = 0;

    if (D2::GetUiFlag(0xA)) {
        y = (FUN_0047a560() ? 0x2B : 0) + DrawAutoMapStatsOffsetY + automapTextOffset;
    }
    else {
        y = 0x6E;
    }

    if (isArrow) {
        y += 0x2D;
    }

    D2::DrawImage(pImage, x, y, -1, isArrow ? 5 : 1, nullptr);
}

ASMPTR GetItemName_Original = 0x48C060;

__declspec(naked) BOOL __fastcall GetItemName_Relocated(D2::Types::UnitAny* item, wchar_t* wBuffer, DWORD dwSize) {
    static ASMPTR GetItemName_Rejoin = 0x48C068;
    __asm {
        push ebp
        mov ebp, esp
        mov eax, 0x1bd8
        jmp GetItemName_Rejoin
    }
}

BOOL __fastcall GetItemName_Intercept(D2::Types::UnitAny* item, wchar_t* wBuffer, DWORD dwSize) {
    BOOL ret = GetItemName_Relocated(item, wBuffer, dwSize);
    DWORD sockets = D2::GetUnitStat(item, 194, 0);

    if (sockets > 0) {
        wchar_t *tmpBuffer = new wchar_t[dwSize], *a = wBuffer;

        while (*a != L'\0' && *a != L'\n') {
            a++;
        }

        if (*a == L'\n') {
            *a++ = L'\0';
            swprintf_s(tmpBuffer, dwSize, L"%ls (%d)\n%ls", wBuffer, sockets, a);
            wcscpy_s(wBuffer, dwSize, tmpBuffer);
        }
        else {
            swprintf_s(tmpBuffer, dwSize, L"%ls (%d)", wBuffer, sockets);
            wcscpy_s(wBuffer, dwSize, tmpBuffer);
        }
    }

    return ret;
}

void __fastcall GetItemDescription(wchar_t* wBuffer) {
    const size_t dwSize = 1024;

    if (Settings["itemInfo"] && CurrentTooltipItem->pItemData->dwItemLevel > 1) {
        wchar_t tmpBuffer[dwSize];
        if (D2::GetUnitStat(CurrentTooltipItem, 194, 0) < 1 && CurrentTooltipItem->pItemData->dwQuality < 4) {
            BYTE maxSockets = GetMaxSocketCount(CurrentTooltipItem);
            if (maxSockets > 0) {
                swprintf(tmpBuffer, 1024, L"\u00FFc5Max Sockets: %d\nItem Level: %d\n%ls", maxSockets, CurrentTooltipItem->pItemData->dwItemLevel, wBuffer);
                wcscpy_s(wBuffer, dwSize, tmpBuffer);
                return;
            }
        }

        swprintf(tmpBuffer, 1024, L"\u00FFc5Item Level: %d\n%ls", CurrentTooltipItem->pItemData->dwItemLevel, wBuffer);
        wcscpy_s(wBuffer, dwSize, tmpBuffer);
    }
}

ASMPTR ItemDescription_Patches[] = { 0x48ED2F, 0x48E39C, 0x48DB1D };
REMOTEFUNC(void __fastcall, CalcTextDimensions, (wchar_t* wBuffer, long* x, long* y), 0x502520);

void __fastcall ItemDescription_Hook(wchar_t* wBuffer, long *x, long *y) {
    GetItemDescription(wBuffer);
    CalcTextDimensions(wBuffer, x, y);
}

BOOL __fastcall OverrideQuestState(int questId, int questState, BOOL value) {
    switch (questId) {
    case 3: // Imbue quest
        if (Settings["imbueOverride"] && questState == 0) {
            return false;
        }
        if (Settings["imbueOverride"] && questState == 1) {
            return true;
        }
        break;

    case 4: // Can open cow potal
        if (Settings["cowsOverride"] && questState == 10) {
            gamelog << "Quest 4:" << questState << " = " << value << std::endl;
            return false;
        }
        break;

    case 7: // Act 1 complete
    case 15: // Act 2 complete
    case 23: // Act 3 complete
    case 28: // Act 4 complete
        break;

    case 35: // Socket Quest
        if (Settings["socketOverride"] && questState == 1) {
            return true;
        }
        break;

    case 41: // Respec Quest
        if (Settings["respecOverride"] && questState == 0) {
            return false;
        }
        if (Settings["respecOverride"] && questState == 1) {
            return true;
        }
        break;
    }

    return value;
}

__declspec(naked) void GetQuestState_Intercept() {
    __asm {
        push eax
        mov ecx, [ebp + 0xc]
        mov edx, [ebp + 0x10]
        call OverrideQuestState
        pop ebp
        ret 0xc
    }
}

DWORD __stdcall OverrideWaypoints(DWORD a, DWORD b) {
    return true;
}

ASMPTR SocketNotGrey_Patches[] = { 0x452857, 0x48E878, 0x48E897 };

DWORD gamestart = 0;

double flashy = 0, speed = 0.03;

ASMPTR GetGlobalLight_Rejoin = 0x61C0B6;

__declspec(naked) void __stdcall GetGlobalLight_Original(void* pAct, BYTE* red, BYTE* green, BYTE* blue) {
    __asm {
        push ebp
        mov ebp, esp
        mov ecx, dword ptr[ebp + 0x8]
        jmp GetGlobalLight_Rejoin
    }
}

void __stdcall GetGlobalLight(void* pAct, BYTE &red, BYTE &green, BYTE &blue) {
    if (Settings["useColors"]) {
        red = (BYTE)(sin(flashy) * 127.5 + 127.5);
        green = (BYTE)(sin(flashy * 2) * 127.5 + 127.5);
        blue = (BYTE)(sin(flashy * 3) * 127.5 + 127.5);
        return;
    }

    GetGlobalLight_Original(pAct, &red, &green, &blue);
}

wchar_t* __fastcall UnitVisualname(D2::Types::UnitAny *pUnit) {
    // For now it falls under item info, as it is as item/unit level
    if (!Settings["itemInfo"]) return D2::GetUnitName(pUnit);

    const size_t dwSize = 1024;
    // Since it returns directly to an append function we just need one static
    // buffer here to reuse, because there's no guarantee that the stack will be
    // preserved. Also Visual Studio complains about returning a temporary variable.
    static wchar_t tmpBuffer[dwSize];

    swprintf(tmpBuffer, dwSize, L"%ls (%d)", D2::GetUnitName(pUnit), D2::GetUnitStat(pUnit, 12/*level*/, 0));
    return tmpBuffer;
}

// This feature class registers itself.
class : public Feature {
public:
    void init() {
        SplashScreenDialogSetup();
        MemoryPatch(0x42fb40) << CALL(SplashScreenHook);

        MemoryPatch(0x4F5623) << BYTESEQ{ 0xC7, 0xC0, 0, 0, 0, 0 }; // Allow multiple windows open
        MemoryPatch(0x476D40) << ASM::RET; // Ignore shaking requests
        MemoryPatch(0x43BF60) << ASM::RET; // Prevent battle.net connections
        MemoryPatch(0x61C0B0) << JUMP(GetGlobalLight);
        MemoryPatch(0x515FB1) << BYTE(0x01); // Delay of 1 on cleaning up sounds after quiting game
        MemoryPatch(0x4781AC) << BYTESEQ{ 0x6A, 0x05, 0x90, 0x90, 0x90 }; // Hyperjoin for TCP/IP games
        MemoryPatch(GetItemName_Original) << JUMP(GetItemName_Intercept);
        MemoryPatch(ItemDescription_Patches[0]) << CALL(ItemDescription_Hook);
        MemoryPatch(ItemDescription_Patches[1]) << CALL(ItemDescription_Hook);
        MemoryPatch(ItemDescription_Patches[2]) << CALL(ItemDescription_Hook);

        // Disable coloring socketed items grey
        MemoryPatch(SocketNotGrey_Patches[0]) << DWORD(0x400000);
        MemoryPatch(SocketNotGrey_Patches[1]) << DWORD(0x400000);
        MemoryPatch(SocketNotGrey_Patches[2]) << DWORD(0x400000);

        // Fix RandTransforms.dat for LoD, to colorize monsters properly
        MemoryPatch(0x4666A5) << BYTE(0x26); // RandTransforms

        MemoryPatch(0x65C34E) << JUMP(GetQuestState_Intercept);
        MemoryPatch(0x45ADE8) << CALL(_drawAutoMapInfo);
        MemoryPatch(0x454ba8) << CALL(UnitVisualname);

        // Repair/Arrow adjustments modified from jaenster's code
        MemoryPatch(0x497D72) << BYTESEQ{ 0x8D, 0x4D, 0xB8, 0xBA } << DWORD(0) << CALL(DrawRepairImageAlertIntercept) << BYTESEQ{ 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0 };
        MemoryPatch(0x497CBA) << BYTESEQ{ 0x8D, 0x4D, 0xB8, 0xBA } << DWORD(1) << CALL(DrawRepairImageAlertIntercept) << BYTESEQ{ 0x5F, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0 };

        AutomapInfoHooks.push_back([]() -> std::wstring {
            return version;
        });

        AutomapInfoHooks.push_back([]() -> std::wstring {
            DWORD elapsed = GetTickCount() - gamestart, seconds = (elapsed / 1000) % 60, minutes = (elapsed / 60000) % 60;
            wchar_t msg[16];
            swprintf_s(msg, L"%d:%02d", minutes, seconds);
            return msg;
        });
    }

    void gameLoop() {
        gamestart = gamestart ? gamestart : GetTickCount();
        flashy += speed;
    }

    void oogLoop() {
        gamestart = 0;

        if (Settings["regenMap"] && !State["regenMap"]) {
            MemoryPatch(0x56A200) << BYTE(0xEB);
            State["regenMap"] = true;
        }
        else if (!Settings["regenMap"] && State["regenMap"]) {
            MemoryPatch(0x56A200) << REVERT(1);
            State["regenMap"] = false;
        }
    }

    void oogPostDraw() {
        DWORD width = 0, height = 0, fileno = 1;
        height = D2::GetTextSize(version.c_str(), &width, &fileno);
        D2::DrawGameText(version.c_str(), D2::ScreenWidth - width - 5, D2::ScreenHeight - 5, 4, 0);
    }
} feature;
