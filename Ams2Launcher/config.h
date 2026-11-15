#pragma once

#include <string>

extern int g_ServerPort;
extern int g_HotkeyApply;
extern int g_HotkeyLauncher;
extern int g_HotkeyQuit;

void LoadServerConfig();
void LoadHotkeyConfig();
void LoadConfiguration();
std::string GetKeyName(int virtualKey);
