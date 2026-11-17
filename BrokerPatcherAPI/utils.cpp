#include "pch.h"
#include "utils.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <iostream>
#include <string>
#include <chrono>

const char* LOG_PATH = "AMS2Broker.log";

static std::mutex g_logMutex;
static uintptr_t g_moduleBase = 0;

std::mutex g_tokenMutex;
std::string g_AuthTokenCache;
bool g_AuthTokenLoaded = false;

std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

void InitLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    FILE* f = nullptr;
    fopen_s(&f, LOG_PATH, "w");
    if (f) {
        fprintf(f, "[HTTP] %s Server gestartet.\n", GetCurrentTimestamp().c_str());
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

std::string ReadAuthTokenFromSharedMemory()
{
    const wchar_t* kSharedMemName = L"Local\\AMS2Launcher_AuthToken";

    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMemName);
    if (!hMap) return "";

    void* pView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pView) {
        CloseHandle(hMap);
        return "";
    }

    std::string token(static_cast<const char*>(pView));
    UnmapViewOfFile(pView);
    CloseHandle(hMap);
    return token;
}

std::string GetHeaderValue(const std::string& headers, const std::string& name)
{
    std::string lower = headers;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);

    std::string needle = name;
    for (auto& c : needle) c = (char)tolower((unsigned char)c);
    needle += ":";

    size_t pos = 0;
    while ((pos = lower.find(needle, pos)) != std::string::npos) {
        if (pos == 0 || (pos >= 2 && lower.compare(pos - 2, 2, "\r\n") == 0)) {
            size_t valueStart = pos + needle.size();
            while (valueStart < headers.size() &&
                (headers[valueStart] == ' ' || headers[valueStart] == '\t'))
                ++valueStart;

            size_t valueEnd = headers.find("\r\n", valueStart);
            if (valueEnd == std::string::npos) valueEnd = headers.size();

            while (valueEnd > valueStart &&
                (headers[valueEnd - 1] == ' ' || headers[valueEnd - 1] == '\t'))
                --valueEnd;

            return headers.substr(valueStart, valueEnd - valueStart);
        }
        pos += needle.size();
    }
    return "";
}

bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string GetAuthToken()
{
    std::lock_guard<std::mutex> lock(g_tokenMutex);
    if (!g_AuthTokenLoaded) {
        g_AuthTokenCache = ReadAuthTokenFromSharedMemory();
        g_AuthTokenLoaded = true;
        Log("[AUTH] Token geladen (%zu Zeichen).\n", g_AuthTokenCache.size());
    }
    return g_AuthTokenCache;
}

bool CheckAuth(const std::string& headers)
{
    std::string expected = GetAuthToken();
    if (expected.empty()) {
        Log("[AUTH] No Token available - blocking all Requests.\n");
        return false;
    }

    std::string got = GetHeaderValue(headers, "X-Auth-Token");
    if (got.empty()) {
        Log("[AUTH] Request missing X-Auth-Token.\n");
        return false;
    }

    bool ok = ConstantTimeEquals(got, expected);
    if (!ok) {
        Log("[AUTH] Token-Mismatch.\n");
    }
    return ok;
}