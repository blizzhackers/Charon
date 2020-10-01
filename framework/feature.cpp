#include "headers/common.h"
#include "../headers/feature.h"
#include <iostream>

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
