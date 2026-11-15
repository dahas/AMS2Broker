#include "Config.h"
#include <windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_map>

namespace fs = std::filesystem;

int g_ServerPort = 58888;
int g_HotkeyApply = VK_INSERT;
int g_HotkeyLauncher = VK_HOME;
int g_HotkeyQuit = VK_END;

static const std::unordered_map<std::string, int> vkMap = {
    { "INSERT", VK_INSERT },{ "DELETE", VK_DELETE },
    { "HOME", VK_HOME }, { "END", VK_END },{ "SPACE", VK_SPACE },
    { "TAB", VK_TAB },{ "ESCAPE", VK_ESCAPE },
    { "F1", VK_F1 },{ "F2", VK_F2 },{ "F3", VK_F3 },{ "F4", VK_F4 },
    { "F5", VK_F5 },{ "F6", VK_F6 },{ "F7", VK_F7 },{ "F8", VK_F8 },
    { "F9", VK_F9 },{ "F10", VK_F10 },{ "F11", VK_F11 },{ "F12", VK_F12 },
    { "LEFT", VK_LEFT },{ "RIGHT", VK_RIGHT },
    { "UP", VK_UP },{ "DOWN", VK_DOWN },
    { "PAGEUP", VK_PRIOR },{ "PAGEDOWN", VK_NEXT },
    { "NUM0", VK_NUMPAD0 },{ "NUM1", VK_NUMPAD1 },{ "NUM2", VK_NUMPAD2 },
    { "NUM3", VK_NUMPAD3 },{ "NUM4", VK_NUMPAD4 },{ "NUM5", VK_NUMPAD5 },
    { "NUM6", VK_NUMPAD6 },{ "NUM7", VK_NUMPAD7 },{ "NUM8", VK_NUMPAD8 },
    { "NUM9", VK_NUMPAD9 },{ "NUM_ADD", VK_ADD },{ "NUM_SUB", VK_SUBTRACT },
};

static fs::path GetConfigPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    return fs::path(exePath).parent_path().parent_path() / L"config.ini";
}

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static int ParseVirtualKey(const std::string& token) {
    std::string upper = token;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    auto it = vkMap.find(upper);
    if (it != vkMap.end()) {
        return it->second;
    }

    if (token.size() == 1) {
        return VkKeyScanA(token[0]) & 0xFF;
    }

    return 0;
}

void LoadServerConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open()) return;

    std::string currentSection = "";
    std::string line;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        if (currentSection == "server") {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = Trim(line.substr(0, eq));
                std::string val = Trim(line.substr(eq + 1));
                if (key == "port" && !val.empty()) {
                    g_ServerPort = std::stoi(val);
                }
            }
        }
    }
}

void LoadHotkeyConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open()) return;

    std::string currentSection = "";
    std::string line;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        if (currentSection == "hotkeys") {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = Trim(line.substr(0, eq));
                std::string val = Trim(line.substr(eq + 1));

                int vk = ParseVirtualKey(val);
                if (vk != 0) {
                    if (key == "apply_patch")       g_HotkeyApply = vk;
                    if (key == "launcher_up_front") g_HotkeyLauncher = vk;
                    if (key == "quit_game")         g_HotkeyQuit = vk;
                }
            }
        }
    }
}

void LoadConfiguration() {
    fs::path configPath = GetConfigPath();
    if (!fs::exists(configPath)) {
        std::cout << "[Warning] config.ini not found at: " << configPath.string() << "\n-> Using default settings." << std::endl;
        return;
    }

    LoadServerConfig();
    LoadHotkeyConfig();
}

std::string GetKeyName(int virtualKey) {
    for (const auto& pair : vkMap) {
        if (pair.second == virtualKey) {
            return pair.first;
        }
    }

    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return std::string(1, (char)virtualKey);
    }

    return "UNKNOWN";
}
