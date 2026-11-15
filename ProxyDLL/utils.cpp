#include "pch.h"
#include "utils.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

const char* LOG_PATH = "C:\\Users\\Public\\ams2_broker.log";

static std::mutex g_logMutex;
static uintptr_t g_moduleBase = 0;

void InitLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* f = nullptr;
    fopen_s(&f, LOG_PATH, "w");
    if (f) {
        fprintf(f, "[HTTP] Server gestartet.\n");
        fclose(f);
    }
}

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* f = nullptr;
    fopen_s(&f, LOG_PATH, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

uintptr_t ModuleBase() {
    if (!g_moduleBase) {
        g_moduleBase = (uintptr_t)GetModuleHandleA(NULL);
        Log("[INIT] moduleBase = 0x%llX\n", (unsigned long long)g_moduleBase);
    }
    return g_moduleBase;
}

uintptr_t At(uintptr_t rva) {
    return ModuleBase() + rva;
}

uintptr_t RvaFromAbsolute(uintptr_t absoluteAddress) {
    const uintptr_t ORIGINAL_BASE = 0x140000000;
    return absoluteAddress - ORIGINAL_BASE;
}

int GetServerPort(int defaultPort) {
    wchar_t dllPath[MAX_PATH];
    HMODULE hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetServerPort, &hModule);
    GetModuleFileNameW(hModule, dllPath, MAX_PATH);

    std::wstring path(dllPath);
    path = path.substr(0, path.find_last_of(L"\\/") + 1);
    path = path.substr(0, path.find_last_of(L"\\/", path.size() - 2) + 1);
    path += L"config.ini";

    UINT port = GetPrivateProfileIntW(L"server", L"port", defaultPort, path.c_str());
    if (port == 0 || port > 65535) return defaultPort;
    return static_cast<int>(port);
}

bool SafeRead(uintptr_t addr, void* out, size_t size) {
    __try {
        memcpy(out, (void*)addr, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWrite32(uintptr_t addr, int value) {
    __try {
        *(volatile int*)addr = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool PatchInt(uintptr_t address, int value, int* before, int* after) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery((void*)address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;

    DWORD oldProtect;
    if (!VirtualProtect((void*)address, sizeof(int), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    int b = -1, a = -1;
    SafeRead(address, &b, sizeof(b));
    bool ok = SafeWrite32(address, value);
    SafeRead(address, &a, sizeof(a));
    VirtualProtect((void*)address, sizeof(int), oldProtect, &oldProtect);

    if (before) *before = b;
    if (after) *after = a;
    return ok && a == value;
}