#include <windows.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <bcrypt.h>

#include "config.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

namespace fs = std::filesystem;

static const char* const kAllowedDllSha256[] = {
    "9afb28d3dc0e75d50e791c5e0df55a065bd3dd6e9bdc9c1c2e2e6b61c5e6d378"
};

std::atomic<bool> g_Running(true);
std::atomic<bool> g_ForceClose(false);
std::atomic<bool> g_CloseDialogOpen(false);
DWORD g_TargetPid = 0;

std::wstring g_ConfigPath = L"";
std::string g_Placeholders = "#ST#=22|#LAPS#=44";

std::string g_AuthToken = "";
HANDLE g_hAuthTokenMapping = NULL;
const wchar_t* kSharedMemName = L"Local\\AMS2Launcher_AuthToken";

#define WM_APP_REFRESH (WM_APP + 1)

static HWND g_hwndMain = NULL;
static HWND g_hwndBanner = NULL;
static HWND g_hwndLog = NULL;
static HWND g_hwndStatus = NULL;
static HFONT g_hFontLog = NULL;
static HFONT g_hFontBanner = NULL;
static HBRUSH g_hbrBg = NULL;

static std::mutex g_LogMutex;
static std::vector<std::string> g_Lines;
static std::string g_Status;

static const COLORREF kColBg = RGB(12, 12, 12);
static const COLORREF kColText = RGB(204, 204, 204);
static const COLORREF kColGreen = RGB(0, 200, 0);
static const COLORREF kColYellow = RGB(255, 215, 0);
static const COLORREF kColRed = RGB(230, 60, 60);

static std::atomic<COLORREF> g_StatusColor(kColText);
static std::atomic<bool> g_Hidden(false);
static void BringLauncherToFront();

static const char* kBanner =
"==============================================================\r\n"
"  __   _  _  ____  ____     ____  ____   __  __ _  ____  ____  \r\n"
" / _\\ ( \\/ )/ ___)(___ \\   (  _ \\(  _ \\ /  \\(  / )(  __)(  _ \\\r\n"
"/    \\/ \\/ \\\\___ \\ / __/    ) _ ( )   /(  O ))  (  ) _)  )   /\r\n"
"\\_/\\_/\\_)(_/(____/(____)   (____/(__\\_) \\__/(__\\_)(____)(__\\_)\r\n"
"==============================================================\r\n"
" AMS2Broker " APP_VERSION ". Created by daHas (C) 2026.\r\n"
"==============================================================";

static std::string ToAnsi(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static void RequestRefresh() {
    if (g_hwndMain) PostMessageA(g_hwndMain, WM_APP_REFRESH, 0, 0);
}

static void Log(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        g_Lines.push_back(s);
    }
    RequestRefresh();
}

static void SetStatus(const std::string& s, COLORREF col = kColText) {
    {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        g_Status = s;
        g_StatusColor = col;
    }
    if (col == kColRed && g_Hidden && g_Visibility == 2) {
        g_Hidden = false;
        BringLauncherToFront();
    }
    RequestRefresh();
}

static void RefreshLogControl() {
    std::string text, status;
    {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        for (size_t i = 0; i < g_Lines.size(); ++i) {
            if (i) text += "\r\n";
            text += g_Lines[i];
        }
        status = g_Status;
    }
    SetWindowTextA(g_hwndLog, text.c_str());
    int len = GetWindowTextLengthA(g_hwndLog);
    SendMessageA(g_hwndLog, EM_SETSEL, len, len);
    SendMessageA(g_hwndLog, EM_SCROLLCARET, 0, 0);

    SetWindowTextA(g_hwndStatus, status.c_str());
    InvalidateRect(g_hwndStatus, NULL, TRUE);
}

static std::atomic<bool> g_AlwaysOnTop(true);

static void BringLauncherToFront() {
    if (!g_hwndMain || g_Hidden) return;
    ShowWindow(g_hwndMain, SW_RESTORE);
    SetWindowPos(g_hwndMain, g_AlwaysOnTop ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwndMain);
}

static void ReleaseTopmost() {
    g_AlwaysOnTop = false;
    if (g_hwndMain)
        SetWindowPos(g_hwndMain, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ExitLater(const std::string& msg, int delayMs, bool revealIfHidden = true) {
    if (revealIfHidden && g_Hidden && g_Visibility == 2) {
        g_Hidden = false;
        BringLauncherToFront();
    }
    SetStatus(msg);
    Sleep(delayMs);
    g_Running = false;
    g_ForceClose = true;
    PostMessageA(g_hwndMain, WM_CLOSE, 0, 0);
}

std::string GenerateRandomToken()
{
    const size_t tokenBytes = 32;
    unsigned char buffer[tokenBytes] = {};

    NTSTATUS status = BCryptGenRandom(
        NULL,
        buffer,
        static_cast<ULONG>(tokenBytes),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (status != 0) {
        Log("BCryptGenRandom failed: " + std::to_string((long)status));
        return "";
    }

    static const char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(tokenBytes * 2);
    for (size_t i = 0; i < tokenBytes; ++i) {
        token.push_back(hex[(buffer[i] >> 4) & 0x0F]);
        token.push_back(hex[buffer[i] & 0x0F]);
    }
    return token;
}

bool WriteTokenToSharedMemory(const std::string& token)
{
    if (g_hAuthTokenMapping) {
        CloseHandle(g_hAuthTokenMapping);
        g_hAuthTokenMapping = NULL;
    }

    DWORD size = static_cast<DWORD>(token.size() + 1);

    g_hAuthTokenMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, kSharedMemName);
    if (!g_hAuthTokenMapping) return false;

    void* pView = MapViewOfFile(g_hAuthTokenMapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!pView) {
        CloseHandle(g_hAuthTokenMapping);
        g_hAuthTokenMapping = NULL;
        return false;
    }

    memcpy(pView, token.c_str(), token.size());
    static_cast<char*>(pView)[token.size()] = '\0';
    UnmapViewOfFile(pView);
    return true;
}

static void PrintError(const char* step) {
    Log(std::string("  [") + step + "] Windows-Error-Code: " + std::to_string(GetLastError()));
}

DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, processName) == 0) {
                    pid = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

void TerminateGameProcess(DWORD pid) {
    if (pid == 0) return;
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        TerminateProcess(hProc, 0);
        CloseHandle(hProc);
    }
}

bool IsModuleLoaded(DWORD pid, const wchar_t* moduleName) {
    HANDLE snap = INVALID_HANDLE_VALUE;

    for (int i = 0; i < 5; i++) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_BAD_LENGTH) return false;
        Sleep(50);
    }
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    bool found = false;

    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool IsProcessAms2(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(h, 0, path, &size) != 0;
    CloseHandle(h);
    if (!ok) return false;

    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    std::wstring file = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
    return _wcsicmp(file.c_str(), L"AMS2AVX.exe") == 0 || _wcsicmp(file.c_str(), L"AMS2.exe") == 0;
}

DWORD GetPidFromGameWindow() {
    HWND hwnd = FindWindowW(NULL, L"Automobilista 2");
    if (!hwnd || !IsWindowVisible(hwnd)) return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || !IsProcessAms2(pid)) return 0;
    return pid;
}

int InjectDLL(DWORD processId, const char* dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION |
        PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ,
        FALSE,
        processId
    );
    if (!hProcess) { PrintError("OpenProcess"); return 0; }

    SIZE_T pathSize = strlen(dllPath) + 1;

    LPVOID pDllPath = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        PrintError("VirtualAllocEx");
        CloseHandle(hProcess);
        return 0;
    }

    if (!WriteProcessMemory(hProcess, pDllPath, dllPath, pathSize, NULL)) {
        PrintError("WriteProcessMemory");
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 0;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPTHREAD_START_ROUTINE pLoadLibrary =
        (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");
    if (!pLoadLibrary) {
        PrintError("GetProcAddress(LoadLibraryA)");
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 0;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibrary, pDllPath, 0, NULL);
    if (!hThread) {
        PrintError("CreateRemoteThread");
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 0;
    }

    DWORD waitResult = WaitForSingleObject(hThread, 15000);

    if (waitResult != WAIT_OBJECT_0) {
        Log("  [WaitForSingleObject] Timeout - LoadLibrary still stuck.");
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return 2;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        Log("  [LoadLibrary] 0 - Load failed.");
        return 0;
    }

    return 1;
}

std::wstring GetSteamInstallPath() {
    wchar_t path[MAX_PATH] = { 0 };
    DWORD bufSize = sizeof(path);
    HKEY hKey;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        bufSize = sizeof(path);
        LONG rc = RegQueryValueExW(hKey, L"SteamPath", nullptr, nullptr,
            (LPBYTE)path, &bufSize);
        RegCloseKey(hKey);
        if (rc == ERROR_SUCCESS && path[0] != L'\0')
            return std::wstring(path);
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        bufSize = sizeof(path);
        LONG rc = RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr,
            (LPBYTE)path, &bufSize);
        RegCloseKey(hKey);
        if (rc == ERROR_SUCCESS && path[0] != L'\0')
            return std::wstring(path);
    }

    return L"";
}

std::vector<std::wstring> GetSteamLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> libs;
    libs.push_back(steamPath);

    std::wstring vdf = steamPath + L"\\steamapps\\libraryfolders.vdf";
    std::wifstream f(vdf);
    if (!f) return libs;

    std::wstring line;
    while (std::getline(f, line)) {
        auto pos = line.find(L"\"path\"");
        if (pos == std::wstring::npos) continue;
        auto q1 = line.find(L'"', pos + 6);
        auto q2 = line.find(L'"', q1 + 1);
        if (q1 == std::wstring::npos || q2 == std::wstring::npos) continue;
        std::wstring p = line.substr(q1 + 1, q2 - q1 - 1);
        std::wstring unescaped;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == L'\\' && i + 1 < p.size() && p[i + 1] == L'\\') {
                unescaped += L'\\'; ++i;
            }
            else unescaped += p[i];
        }
        libs.push_back(unescaped);
    }
    return libs;
}

std::wstring GetDefaultAms2Path() {
    std::wstring steamPath = GetSteamInstallPath();
    if (!steamPath.empty()) {
        for (auto& lib : GetSteamLibraryFolders(steamPath)) {
            std::wstring base = lib + L"\\steamapps\\common\\Automobilista 2\\";
            std::wstring avx = base + L"AMS2AVX.exe";
            if (fs::exists(avx)) return avx;
            std::wstring normal = base + L"AMS2.exe";
            if (fs::exists(normal)) return normal;
        }
    }
    return g_GamePathToExe;
}

void TriggerRestApiRequest() {
    SetStatus("> [Patching] Hold on! Applying configuration...", kColYellow);

    std::string fileContent = "";
    if (fs::exists(g_ConfigPath)) {
        std::ifstream file(g_ConfigPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        fileContent = buffer.str();
    }
    else {
        SetStatus("> [Patching] Failed: Template file not found under: " + ToAnsi(g_ConfigPath), kColRed);
        return;
    }

    HINTERNET hSession = InternetOpenA("AMS2Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hSession) { SetStatus("> [Patching] Failed: InternetOpen failed.", kColRed); return; }

    HINTERNET hConnect = InternetConnectA(hSession, "127.0.0.1", g_ServerPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { SetStatus("> [Patching] Failed: InternetConnect failed.", kColRed); InternetCloseHandle(hSession); return; }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/patch", NULL, NULL, NULL, 0, 0);
    if (!hRequest) { SetStatus("> [Patching] Failed: HttpOpenRequest failed.", kColRed); InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return; }

    std::string headerStr =
        "Content-Type: application/json\r\n"
        "X-Auth-Token: " + g_AuthToken + "\r\n"
        "X-Placeholders: " + g_Placeholders + "\r\n";

    BOOL sent = HttpSendRequestA(
        hRequest, headerStr.c_str(), (DWORD)headerStr.size(),
        (LPVOID)fileContent.c_str(), (DWORD)fileContent.size());

    if (sent) {
        if (g_Hidden && g_Visibility == 2) {
            g_Hidden = false;
            BringLauncherToFront();
        }
        SetStatus("> [Patching] Success! You can now start the session.", kColGreen);
        ReleaseTopmost();
    }
    else {
        SetStatus("> [Patching] Failed: Error sending request. Windows-Error: " + std::to_string(GetLastError()), kColRed);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
}

bool CheckDllReady() {
    HINTERNET hSession = InternetOpenA("AMS2Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hSession) return false;

    HINTERNET hConnect = InternetConnectA(hSession, "127.0.0.1", g_ServerPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", "/ready", NULL, NULL, NULL, 0, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return false;
    }

    std::string headerStr = "X-Auth-Token: " + g_AuthToken + "\r\n";
    BOOL sent = HttpSendRequestA(hRequest, headerStr.c_str(), (DWORD)headerStr.size(), NULL, 0);
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (sent) {
        HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);

    return (sent && statusCode == 200);
}

void HotkeyListener() {
    bool applyPressed = false, quitPressed = false;

    while (g_Running) {
        if (GetAsyncKeyState(g_HotkeyApply) & 0x8000) {
            if (!applyPressed) {
                applyPressed = true;
                BringLauncherToFront();
                TriggerRestApiRequest();
            }
        }
        else {
            applyPressed = false;
        }

        if (GetAsyncKeyState(g_HotkeyLauncher) & 0x8000) {
            if (g_Visibility > 0) g_Hidden = false;
            SetStatus("");
            BringLauncherToFront();
            Sleep(250);
        }

        if (GetAsyncKeyState(g_HotkeyQuit) & 0x8000) {
            if (!quitPressed) {
                quitPressed = true;
                SetStatus("");
                BringLauncherToFront();
                PostMessageA(g_hwndMain, WM_CLOSE, 0, 0);
            }
        }
        else {
            quitPressed = false;
        }

        Sleep(100);
    }
}

static void FocusWindow(HWND hwnd) {
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread)
        attached = AttachThreadInput(myThread, fgThread, TRUE) != FALSE;
    SetForegroundWindow(hwnd);
    if (attached)
        AttachThreadInput(myThread, fgThread, FALSE);
}

static void ParkMouse(HWND hwndGame) {
    RECT r;
    if (!GetWindowRect(hwndGame, &r)) return;
    int x = r.left + (r.right - r.left) / 2;
    int y = r.top + 2;
    SetCursorPos(x, y);

    RECT clip = { x, y, x + 1, y + 1 };
    ClipCursor(&clip);

    INPUT mv = {};
    mv.type = INPUT_MOUSE;
    mv.mi.dwFlags = MOUSEEVENTF_MOVE;
    mv.mi.dx = 1;
    SendInput(1, &mv, sizeof(INPUT));
    Sleep(50);
}

static bool Sha256OfFile(HANDLE hFile, std::string& outHex) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return false;

    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0);
    std::vector<UCHAR> obj(objLen);

    if (BCryptCreateHash(hAlg, &hHash, obj.data(), objLen, NULL, 0, 0) == 0) {
        LARGE_INTEGER zero = {};
        SetFilePointerEx(hFile, zero, NULL, FILE_BEGIN);

        std::vector<UCHAR> buf(64 * 1024);
        DWORD read = 0;
        bool readOk = true;
        while (true) {
            if (!ReadFile(hFile, buf.data(), (DWORD)buf.size(), &read, NULL)) { readOk = false; break; }
            if (read == 0) break;
            if (BCryptHashData(hHash, buf.data(), read, 0) != 0) { readOk = false; break; }
        }

        UCHAR digest[32] = {};
        if (readOk && BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
            static const char hexd[] = "0123456789abcdef";
            outHex.clear();
            for (int i = 0; i < 32; ++i) {
                outHex.push_back(hexd[digest[i] >> 4]);
                outHex.push_back(hexd[digest[i] & 0x0F]);
            }
            ok = true;
        }
        BCryptDestroyHash(hHash);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

static HANDLE OpenVerifiedDll(const std::wstring& path, std::string& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        err = "Cannot open PatcherAPI.dll. Windows-Error: " + std::to_string(GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    std::string hex;
    if (!Sha256OfFile(h, hex)) {
        CloseHandle(h);
        err = "Could not compute SHA-256 of PatcherAPI.dll.";
        return INVALID_HANDLE_VALUE;
    }

    for (const char* allowed : kAllowedDllSha256) {
        if (_stricmp(hex.c_str(), allowed) == 0) return h;
    }

    CloseHandle(h);
    err = "PatcherAPI.dll is not trusted. SHA-256: " + hex;
    return INVALID_HANDLE_VALUE;
}

static void BrokerWorker(int argc, char** argv) {
    Log("--------------------------------------------------------------");
    Log("PLEASE READ:");
    Log("The broker will automatically configure the settings. Wait for");
    Log("the confirmation. If the confirmation appears while you are");
    Log("NOT in 'Single Race', manually switch to 'Single Race' and press");
    Log("[" + std::string(GetKeyName(g_HotkeyApply)) + "] on the keyboard to apply the patch. After patching");
    Log("and before you start, you can manually adjust the new settings.");
    Log("--------------------------------------------------------------");
    Log("");
    Log("[Keyboard Controls]");
    Log("  [" + std::string(GetKeyName(g_HotkeyApply)) + "] -> Apply Patch");
    Log("  [" + std::string(GetKeyName(g_HotkeyLauncher)) + "] -> Bring Launcher up front");
    Log("  [" + std::string(GetKeyName(g_HotkeyQuit)) + "] -> Quit Game & Launcher");
    Log("");

    g_AuthToken = GenerateRandomToken();
    if (g_AuthToken.empty()) {
        ExitLater("Fatal: Could not generate secure auth token. Abort.", 4000);
        return;
    }
    if (!WriteTokenToSharedMemory(g_AuthToken)) {
        ExitLater("Warning: Shared memory for auth token failed.", 4000);
        return;
    }

    std::thread listener(HotkeyListener);
    listener.detach();

    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(NULL, exePathW, MAX_PATH);
    fs::path launcherDir = fs::path(exePathW).parent_path();
    fs::path templatesPath(g_TemplatesPath);
    if (templatesPath.has_root_name() || templatesPath.has_root_directory()) {
        ExitLater("Error: templates_path in config.ini must be relative to the launcher.", 4000);
        return;
    }
    fs::path templatesDir = (launcherDir / templatesPath).lexically_normal();
    //Log("Templates: " + ToAnsi(templatesDir.wstring()));
    std::wstring relativeConfigPath = L"quick-race-example.json";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--template" || arg == "-t") && i + 1 < argc) {
            std::string pathStr = argv[i + 1];
            relativeConfigPath = std::wstring(pathStr.begin(), pathStr.end());
            i++;
        }
        else if ((arg == "--placeholders" || arg == "-p") && i + 1 < argc) {
            g_Placeholders = argv[i + 1];
            i++;
        }
    }

    g_ConfigPath = (templatesDir / relativeConfigPath).wstring();

    if (!fs::exists(g_ConfigPath)) {
        ExitLater("Error: Templates folder not found. Closing...", 4000);
        return;
    }

    std::wstring appPath = GetDefaultAms2Path();

    if (!fs::exists(appPath)) {
        ExitLater("Error: Game not found. Closing...", 4000);
        return;
    }

    std::string verifyError;
    HANDLE hDllLock = OpenVerifiedDll((launcherDir / L"PatcherAPI.dll").wstring(), verifyError);
    if (hDllLock == INVALID_HANDLE_VALUE) {
        Log(verifyError);
        ExitLater("DLL verification failed! AMS2 was not started.", 6000);
        return;
    }

    std::wstring workingDirW = fs::path(appPath).parent_path().wstring();

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(appPath.data(), NULL, NULL, NULL, FALSE, 0, NULL, workingDirW.c_str(), &si, &pi)) {
        ExitLater("Error launching AMS2! Windows-Error-Code: " + std::to_string(GetLastError()), 3000);
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    SetStatus("Starting AUTOMOBILISTA2...");

    DWORD targetPid = 0;
    DWORD lastPid = 0;
    int stableCount = 0;
    bool foregroundOnce = false;
    const int requiredStable = 3;
    const int timeoutSeconds = 90;

    for (int t = 0; t < timeoutSeconds && g_Running; t++) {
        HWND hwndCheck = FindWindowW(NULL, L"Automobilista 2");
        if (hwndCheck && !foregroundOnce) {
            BringLauncherToFront();
            foregroundOnce = true;
        }

        DWORD pid = GetPidFromGameWindow();

        if (pid != 0 && pid == lastPid && IsModuleLoaded(pid, L"d3d11.dll")) {
            stableCount++;
        }
        else {
            stableCount = 0;
        }
        lastPid = pid;

        if (stableCount >= requiredStable) {
            targetPid = pid;
            g_TargetPid = pid;
            break;
        }
        Sleep(1000);
    }

    if (!g_Running) return;

    if (targetPid == 0) {
        targetPid = GetProcessIdByName(L"AMS2AVX.exe");
        if (targetPid == 0) targetPid = GetProcessIdByName(L"AMS2.exe");
        if (targetPid == 0) {
            ExitLater("Game process not found. Abort.", 3000);
            return;
        }
        SetStatus("Timeout. Trying with PID " + std::to_string(targetPid) + "...");
    }
    else {
        SetStatus("Game ready (PID: " + std::to_string(targetPid) + ").");
    }
    g_TargetPid = targetPid;

    Sleep(1000);

    SetStatus("Injecting DLL...");

    fs::path dllPathFs = launcherDir / L"PatcherAPI.dll";
    std::string dllPathStr = dllPathFs.string();
    const char* dllPath = dllPathStr.c_str();
    const wchar_t* dllName = L"PatcherAPI.dll";

    bool success = false;
    const int maxRetries = 5;

    for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
        if (IsModuleLoaded(targetPid, dllName)) {
            success = true;
            break;
        }

        if (!IsProcessAms2(targetPid)) {
            SetStatus("Target process not found. Abort.");
            break;
        }

        int result = InjectDLL(targetPid, dllPath);

        if (result == 1) {
            Sleep(200);
            success = IsModuleLoaded(targetPid, dllName);
        }
        else if (result == 2) {
            SetStatus("Abort: LoadLibrary-Thread stuck. No retry.");
            break;
        }

        if (!success && attempt < maxRetries) {
            SetStatus("Attempt " + std::to_string(attempt) + " failed. Next attempt in 600 ms...");
            Sleep(600);
        }
    }

    if (success) {
        SetStatus("Success!");
        Sleep(600);
    }
    else {
        ExitLater("DLL injection failed! Exiting launcher.", 4000);
        return;
    }

    SetStatus("Waiting for main menu...");

    while (g_Running && IsProcessAms2(targetPid)) {
        if (CheckDllReady()) {
            Sleep(500);
            HWND hwndGame = FindWindowW(NULL, L"Automobilista 2");
            if (hwndGame) {
                FocusWindow(hwndGame);
                ParkMouse(hwndGame); // Avoid Mouse hovering
                Sleep(300);

                INPUT inputs[2] = {};

                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wVk = VK_RETURN;
                inputs[0].ki.wScan = (WORD)MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
                inputs[0].ki.dwFlags = 0;

                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wVk = VK_RETURN;
                inputs[1].ki.wScan = (WORD)MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
                inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

                SendInput(2, inputs, sizeof(INPUT)); // Enter Single Race
                Sleep(100);
                ClipCursor(NULL); // Release Cursor
                BringLauncherToFront();
                TriggerRestApiRequest(); // Apply Patch!
            }
            break;
        }

        HWND hwnd = FindWindowW(NULL, L"Automobilista 2");
        if (hwnd == NULL && targetPid != 0) {
            Sleep(1000);
            if (FindWindowW(NULL, L"Automobilista 2") == NULL && !IsProcessAms2(targetPid)) {
                break;
            }
        }
        Sleep(1000);
    }

    if (!g_Running) return;

    while (g_Running && IsProcessAms2(targetPid)) {
        HWND hwnd = FindWindowW(NULL, L"Automobilista 2");
        if (hwnd == NULL && targetPid != 0) {
            Sleep(1000);
            if (FindWindowW(NULL, L"Automobilista 2") == NULL && !IsProcessAms2(targetPid)) {
                break;
            }
        }
        Sleep(2000);
    }

    if (!g_Running) return;

    ExitLater("AMS2 closed. Exiting launcher.", 1500, false);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwndBanner = CreateWindowExA(0, "STATIC", kBanner,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 8, 620, 130, hwnd, NULL, NULL, NULL);
        SendMessageA(g_hwndBanner, WM_SETFONT, (WPARAM)g_hFontBanner, TRUE);

        HDC hdcMeasure = GetDC(hwnd);
        HFONT hOldFont = (HFONT)SelectObject(hdcMeasure, g_hFontLog);
        TEXTMETRICA tm = {};
        GetTextMetricsA(hdcMeasure, &tm);
        SelectObject(hdcMeasure, hOldFont);
        ReleaseDC(hwnd, hdcMeasure);

        const int logTop = 145;
        const int logLines = 14;
        int logHeight = tm.tmHeight * logLines + 6;

        g_hwndLog = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, logTop, 620, logHeight, hwnd, NULL, NULL, NULL);
        SendMessageA(g_hwndLog, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);

        g_hwndStatus = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            10, logTop + logHeight, 540, tm.tmHeight + 6, hwnd, NULL, NULL, NULL);
        SendMessageA(g_hwndStatus, WM_SETFONT, (WPARAM)g_hFontLog, TRUE);
        return 0;
    }

    case WM_APP_REFRESH:
        RefreshLogControl();
        return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        COLORREF col = kColText;
        if ((HWND)lParam == g_hwndBanner) col = kColGreen;
        else if ((HWND)lParam == g_hwndStatus) col = g_StatusColor;
        SetBkColor(hdc, kColBg);
        SetTextColor(hdc, col);
        return (LRESULT)g_hbrBg;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_hbrBg);
        return 1;
    }

    case WM_CLOSE: {
        if (g_ForceClose) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (g_CloseDialogOpen.exchange(true)) return 0;

        int r = MessageBoxA(hwnd,
            "Really quit?\n\nThis also closes AUTOMOBILISTA2.",
            "AMS2Broker",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TOPMOST);
        g_CloseDialogOpen = false;

        if (r == IDYES) {
            g_Running = false;
            TerminateGameProcess(g_TargetPid);
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            g_Running = false;
            TerminateGameProcess(g_TargetPid);
        }
        return 0;

    case WM_DESTROY:
        g_Running = false;
        if (g_hAuthTokenMapping) {
            CloseHandle(g_hAuthTokenMapping);
            g_hAuthTokenMapping = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    HANDLE hSingleInstance = CreateMutexW(NULL, TRUE, L"Local\\AMS2Broker_SingleInstance");
    if (hSingleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hwndOther = FindWindowA("AMS2BrokerWnd", NULL);
        if (hwndOther) {
            ShowWindow(hwndOther, SW_RESTORE);
            SetForegroundWindow(hwndOther);
        }
        MessageBoxA(NULL, "AMS2Broker is already running.", "AMS2Broker",
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        CloseHandle(hSingleInstance);
        return 0;
    }

    g_hbrBg = CreateSolidBrush(kColBg);
    g_hFontLog = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    g_hFontBanner = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hbrBg;
    wc.lpszClassName = "AMS2BrokerWnd";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) return 1;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_TOPMOST;
    RECT rc = { 0, 0, 560, 440 };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    g_hwndMain = CreateWindowExA(exStyle, "AMS2BrokerWnd", "AMS2Broker",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL);
    if (!g_hwndMain) return 1;

    LoadConfiguration();
    g_Hidden = g_Visibility != 1;

    if (!g_Hidden) {
        ShowWindow(g_hwndMain, nCmdShow);
        UpdateWindow(g_hwndMain);
    }

    int argc = __argc;
    char** argv = __argv;
    std::thread worker(BrokerWorker, argc, argv);
    worker.detach();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
