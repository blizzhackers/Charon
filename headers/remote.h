#pragma once

#include "common.h"
#include "D2Structs.h"

typedef unsigned long ASMPTR;

#ifdef DEFINE_REMOTE_REFERENCES

#define GLOBALFUNC(r, n, a, o) namespace FuncDefs { typedef r n a; } FuncDefs::n *n = (FuncDefs::n*)o;
#define GLOBALREF(t, n, o) namespace VarDefs { typedef t n; } VarDefs::n &n = *(VarDefs::n *)o;
#define GLOBALPTR(t, n, o) namespace VarDefs { typedef t n; } VarDefs::n *n = (VarDefs::n *)o;

#else

#define GLOBALFUNC(r, n, a, o) namespace FuncDefs { typedef r n a; } extern FuncDefs::n *n;
#define GLOBALREF(t, n, o) namespace VarDefs { typedef t n; } extern VarDefs::n &n;
#define GLOBALPTR(t, n, o) namespace VarDefs { typedef t n; } extern VarDefs::n *n;

#endif

#pragma warning( disable : 4229 )

namespace D2 {

	// Basically, if the address represents an array (not a pointer to an array) use this
	// global pointer 'GLOBALPTR' macro, otherwise use global reference 'GLOBALREF' instead
	GLOBALREF(Types::UnitHashTableCollection, ClientSideUnitHashTables, 0x7A5270);
	GLOBALREF(Types::UnitHashTableCollection, ServerSideUnitHashTables, 0x7A5E70);
	GLOBALREF(Types::CurrentPlayerUnit*, pPlayerUnit, 0x7A6A70);
	GLOBALREF(DWORD, gbSuppressItemSelection, 0x7A6A90);
	GLOBALREF(HWND, hwndD2GFX_Window, 0x7C8CBC);
	GLOBALREF(int, eD2GSGameType_007a0610, 0x7A0610);

	// For referencing D2's functions use this specialized macro instead
	GLOBALFUNC(DWORD __fastcall, SetAndReturnLastFont, (DWORD dwFileNo), 0x502EF0);
	GLOBALFUNC(void __fastcall, DRAW_text, (const wchar_t* wStr, int xPos, int yPos, DWORD dwColor, BOOL bMultiLineCenterFlag), 0x502320);
	GLOBALFUNC(void __stdcall, D2GFX_DrawLine, (int nXStart, int nYStart, int nXEnd, int nYEnd, DWORD dwColor, DWORD nAlpha), 0x4F6380);
	GLOBALFUNC(void __stdcall, D2GFX_DrawRectEx, (RECT* pRect, BYTE nPaletteIndex), 0x4F62A0);
	GLOBALFUNC(void __stdcall, D2GFX_DrawSolidRectAlpha, (int nXStart, int nYStart, int nXEnd, int nYEnd, DWORD dwColor, DWORD nAlpha), 0x4F6340);
	GLOBALFUNC(BOOL __stdcall, D2GFX_DrawImage, (void* pDC6Context, int nPositionX, int nPositionY, int dwGamma, DWORD eMode, void* pPalette), 0x4F6480);
	GLOBALFUNC(void __fastcall, TEXT_CalcTextDimensions, (const wchar_t* wStr, int* pWidth, int* pHeight), 0x502520);
	GLOBALFUNC(DWORD __stdcall, STATSEX_GetCurrentStat, (Types::UnitAny* pUnit, DWORD dwStat, DWORD dwStat2), 0x625480);
	GLOBALFUNC(wchar_t* __fastcall, GetName, (Types::UnitAny* unit), 0x464a60);
	GLOBALFUNC(void __stdcall, D2GFX_GetWindowSizeByResolutionMode, (int nResolutionMode, int* pResRightMax, int* pResBottomMax), 0x4F5570);
	GLOBALFUNC(DWORD __stdcall, D2GFX_GetResolutionMode, (), 0x4F5160);
	GLOBALFUNC(DWORD __fastcall, GetUIFlag, (DWORD dwVarNo), 0x4538D0);
	GLOBALFUNC(void __stdcall, AddRoomData, (Types::Act* ptAct, int LevelId, int Xpos, int Ypos, D2::Types::Room1* pRoom), 0x61A070);
	GLOBALFUNC(Types::LevelTxt* __stdcall, TXT_Levels_GetLine, (DWORD levelno), 0x61DB70);
	GLOBALFUNC(Types::ObjectTxt* __stdcall, TXT_Objects_GetLine, (DWORD objno), 0x640E90);
	GLOBALFUNC(Types::ItemTxt* __stdcall, TXT_Items_GetLine, (DWORD itemno), 0x6335F0);
	GLOBALFUNC(void, UIMENU_MainMenu, (), 0x4336c0); // No params, so calling convention doesn't matter

    GLOBALFUNC(DWORD __fastcall, SERVER_SpawnPortal, (D2::Types::IncompleteGameData* pGame, D2::Types::UnitAny* pUnit, D2::Types::Room1* pDrlgRoom, int nX, int nY, DWORD eD2LevelId, D2::Types::UnitAny** param_7, int nClassId, DWORD param_9), 0x56D130);
    GLOBALFUNC(void __fastcall, FindSpawnableLocation, (D2::Types::Room1* pDrlgRoom, POINT* pos, DWORD param_1_00, DWORD param_2_00, D2::Types::Room1** pRoomsNear, DWORD param_6, int param_7), 0x545340);
    GLOBALFUNC(void __stdcall, GetCoords, (D2::Types::UnitAny* pUnit, POINT* pPoint), 0x620870);
    GLOBALFUNC(DWORD __stdcall, GetActNoFromLevelNumber, (int levelId), 0x6427f0);
}

#pragma warning( default : 4229 )

#define REMOTEFUNC(r, n, a, o) namespace FuncDefs { typedef r n a; } FuncDefs::n *n = (FuncDefs::n*)o;
#define REMOTEREF(t, n, o) namespace VarDefs { typedef t n; } VarDefs::n &n = *(VarDefs::n *)o;
#define REMOTEPTR(t, n, o) namespace VarDefs { typedef t n; } VarDefs::n *n = (VarDefs::n *)o;
