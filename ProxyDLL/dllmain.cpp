#include "pch.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cctype>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json.h"
#include "utils.h"

#pragma comment(lib, "ws2_32.lib")

const int SERVER_PORT = 58888;

const bool USE_CAR_HOOK = false;
const uintptr_t CAR_HOOK_RVA = 0x3FD3D1;

const uintptr_t CLASS_ROOT_ADDR = 0x1818C05C0;
const uintptr_t CLASS_OFF1 = 0x130;
const uintptr_t CLASS_OFF2 = 0xA8;
const int CLASS_STRIDE = 0x930;
const int CLASS_FLAG_OFFSET = 0x52C;
const int MAX_CLASSES = 89;

const size_t MAX_REQUEST_SIZE = 64 * 1024;

static std::mutex g_hookMutex;
static uintptr_t g_forceCarAddr = 0;

struct IntSetting {
    const std::vector<std::string>* path;
    uintptr_t absoluteAddress;
};

const uintptr_t CAR_ADDRESS = 0x1818C41F4;
const uintptr_t LIVERY_ADDRESS = 0x1818C4764;
const uintptr_t TRACK_ADDRESS = 0x1818C1674;
const uintptr_t PRACTICE_ADDRESS = 0x1817BD934;
const uintptr_t QUALIFYING_ADDRESS = 0x1817CA454;
const uintptr_t DURATION_TYPE_ADDRESS = 0x1818AE1D4;
const uintptr_t TIME_ADDRESS = 0x1818AE574;
const uintptr_t EXTRA_LAP_ADDRESS = 0x1818AE744;
const uintptr_t LAPS_ADDRESS = 0x1818AE3A4;
const uintptr_t DATE_TYPE_ADDRESS = 0x1818B26B4;
const uintptr_t CUSTOM_DAY_ADDRESS = 0x1818B2FC4;
const uintptr_t CUSTOM_MONTH_ADDRESS = 0x1818B3194;
const uintptr_t CUSTOM_YEAR_ADDRESS = 0x1818B3364;
const uintptr_t STARTTIME_ADDRESS = 0x1818B2C24;
const uintptr_t TIME_PROG_ADDRESS = 0x1818B0B84;
const uintptr_t CLASS_ROOT_ADDRESS = 0x1818C05C0;

namespace P {
    static const std::vector<std::string> Car = { "Vehicle", "Car" };
    static const std::vector<std::string> Livery = { "Vehicle", "Livery" };
    static const std::vector<std::string> Track = { "Track" };
    static const std::vector<std::string> Practice = { "Practice", "Enabled" };
    static const std::vector<std::string> Qualifying = { "Qualifying", "Enabled" };
    static const std::vector<std::string> RaceDurType = { "Race", "DurationType" };
    static const std::vector<std::string> RaceTime = { "Race", "Time" };
    static const std::vector<std::string> RaceExtraLap = { "Race", "+1Lap" };
    static const std::vector<std::string> RaceLaps = { "Race", "Laps" };
    static const std::vector<std::string> RaceDateType = { "Race", "DateType" };
    static const std::vector<std::string> RaceCustomDay = { "Race", "CustomDay" };
    static const std::vector<std::string> RaceCustomMonth = { "Race", "CustomMonth" };
    static const std::vector<std::string> RaceCustomYear = { "Race", "CustomYear" };
    static const std::vector<std::string> RaceStartTime = { "Race", "StartTime" };
    static const std::vector<std::string> RaceTimeProg = { "Race", "TimeProgression" };
}

static const IntSetting SETTINGS[] = {
    { &P::Car,              CAR_ADDRESS },
    { &P::Livery,           LIVERY_ADDRESS },
    { &P::Track,            TRACK_ADDRESS },
    { &P::Practice,         PRACTICE_ADDRESS },
    { &P::Qualifying,       QUALIFYING_ADDRESS },
    { &P::RaceDurType,      DURATION_TYPE_ADDRESS },
    { &P::RaceTime,         TIME_ADDRESS },
    { &P::RaceExtraLap,     EXTRA_LAP_ADDRESS },
    { &P::RaceLaps,         LAPS_ADDRESS },
    { &P::RaceDateType,     DATE_TYPE_ADDRESS },
    { &P::RaceCustomDay,    CUSTOM_DAY_ADDRESS },
    { &P::RaceCustomMonth,  CUSTOM_MONTH_ADDRESS },
    { &P::RaceCustomYear,   CUSTOM_YEAR_ADDRESS },
    { &P::RaceStartTime,    STARTTIME_ADDRESS },
    { &P::RaceTimeProg,     TIME_PROG_ADDRESS },
};

bool InstallCarHook() {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    if (g_forceCarAddr) return true;

    uintptr_t base = ModuleBase();
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

bool ApplySettings(const std::string& json) {
    bool allOk = true;
    for (const auto& s : SETTINGS) {
        if (USE_CAR_HOOK && s.absoluteAddress == CAR_ADDRESS) continue;

        long long v = 0;
        if (!GetIntAtPath(json, *s.path, v)) continue;

        int target = (int)v;
        if (s.absoluteAddress == LIVERY_ADDRESS) target += 1;

        int before = -1, after = -1;

        uintptr_t rva = RvaFromAbsolute(s.absoluteAddress);
        bool ok = PatchInt(At(rva), target, &before, &after);

        Log("[SET] %s json=%lld ziel=%d ok=%d vorher=%d nachher=%d\n",
            s.path->back().c_str(), v, target, ok ? 1 : 0, before, after);
        if (!ok) allOk = false;
    }
    return allOk;
}

bool ApplyCarHook(const std::string& json) {
    if (!USE_CAR_HOOK) return true;
    long long car = 0;
    if (!GetIntAtPath(json, P::Car, car)) return true;
    if (!InstallCarHook()) return false;
    bool ok = SafeWrite32(g_forceCarAddr, (int)car);
    Log("[CAR] forceCar=%lld ok=%d\n", car, ok ? 1 : 0);
    return ok;
}

bool ResolveClassBase(uintptr_t& base) {
    uintptr_t p1 = 0, p2 = 0, p3 = 0;
    uintptr_t rva = RvaFromAbsolute(CLASS_ROOT_ADDR);
    if (!SafeRead(At(rva) + CLASS_OFF1, &p1, sizeof(p1)) || !p1) return false;
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

bool ApplyClassStates(const std::string& json) {
    std::map<int, int> parsed;
    if (!ParseClassStates(json, parsed)) return true;
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
            bool classesOk = ApplyClassStates(json);

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

    InitLog();

    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(HandleClient, clientSocket).detach();
        }
    }
}

bool ValidateAddresses() {
    bool ok = true;

    for (const auto& s : SETTINGS) {
        int probe = 0;
        uintptr_t rva = RvaFromAbsolute(s.absoluteAddress);
        if (!SafeRead(At(rva), &probe, sizeof(probe))) {
            Log("[INIT] Adresse 0x%llX (RVA 0x%llX) ungueltig - Update oder ASLR-Problem?\n",
                (unsigned long long)s.absoluteAddress, (unsigned long long)rva);
            ok = false;
        }
    }

    int classProbe = 0;
    uintptr_t classRva = RvaFromAbsolute(CLASS_ROOT_ADDR);
    if (!SafeRead(At(classRva), &classProbe, sizeof(classProbe))) {
        Log("[INIT] CLASS_ROOT_ADDR (RVA 0x%llX) ungueltig - Update oder ASLR-Problem?\n",
            (unsigned long long)classRva);
        ok = false;
    }

    return ok;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    Sleep(2000);
    ValidateAddresses();
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