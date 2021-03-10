#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/dialog.h"
#include <unordered_map>
#include <functional>
#include <fstream>
#include <string>
#include <cmath>

wchar_t settingsPath[512] = { 0 };

void LoadSettings() {
    std::ifstream file(settingsPath);
    std::string key;
    DWORD value;

    while (file) {
        std::getline(file, key, ':');
        file >> value;
        if (key.length()) {
            Settings[key] = value;
        }
        std::getline(file, key);
    }
}

void SaveSettings() {
    std::ofstream file(settingsPath);

    for (std::pair<std::string, DWORD> element : Settings) {
        if (element.first.length()) {
            file << element.first << ": " << element.second << std::endl;
        }
    }
}

struct DialogToggleInfo {
    std::wstring label;
    std::function<std::wstring()> valuecallback;
    std::function<void(MouseButton button, bool down)> clickhandler;
    DialogToggleInfo(std::wstring label, std::function<std::wstring()> valuecallback, std::function<void(MouseButton button, bool down)> clickhandler) {
        this->label = label;
        this->valuecallback = valuecallback;
        this->clickhandler = clickhandler;
    }
};

std::vector<std::vector<DialogToggleInfo*>> SettingsColumns = {
    { // First Column
        new DialogToggleInfo(L"Reveal Map",
            []() -> std::wstring {
                return Settings["revealLevel"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["revealLevel"] = !Settings["revealLevel"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Show Monsters",
            []() -> std::wstring {
                return Settings["showMonsters"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["showMonsters"] = !Settings["showMonsters"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Show Items",
            []() -> std::wstring {
                return Settings["showItems"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["showItems"] = !Settings["showItems"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Show Missiles",
            []() -> std::wstring {
                return Settings["showMissiles"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["showMissiles"] = !Settings["showMissiles"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Disable Rooftops",
            []() -> std::wstring {
                return Settings["disableRoofs"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["disableRoofs"] = !Settings["disableRoofs"];
                SaveSettings();
            }),
        nullptr, // Empty Gap
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Regen Single Player Maps",
            []() -> std::wstring {
                return Settings["regenMap"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["regenMap"] = !Settings["regenMap"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Show Extra Item Info",
            []() -> std::wstring {
                return Settings["itemInfo"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["itemInfo"] = !Settings["itemInfo"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Default /nopickup",
            []() -> std::wstring {
                return Settings["noPickup"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["noPickup"] = !Settings["noPickup"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Prevent socketing",
            []() -> std::wstring {
                return Settings["preventSockets"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["preventSockets"] = !Settings["preventSockets"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Lock Alt Items",
            []() -> std::wstring {
                return Settings["altToggle"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["altToggle"] = !Settings["altToggle"];
                SaveSettings();
            }),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Repeatable Respec Quest",
            []() -> std::wstring {
                return Settings["respecOverride"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["respecOverride"] = !Settings["respecOverride"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Repeatable Imbue Quest",
            []() -> std::wstring {
                return Settings["imbueOverride"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["imbueOverride"] = !Settings["imbueOverride"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Repeatable Socket Quest",
            []() -> std::wstring {
                return Settings["socketOverride"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["socketOverride"] = !Settings["socketOverride"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Repeatable Cow Portal",
            []() -> std::wstring {
                return Settings["cowsOverride"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["cowsOverride"] = !Settings["cowsOverride"];
                SaveSettings();
            }),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Disable splash on start",
            []() -> std::wstring {
                return Settings["disableSplash"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["disableSplash"] = !Settings["disableSplash"];
                SaveSettings();
            }),
    }, { // Second Column
        new DialogToggleInfo(L"\u00FFc3*\u00FFc4Enable Ladder Items",
            []() -> std::wstring {
                return Settings["ladderItems"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["ladderItems"] = !Settings["ladderItems"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"\u00FFc3*\u00FFc4Tweak Drops for Solo",
            []() -> std::wstring {
                return Settings["rebalanceDrops"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["rebalanceDrops"] = !Settings["rebalanceDrops"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"\u00FFc4Item QoL Fixes",
            []() -> std::wstring {
                return Settings["itemQOL"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["itemQOL"] = !Settings["itemQOL"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"XP Bonus",
            []() -> std::wstring {
                if (Settings["xpBonus"] < 2) {
                    Settings["xpBonus"] = 1;
                }

                if (Settings["xpBonus"] == 3) {
                    return L"\u00FFc2x3";
                }
                else if (Settings["xpBonus"] >= 2) {
                    return L"\u00FFc4x" + std::to_wstring(Settings["xpBonus"]);
                } else {
                    return L"\u00FFc1Off";
                }
            }, [](MouseButton button, bool down) -> void {
                if (down) return;

                if (button == MouseButton::LEFT && Settings["xpBonus"] < 1024 * 16) {
                    if (Settings["xpBonus"] < 4) {
                        Settings["xpBonus"]++;
                    }
                    else {
                        Settings["xpBonus"] <<= 1;
                    }
                } else if (button == MouseButton::RIGHT && Settings["xpBonus"] > 1) {
                    if (Settings["xpBonus"] <= 4) {
                        Settings["xpBonus"]--;
                    }
                    else {
                        Settings["xpBonus"] >>= 1;
                    }
                }

                SaveSettings();
            }),
        new DialogToggleInfo(L"XP Min Award",
            []() -> std::wstring {
                if (Settings["xpMin"] > 8000) {
                    return L"\u00FFc4MAX";
                } else {
                    return L"\u00FFc3" + std::to_wstring(Settings["xpMin"]);
                }
            }, [](MouseButton button, bool down) -> void {
                if (down) return;

                if (button == MouseButton::LEFT && Settings["xpMin"] <= 8000) {
                    Settings["xpMin"] += 250;
                } else if (button == MouseButton::RIGHT && Settings["xpMin"] > 0) {
                    Settings["xpMin"] -= 250;
                }

                SaveSettings();
            }),
        new DialogToggleInfo(L"Info Popups",
            []() -> std::wstring {
                return Settings["infoPopups"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["infoPopups"] = !Settings["infoPopups"];
                SaveSettings();
            }),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"\u00FFc;*\u00FFc4Always D3D Mode",
            []() -> std::wstring {
                return Settings["alwaysD3D"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["alwaysD3D"] = !Settings["alwaysD3D"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"AD3D Filter",
            []() -> std::wstring {
                std::wstring ret = Settings["alwaysD3DFilter"] ? L"\u00FFc8" : L"\u00FFc5";
                ret += filterParams[Settings["alwaysD3DFilter"]];
                return ret;
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["alwaysD3DFilter"] = (Settings["alwaysD3DFilter"] + 1) % 6;
                SaveSettings();
            }),
        new DialogToggleInfo(L"AD3D Start Fullscreen",
            []() -> std::wstring {
                return Settings["alwaysD3DStartFull"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["alwaysD3DStartFull"] = !Settings["alwaysD3DStartFull"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Draw all states",
             []() -> std::wstring {
                 return Settings["drawAllStates"] ? L"\u00FFc2On" : L"\u00FFc1Off";
             }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["drawAllStates"] = !Settings["drawAllStates"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Omnivision",
            []() -> std::wstring {
                return Settings["omnivision"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["omnivision"] = !Settings["omnivision"];
                SaveSettings();
            }),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Draw Color Swatch",
            []() -> std::wstring {
                return State["drawSwatch"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                State["drawSwatch"] = !State["drawSwatch"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Disable Weather Effects",
            []() -> std::wstring {
                return Settings["disableWeather"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["disableWeather"] = !Settings["disableWeather"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Debug Mode",
            []() -> std::wstring {
                return Settings["debugMode"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["debugMode"] = !Settings["debugMode"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Debug Mode Type",
            []() -> std::wstring {
                switch (Settings["debugModeType"]) {
                case DebugMode::NORMAL:
                    return L"\u00FFc5Normal";
                case DebugMode::DARK:
                    return L"\u00FFc3Dark";
                case DebugMode::HIDDEN:
                default:
                    return L"\u00FFc1Hidden";
                }
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["debugModeType"] = (Settings["debugModeType"] + 1) % 3;
                SaveSettings();
            }),
        new DialogToggleInfo(L"Show Walls in Debug",
            []() -> std::wstring {
                return Settings["debugModeWalls"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["debugModeWalls"] = !Settings["debugModeWalls"];
                SaveSettings();
            }),
        new DialogToggleInfo(L"Debug Packets",
            []() -> std::wstring {
                return Settings["debugPackets"] ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["debugPackets"] = !Settings["debugPackets"];
                SaveSettings();
            }),
    },
};

namespace SettingsFeature {
    Element* dialog;

    const static int __fastcall shouldPause() {
        return dialog->isVisible() || State["paused"];
    }

    int __declspec(naked) pauseGameIntercept() {
        const static ASMPTR GetUiFlagWrapper = 0x453a90;
        const static ASMPTR Continue = 0x44efed;
        const static ASMPTR PauseGame = 0x44f00a;
        __asm {


                CALL shouldPause
                CMP EAX, 1
                JNE original
                JMP PauseGame

            original:
                // Original code
                MOV     ECX, 0x9
                CALL    GetUiFlagWrapper    // eax = GetUiFlagWrapper(9)
                JMP     Continue;


        }
    }

    class : public Feature {
    public:
        void init() {
            int inset = 5, width = 200, height = 15, top = 28, font = 0, dialogWidth = 0, dialogHeight = top + 2 * height;

            for (std::vector<DialogToggleInfo*>& SettingsColumn : SettingsColumns) {
                dialogHeight = max(dialogHeight, top + ((int)SettingsColumn.size() + 2) * height);
                dialogWidth += width;
            }

            dialog = new Dialog();
            dialog->setPos(-dialogWidth / 2, -dialogHeight / 2);
            dialog->setDimensions(dialogWidth, dialogHeight);
            // bind the enter key to the OK button
            dialog->onKey([](DWORD keyCode, bool down, DWORD flags) -> bool {
                return false; // Block keypresses to D2 and dialogs behind this one (windowMessage and keyEvent still work regardless)
            });

            TextElement* dlabel = new TextElement();
            dlabel->setPos(0, 0); dlabel->setTextOffset(0, -6); dlabel->setDimensions(dialogWidth, 25); dlabel->setFrame(0x2, 0xFF, 0xD); dlabel->setFont(5); dlabel->setOrientation(Orientation::CENTER); dlabel->setText(L"Charon Settings"); dlabel->show(); dialog->addChild(dlabel);

            Element* close = new Element();
            int xsize = 13, xinset = 6;
            close->setPos(dialogWidth - xinset - xsize, xinset); close->setDimensions(xsize, xsize); close->setFrame(0, 0xFF, 0xD); close->show(); dialog->addChild(close);
            close->addLine({ 0, 0 }, { xsize, xsize }, 0xD);
            close->addLine({ 0, xsize }, { xsize, 0 }, 0xD);
            close->getLinesCallback([&](std::vector<LineInfo> lines) -> std::vector<LineInfo> {
                // Example callback; Since 'lines' contains all the data from addLine,
                // we can modify it or return a completely new vector here.
                return lines;
                });

            close->onClick([&](MouseButton button, bool down) -> void {
                dialog->hide();
                });

            TextElement* caption = new TextElement();
            caption->setPos(0, dialogHeight - height); caption->setDimensions(400, height); caption->setTextOffset(0, -4); caption->setFont(font); caption->setText(L"\u00FFc3*\u00FFc4Requires New Game, \u00FFc;*\u00FFc4Requires Restart"); caption->setOrientation(Orientation::CENTER); caption->show(); dialog->addChild(caption);

            int x = inset, y;

            for (std::vector<DialogToggleInfo*>& SettingsColumn : SettingsColumns) {
                y = top;
                for (DialogToggleInfo* setting : SettingsColumn) {
                    if (setting != nullptr) {
                        TextElement* elem;
                        Element* group = new Element();
                        group->setPos(x, y); group->setDimensions(width - inset * 2, height); group->show(); dialog->addChild(group);
                        elem = new TextElement(); elem->setTextOffset(0, -4); elem->setDimensions(width - inset * 2, height); elem->setFont(font); elem->setText(setting->label); elem->show(); group->addChild(elem);
                        elem = new TextElement(); elem->setTextOffset(0, -4); elem->setDimensions(width - inset * 2, height); elem->setFont(font); elem->setOrientation(Orientation::RIGHT); elem->show(); group->addChild(elem);
                        elem->setText(setting->valuecallback);
                        group->onClick(setting->clickhandler);
                    }
                    y += height;
                }
                x += width;
            }

            D2::NoPickUp = Settings["noPickup"];

            // Pause the game like with the normal esc menu
            MemoryPatch(0x44efe3) << JUMP(pauseGameIntercept);
        }

        bool keyEvent(DWORD keyCode, bool down, DWORD flags) {
            switch (keyCode) {
            case VK_ESCAPE:
                if (dialog->isVisible()) {
                    if (down) {
                        dialog->hide();
                        return false;
                    }
                }

                break;
            case VK_F11:
                dialog->setVisibile(!dialog->isVisible());
                return false;
            }

            return true;
        }

    } feature;
}