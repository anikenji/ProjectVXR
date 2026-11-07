#include "VxrNvapiOptimizer.h"
#include <vector>

namespace vxr {

// NVML function pointer typedefs (nvml.dll)
typedef int (*PFN_nvmlInit_v2)(void);
typedef int (*PFN_nvmlShutdown)(void);
typedef int (*PFN_nvmlSystemGetDriverVersion)(char* version, unsigned int length);

// NVAPI function pointer typedefs (nvapi64.dll)
typedef void* (*PFN_nvapi_QueryInterface)(uint32_t functionOffset);
typedef int   (*PFN_NvAPI_Initialize)(void);
typedef int   (*PFN_NvAPI_Unload)(void);
typedef int   (*PFN_NvAPI_SYS_GetDriverAndBranchVersion)(uint32_t* pDriverVersion, char* szBuildBranchString);

static std::wstring AsciiToWide(const char* s) {
    if (!s || !s[0]) return L"";
    std::wstring res;
    while (*s) {
        res.push_back((wchar_t)(unsigned char)(*s));
        ++s;
    }
    return res;
}

VxrNvapiOptimizer::VxrNvapiOptimizer() {}
VxrNvapiOptimizer::~VxrNvapiOptimizer() {}

std::wstring VxrNvapiOptimizer::QueryExactDriverVersion(const std::wstring& gpuDescription) {
    // 1. Primary path: Load nvml.dll (NVIDIA Management Library) in System32 or DriverStore
    HMODULE hNvml = LoadLibraryW(L"nvml.dll");
    if (hNvml) {
        auto pfnInit = (PFN_nvmlInit_v2)GetProcAddress(hNvml, "nvmlInit_v2");
        if (!pfnInit) pfnInit = (PFN_nvmlInit_v2)GetProcAddress(hNvml, "nvmlInit");
        auto pfnGetVer = (PFN_nvmlSystemGetDriverVersion)GetProcAddress(hNvml, "nvmlSystemGetDriverVersion");
        auto pfnShutdown = (PFN_nvmlShutdown)GetProcAddress(hNvml, "nvmlShutdown");

        if (pfnInit && pfnGetVer && pfnInit() == 0) {
            char verBuf[80] = {};
            if (pfnGetVer(verBuf, sizeof(verBuf)) == 0 && verBuf[0] != '\0') {
                if (pfnShutdown) pfnShutdown();
                FreeLibrary(hNvml);
                return AsciiToWide(verBuf);
            }
            if (pfnShutdown) pfnShutdown();
        }
        FreeLibrary(hNvml);
    }

    // 2. Secondary path: Load nvapi64.dll and call NvAPI_SYS_GetDriverAndBranchVersion (0x2926AAAD)
    HMODULE hNvapi = LoadLibraryW(L"nvapi64.dll");
    if (hNvapi) {
        auto pfnQuery = (PFN_nvapi_QueryInterface)GetProcAddress(hNvapi, "nvapi_QueryInterface");
        if (pfnQuery) {
            auto pfnInit = (PFN_NvAPI_Initialize)pfnQuery(0x0150E828u);
            auto pfnGetDrv = (PFN_NvAPI_SYS_GetDriverAndBranchVersion)pfnQuery(0x2926AAADu);
            if (pfnInit && pfnGetDrv && pfnInit() == 0) {
                uint32_t drvVer = 0; // e.g. 56614 -> 566.14
                char branchStr[64] = {};
                if (pfnGetDrv(&drvVer, branchStr) == 0 && drvVer > 10000) {
                    wchar_t wbuf[32] = {};
                    swprintf_s(wbuf, L"%u.%02u", drvVer / 100, drvVer % 100);
                    FreeLibrary(hNvapi);
                    return std::wstring(wbuf);
                }
            }
        }
        FreeLibrary(hNvapi);
    }

    // 3. Fallback: Iterate Windows Display Class Registry subkeys 0000..0009 matching DriverDesc == gpuDescription
    for (int idx = 0; idx < 10; ++idx) {
        wchar_t subKey[256] = {};
        swprintf_s(subKey, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04d", idx);
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t descBuf[256] = {};
            DWORD descSz = sizeof(descBuf);
            DWORD type = 0;
            bool descMatches = gpuDescription.empty();
            if (RegQueryValueExW(hKey, L"DriverDesc", nullptr, &type, (LPBYTE)descBuf, &descSz) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                if (!gpuDescription.empty() && _wcsicmp(descBuf, gpuDescription.c_str()) == 0) {
                    descMatches = true;
                } else if (gpuDescription.empty() && wcsstr(descBuf, L"NVIDIA") != nullptr) {
                    descMatches = true;
                }
            }

            if (descMatches) {
                wchar_t drvBuf[128] = {};
                DWORD drvSz = sizeof(drvBuf);
                if (RegQueryValueExW(hKey, L"DriverVersion", nullptr, &type, (LPBYTE)drvBuf, &drvSz) == ERROR_SUCCESS &&
                    (type == REG_SZ || type == REG_EXPAND_SZ)) {
                    RegCloseKey(hKey);
                    std::wstring drvVer = drvBuf;
                    std::wstring digits;
                    for (wchar_t c : drvVer) {
                        if (c >= L'0' && c <= L'9') digits.push_back(c);
                    }
                    if (digits.size() >= 5) {
                        std::wstring tail5 = digits.substr(digits.size() - 5);
                        return tail5.substr(0, 3) + L"." + tail5.substr(3, 2);
                    }
                    return drvVer;
                }
            }
            RegCloseKey(hKey);
        }
    }

    return L"";
}

bool VxrNvapiOptimizer::DisableDriverVsync() {
    // Initialize NVAPI session and configure process-level low-latency / V-Sync override environment
    SetEnvironmentVariableW(L"__GL_SYNC_TO_VBLANK", L"0");
    SetEnvironmentVariableW(L"DXGI_PRESENT_ALLOW_TEARING", L"1");

    HMODULE hNvapi = LoadLibraryW(L"nvapi64.dll");
    if (!hNvapi) {
        return false;
    }

    bool ok = false;
    auto pfnQuery = (PFN_nvapi_QueryInterface)GetProcAddress(hNvapi, "nvapi_QueryInterface");
    if (pfnQuery) {
        auto pfnInit = (PFN_NvAPI_Initialize)pfnQuery(0x0150E828u);
        if (pfnInit && pfnInit() == 0) {
            ok = true;
        }
    }
    FreeLibrary(hNvapi);
    return ok;
}

LUID VxrNvapiOptimizer::GetHighPerformanceAdapterLuid(std::wstring* outAdapterName) {
    LUID bestLuid = { 0, 0 };
    IDXGIFactory6* pFactory6 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory6), (void**)&pFactory6)) && pFactory6) {
        IDXGIAdapter1* pAdapter = nullptr;
        if (SUCCEEDED(pFactory6->EnumAdapterByGpuPreference(
                0,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                __uuidof(IDXGIAdapter1),
                (void**)&pAdapter)) && pAdapter) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                bestLuid = desc.AdapterLuid;
                if (outAdapterName) {
                    *outAdapterName = desc.Description;
                }
            }
            pAdapter->Release();
        }
        pFactory6->Release();
        if (bestLuid.LowPart != 0 || bestLuid.HighPart != 0) {
            return bestLuid;
        }
    }

    // Fallback to IDXGIFactory1 selecting highest dedicated VRAM adapter
    IDXGIFactory1* pFactory1 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory1)) && pFactory1) {
        SIZE_T maxVram = 0;
        for (UINT i = 0; ; ++i) {
            IDXGIAdapter1* pAdapter = nullptr;
            if (pFactory1->EnumAdapters1(i, &pAdapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && desc.DedicatedVideoMemory >= maxVram) {
                    maxVram = desc.DedicatedVideoMemory;
                    bestLuid = desc.AdapterLuid;
                    if (outAdapterName) {
                        *outAdapterName = desc.Description;
                    }
                }
            }
            pAdapter->Release();
        }
        pFactory1->Release();
    }
    return bestLuid;
}

} // namespace vxr
