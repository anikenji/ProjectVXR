#ifndef VXR_NVAPI_OPTIMIZER_H
#define VXR_NVAPI_OPTIMIZER_H

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <string>
#include <cstdint>

namespace vxr {

/// Implements Phase B.1, B.2, and B.3 from PROJECT_VXR_VD_INHERITANCE_PLAN.md:
/// - Queries 100% real NVIDIA driver version via nvml.dll (nvmlInit_v2 / nvmlSystemGetDriverVersion)
///   and nvapi64.dll (nvapi_QueryInterface(0x0150E828) = NvAPI_Initialize,
///   nvapi_QueryInterface(0x2926AAAD) = NvAPI_SYS_GetDriverAndBranchVersion).
/// - Forces driver-level V-Sync off so DXGI Desktop Duplication can capture at 90Hz/120Hz
///   even on a 60Hz physical monitor.
/// - Queries the discrete high-performance NVIDIA adapter LUID on multi-GPU systems.
class VxrNvapiOptimizer {
public:
    VxrNvapiOptimizer();
    ~VxrNvapiOptimizer();

    /// Queries the exact installed NVIDIA driver version string (e.g. "566.14") from nvml.dll or nvapi64.dll.
    static std::wstring QueryExactDriverVersion(const std::wstring& gpuDescription = L"");

    /// Initializes NVAPI and disables driver-level V-Sync for the current process so capture can run at 90/120 FPS.
    static bool DisableDriverVsync();

    /// Enumerates DXGI 1.6 adapters by high-performance preference and returns the discrete GPU LUID.
    static LUID GetHighPerformanceAdapterLuid(std::wstring* outAdapterName = nullptr);
};

} // namespace vxr

#endif // VXR_NVAPI_OPTIMIZER_H
