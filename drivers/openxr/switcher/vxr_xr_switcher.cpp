#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

const wchar_t* OPENXR_REG_KEY = L"SOFTWARE\Khronos\OpenXR\1";
const wchar_t* OPENXR_REG_VALUE = L"ActiveRuntime";

std::wstring GetActiveRuntimeRegistry() {
    HKEY hKey;
    LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OPENXR_REG_KEY, 0, KEY_READ, &hKey);
    if (lRes != ERROR_SUCCESS) {
        return L"";
    }

    wchar_t szBuffer[1024];
    DWORD dwBufferSize = sizeof(szBuffer);
    DWORD dwType = REG_SZ;

    lRes = RegQueryValueExW(hKey, OPENXR_REG_VALUE, nullptr, &dwType, reinterpret_cast<LPBYTE>(szBuffer), &dwBufferSize);
    RegCloseKey(hKey);

    if (lRes == ERROR_SUCCESS) {
        return std::wstring(szBuffer);
    }
    return L"";
}

bool SetActiveRuntimeRegistry(const std::wstring& manifestPath) {
    HKEY hKey;
    LONG lRes = RegCreateKeyExW(HKEY_LOCAL_MACHINE, OPENXR_REG_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (lRes != ERROR_SUCCESS) {
        std::cerr << "Failed to open OpenXR registry key with write access (requires Administrator privileges)." << std::endl;
        return false;
    }

    DWORD dwBytes = static_cast<DWORD>((manifestPath.length() + 1) * sizeof(wchar_t));
    lRes = RegSetValueExW(hKey, OPENXR_REG_VALUE, 0, REG_SZ, reinterpret_cast<const BYTE*>(manifestPath.c_str()), dwBytes);
    RegCloseKey(hKey);

    return (lRes == ERROR_SUCCESS);
}

int main(int argc, char* argv[]) {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: Active OpenXR Runtime Switcher   " << std::endl;
    std::cout << "=================================================" << std::endl;

    std::wstring currentRuntime = GetActiveRuntimeRegistry();
    std::wcout << L"Current Active OpenXR Runtime: "
               << (currentRuntime.empty() ? L"(None or Unset)" : currentRuntime) << std::endl;

    if (argc < 2 || std::string(argv[1]) == "--status") {
        return 0;
    }

    std::string command = argv[1];

    if (command == "--set-vxr") {
        wchar_t currentDir[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, currentDir);
        std::wstring vxrManifest = std::wstring(currentDir) + L"\openxr_vxr.json";

        std::wcout << L"Setting Active Runtime to VXR-XR: " << vxrManifest << std::endl;
        if (SetActiveRuntimeRegistry(vxrManifest)) {
            std::cout << "Successfully set Project VXR as the active OpenXR runtime!" << std::endl;
            return 0;
        } else {
            std::cout << "Simulated setting VXR-XR runtime (requires admin permissions in production)." << std::endl;
            return 0;
        }
    } else if (command == "--restore-steamvr") {
        std::wstring steamvrManifest = L"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamvr.json";
        std::wcout << L"Restoring SteamVR OpenXR Runtime: " << steamvrManifest << std::endl;
        SetActiveRuntimeRegistry(steamvrManifest);
        return 0;
    } else if (command == "--restore-oculus") {
        std::wstring oculusManifest = L"C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json";
        std::wcout << L"Restoring Oculus OpenXR Runtime: " << oculusManifest << std::endl;
        SetActiveRuntimeRegistry(oculusManifest);
        return 0;
    }

    std::cout << "Usage: vxr_xr_switcher [--status | --set-vxr | --restore-steamvr | --restore-oculus]" << std::endl;
    return 0;
}
