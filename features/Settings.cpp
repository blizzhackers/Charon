#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/dialog.h"
#include "headers/opengl_game.h"
#include <unordered_map>
#include <functional>
#include <fstream>
#include <string>
#include <cmath>
#include "headers/settings_data.h"

REMOTEFUNC(void __stdcall, GAME_SetFrameRate, (int32_t param_1), 0x52df80);

wchar_t settingsPath[512] = { 0 };

void LoadSettings() {
    std::ifstream file(settingsPath);
    Settings = CharonSettings::Load(file);
}

void SaveSettings() {
    if (!CharonSettings::Save(settingsPath, Settings))
        gamelog << "Could not save settings; the previous file was preserved." << std::endl;
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

DialogToggleInfo* BooleanSetting(const wchar_t* label, const char* key) {
    return new DialogToggleInfo(label,
        [key]() -> std::wstring {
            return Settings.at(key) ? L"\u00FFc2On" : L"\u00FFc1Off";
        }, [key](MouseButton button, bool down) {
            if (down) return;
            Settings[key] = !Settings.at(key);
            SaveSettings();
        });
}

std::vector<std::vector<DialogToggleInfo*>> SettingsColumns = {
    { // First Column
        BooleanSetting(L"Reveal Map", "revealLevel"),
        BooleanSetting(L"Show Monsters", "showMonsters"),
        BooleanSetting(L"Show Items", "showItems"),
        BooleanSetting(L"Show Missiles", "showMissiles"),
        BooleanSetting(L"Disable Rooftops", "disableRoofs"),
        BooleanSetting(L"Info Popups", "infoPopups"),
        nullptr, // Empty Gap
        BooleanSetting(L"Regen Single Player Maps", "regenMap"),
        BooleanSetting(L"Show Extra Item Info", "itemInfo"),
        BooleanSetting(L"Default /nopickup", "noPickup"),
        BooleanSetting(L"Prevent socketing", "preventSockets"),
        BooleanSetting(L"Lock Alt Items", "altToggle"),
        nullptr, // Empty Gap
        BooleanSetting(L"Repeatable Respec Quest", "respecOverride"),
        BooleanSetting(L"Repeatable Imbue Quest", "imbueOverride"),
        BooleanSetting(L"Repeatable Socket Quest", "socketOverride"),
        BooleanSetting(L"Repeatable Cow Portal", "cowsOverride"),
        nullptr, // Empty Gap
        BooleanSetting(L"Disable screen shake", "disableShake"),
        BooleanSetting(L"Disable splash on start", "disableSplash"),
    }, { // Second Column
        BooleanSetting(L"\u00FFc3*\u00FFc4Enable Ladder Features", "ladderItems"),
        new DialogToggleInfo(L"SOJ Sale Time (seconds)",
            []() -> std::wstring {
                return Settings.at("sojSaleTime") == 150 ? L"\u00FFc2~150" : (L"\u00FFc4~" + std::to_wstring(Settings.at("sojSaleTime")));
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                
                if (button == MouseButton::LEFT) {
                    Settings["sojSaleTime"] = CharonSettings::Bounded("sojSaleTime", Settings["sojSaleTime"] + (Settings["sojSaleTime"] < 15 ? 1 : 15));
                }
                else if (button == MouseButton::RIGHT && Settings["sojSaleTime"] > 1) {
                    Settings["sojSaleTime"] -= Settings["sojSaleTime"] <= 15 ? 1 : 15;
                }

                SaveSettings();
            }),
        BooleanSetting(L"\u00FFc3*\u00FFc4Tweak Drops for Solo", "rebalanceDrops"),
        BooleanSetting(L"\u00FFc4Item QoL Fixes", "itemQOL"),
        new DialogToggleInfo(L"XP Bonus",
            []() -> std::wstring {
                if (Settings.at("xpBonus") == 3) {
                    return L"\u00FFc2x3";
                }
                else if (Settings.at("xpBonus") >= 2) {
                    return L"\u00FFc4x" + std::to_wstring(Settings.at("xpBonus"));
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
                        Settings["xpBonus"] = CharonSettings::Bounded("xpBonus", Settings["xpBonus"] << 1);
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
                if (Settings.at("xpMin") > 8000) {
                    return L"\u00FFc4MAX";
                } else {
                    return L"\u00FFc3" + std::to_wstring(Settings.at("xpMin"));
                }
            }, [](MouseButton button, bool down) -> void {
                if (down) return;

                if (button == MouseButton::LEFT && Settings["xpMin"] <= 8000) {
                    Settings["xpMin"] = CharonSettings::Bounded("xpMin", Settings["xpMin"] + 250);
                } else if (button == MouseButton::RIGHT && Settings["xpMin"] > 0) {
                    Settings["xpMin"] = Settings["xpMin"] > 250 ? Settings["xpMin"] - 250 : 0;
                }

                SaveSettings();
            }),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Display Mode",
            []() -> std::wstring {
                return Settings.at("openGLFullscreen") ? L"\u00FFc4Fullscreen" : L"\u00FFc4Windowed";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                CharonGL::ToggleFullscreen();
            }),
        new DialogToggleInfo(L"OpenGL Filter",
            []() -> std::wstring {
                const wchar_t* names[] = {L"Nearest", L"Bilinear + CAS", L"Jinc2", L"xBRZ", L"ScaleFX", L"CRT", L"CRT Curved", L"LCD"};
                static_assert(std::size(names) == static_cast<unsigned>(CharonGL::Filter::Count), "Filter labels match saved indices");
                return L"\u00FFc4" + std::wstring(names[Settings.at("openGLFilter") % static_cast<unsigned>(CharonGL::Filter::Count)]);
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                Settings["openGLFilter"] = (Settings["openGLFilter"] + 1) % static_cast<unsigned>(CharonGL::Filter::Count);
                SaveSettings();
            }),
        BooleanSetting(L"Draw all states", "drawAllStates"),
        BooleanSetting(L"Omnivision", "omnivision"),
        nullptr, // Empty Gap
        new DialogToggleInfo(L"Draw Color Swatch",
            []() -> std::wstring {
                return State.find("drawSwatch") != State.end() && State.at("drawSwatch") ? L"\u00FFc2On" : L"\u00FFc1Off";
            }, [](MouseButton button, bool down) -> void {
                if (down) return;
                State["drawSwatch"] = !State["drawSwatch"];
                SaveSettings();
            }),
        BooleanSetting(L"Disable Weather Effects", "disableWeather"),
        BooleanSetting(L"Debug Mode", "debugMode"),
        new DialogToggleInfo(L"Debug Mode Type",
            []() -> std::wstring {
                switch (Settings.at("debugModeType")) {
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
        BooleanSetting(L"Show Walls in Debug", "debugModeWalls"),
        BooleanSetting(L"Debug Packets", "debugPackets"),
        new DialogToggleInfo(L"Target Single Player FPS",
            []() -> std::wstring {
                return L"\u00FFc4" + std::to_wstring(Settings.at("TargetFPS"));
            }, [](MouseButton button, bool down) -> void {
                if (down) return;

                if (button == MouseButton::LEFT) {
                    Settings["TargetFPS"] = CharonSettings::Bounded("TargetFPS", Settings["TargetFPS"] + 5);
                }
                else if (button == MouseButton::RIGHT && Settings["TargetFPS"] > 5) {
                    Settings["TargetFPS"] -= 5;
                }

                GAME_SetFrameRate(Settings["TargetFPS"]);

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
        const static ASMPTR UI_GetFlag = 0x453a90;
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
                CALL    UI_GetFlag    // eax = UI_GetFlag(9)
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

            D2::gbSuppressItemSelection = Settings["noPickup"];

            GAME_SetFrameRate(Settings["TargetFPS"]);

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
