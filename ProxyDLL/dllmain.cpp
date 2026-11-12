#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "pch.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

const int SERVER_PORT = 58888;

const uintptr_t CAR_ADDRESS = 0x1818C41F4;
const uintptr_t LIVERY_ADDRESS = 0x1818C4764;
const uintptr_t TRACK_ADDRESS = 0x1818C1674;
const uintptr_t PRACTICE_ADDRESS = 0x1817BD934;
const uintptr_t QUALIFYING_ADDRESS = 0x1817CA454;
const uintptr_t DURATION_TYPE_ADDRESS = 0x1818AE1D4;
const uintptr_t LAP_ADDRESS = 0x1818AE3A4;

const bool USE_CAR_HOOK = false;
const uintptr_t CAR_HOOK_RVA = 0x3FD3D1;

const uintptr_t CLASS_ROOT = 0x1818C05C0;
const uintptr_t CLASS_OFF1 = 0x130;
const uintptr_t CLASS_OFF2 = 0xA8;
const int CLASS_STRIDE = 0x930;
const int CLASS_FLAG_OFFSET = 0x52C;
const int MAX_CLASSES = 89;

const size_t MAX_REQUEST_SIZE = 64 * 1024;
const char* LOG_PATH = "C:\\Users\\Public\\ams2_broker.log";

static std::mutex g_logMutex;
static std::mutex g_hookMutex;
static uintptr_t g_forceCarAddr = 0;

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

static bool SafeRead(uintptr_t addr, void* out, size_t size) {
    __try {
        memcpy(out, (void*)addr, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeWrite32(uintptr_t addr, int value) {
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

bool InstallCarHook() {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    if (g_forceCarAddr) return true;

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    uintptr_t site = base + CAR_HOOK_RVA;

    static const uint8_t orig[7] = { 0x41, 0x8B, 0xD4, 0x48, 0x89, 0x45, 0x40 };
    uint8_t cur[7] = {};
    if (!SafeRead(site, cur, sizeof(cur)) || memcmp(cur, orig, sizeof(orig)) != 0) {
        Log("[CAR] Originalbytes an 0x%llX stimmen nicht, Hook nicht installiert.\n", (unsigned long long)site);
        return false;
    }

    uintptr_t lo = base > 0x70000000ULL ? base - 0x70000000ULL : 0x10000;
    uint8_t* cave = nullptr;
    for (uintptr_t a = base - 0x10000; a > lo; a -= 0x10000) {
        cave = (uint8_t*)VirtualAlloc((void*)a, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (cave) break;
    }
    if (!cave) {
        Log("[CAR] Kein Speicher in Reichweite des Moduls.\n");
        return false;
    }
    memset(cave, 0xCC, 0x1000);

    uint8_t* p = cave;
    *p++ = 0x83; *p++ = 0x3D;
    *(int32_t*)p = 0x100 - 7; p += 4;
    *p++ = 0x00;
    *p++ = 0x74; *p++ = 0x07;
    *p++ = 0x44; *p++ = 0x8B; *p++ = 0x25;
    *(int32_t*)p = 0x100 - 16; p += 4;
    *p++ = 0x44; *p++ = 0x89; *p++ = 0xE2;
    *p++ = 0x48; *p++ = 0x89; *p++ = 0x45; *p++ = 0x40;
    *p++ = 0xE9;
    *(int32_t*)p = (int32_t)((intptr_t)(site + 7) - (intptr_t)(cave + 28)); p += 4;
    *(int32_t*)(cave + 0x100) = 0;

    FlushInstructionCache(GetCurrentProcess(), cave, 0x1000);

    uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    *(int32_t*)(patch + 1) = (int32_t)((intptr_t)cave - (intptr_t)(site + 5));

    DWORD oldProtect;
    if (!VirtualProtect((void*)site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[CAR] VirtualProtect fehlgeschlagen.\n");
        return false;
    }
    memcpy((void*)site, patch, sizeof(patch));
    VirtualProtect((void*)site, sizeof(patch), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, sizeof(patch));

    g_forceCarAddr = (uintptr_t)(cave + 0x100);
    Log("[CAR] Hook installiert: site=0x%llX cave=0x%llX\n",
        (unsigned long long)site, (unsigned long long)cave);
    return true;
}

static std::string StripComments(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') i++;
        }
        else {
            out += s[i++];
        }
    }
    return out;
}

static std::string Section(const std::string& json, const char* name) {
    std::string quoted = std::string("\"") + name + "\"";
    size_t k = json.find(quoted);
    if (k == std::string::npos) return "";
    size_t b = json.find_first_not_of(" \t\r\n:", k + quoted.size());
    if (b == std::string::npos || json[b] != '{') return "";
    int depth = 0;
    for (size_t i = b; i < json.size(); i++) {
        if (json[i] == '{') depth++;
        else if (json[i] == '}') {
            if (--depth == 0) return json.substr(b + 1, i - b - 1);
        }
    }
    return "";
}

static bool GetInt(const std::string& scope, const char* key, long long& out) {
    size_t k = scope.find(std::string("\"") + key + "\"");
    if (k == std::string::npos) return false;
    size_t c = scope.find(':', k);
    if (c == std::string::npos) return false;
    size_t s = scope.find_first_not_of(" \t\r\n", c + 1);
    if (s == std::string::npos) return false;
    if (scope.compare(s, 4, "true") == 0) { out = 1; return true; }
    if (scope.compare(s, 5, "false") == 0) { out = 0; return true; }
    size_t e = scope.find_first_not_of("-0123456789", s);
    if (e == s) return false;
    try {
        out = std::stoll(scope.substr(s, e == std::string::npos ? std::string::npos : e - s));
    }
    catch (...) {
        return false;
    }
    return true;
}

struct IntSetting {
    const char* section;
    const char* key;
    uintptr_t address;
};

static const IntSetting SETTINGS[] = {
    { "Vehicle",    "Car",           CAR_ADDRESS },
    { "Vehicle",    "Livery",        LIVERY_ADDRESS },
    { "",           "Track",         TRACK_ADDRESS },
    { "Practice",   "Enabled",       PRACTICE_ADDRESS },
    { "Qualifying", "Enabled",       QUALIFYING_ADDRESS },
    { "Race",       "Duration Type", DURATION_TYPE_ADDRESS },
    { "Race",       "Laps",          LAP_ADDRESS },
};

bool ApplySettings(const std::string& json) {
    bool allOk = true;
    for (const auto& s : SETTINGS) {
        if (USE_CAR_HOOK && s.address == CAR_ADDRESS) continue;

        std::string scope = s.section[0] ? Section(json, s.section) : json;
        long long v = 0;
        if (!GetInt(scope, s.key, v)) continue;

        int before = -1, after = -1;
        bool ok = PatchInt(s.address, (int)v, &before, &after);
        Log("[SET] %s.%s ziel=%lld ok=%d vorher=%d nachher=%d\n",
            s.section, s.key, v, ok ? 1 : 0, before, after);
        if (!ok) allOk = false;
    }
    return allOk;
}

bool ApplyCarHook(const std::string& json) {
    if (!USE_CAR_HOOK) return true;
    std::string vehicle = Section(json, "Vehicle");
    long long car = 0;
    if (!GetInt(vehicle, "Car", car)) return true;
    if (!InstallCarHook()) return false;
    bool ok = SafeWrite32(g_forceCarAddr, (int)car);
    Log("[CAR] forceCar=%lld ok=%d\n", car, ok ? 1 : 0);
    return ok;
}

bool ResolveClassBase(uintptr_t& base) {
    uintptr_t p1 = 0, p2 = 0, p3 = 0;
    if (!SafeRead(CLASS_ROOT + CLASS_OFF1, &p1, sizeof(p1)) || !p1) return false;
    if (!SafeRead(p1 + CLASS_OFF2, &p2, sizeof(p2)) || !p2) return false;
    if (!SafeRead(p2, &p3, sizeof(p3)) || !p3) return false;
    base = p3;
    return true;
}

uintptr_t ClassFlagAddress(uintptr_t base, int classNumber) {
    return base + CLASS_FLAG_OFFSET + (uintptr_t)(classNumber - 1) * CLASS_STRIDE;
}

bool EntryIsClass(uintptr_t base, int classNumber) {
    uintptr_t entry = base + (uintptr_t)(classNumber - 1) * CLASS_STRIDE;
    uint64_t head0 = 0, ref0 = 0;
    int flag = -1;
    if (!SafeRead(entry, &head0, sizeof(head0))) return false;
    if (!SafeRead(base, &ref0, sizeof(ref0))) return false;
    if (!SafeRead(entry + CLASS_FLAG_OFFSET, &flag, sizeof(flag))) return false;
    return head0 == ref0 && (flag == 0 || flag == 1);
}

bool ApplyClassesNow(const std::map<int, int>& parsed) {
    uintptr_t base = 0;
    if (!ResolveClassBase(base) || !EntryIsClass(base, 1)) {
        Log("[CLASS] Liste nicht bereit.\n");
        return false;
    }

    bool allOk = true;
    for (const auto& kv : parsed) {
        if (!EntryIsClass(base, kv.first)) {
            Log("[CLASS] Klasse %d nicht in der Liste.\n", kv.first);
            allOk = false;
            continue;
        }
        uintptr_t addr = ClassFlagAddress(base, kv.first);
        int before = -1, after = -1;
        SafeRead(addr, &before, sizeof(before));
        bool ok = SafeWrite32(addr, kv.second);
        SafeRead(addr, &after, sizeof(after));
        Log("[CLASS] Klasse %d ziel=%d ok=%d vorher=%d nachher=%d\n",
            kv.first, kv.second, ok ? 1 : 0, before, after);
        if (!ok || after != kv.second) allOk = false;
    }
    return allOk;
}

bool ParseClassStates(const std::string& json) {
    size_t classPos = json.find("\"ClassStates\"");
    if (classPos == std::string::npos) return true;

    size_t startBrace = json.find('{', classPos);
    if (startBrace == std::string::npos) return true;
    size_t endBrace = json.find('}', startBrace);
    if (endBrace == std::string::npos) return true;

    std::string classStr = json.substr(startBrace + 1, endBrace - startBrace - 1);

    std::map<int, int> parsed;
    size_t pos = 0;

    while (pos < classStr.length()) {
        size_t qs = classStr.find('"', pos);
        if (qs == std::string::npos) break;
        size_t qe = classStr.find('"', qs + 1);
        if (qe == std::string::npos) break;
        std::string key = classStr.substr(qs + 1, qe - qs - 1);

        size_t colon = classStr.find(':', qe);
        if (colon == std::string::npos) break;
        size_t v = classStr.find_first_not_of(" \t\r\n", colon + 1);
        if (v == std::string::npos) break;
        if (classStr[v] == '"') {
            v++;
            if (v >= classStr.length()) break;
        }

        int val = -1;
        size_t consumed = 1;
        if (classStr.compare(v, 4, "true") == 0) { val = 1; consumed = 4; }
        else if (classStr.compare(v, 5, "false") == 0) { val = 0; consumed = 5; }
        else if (classStr[v] == '1') { val = 1; }
        else if (classStr[v] == '0') { val = 0; }

        pos = v + consumed;
        if (val < 0) continue;

        int classNumber = 0;
        try {
            classNumber = std::stoi(key);
        }
        catch (...) {
            continue;
        }

        if (classNumber < 1 || classNumber > MAX_CLASSES) continue;
        parsed[classNumber] = val;
    }

    if (parsed.empty()) return true;
    return ApplyClassesNow(parsed);
}

bool ReadHttpRequest(SOCKET s, std::string& headers, std::string& body) {
    std::string data;
    char buf[4096];
    size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, n);
        if (data.size() > MAX_REQUEST_SIZE) return false;
        headerEnd = data.find("\r\n\r\n");
    }

    headers = data.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;

    std::string lower = headers;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);

    size_t contentLength = 0;
    size_t clPos = lower.find("content-length:");
    if (clPos != std::string::npos) {
        try {
            contentLength = (size_t)std::stoul(lower.substr(clPos + 15));
        }
        catch (...) {
            return false;
        }
    }
    if (contentLength > MAX_REQUEST_SIZE) return false;

    while (data.size() - bodyStart < contentLength) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, n);
        if (data.size() > MAX_REQUEST_SIZE) return false;
    }

    body = data.substr(bodyStart, contentLength);
    return true;
}

void SendResponse(SOCKET s, const char* status, const char* text) {
    std::string resp = std::string("HTTP/1.1 ") + status +
        "\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: " +
        std::to_string(strlen(text)) + "\r\n\r\n" + text;
    send(s, resp.c_str(), (int)resp.size(), 0);
}

void HandleClient(SOCKET clientSocket) {
    DWORD timeoutMs = 3000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    std::string headers, body;
    if (!ReadHttpRequest(clientSocket, headers, body)) {
        Log("[HTTP] Request unvollstaendig oder zu gross.\n");
        SendResponse(clientSocket, "400 Bad Request", "BAD REQUEST");
        closesocket(clientSocket);
        return;
    }

    if (headers.find("POST /patch") != std::string::npos) {
        if (body.empty()) {
            Log("[HTTP] Leerer Body.\n");
            SendResponse(clientSocket, "400 Bad Request", "EMPTY BODY");
        }
        else {
            std::string json = StripComments(body);
            bool setOk = ApplySettings(json);
            bool carOk = ApplyCarHook(json);
            bool classesOk = ParseClassStates(json);

            if (!carOk) SendResponse(clientSocket, "500 Internal Server Error", "CAR HOOK FAILED");
            else if (!setOk) SendResponse(clientSocket, "500 Internal Server Error", "SETTING FAILED");
            else if (!classesOk) SendResponse(clientSocket, "503 Service Unavailable", "CLASSES NOT READY");
            else SendResponse(clientSocket, "200 OK", "OK");
        }
    }
    else {
        SendResponse(clientSocket, "200 OK", "PONG");
    }

    closesocket(clientSocket);
}

void StartHttpServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    serverAddr.sin_port = htons(SERVER_PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(serverSocket);
        return;
    }

    listen(serverSocket, SOMAXCONN);

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        FILE* f = nullptr;
        fopen_s(&f, LOG_PATH, "w");
        if (f) {
            fprintf(f, "[HTTP] Server gestartet.\n");
            fclose(f);
        }
    }

    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(HandleClient, clientSocket).detach();
        }
    }
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    Sleep(2000);
    StartHttpServer();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        break;
    }
    return TRUE;
}