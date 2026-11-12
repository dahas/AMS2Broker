#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

static void PrintError(const char* step) {
    std::cerr << "  [" << step << "] Windows-Fehlercode: " << GetLastError() << std::endl;
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
    return _wcsicmp(file.c_str(), L"AMS2AVX.exe") == 0;
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
        std::cerr << "  [WaitForSingleObject] Timeout - LoadLibrary hängt noch." << std::endl;
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
        std::cerr << "  [LoadLibrary] lieferte 0 - DLL wurde nicht geladen "
            "(Abhängigkeit fehlt? DllMain FALSE? Architektur?)." << std::endl;
        return 0;
    }

    return 1;
}

int main() {
    std::cout << "Launching AMS2 ..." << std::endl;

    wchar_t appPath[] = L"E:\\Steam\\steamapps\\common\\Automobilista 2\\AMS2AVX.exe";
    wchar_t workingDir[] = L"E:\\Steam\\steamapps\\common\\Automobilista 2";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(appPath, NULL, NULL, NULL, FALSE, 0, NULL, workingDir, &si, &pi)) {
        std::cerr << "Fehler beim Starten von AMS2AVX.exe! Windows-Fehlercode: "
            << GetLastError() << std::endl;
        Sleep(3000);
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "Process started (Start-PID: " << pi.dwProcessId
        << "). Waiting on Render-Window ..." << std::endl;

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
            std::cerr << "Spielprozess nicht gefunden. Abbruch." << std::endl;
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

    const char* dllPath = "D:\\DotNet\\ProxyDLL\\ProxyDLL.dll";
    const wchar_t* dllName = L"ProxyDLL.dll";

    bool success = false;
    const int maxRetries = 5;

    for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {

        if (IsModuleLoaded(targetPid, dllName)) {
            success = true;
            break;
        }

        if (!IsProcessAms2(targetPid)) {
            std::cerr << "Zielprozess existiert nicht mehr. Abbruch." << std::endl;
            break;
        }

        int result = InjectDLL(targetPid, dllPath);

        if (result == 1) {
            Sleep(200);
            success = IsModuleLoaded(targetPid, dllName);
            if (!success) {
                std::cerr << "  LoadLibrary meldete Erfolg, Modul aber nicht gelistet." << std::endl;
            }
        }
        else if (result == 2) {
            std::cerr << "Abbruch: LoadLibrary-Thread hängt. Kein Retry." << std::endl;
            break;
        }

        if (!success && attempt < maxRetries) {
            std::cout << "Versuch " << attempt << " fehlgeschlagen, neuer Versuch in 600 ms..."
                << std::endl;
            Sleep(600);
        }
    }

    if (success) {
        std::cout << "Successfully initialized!" << std::endl;
    }
    else {
        std::cerr << "Injection failed!" << std::endl;
    }

    Sleep(3000);
    return success ? 0 : 1;
}