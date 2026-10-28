#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include <shellapi.h>
#include <cstdlib>

REMOTEREF(BOOL, D2Ini_bUseDirect, 0x74c82c);
REMOTEFUNC(DWORD __fastcall, ARCHIVE_LoadMPQ, (const char* szMpqFileName, int nZero, DWORD fpRequiredUserAction, int nTimeTBC), 0x517332);

BOOL GetUseDirect(void) {
    for (Feature* f = Features; f; f = f->next) {
        for (std::string mpqname : f->mpq) {
            if (ARCHIVE_LoadMPQ(mpqname.c_str(), 0, 0, 5000)) {
                gamelog << "Using MPQ: " << mpqname.c_str() << std::endl;
            }
            else {
                gamelog << "Failed to load: " << mpqname.c_str() << std::endl;
            }
        }
    }

    return D2Ini_bUseDirect;
}

namespace MPQLoader {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(0x4fac38) << CALL(GetUseDirect);

            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            char arg[128];
            size_t len;

            for (int c = 0; c < argc; c++) {
                if (!wcscmp(argv[c], L"-mpq") && ++c < argc) {
                    wcstombs_s(&len, arg, argv[c], 128);
                    mpq.push_back(arg);
                }
            }

        }
    } feature;

}
