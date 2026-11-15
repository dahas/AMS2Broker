#include <windows.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <bcrypt.h>
#include "config.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

std::atomic<bool> g_Running(true);
DWORD g_TargetPid = 0;
std::string g_DllPathStr = "";

std::wstring g_ConfigPath = L"";
std::string g_Placeholders = "#ST#=22|#LAPS#=44";

std::string g_AuthToken = "";
HANDLE g_hAuthTokenMapping = NULL;

const wchar_t* kSharedMemName = L"Local\\AMS2Launcher_AuthToken";

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
        std::cerr << "BCryptGenRandom failed: " << status << std::endl;
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
    std::cerr << "  [" << step << "] Windows-Error-Code: " << GetLastError() << std::endl;
}

void SetupLauncherWindow() {
    HWND hwndConsole = GetConsoleWindow();
    if (hwndConsole) {
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hStdout != INVALID_HANDLE_VALUE) {
            COORD coord = { 90, 25 };
            SetConsoleScreenBufferSize(hStdout, coord);

            SMALL_RECT rect = { 0, 0, 89, 24 };
            SetConsoleWindowInfo(hStdout, TRUE, &rect);
        }

        SetWindowPos(hwndConsole, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }
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

// Hilfsfunktion, um das Spiel (AMS2) gewaltsam zu beenden, falls der Launcher schließt
void TerminateGameProcess(DWORD pid) {
    if (pid == 0) return;
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        TerminateProcess(hProc, 0);
        CloseHandle(hProc);
    }
}

// Konsolen-Event-Handler: Fängt das Schließen des Launchers ab
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_SHUTDOWN_EVENT || ctrlType == CTRL_C_EVENT) {
        g_Running = false;
        if (g_TargetPid != 0) {
            std::cout << "\nLauncher closing. Terminating AMS2 process (PID: " << g_TargetPid << ") ..." << std::endl;
            TerminateGameProcess(g_TargetPid);
        }
    }
    return FALSE;
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

HWND GetGameHWnd() {
    return FindWindowW(NULL, L"Automobilista 2");
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
        std::cerr << "  [WaitForSingleObject] Timeout - LoadLibrary still stuck." << std::endl;
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
        std::cerr << "  [LoadLibrary] 0 - Load failed." << std::endl;
        return 0;
    }

    return 1;
}

std::wstring GetSteamInstallPath() {
    HKEY hKey;
    wchar_t path[MAX_PATH] = { 0 };
    DWORD bufferSize = sizeof(path);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"InstallPath", NULL, NULL, (LPBYTE)path, &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::wstring(path);
        }
        RegCloseKey(hKey);
    }
    return L"";
}

std::wstring GetDefaultAms2Path() {
    std::wstring steamPath = GetSteamInstallPath();
    if (!steamPath.empty()) {
        std::wstring ams2Path = steamPath + L"\\steamapps\\common\\Automobilista 2\\AMS2AVX.exe";
        if (fs::exists(ams2Path)) {
            return ams2Path;
        }
        std::wstring normalPath = steamPath + L"\\steamapps\\common\\Automobilista 2\\AMS2.exe";
        if (fs::exists(normalPath)) {
            return normalPath;
        }
    }
    return L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Automobilista 2\\AMS2AVX.exe";
}

void TriggerRestApiRequest() {
    std::cout << "\n> [Patching] Hold on! Applying configuration ..." << std::endl;

    std::string fileContent = "";
    if (fs::exists(g_ConfigPath)) {
        std::ifstream file(g_ConfigPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        fileContent = buffer.str();
    }
    else {
        std::wcout << "> [Patching] Warning: Config file not found under: " << g_ConfigPath << ", sending empty body." << std::endl;
    }

    HINTERNET hSession = InternetOpenA("AMS2Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hSession) { std::cerr << "> InternetOpen failed." << std::endl; return; }

    HINTERNET hConnect = InternetConnectA(hSession, "127.0.0.1", g_ServerPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { std::cerr << "> InternetConnect failed." << std::endl; InternetCloseHandle(hSession); return; }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", "/patch", NULL, NULL, NULL, 0, 0);
    if (!hRequest) { std::cerr << "> HttpOpenRequest failed." << std::endl; InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return; }

    std::string headerStr =
        "Content-Type: application/json\r\n"
        "X-Auth-Token: " + g_AuthToken + "\r\n"
        "X-Placeholders: " + g_Placeholders + "\r\n";

    BOOL sent = HttpSendRequestA(
        hRequest, headerStr.c_str(), (DWORD)headerStr.size(),
        (LPVOID)fileContent.c_str(), (DWORD)fileContent.size());
    
    if (sent) {
        std::cout << "> [Patching] Success! You can now start the session." << std::endl;
    }
    else {
        std::cerr << "> [Patching] Error sending request. Windows-Error: " << GetLastError() << std::endl;
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

void BringLauncherToFront() {
    HWND hwndConsole = GetConsoleWindow();
    if (hwndConsole) {
        ShowWindow(hwndConsole, SW_RESTORE);
        SetForegroundWindow(hwndConsole);
        Sleep(500);
        ShowWindow(hwndConsole, SW_RESTORE);
        SetForegroundWindow(hwndConsole);
    }
}

void HotkeyListener() {
    std::cout << "\n[Keyboard Controls]" << std::endl;
    std::cout << "  [" << GetKeyName(g_HotkeyApply) << "] -> Apply Patch" << std::endl;
    std::cout << "  [" << GetKeyName(g_HotkeyLauncher) << "] -> Bring Launcher up front" << std::endl;
    std::cout << "  [" << GetKeyName(g_HotkeyQuit) << "] -> Quit Game\n" << std::endl;

    bool pPressed = false, qPressed = false;

    while (g_Running) {
        HWND hwndConsole = GetConsoleWindow();
        if (hwndConsole) {
            SetWindowPos(hwndConsole, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }

        if (GetAsyncKeyState(g_HotkeyApply) & 0x8000) {
            if (!pPressed) {
                pPressed = true;
                BringLauncherToFront();
                TriggerRestApiRequest();
            }
        }
        else {
            pPressed = false;
        }

        if (GetAsyncKeyState(g_HotkeyLauncher) & 0x8000) {
            BringLauncherToFront();
            Sleep(250);
        }

        if (GetAsyncKeyState(g_HotkeyQuit) & 0x8000) {
            if (!qPressed) {
                qPressed = true;
                BringLauncherToFront();
                std::cout << "\n> Really quit the game? (Y/N): " << std::flush;

                while (true) {
                    if (GetAsyncKeyState('Y') & 0x8000) {
                        std::cout << "Y\n> Quitting Game ..." << std::endl;
                        TerminateGameProcess(g_TargetPid);
                        g_Running = false;
                        break;
                    }
                    else if (GetAsyncKeyState('N') & 0x8000 || GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                        std::cout << "N" << std::endl;
                        Sleep(500);
                        break;
                    }
                    Sleep(50);
                }
            }
        }
        else {
            qPressed = false;
        }

        Sleep(100);
    }
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    SetupLauncherWindow();

    LoadConfiguration();

    g_AuthToken = GenerateRandomToken();
    if (g_AuthToken.empty()) {
        std::cerr << "Fatal: Could not generate secure auth token. Abort." << std::endl;
        Sleep(4000);
        return 1;
    }
    if (!WriteTokenToSharedMemory(g_AuthToken)) {
        std::cerr << "Warning: Shared memory for auth token failed." << std::endl;
        Sleep(4000);
        return 1;
    }

    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(NULL, exePathW, MAX_PATH);
    fs::path launcherDir = fs::path(exePathW).parent_path();
    fs::path templatesDir = launcherDir.parent_path() / L"Templates";

    std::wstring relativeConfigPath = L"single-race-example.json";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
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

    std::wcout << "Config: " << relativeConfigPath << std::endl;

    std::wstring appPath = GetDefaultAms2Path();

    if (!fs::exists(appPath)) {
        std::cerr << "Error: EXE not found at this location:\n"
            << std::string(appPath.begin(), appPath.end()) << std::endl;
        Sleep(4000);
        return 1;
    }

    std::wstring workingDirW = fs::path(appPath).parent_path().wstring();

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(appPath.data(), NULL, NULL, NULL, FALSE, 0, NULL, workingDirW.c_str(), &si, &pi)) {
        std::cerr << "Error launching AMS2! Windows-Error-Code: " << GetLastError() << std::endl;
        Sleep(3000);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "Launching AMS2 ..." << std::endl;

    DWORD targetPid = 0;
    DWORD lastPid = 0;
    int stableCount = 0;
    bool foregroundOnce = false;
    const int requiredStable = 3;
    const int timeoutSeconds = 90;

    for (int t = 0; t < timeoutSeconds; t++) {
        HWND hwndCheck = FindWindowW(NULL, L"Automobilista 2");
        if (hwndCheck && !foregroundOnce) {
            HWND hwndConsole = GetConsoleWindow();
            if (hwndConsole) {
                ShowWindow(hwndConsole, SW_RESTORE);
                SetForegroundWindow(hwndConsole);
                SetWindowPos(hwndConsole, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
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

    if (targetPid == 0) {
        targetPid = GetProcessIdByName(L"AMS2AVX.exe");
        if (targetPid == 0) targetPid = GetProcessIdByName(L"AMS2.exe");
        if (targetPid == 0) {
            std::cerr << "Game process not found. Abort." << std::endl;
            Sleep(3000);
            return 1;
        }
        std::cout << "Timeout. Trying with PID " << targetPid << " ..." << std::endl;
    }
    else {
        std::cout << "Game ready (PID: " << targetPid << ")." << std::endl;
    }
    g_TargetPid = targetPid;

    Sleep(1000);

    //std::cout << "Injecting DLL ..." << std::endl;

    wchar_t exePathW2[MAX_PATH];
    GetModuleFileNameW(NULL, exePathW2, MAX_PATH);
    fs::path launcherDir2 = fs::path(exePathW2).parent_path();
    fs::path dllPathFs = launcherDir2.parent_path() / L"ProxyDLL" / L"ProxyDll.dll";

    std::string dllPathStr = dllPathFs.string();
    const char* dllPath = dllPathStr.c_str();
    const wchar_t* dllName = L"ProxyDll.dll";

    bool success = false;
    const int maxRetries = 5;

    for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
        if (IsModuleLoaded(targetPid, dllName)) {
            success = true;
            break;
        }

        if (!IsProcessAms2(targetPid)) {
            std::cerr << "Target process not found. Abort." << std::endl;
            break;
        }

        int result = InjectDLL(targetPid, dllPath);

        if (result == 1) {
            Sleep(200);
            success = IsModuleLoaded(targetPid, dllName);
        }
        else if (result == 2) {
            std::cerr << "Abort: LoadLibrary-Thread stuck. No retry." << std::endl;
            break;
        }

        if (!success && attempt < maxRetries) {
            std::cout << "Attempt " << attempt << " failed. Next attempt in 600 ms..." << std::endl;
            Sleep(600);
        }
    }

    if (success) {
        //std::cout << "Success!" << std::endl;
    }
    else {
        std::cerr << "DLL injection failed! Exiting launcher." << std::endl;
        return 0;
    }

    std::cout << "\n-----------------------------------------------------" << std::endl;
    std::cout << "PLEASE READ:" << std::endl;
    std::cout << "Once you are in the main menu, go to the single race." << std::endl;
    std::cout << "Then press [" << GetKeyName(g_HotkeyApply) << "] on your keyboard to patch the settings." << std::endl;
    std::cout << "The patch will NOT be applied outside the single race.\n" << std::endl;
    std::cout << "Wait for the confirmation in the launcher. Don't expect" << std::endl;
    std::cout << "the settings to be directly visible - they are applied in" << std::endl;
    std::cout << "the background. You'll have it when you start the session." << std::endl;
    std::cout << "-----------------------------------------------------\n" << std::endl;

    std::cout << "Waiting for main menu ..." << std::endl;

    while (g_Running && IsProcessAms2(targetPid)) {
        if (CheckDllReady()) {
            break;
        }

        HWND hwnd = FindWindowW(NULL, L"Automobilista 2");
        if (hwnd == NULL && targetPid != 0) {
            Sleep(1000);
            if (FindWindowW(NULL, L"Automobilista 2") == NULL && !IsProcessAms2(targetPid)) {
                g_Running = false;
                break;
            }
        }
        Sleep(1000);
    }

    if (!g_Running) {
        std::cout << "Game closed before reaching main menu. Exiting launcher." << std::endl;
        return 0;
    }

    BringLauncherToFront();

    std::thread listener(HotkeyListener);
    listener.detach();

    while (IsProcessAms2(targetPid)) {
        HWND hwnd = FindWindowW(NULL, L"Automobilista 2");
        if (hwnd == NULL && targetPid != 0) {
            Sleep(1000);
            if (FindWindowW(NULL, L"Automobilista 2") == NULL && !IsProcessAms2(targetPid)) {
                break;
            }
        }
        Sleep(2000);
    }

    g_Running = false;
    std::cout << "AMS2 closed. Exiting launcher." << std::endl;

    if (g_hAuthTokenMapping) {
        CloseHandle(g_hAuthTokenMapping);
        g_hAuthTokenMapping = NULL;
    }

    return 0;
}