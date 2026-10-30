/**
 * Lua `oog` module: out-of-game screen detection and menu control.
 *
 * Controls are read from the live D2Win form list each call and identified by
 * their address; scripts never hold pointers. Actions re-validate the control
 * against the live list, then drive it through the control's own handlers the
 * same way the game's hotkey path does, so state, sounds and callbacks match a
 * user click. Addresses and layouts: Diablo II 1.14d (redivivus, address-keyed).
 */
#include "headers/common.h"
#include "headers/remote.h"
#include "headers/script_host.h"
#include <cstdint>

namespace ScriptOog {

namespace {

#pragma pack(push, 1)
struct Control;
struct ControlMsg {
    void* target;       // control, or the game window for outer input handlers
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
    uint32_t commandCode;
    uint32_t reserved;
    uint32_t handled;
    uint32_t result;
};
static_assert(sizeof(ControlMsg) == 0x20, "D2ControlMsg is 0x20 bytes");

typedef BOOL(__stdcall* MsgHandler)(ControlMsg*);
struct Control {
    uint32_t type;      // 00
    void* dc6;          // 04
    uint32_t state;     // 08 bit0 enabled, bit2 active
    int32_t x;          // 0C
    int32_t bottom;     // 10
    int32_t width;      // 14
    int32_t height;     // 18
    void* draw;         // 1C
    void* drawEx;       // 20
    MsgHandler push;    // 24 mouse down
    MsgHandler mouse;   // 28 mouse up
    MsgHandler listCheck; // 2C
    MsgHandler key;     // 30
    MsgHandler onPress; // 34
    void* drawAnim;     // 38
    Control* next;      // 3C
};
static_assert(sizeof(Control) == 0x40, "D2ControlStrc is 0x40 bytes");

struct CharSelEntry {
    char name[256];        // 000
    uint8_t command[0x220]; // 100
    uint8_t classId;       // 320
    uint8_t pad321;
    uint16_t level;        // 322
    uint16_t flags;        // 324: 4 hardcore, 8 dead, 0x20 expansion, 0x40 ladder
    uint8_t pad326[0x334 - 0x326];
    uint32_t entryType;    // 334: 0 character, 1 Create New, 2 retrieving
    uint8_t pad338[0x34C - 0x338];
    CharSelEntry* next;    // 34C
};
static_assert(offsetof(CharSelEntry, classId) == 0x320 && offsetof(CharSelEntry, next) == 0x34C, "D2CharSelStrc layout");
#pragma pack(pop)

enum ControlType { EditBox = 1, Image = 2, TextBox = 4, Scrollbar = 5, Button = 6, List = 7 };

REMOTEREF(Control*, FormsList, 0x7D55BC);
REMOTEREF(Control*, ModalFormStart, 0x7D55D4);
REMOTEREF(int, FormControlCount, 0x7D55C4);
REMOTEREF(Control*, FocusedControl, 0x7D55CC);
REMOTEREF(BYTE*, LauncherIni, 0x7795D4);
REMOTEREF(int, SelectedCreateClass, 0x70CB80);
REMOTEPTR(Control*, CreateClassPortraits, 0x77956C);
REMOTEREF(Control*, CreateNameEdit, 0x77934C);
REMOTEREF(Control*, CreateOkButton, 0x779330);
REMOTEREF(BOOL, MessageLoopActive, 0x72DDD4);
REMOTEREF(uint32_t, CharSelectionMode, 0x7795EC);
REMOTEREF(CharSelEntry*, CharSelFirst, 0x779DBC);
REMOTEREF(int, CharSelSelectedIndex, 0x70CC00);

REMOTEFUNC(BOOL __fastcall, D2WINEDITBOX_SetTextWide, (Control* pEdit, const wchar_t* wszText), 0x4FF5A0);
REMOTEFUNC(void __stdcall, INPUT_FormKeyHandler, (ControlMsg* pMsg), 0x4F9B60);
REMOTEFUNC(void __stdcall, INPUT_FormKeyUpHandler, (ControlMsg* pMsg), 0x4F9BF0);
REMOTEFUNC(void __stdcall, INPUT_FormTabKey, (ControlMsg* pMsg), 0x4FA760);
REMOTEFUNC(BOOL __stdcall, D2WINMAIN_SetFocusedControl, (Control* pControl), 0x4F91C0);
REMOTEFUNC(wchar_t* __fastcall, D2WINTEXTBOX_GetLineCellValue, (Control* pTextBox, int nRow, int nColumn), 0x4FBB90);

// OnPress / Push handlers that identify well-known controls.
const uintptr_t SinglePlayerPress = 0x435CD0;
const uintptr_t CharSelectOkPress = 0x439840;
const uintptr_t CharSlotPress = 0x43A9D0;
const uintptr_t CreateNewPress = 0x435E00;
const uintptr_t CreateHeroPress = 0x436590;
const uintptr_t ClassPortraitPress = 0x433BF0;
const uintptr_t CreateOkPress = 0x4369F0;
const uintptr_t HardcoreTogglePress = 0x430730;
const uintptr_t ExpansionTogglePress = 0x430770;
const uintptr_t HardcoreConfirmPress = 0x436360;
const uintptr_t DifficultyPress[3] = { 0x439B80, 0x439BA0, 0x439BC0 };
const uintptr_t AnimImagePush = 0x500790;
const int ButtonClickAnim = 0x4C, ButtonLabel = 0x64, EditText = 0x5C, EditMaxLength = 0x48;
const int TextBoxLineCount = 0x54, TextBoxColumnCount = 0x64, AnimImageState = 0x50;
const int ListCount = 0x50, ListSelected = 0x54, ListFirstEntry = 0x64, ListEntryNext = 0x220;
const size_t MaxControls = 1024;

// Portrait array index -> player class id (array order is not class order).
const int PortraitClass[7] = { 4, 2, 0, 1, 3, 5, 6 };
const char* ClassNames[7] = { "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };

template <class T> T& Field(Control* control, int offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<BYTE*>(control) + offset);
}

uintptr_t Address(const void* pointer) { return reinterpret_cast<uintptr_t>(pointer); }

std::vector<Control*> LiveControls() {
    std::vector<Control*> controls;
    for (Control* c = FormsList; c && controls.size() < MaxControls; c = c->next) controls.push_back(c);
    return controls;
}

bool InModalSegment(Control* control) {
    if (!ModalFormStart) return true;
    for (Control* c = ModalFormStart; c; c = c->next) {
        if (c == control) return true;
    }
    return false;
}

Control* FindLive(uintptr_t id) {
    for (Control* c : LiveControls()) {
        if (Address(c) == id) return c;
    }
    return nullptr;
}

Control* FindByPress(uintptr_t handler) {
    for (Control* c : LiveControls()) {
        if (Address(c->onPress) == handler) return c;
    }
    return nullptr;
}

bool IsLive(Control* control) {
    return control && FindLive(Address(control)) == control;
}

// Identifies the current set of live controls; changes when any form is
// created or destroyed (FNV-1a over control addresses and types).
uint32_t Generation() {
    uint32_t hash = 2166136261u;
    for (Control* c : LiveControls()) {
        for (uint32_t value : { uint32_t(Address(c)), c->type }) {
            hash = (hash ^ value) * 16777619u;
        }
    }
    return hash;
}

const char* TypeName(uint32_t type) {
    switch (type) {
    case EditBox: return "edit";
    case Image: return "image";
    case 3: return "anim_image";
    case TextBox: return "textbox";
    case Scrollbar: return "scrollbar";
    case Button: return "button";
    case List: return "list";
    case 8: return "timer";
    case 9: return "video";
    case 10: return "progress";
    case 11: return "popup";
    case 12: return "account_list";
    case 13: return "image_ex";
    default: return "unknown";
    }
}

std::string Role(Control* c, int& classId) {
    classId = -1;
    const uintptr_t press = Address(c->onPress);
    for (int i = 0; i < 7; ++i) {
        // Launcher globals can outlive their forms: confirm the type as well.
        if (CreateClassPortraits[i] == c && c->type == Image) {
            classId = PortraitClass[i];
            return "class";
        }
    }
    if (c == CreateNameEdit && c->type == EditBox) return "name";
    if (press == SinglePlayerPress) return "single_player";
    if (press == CharSelectOkPress && c->type == Button) return "select_ok";
    if (press == CharSlotPress) return "character_slot";
    if (press == CreateNewPress || press == CreateHeroPress) return "create_new";
    if (press == CreateOkPress) return "create_ok";
    if (press == HardcoreTogglePress) return "hardcore";
    if (press == ExpansionTogglePress) return "expansion";
    if (press == HardcoreConfirmPress) return "hardcore_confirm";
    for (int i = 0; i < 3; ++i) {
        if (press == DifficultyPress[i]) return i == 0 ? "normal" : i == 1 ? "nightmare" : "hell";
    }
    return "";
}

std::string ControlText(Control* c) {
    switch (c->type) {
    case Button:
        return Script::Narrow(std::wstring(&Field<wchar_t>(c, ButtonLabel), wcsnlen(&Field<wchar_t>(c, ButtonLabel), 255)));
    case EditBox:
        return Script::Narrow(std::wstring(&Field<wchar_t>(c, EditText), wcsnlen(&Field<wchar_t>(c, EditText), 254)));
    case TextBox: {
        std::wstring text;
        const int rows = Field<int>(c, TextBoxLineCount);
        const int columns = Field<int>(c, TextBoxColumnCount);
        for (int row = 0; row < rows && row < 64; ++row) {
            for (int column = 0; column < columns && column < 5; ++column) {
                const wchar_t* cell = D2WINTEXTBOX_GetLineCellValue(c, row, column);
                if (!cell) continue;
                if (!text.empty()) text += column ? L'\t' : L'\n';
                text.append(cell, wcsnlen(cell, 512));
            }
        }
        return Script::Narrow(text);
    }
    case List: {
        std::wstring text;
        BYTE* entry = Field<BYTE*>(c, ListFirstEntry);
        for (int i = 0; entry && i < 256; ++i, entry = *reinterpret_cast<BYTE**>(entry + ListEntryNext)) {
            if (i) text += L'\n';
            const wchar_t* label = reinterpret_cast<const wchar_t*>(entry);
            text.append(label, wcsnlen(label, 255));
        }
        return Script::Narrow(text);
    }
    default:
        return "";
    }
}

void PushControl(lua_State* L, Control* c, int index) {
    lua_newtable(L);
    Script::SetField(L, "id", double(Address(c)));
    Script::SetField(L, "index", index);
    Script::SetField(L, "generation", double(Generation()));
    Script::SetField(L, "type", TypeName(c->type));
    Script::SetField(L, "x", int(c->x));
    Script::SetField(L, "y", int(c->bottom - c->height));
    Script::SetField(L, "width", int(c->width));
    Script::SetField(L, "height", int(c->height));
    Script::SetField(L, "enabled", (c->state & 1) != 0);
    Script::SetField(L, "active", (c->state & 4) != 0);
    Script::SetField(L, "modal", ModalFormStart != nullptr && InModalSegment(c));
    Script::SetField(L, "focused", c == FocusedControl);
    int classId;
    const std::string role = Role(c, classId);
    if (!role.empty()) Script::SetField(L, "role", role);
    if (classId >= 0) {
        Script::SetField(L, "class_id", classId);
        Script::SetField(L, "class", ClassNames[classId]);
        Script::SetField(L, "anim_state", Field<int>(c, AnimImageState));
    }
    const std::string text = ControlText(c);
    if (!text.empty()) Script::SetField(L, "text", text);
    if (c->type == EditBox) Script::SetField(L, "max_length", Field<int>(c, EditMaxLength));
    if (c->type == List) {
        Script::SetField(L, "count", Field<int>(c, ListCount));
        Script::SetField(L, "selected", Field<int>(c, ListSelected));
    }
}

bool ControlUsable(Control* c) {
    return (c->state & 5) == 5 && InModalSegment(c);
}

const char* DetectScreen() {
    if (!FormsList) return MessageLoopActive ? "none" : "transition";
    bool characterSelect = false, mainMenu = false, difficulty = false, create = false;
    for (Control* c : LiveControls()) {
        const uintptr_t press = Address(c->onPress);
        for (uintptr_t handler : DifficultyPress) difficulty |= press == handler;
        mainMenu |= press == SinglePlayerPress && (c->state & 5) == 5;
        characterSelect |= press == CharSlotPress || press == CharSelectOkPress;
        create |= c == CreateNameEdit && CreateNameEdit != nullptr;
    }
    if (!MessageLoopActive) return "transition";
    if (difficulty) return "difficulty";
    if (ModalFormStart) return "popup";
    if (create) return SelectedCreateClass < 0 ? "create_class" : "create_name";
    if (characterSelect) return "character_select";
    if (mainMenu) return "main_menu";
    return "unknown";
}

// Mirrors D2WINBUTTON_HandleHotkey: the dispatch counter defers form frees
// while the control's own handlers run.
bool Press(Control* c) {
    const int centerX = c->x + c->width / 2, centerY = c->bottom - c->height / 2;
    ControlMsg down = {};
    down.target = c;
    down.message = WM_LBUTTONDOWN;
    down.lParam = MAKELPARAM(centerX, centerY);
    ++FormControlCount;
    BOOL result = FALSE;
    if (c->type == Button && c->push) {
        Field<int>(c, ButtonClickAnim) = 1;
        result = c->push(&down);
        if (c->mouse) {
            ControlMsg up = {};
            up.target = c;
            up.message = WM_LBUTTONUP;
            up.lParam = down.lParam;
            c->mouse(&up);
            result = TRUE;
        }
    }
    else if (c->type == Image && Address(c->push) == AnimImagePush) {
        // A portrait selects only from its hover state; the cursor would set it.
        if (Field<int>(c, AnimImageState) == 0) Field<int>(c, AnimImageState) = 1;
        result = c->push(&down);
    }
    else if (c->type == TextBox && c->onPress) {
        // Character slots: a non-click message selects without double-click activation.
        ControlMsg select = {};
        select.target = c;
        result = c->onPress(&select);
    }
    else if (c->push) {
        result = c->push(&down);
    }
    --FormControlCount;
    return result != FALSE;
}

void SendKey(UINT message, WPARAM key) {
    ControlMsg msg = {};
    msg.target = D2::hwndD2GFX_Window;
    msg.message = message;
    msg.wParam = (uint32_t)key;
    msg.lParam = 1;
    if (message == WM_CHAR) INPUT_FormKeyHandler(&msg);
    else if (key == VK_ESCAPE) INPUT_FormTabKey(&msg);
    else INPUT_FormKeyUpHandler(&msg);
}

// Accepts a control table from oog.controls() (checked against the current
// screen generation, so a stale snapshot cannot hit a replacement control
// allocated at the same address) or a bare id.
uintptr_t ArgControl(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TTABLE) {
        Script::RawField(L, index, "id");
        Script::RawField(L, index, "generation");
        const double id = lua_tonumber(L, -2);
        const bool stale = lua_isnumber(L, -1) && uint32_t(lua_tonumber(L, -1)) != Generation();
        lua_pop(L, 2);
        if (stale) throw Script::Error("the screen changed since these controls were read");
        return (uintptr_t)id;
    }
    return (uintptr_t)Script::ArgNumber(L, index);
}

int OogScreen(lua_State* L) {
    lua_pushstring(L, DetectScreen());
    return 1;
}

int OogControls(lua_State* L) {
    const std::vector<Control*> controls = LiveControls();
    lua_createtable(L, (int)controls.size(), 0);
    int index = 0;
    for (Control* c : controls) {
        PushControl(L, c, index + 1);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int OogClick(lua_State* L) {
    Control* c = FindLive(ArgControl(L, 1));
    if (!c) throw Script::Error("control is no longer on screen");
    if (!ControlUsable(c)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "control is disabled, inactive or behind a popup");
        return 2;
    }
    lua_pushboolean(L, Press(c));
    return 1;
}

// The character name filter normally runs per typed key; a programmatic set
// bypasses it, so apply the same rules: 2..15 ASCII letters, at most one '-'
// or '_', not first or last.
bool ValidCharacterName(const std::wstring& name) {
    if (name.size() < 2 || name.size() > 15) return false;
    int separators = 0;
    for (wchar_t ch : name) {
        if (ch == L'-' || ch == L'_') ++separators;
        else if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z'))) return false;
    }
    const wchar_t first = name.front(), last = name.back();
    return separators <= 1 && first != L'-' && first != L'_' && last != L'-' && last != L'_';
}

int OogSetText(lua_State* L) {
    Control* c = FindLive(ArgControl(L, 1));
    const std::wstring text = Script::Widen(Script::ArgString(L, 2));
    if (!c || c->type != EditBox) throw Script::Error("not a live edit box");
    if (!ControlUsable(c)) throw Script::Error("edit box is disabled or inactive");
    const int maxLength = Field<int>(c, EditMaxLength);
    if ((int)text.size() >= (maxLength > 0 ? maxLength : 254) || text.size() >= 254) {
        throw Script::Error("text is longer than the edit box allows");
    }
    if (c == CreateNameEdit && !ValidCharacterName(text)) throw Script::Error("not a valid character name");
    lua_pushboolean(L, D2WINEDITBOX_SetTextWide(c, text.c_str()) != FALSE);
    return 1;
}

int OogFocus(lua_State* L) {
    Control* c = FindLive(ArgControl(L, 1));
    // Keyboard focus feeds edit-box handlers; any other control type would halt the game.
    if (!c || c->type != EditBox) throw Script::Error("only a live edit box can take focus");
    lua_pushboolean(L, D2WINMAIN_SetFocusedControl(c) != FALSE);
    return 1;
}

int OogKey(lua_State* L) {
    const int key = Script::ArgInt(L, 1);
    SendKey(WM_KEYDOWN, key);
    return 0;
}

int OogType(lua_State* L) {
    const std::wstring text = Script::Widen(Script::ArgString(L, 1));
    for (wchar_t ch : text) SendKey(WM_CHAR, ch);
    return 0;
}

int OogCharacters(lua_State* L) {
    lua_newtable(L);
    int index = 0;
    for (CharSelEntry* e = CharSelFirst; e && index < 256; e = e->next) {
        if (e->entryType != 0) continue;
        lua_newtable(L);
        Script::SetField(L, "name", std::string(e->name, strnlen(e->name, 16)));
        Script::SetField(L, "class_id", int(e->classId));
        if (e->classId < 7) Script::SetField(L, "class", ClassNames[e->classId]);
        Script::SetField(L, "level", int(e->level));
        Script::SetField(L, "hardcore", (e->flags & 4) != 0);
        Script::SetField(L, "dead", (e->flags & 8) != 0);
        Script::SetField(L, "expansion", (e->flags & 0x20) != 0);
        lua_rawseti(L, -2, ++index);
    }
    return 1;
}

int OogCreateState(lua_State* L) {
    lua_newtable(L);
    const BYTE* ini = LauncherIni;
    if (ini) {
        Script::SetField(L, "class_id", int(ini[0x1EE]));
        const uint16_t status = *reinterpret_cast<const uint16_t*>(ini + 0x1EF);
        Script::SetField(L, "hardcore", (status & 4) != 0);
        Script::SetField(L, "expansion", (status & 0x20) != 0);
    }
    Script::SetField(L, "selected_class", SelectedCreateClass);
    Script::SetField(L, "selection_mode", int(CharSelectionMode));
    Script::SetField(L, "selected_character", CharSelSelectedIndex);
    return 1;
}

const bool registered = (Script::RegisterModule(Script::OutOfGame, [](lua_State* L) {
    static const luaL_Reg functions[] = {
        { "screen", Script::Bind<OogScreen> },
        { "controls", Script::Bind<OogControls> },
        { "click", Script::Bind<OogClick> },
        { "set_text", Script::Bind<OogSetText> },
        { "focus", Script::Bind<OogFocus> },
        { "key", Script::Bind<OogKey> },
        { "type", Script::Bind<OogType> },
        { "characters", Script::Bind<OogCharacters> },
        { "create_state", Script::Bind<OogCreateState> },
        { nullptr, nullptr },
    };
    lua_newtable(L);
    luaL_setfuncs(L, functions, 0);
    lua_setglobal(L, "oog");
}), true);

}

}
