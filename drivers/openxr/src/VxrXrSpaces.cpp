#include "VxrXrSpaces.h"
#include <cstring>

namespace vxr {

VxrXrSpace::VxrXrSpace(XrReferenceSpaceType type, const XrPosef& poseInReferenceSpace)
    : m_type(type)
    , m_poseInSpace(poseInReferenceSpace)
{
}

VxrXrSpace::~VxrXrSpace() {
}

XrResult VxrXrSpace::LocateSpace(
    const VxrXrSpace* space,
    const VxrXrSpace* baseSpace,
    XrTime time,
    XrSpaceLocation* location
) {
    if (!space || !baseSpace || !location) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    location->type = XR_TYPE_SPACE_LOCATION;
    location->next = nullptr;
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
                            | XR_SPACE_LOCATION_POSITION_VALID_BIT
                            | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
                            | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

    // View space relative to Local/Stage space (default seated head height 1.7m)
    if (space->GetType() == XR_REFERENCE_SPACE_TYPE_VIEW &&
       (baseSpace->GetType() == XR_REFERENCE_SPACE_TYPE_LOCAL || baseSpace->GetType() == XR_REFERENCE_SPACE_TYPE_STAGE))
    {
        location->pose.position.x = space->GetPoseInSpace().position.x;
        location->pose.position.y = 1.70f + space->GetPoseInSpace().position.y;
        location->pose.position.z = space->GetPoseInSpace().position.z;

        location->pose.orientation.x = 0.0f;
        location->pose.orientation.y = 0.0f;
        location->pose.orientation.z = 0.0f;
        location->pose.orientation.w = 1.0f;
    } else {
        // Identity relative transform
        location->pose.position.x = 0.0f;
        location->pose.position.y = 0.0f;
        location->pose.position.z = 0.0f;

        location->pose.orientation.x = 0.0f;
        location->pose.orientation.y = 0.0f;
        location->pose.orientation.z = 0.0f;
        location->pose.orientation.w = 1.0f;
    }

    return XR_SUCCESS;
}

} // namespace vxr
