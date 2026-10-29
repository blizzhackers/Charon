#pragma once

#include <windows.h>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "opengl_device.h"

namespace CharonSettings {
    using Values = std::unordered_map<std::string, DWORD>;
    struct Definition { const char* key; DWORD initial, minimum, maximum; };
    inline constexpr Definition Definitions[] = {
        {"revealLevel",0,0,1}, {"showMonsters",0,0,1}, {"showItems",0,0,1},
        {"showMissiles",0,0,1}, {"disableRoofs",0,0,1}, {"infoPopups",0,0,1},
        {"regenMap",0,0,1}, {"itemInfo",0,0,1}, {"noPickup",0,0,1},
        {"preventSockets",0,0,1}, {"altToggle",0,0,1}, {"respecOverride",0,0,1},
        {"imbueOverride",0,0,1}, {"socketOverride",0,0,1}, {"cowsOverride",0,0,1},
        {"disableShake",0,0,1}, {"disableSplash",0,0,1}, {"ladderItems",0,0,1},
        {"rebalanceDrops",0,0,1}, {"itemQOL",0,0,1}, {"drawAllStates",0,0,1},
        {"omnivision",0,0,1}, {"disableWeather",0,0,1}, {"debugMode",0,0,1},
        {"debugModeWalls",0,0,1}, {"debugPackets",0,0,1}, {"debugModeType",0,0,2},
        {"xpBonus",1,1,16384}, {"xpMin",0,0,8250},
        // The sale timer is multiplied by 25 before use as a signed random bound.
        {"sojSaleTime",150,1,MAXLONG / 25}, {"TargetFPS",25,1,MAXLONG},
        {"PlayerCountOverride",1,1,MAXDWORD}, {"sojCount",0,0,MAXDWORD}, {"sojLimit",0,0,MAXDWORD},
        {"openGLFullscreen",1,0,1}, {"openGLFilter",0,0,static_cast<DWORD>(CharonGL::Filter::Count)-1},
        {"openGLFilterVersion",2,0,MAXDWORD},
    };

    inline DWORD Bounded(const std::string& key, DWORD value) {
        for (const auto& definition : Definitions)
            if (key == definition.key)
                return value < definition.minimum ? definition.initial : (std::min)(value, definition.maximum);
        return value;
    }

    inline Values Defaults() {
        Values values;
        for (const auto& definition : Definitions) values.emplace(definition.key, definition.initial);
        return values;
    }

    inline std::string Trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r");
        return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    inline Values Load(std::istream& file) {
        Values values = Defaults();
        DWORD filterVersion = 0;
        std::string line;
        while (std::getline(file, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = Trim(line.substr(0, colon));
            const std::string number = Trim(line.substr(colon + 1));
            if (key.empty() || number.empty()) continue;
            DWORD value = 0;
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
            if (parsed.ec != std::errc() || parsed.ptr != number.data() + number.size()) continue;
            // Keep unknown numeric settings for forward compatibility.
            values[key] = value;
            if (key == "openGLFilterVersion") filterVersion = value;
        }
        if (filterVersion < 2)
            values["openGLFilter"] = static_cast<DWORD>(CharonGL::MigrateLegacyFilter(values["openGLFilter"], filterVersion));
        values["openGLFilterVersion"] = (std::max)(2ul, filterVersion);
        for (auto& value : values) value.second = Bounded(value.first, value.second);
        return values;
    }

    inline bool Save(const std::filesystem::path& path, const Values& values) {
        std::ostringstream text;
        for (const auto& value : values) {
            if (value.first.empty() || value.first.find_first_of(":\r\n") != std::string::npos) return false;
            text << value.first << ": " << Bounded(value.first, value.second) << '\n';
        }
        if (!text) return false;
        const std::string bytes = text.str();
        if (bytes.size() > MAXDWORD) return false;
        auto directory = path.parent_path();
        if (directory.empty()) directory = L".";
        wchar_t temporary[MAX_PATH];
        if (!GetTempFileNameW(directory.c_str(), L"chr", 0, temporary)) return false;
        HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool success = false;
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                && written == bytes.size() && FlushFileBuffers(file);
            if (!CloseHandle(file)) success = false;
        }
        // The old file stays intact unless the complete temporary file can replace it.
        if (success) success = MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!success) DeleteFileW(temporary);
        return success;
    }
}
