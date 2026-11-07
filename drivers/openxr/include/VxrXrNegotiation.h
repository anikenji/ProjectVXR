#ifndef VXR_XR_NEGOTIATION_H
#define VXR_XR_NEGOTIATION_H

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_EXTENSION_PROTOTYPES

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef VXR_OPENXR_RUNTIME_EXPORTS
    #define VXR_XR_API __declspec(dllexport)
  #else
    #define VXR_XR_API
  #endif
#else
  #define VXR_XR_API
#endif

typedef enum XrLoaderInterfaceStructs {
    XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED = 0,
    XR_LOADER_INTERFACE_STRUCT_LOADER_INFO = 1,
    XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST = 2,
} XrLoaderInterfaceStructs;

#define XR_LOADER_INFO_STRUCT_VERSION 1
#define XR_RUNTIME_REQUEST_STRUCT_VERSION 1
#define XR_CURRENT_LOADER_RUNTIME_VERSION 1

typedef struct XrNegotiateLoaderInfo {
    XrLoaderInterfaceStructs structType;
    uint32_t structVersion;
    size_t structSize;
    uint32_t minInterfaceVersion;
    uint32_t maxInterfaceVersion;
    XrVersion minApiVersion;
    XrVersion maxApiVersion;
} XrNegotiateLoaderInfo;

typedef struct XrNegotiateRuntimeRequest {
    XrLoaderInterfaceStructs structType;
    uint32_t structVersion;
    size_t structSize;
    uint32_t runtimeInterfaceVersion;
    XrVersion runtimeApiVersion;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr;
} XrNegotiateRuntimeRequest;

/// Khronos OpenXR Loader-Runtime negotiation entry point
VXR_XR_API XrResult xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest
);

#ifdef __cplusplus
}
#endif

#endif // VXR_XR_NEGOTIATION_H
