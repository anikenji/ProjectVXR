#include "VxrVideoEncoder.h"
#include "VxrNvencEncoder.h"
#include "VxrAmfEncoder.h"

namespace vxr {

std::unique_ptr<IVxrVideoEncoder> CreateVideoEncoder(VxrEncoderBackend backend) {
    switch (backend) {
        case VXR_BACKEND_NVENC:
            return std::make_unique<VxrNvencEncoder>();
        case VXR_BACKEND_AMF:
            return std::make_unique<VxrAmfEncoder>();
        case VXR_BACKEND_AUTO:
        default: {
            // Prefer NVENC on NVIDIA hardware
            auto nvenc = std::make_unique<VxrNvencEncoder>();
            return nvenc;
        }
    }
}

} // namespace vxr
