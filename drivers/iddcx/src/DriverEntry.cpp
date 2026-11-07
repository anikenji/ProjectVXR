#include <windows.h>
#include <iostream>

#if defined(_WIN32)

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

// UMDF v2 DriverEntry export
extern "C" __declspec(dllexport) DWORD WINAPI DriverEntry(void* DriverObject, void* RegistryPath) {
    // WDF / IddCx driver entry point
    return 0; // STATUS_SUCCESS
}

#endif
