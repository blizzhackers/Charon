#include "headers/common.h"
#include "headers/feature.h"

HotkeyCallbackMap HotkeyCallbacks;
AutomapInfoCallbackList AutomapInfoHooks;
InputCallbackMap ChatInputCallbacks;
StateMap State;
StateMap Settings;
Feature* Features;

Feature::Feature() {
	this->next = Features;
	Features = this;
}

Feature::~Feature() { }
void Feature::init() { }
void Feature::postInit() { }
void Feature::deinit() { }
void Feature::gameLoop() { }
void Feature::oogLoop() { }
void Feature::gameServerLoop(D2::Types::IncompleteGameData* pGame) { }
bool Feature::windowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) { return true; }
bool Feature::keyEvent(DWORD keyCode, bool down, DWORD flags) { return true; }
bool Feature::chatInput(InputStream msg) { return true; }
void Feature::gameUnitPreDraw() { }
void Feature::gameUnitPostDraw() { }
void Feature::gameAutomapPreDraw() { }
void Feature::gameAutomapPostDraw() { }
void Feature::gamePostDraw() { }
void Feature::oogPostDraw() { }
void Feature::allPostDraw() { }
void Feature::allFinalDraw() { }
void Feature::preDraw() { }
void Feature::roomInit(D2::Types::IncompleteGameData* pGame, D2::Types::Room1* pRoom1) { }
void Feature::serverExpAward(DWORD exp, Ghidra::D2UnitStrc* pUnit, Ghidra::D2GameStrc* pGame) { }
void Feature::valueFromServer(D2::Types::LivingUnit* unit, int value, char color) { }
void Feature::serverGetCustomData(int clientId, char* pBytes, int nSize) { }
void Feature::clientGetCustomData(char* pBytes, int nSize) { }
Task::Task<> Feature::async() {co_return;}