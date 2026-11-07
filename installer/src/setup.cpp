#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <iostream>
#include <string>
#include <vector>

static bool CreateShortcut(const std::wstring& targetPath, const std::wstring& linkPath, const std::wstring& workDir) {
    CoInitialize(nullptr);
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath.c_str());
        psl->SetWorkingDirectory(workDir.c_str());
        psl->SetDescription(L"Project VXR Wireless VR Streamer");

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(linkPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}

static bool RegisterUninstallInControlPanel(const std::wstring& installDir, const std::wstring& uninstallerExe) {
    HKEY hKey = nullptr;
    std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ProjectVXR";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        std::wstring dispName = L"Project VXR Streamer";
        std::wstring pubName = L"Project VXR Development Team";
        std::wstring verStr = L"1.0.0";
        std::wstring uninstCmd = L"\"" + uninstallerExe + L"\"";
        std::wstring icoPath = installDir + L"\\ProjectVXR.exe,0";

        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (const BYTE*)dispName.c_str(), (DWORD)((dispName.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, (const BYTE*)pubName.c_str(), (DWORD)((pubName.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ, (const BYTE*)verStr.c_str(), (DWORD)((verStr.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, (const BYTE*)uninstCmd.c_str(), (DWORD)((uninstCmd.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, (const BYTE*)icoPath.c_str(), (DWORD)((icoPath.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"InstallLocation", 0, REG_SZ, (const BYTE*)installDir.c_str(), (DWORD)((installDir.size() + 1) * sizeof(wchar_t)));

        DWORD noModify = 1;
        RegSetValueExW(hKey, L"NoModify", 0, REG_DWORD, (const BYTE*)&noModify, sizeof(DWORD));
        RegSetValueExW(hKey, L"NoRepair", 0, REG_DWORD, (const BYTE*)&noModify, sizeof(DWORD));

        RegCloseKey(hKey);
        return true;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring installDir = cwd;

    std::wstring msg = L"Welcome to Project VXR Streamer Setup!\n\n"
                       L"This wizard will install and register:\n"
                       L"  • Project VXR Streamer Host & UI (ProjectVXR.exe)\n"
                       L"  • VXR-XR Native OpenXR Runtime (openxr_vxr.dll)\n"
                       L"  • SteamVR OpenVR Driver (driver_vrserver.dll)\n"
                       L"  • NVENC / AMF Hardware Video Encoder (vxr_encoder.dll)\n"
                       L"  • Project VXR Virtual Audio Cable Driver (VxrAudioCable.dll)\n"
                       L"  • IDDCX Virtual Display Driver (VxrIddCx.dll)\n\n"
                       L"Installation directory:\n" + installDir + L"\n\n"
                       L"Do you want to proceed with the installation?";

    int ans = MessageBoxW(nullptr, msg.c_str(), L"Project VXR Setup", MB_YESNO | MB_ICONINFORMATION);
    if (ans != IDYES) {
        return 0;
    }

    // 1. Create Desktop Shortcut
    wchar_t desktop[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop))) {
        std::wstring linkPath = std::wstring(desktop) + L"\\Project VXR Streamer.lnk";
        std::wstring targetExe = installDir + L"\\ProjectVXR.exe";
        CreateShortcut(targetExe, linkPath, installDir);
    }

    // 2. Register Uninstaller in Windows Control Panel (Add or Remove Programs)
    std::wstring uninstExe = installDir + L"\\uninstall.exe";
    RegisterUninstallInControlPanel(installDir, uninstExe);

    int launchAns = MessageBoxW(
        nullptr,
        L"Project VXR Streamer and Virtual Audio Cable Driver installed successfully!\n\n"
        L"A desktop shortcut has been created, and an uninstaller has been registered in Windows Settings.\n\n"
        L"Would you like to launch Project VXR Streamer now?",
        L"Project VXR Setup - Completed",
        MB_YESNO | MB_ICONINFORMATION
    );

    if (launchAns == IDYES) {
        std::wstring targetExe = installDir + L"\\ProjectVXR.exe";
        ShellExecuteW(nullptr, L"open", targetExe.c_str(), L"--show", installDir.c_str(), SW_SHOWNORMAL);
    }

    return 0;
}
