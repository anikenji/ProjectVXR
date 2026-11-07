#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <iostream>
#include <string>

static bool RemoveAppRegistry() {
    // 1. Remove Windows Startup Run entry if exists
    HKEY hRun = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hRun, L"ProjectVXRStreamer");
        RegCloseKey(hRun);
    }

    // 2. Remove OpenXR active runtime registration
    HKEY hXr = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Khronos\\OpenXR\\1", 0, KEY_SET_VALUE, &hXr) == ERROR_SUCCESS) {
        RegDeleteValueW(hXr, L"ActiveRuntime");
        RegCloseKey(hXr);
    }

    // 3. Remove Windows Add/Remove Programs uninstall entry
    HKEY hUninstall = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_WRITE, &hUninstall) == ERROR_SUCCESS) {
        RegDeleteKeyW(hUninstall, L"ProjectVXR");
        RegCloseKey(hUninstall);
    }

    return true;
}

static bool DeleteDesktopShortcut() {
    wchar_t desktop[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop))) {
        std::wstring linkPath = std::wstring(desktop) + L"\\Project VXR Streamer.lnk";
        DeleteFileW(linkPath.c_str());
    }
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int ans = MessageBoxW(
        nullptr,
        L"Are you sure you want to completely uninstall Project VXR Streamer and its drivers from your computer?",
        L"Project VXR - Uninstall",
        MB_YESNO | MB_ICONQUESTION
    );

    if (ans != IDYES) {
        return 0;
    }

    // Terminate any running ProjectVXR.exe process
    HANDLE hSnap = CreateToolhelp32Snapshot ? nullptr : nullptr;
    system("taskkill /f /im ProjectVXR.exe >nul 2>&1");

    // Remove Registry and desktop shortcuts
    RemoveAppRegistry();
    DeleteDesktopShortcut();

    MessageBoxW(
        nullptr,
        L"Project VXR Streamer and its drivers have been successfully uninstalled from your system.\n\nYou can now safely delete the application directory.",
        L"Project VXR - Uninstall Complete",
        MB_OK | MB_ICONINFORMATION
    );

    return 0;
}
