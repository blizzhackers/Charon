/**
 * Charon - For the finer life in Diablo 2.
 */
#define DEFINE_REMOTE_REFERENCES true
#include <windows.h>
#include "headers/common.h"
#include "headers/feature.h"
#include "headers/remote.h"
#include "headers/hook.h"
#include <shlobj.h>
#include <iostream>

HINSTANCE hInst = nullptr;
wchar_t saveDir[512] = { 0 };

namespace {
bool initialized = false;
REMOTEFUNC(void, ApplicationMainContinue, (), 0x405c36);

void InitializeCharon() {
    if (initialized) return;
    // Hooks retain Charon code pointers for the game lifetime; live unload is unsupported.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&InitializeCharon), &pinned)) {
        TerminateProcess(GetCurrentProcess(), GetLastError());
        return;
    }
    initialized = true;
    PWSTR roamingAppDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roamingAppDataPath))) {
        swprintf_s(saveDir, L"%ws\\Charon", roamingAppDataPath);
    } else {
        const DWORD length = GetModuleFileNameW(hInst, saveDir, _countof(saveDir));
        if (!length || length >= _countof(saveDir)) {
            TerminateProcess(GetCurrentProcess(), ERROR_INSUFFICIENT_BUFFER);
            return;
        }
        wchar_t* separator = wcsrchr(saveDir, L'\\');
        if (!separator) {
            TerminateProcess(GetCurrentProcess(), ERROR_BAD_PATHNAME);
            return;
        }
        *separator = 0;
    }
    CoTaskMemFree(roamingAppDataPath);
    swprintf_s(settingsPath, L"%ws\\settings.txt", saveDir);
    CreateDirectoryW(saveDir, nullptr);
    LoadSettings();
    for (Feature* f = Features; f; f = f->next) f->init();
    gamelog << COLOR(2) << "Charon loaded." << std::endl
        << COLOR(3) << "Press F11 to edit settings." << std::endl;
}

__declspec(naked) void InitializeCharonHook() {
    __asm {
        pushfd
        pushad
        call InitializeCharon
        popad
        popfd
        // Replay ApplicationMain's complete overwritten prologue.
        push ebp
        mov ebp, esp
        sub esp, 0x10
        jmp ApplicationMainContinue
    }
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        hInst = module;
        // Run on the main thread after DLL loading, before MPQs and renderer selection.
        MemoryPatch(0x405c30) << JUMP(InitializeCharonHook) << ASM::NOP;
    } else if (reason == DLL_PROCESS_DETACH && !reserved && !initialized) {
        // An explicit unload before startup must not leave the bootstrap pointing here.
        RevertBytes(0x405c30, 6);
    }
    // Process termination must not acquire feature/GL locks under the loader lock.
    // Static CRT needs thread notifications; do not call DisableThreadLibraryCalls.
    return TRUE;
}
