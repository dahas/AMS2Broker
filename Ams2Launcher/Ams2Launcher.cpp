#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

static void PrintError(const char* step) {
    std::cerr << "  [" << step << "] Windows-Error-Code: " << GetLastError() << std::endl;
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
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return 0;

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
        std::cerr << "  [LoadLibrary] 0 - DLL not loaded "
            "(Missing dependency? DllMain FALSE? Architecture?)." << std::endl;
        return 0;
    }

    return 1;
}

std::wstring GetSteamInstallPath() {
    HKEY hKey;
    wchar_t path[MAX_PATH] = { 0 };
    DWORD bufferSize = sizeof(path);

    // 64-Bit Registry-Pfad für Steam
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

int main(int argc, char* argv[]) {
    std::cout << "Launching AMS2 ..." << std::endl;

    std::wstring appPath;

    if (argc > 1) {
        std::string arg(argv[1]);
        appPath = std::wstring(arg.begin(), arg.end());
    }
    else {
        appPath = GetDefaultAms2Path();
    }

    if (!fs::exists(appPath)) {
        std::cerr << "Error: EXE not found at this location:\n"
            << std::string(appPath.begin(), appPath.end()) << std::endl;
        Sleep(4000);
        return 1;
    }

    std::wstring workingDirW = fs::path(appPath).parent_path().wstring();

    STARTUPINFOW si = { sizeof(si) };

    PROCESS_INFORMATION pi = {};
    std::wstring mutableAppPath = appPath;

    if (!CreateProcessW(mutableAppPath.data(), NULL, NULL, NULL, FALSE, 0, NULL, workingDirW.c_str(), &si, &pi)) {
        std::cerr << "Error launching AMS2! Windows-Error-Code: "
            << GetLastError() << std::endl;
        Sleep(3000);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "Process started (Start-PID: " << pi.dwProcessId
        << "). Waiting on GameEngine ..." << std::endl;

    DWORD targetPid = 0;
    DWORD lastPid = 0;
    int stableCount = 0;
    const int requiredStable = 3;
    const int timeoutSeconds = 60;

    for (int t = 0; t < timeoutSeconds; t++) {
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
            break;
        }
        Sleep(1000);
    }

    if (targetPid == 0) {
        targetPid = GetProcessIdByName(L"AMS2AVX.exe");
        if (targetPid == 0) {
            targetPid = GetProcessIdByName(L"AMS2.exe");
        }
        if (targetPid == 0) {
            std::cerr << "Game process not found. Abort." << std::endl;
            Sleep(3000);
            return 1;
        }
        std::cout << "Timeout. Trying with PID "
            << targetPid << " ..." << std::endl;
    }
    else {
        std::cout << "Game ready (PID: " << targetPid << ")." << std::endl;
    }

    Sleep(1000);

    std::cout << "Injecting DLL ..." << std::endl;

    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(NULL, exePathW, MAX_PATH);
    fs::path launcherDir = fs::path(exePathW).parent_path();
    fs::path dllPathFs = launcherDir.parent_path() / L"ProxyDLL" / L"ProxyDll.dll";

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
            if (!success) {
                std::cerr << "  LoadLibrary successfull. Module not listet." << std::endl;
            }
        }
        else if (result == 2) {
            std::cerr << "Abort: LoadLibrary-Thread stuck. No retry." << std::endl;
            break;
        }

        if (!success && attempt < maxRetries) {
            std::cout << "Attempt " << attempt << " failed. Next attempt in 600 ms..."
                << std::endl;
            Sleep(600);
        }
    }

    if (success) {
        std::cout << "Success!" << std::endl;
    }
    else {
        std::cerr << "Injection failed!" << std::endl;
    }

    Sleep(3000);
    return success ? 0 : 1;
}