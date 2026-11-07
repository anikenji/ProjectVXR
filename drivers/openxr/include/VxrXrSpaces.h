#ifndef VXR_XR_SPACES_H
#define VXR_XR_SPACES_H

#include "VxrXrNegotiation.h"

namespace vxr {

class VxrXrSpace {
public:
    VxrXrSpace(XrReferenceSpaceType type, const XrPosef& poseInReferenceSpace);
    ~VxrXrSpace();

    XrReferenceSpaceType GetType() const { return m_type; }
    const XrPosef& GetPoseInSpace() const { return m_poseInSpace; }

    static XrResult LocateSpace(
        const VxrXrSpace* space,
        const VxrXrSpace* baseSpace,
        XrTime time,
        XrSpaceLocation* location
    );

private:
    XrReferenceSpaceType m_type;
    XrPosef m_poseInSpace;
};

} // namespace vxr

#endif // VXR_XR_SPACES_H
