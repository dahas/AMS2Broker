#pragma once
#include "pch.h"
#include <windows.h>
#include <cstdint>
#include <string>

extern const char* LOG_PATH;

void InitLog();

void Log(const char* fmt, ...);

uintptr_t ModuleBase();
uintptr_t At(uintptr_t rva);
uintptr_t RvaFromAbsolute(uintptr_t absoluteAddress);

bool SafeRead(uintptr_t addr, void* out, size_t size);
bool SafeWrite32(uintptr_t addr, int value);
bool PatchInt(uintptr_t address, int value, int* before, int* after);
