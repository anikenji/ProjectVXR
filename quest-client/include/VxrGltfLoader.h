#ifndef VXR_GLTF_LOADER_H
#define VXR_GLTF_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <cstdlib>

namespace vxr {

static constexpr uint32_t VXR_GLB_MAGIC      = 0x46546C67u; // "glTF"
static constexpr uint32_t VXR_GLB_CHUNK_JSON = 0x4E4F534Au; // "JSON"
static constexpr uint32_t VXR_GLB_CHUNK_BIN  = 0x004E4942u; // "BIN\0"

#pragma pack(push, 1)
struct GlbFileHeader {
    uint32_t magic;   // 0x46546C67 ("glTF")
    uint32_t version; // 2
    uint32_t length;  // total byte length of the .glb file
};

struct GlbChunkHeader {
    uint32_t chunkLength;
    uint32_t chunkType; // 0x4E4F534A ("JSON") or 0x004E4942 ("BIN\0")
};
#pragma pack(pop)

struct GltfVertex {
    float pos[3];
    float normal[3];
    float color[3];
    float uv[2];
};

struct GltfMesh {
    std::string name;
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t> indices;
};

struct GltfModel {
    std::string filepath;
    uint32_t version = 0;
    std::string json_chunk;
    std::vector<GltfMesh> meshes;
    bool valid = false;
};

namespace detail {

struct GlbBufferViewDesc {
    uint32_t byteOffset = 0;
    uint32_t byteLength = 0;
    uint32_t byteStride = 0;
};

struct GlbAccessorDesc {
    int bufferView = -1;
    uint32_t byteOffset = 0;
    uint32_t componentType = 5126; // 5126=FLOAT, 5123=USHORT, 5125=UINT
    uint32_t count = 0;
    std::string type;
};

inline int ParseJsonIntField(const std::string& objStr, const char* key, int defaultVal) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = objStr.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos = objStr.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < objStr.size() && (objStr[pos] == ' ' || objStr[pos] == '\t' || objStr[pos] == '\n' || objStr[pos] == '\r')) {
        pos++;
    }
    return std::atoi(objStr.c_str() + pos);
}

inline std::string ParseJsonStringField(const std::string& objStr, const char* key) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = objStr.find(pattern);
    if (pos == std::string::npos) return "";
    pos = objStr.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    size_t q1 = objStr.find('"', pos + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = objStr.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return objStr.substr(q1 + 1, q2 - q1 - 1);
}

inline void ParseJsonObjectsArray(const std::string& json, const char* arrayKey, std::vector<std::string>* outObjects) {
    outObjects->clear();
    std::string pattern = std::string("\"") + arrayKey + "\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::string::npos) return;
    size_t arrStart = json.find('[', keyPos + pattern.size());
    if (arrStart == std::string::npos) return;

    int bracketDepth = 0;
    int braceDepth = 0;
    size_t objStart = 0;
    for (size_t i = arrStart; i < json.size(); ++i) {
        char c = json[i];
        if (c == '[') {
            bracketDepth++;
        } else if (c == ']') {
            bracketDepth--;
            if (bracketDepth == 0) break;
        } else if (c == '{') {
            if (braceDepth == 0) objStart = i;
            braceDepth++;
        } else if (c == '}') {
            braceDepth--;
            if (braceDepth == 0 && bracketDepth == 1) {
                outObjects->push_back(json.substr(objStart, i - objStart + 1));
            }
        }
    }
}

} // namespace detail

/// Binary glTF 2.0 (.glb) 3D Environment Room Mesh Loader
class VxrGltfLoader {
public:
    VxrGltfLoader() = default;
    ~VxrGltfLoader() = default;

    /// Loads and parses a binary .glb (glTF 2.0) file from disk
    inline bool LoadFromFile(const std::string& filepath, GltfModel* outModel) const {
        if (!outModel) return false;
        outModel->valid = false;
        outModel->filepath = filepath;

        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }

        std::streamsize fileSize = file.tellg();
        if (fileSize < static_cast<std::streamsize>(sizeof(GlbFileHeader) + sizeof(GlbChunkHeader))) {
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            return false;
        }

        bool ok = LoadFromMemory(buffer.data(), buffer.size(), outModel);
        outModel->filepath = filepath;
        return ok;
    }

    /// Loads and parses a binary .glb (glTF 2.0) buffer in memory
    inline bool LoadFromMemory(const uint8_t* data, size_t size, GltfModel* outModel) const {
        if (!data || size < sizeof(GlbFileHeader) + sizeof(GlbChunkHeader) || !outModel) {
            return false;
        }

        outModel->meshes.clear();
        outModel->json_chunk.clear();
        outModel->valid = false;

        GlbFileHeader hdr = {};
        std::memcpy(&hdr, data, sizeof(GlbFileHeader));
        if (hdr.magic != VXR_GLB_MAGIC || hdr.version != 2 || hdr.length > size) {
            return false;
        }
        outModel->version = hdr.version;

        const uint8_t* binData = nullptr;
        size_t binSize = 0;

        size_t offset = sizeof(GlbFileHeader);
        while (offset + sizeof(GlbChunkHeader) <= hdr.length) {
            GlbChunkHeader chunk = {};
            std::memcpy(&chunk, data + offset, sizeof(GlbChunkHeader));
            offset += sizeof(GlbChunkHeader);

            if (offset + chunk.chunkLength > hdr.length) {
                break;
            }

            if (chunk.chunkType == VXR_GLB_CHUNK_JSON) {
                outModel->json_chunk.assign(reinterpret_cast<const char*>(data + offset), chunk.chunkLength);
            } else if (chunk.chunkType == VXR_GLB_CHUNK_BIN) {
                binData = data + offset;
                binSize = chunk.chunkLength;
            }

            offset += chunk.chunkLength;
        }

        if (outModel->json_chunk.empty()) {
            return false;
        }

        std::vector<std::string> bvStrings;
        detail::ParseJsonObjectsArray(outModel->json_chunk, "bufferViews", &bvStrings);
        std::vector<detail::GlbBufferViewDesc> bufferViews;
        for (const auto& s : bvStrings) {
            detail::GlbBufferViewDesc bv;
            bv.byteOffset = static_cast<uint32_t>(detail::ParseJsonIntField(s, "byteOffset", 0));
            bv.byteLength = static_cast<uint32_t>(detail::ParseJsonIntField(s, "byteLength", 0));
            bv.byteStride = static_cast<uint32_t>(detail::ParseJsonIntField(s, "byteStride", 0));
            bufferViews.push_back(bv);
        }

        std::vector<std::string> accStrings;
        detail::ParseJsonObjectsArray(outModel->json_chunk, "accessors", &accStrings);
        std::vector<detail::GlbAccessorDesc> accessors;
        for (const auto& s : accStrings) {
            detail::GlbAccessorDesc acc;
            acc.bufferView = detail::ParseJsonIntField(s, "bufferView", -1);
            acc.byteOffset = static_cast<uint32_t>(detail::ParseJsonIntField(s, "byteOffset", 0));
            acc.componentType = static_cast<uint32_t>(detail::ParseJsonIntField(s, "componentType", 5126));
            acc.count = static_cast<uint32_t>(detail::ParseJsonIntField(s, "count", 0));
            acc.type = detail::ParseJsonStringField(s, "type");
            accessors.push_back(acc);
        }

        if (binData && binSize > 0 && !accessors.empty() && !bufferViews.empty()) {
            int posAccIdx = detail::ParseJsonIntField(outModel->json_chunk, "POSITION", 0);
            int normAccIdx = detail::ParseJsonIntField(outModel->json_chunk, "NORMAL", -1);
            int idxAccIdx = detail::ParseJsonIntField(outModel->json_chunk, "indices", -1);

            if (posAccIdx >= 0 && posAccIdx < static_cast<int>(accessors.size())) {
                const auto& posAcc = accessors[posAccIdx];
                if (posAcc.bufferView >= 0 && posAcc.bufferView < static_cast<int>(bufferViews.size()) && posAcc.count > 0) {
                    const auto& posBv = bufferViews[posAcc.bufferView];
                    size_t posStride = (posBv.byteStride >= 12) ? posBv.byteStride : 12;
                    size_t posStart = static_cast<size_t>(posBv.byteOffset) + posAcc.byteOffset;

                    if (posStart + static_cast<size_t>(posAcc.count) * posStride <= binSize) {
                        GltfMesh mesh;
                        mesh.name = "glb_mesh_0";
                        mesh.vertices.resize(posAcc.count);

                        for (uint32_t v = 0; v < posAcc.count; ++v) {
                            const float* p = reinterpret_cast<const float*>(binData + posStart + v * posStride);
                            mesh.vertices[v].pos[0] = p[0];
                            mesh.vertices[v].pos[1] = p[1];
                            mesh.vertices[v].pos[2] = p[2];
                            mesh.vertices[v].normal[0] = 0.0f;
                            mesh.vertices[v].normal[1] = 1.0f;
                            mesh.vertices[v].normal[2] = 0.0f;
                            mesh.vertices[v].color[0] = 0.22f;
                            mesh.vertices[v].color[1] = 0.26f;
                            mesh.vertices[v].color[2] = 0.34f;
                            mesh.vertices[v].uv[0] = 0.0f;
                            mesh.vertices[v].uv[1] = 0.0f;
                        }

                        if (normAccIdx >= 0 && normAccIdx < static_cast<int>(accessors.size())) {
                            const auto& nAcc = accessors[normAccIdx];
                            if (nAcc.bufferView >= 0 && nAcc.bufferView < static_cast<int>(bufferViews.size()) &&
                                nAcc.count == posAcc.count) {
                                const auto& nBv = bufferViews[nAcc.bufferView];
                                size_t nStride = (nBv.byteStride >= 12) ? nBv.byteStride : 12;
                                size_t nStart = static_cast<size_t>(nBv.byteOffset) + nAcc.byteOffset;
                                if (nStart + static_cast<size_t>(nAcc.count) * nStride <= binSize) {
                                    for (uint32_t v = 0; v < nAcc.count; ++v) {
                                        const float* n = reinterpret_cast<const float*>(binData + nStart + v * nStride);
                                        mesh.vertices[v].normal[0] = n[0];
                                        mesh.vertices[v].normal[1] = n[1];
                                        mesh.vertices[v].normal[2] = n[2];
                                    }
                                }
                            }
                        }

                        if (idxAccIdx >= 0 && idxAccIdx < static_cast<int>(accessors.size())) {
                            const auto& iAcc = accessors[idxAccIdx];
                            if (iAcc.bufferView >= 0 && iAcc.bufferView < static_cast<int>(bufferViews.size()) &&
                                iAcc.count > 0) {
                                const auto& iBv = bufferViews[iAcc.bufferView];
                                size_t iStart = static_cast<size_t>(iBv.byteOffset) + iAcc.byteOffset;
                                if (iAcc.componentType == 5123 && iStart + iAcc.count * 2 <= binSize) {
                                    const uint16_t* idx16 = reinterpret_cast<const uint16_t*>(binData + iStart);
                                    mesh.indices.resize(iAcc.count);
                                    for (uint32_t k = 0; k < iAcc.count; ++k) mesh.indices[k] = idx16[k];
                                } else if (iAcc.componentType == 5125 && iStart + iAcc.count * 4 <= binSize) {
                                    const uint32_t* idx32 = reinterpret_cast<const uint32_t*>(binData + iStart);
                                    mesh.indices.assign(idx32, idx32 + iAcc.count);
                                }
                            }
                        } else {
                            mesh.indices.resize(posAcc.count);
                            for (uint32_t k = 0; k < posAcc.count; ++k) mesh.indices[k] = k;
                        }

                        outModel->meshes.push_back(std::move(mesh));
                    }
                }
            }
        }

        outModel->valid = true;
        return true;
    }

    /// Attempts to load a custom .glb environment room from /sdcard/ProjectVXR/Environments/
    inline bool LoadEnvironmentByIndex(uint32_t envIndex, GltfModel* outModel) const {
        static const char* kEnvFiles[] = {
            "/sdcard/ProjectVXR/Environments/first_class.glb",
            "/sdcard/ProjectVXR/Environments/high_castra.glb",
            "/sdcard/ProjectVXR/Environments/dark_cinema.glb",
            "/sdcard/ProjectVXR/Environments/void_black.glb",
            "/sdcard/ProjectVXR/Environments/nebula_space.glb",
            "/sdcard/ProjectVXR/Environments/home_theater.glb",
            "/sdcard/ProjectVXR/Environments/custom_room.glb"
        };
        uint32_t idx = (envIndex < 7) ? envIndex : 6;
        return LoadFromFile(kEnvFiles[idx], outModel);
    }
};

} // namespace vxr

#endif // VXR_GLTF_LOADER_H
