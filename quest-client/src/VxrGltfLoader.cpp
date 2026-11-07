#include "VxrGltfLoader.h"

namespace vxr {

bool LoadVxrEnvironmentGlbFile(const std::string& filepath, GltfModel* outModel) {
    VxrGltfLoader loader;
    return loader.LoadFromFile(filepath, outModel);
}

bool LoadVxrEnvironmentGlbMemory(const uint8_t* data, size_t size, GltfModel* outModel) {
    VxrGltfLoader loader;
    return loader.LoadFromMemory(data, size, outModel);
}

} // namespace vxr
