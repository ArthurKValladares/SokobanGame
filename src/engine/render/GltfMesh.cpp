#include "engine/render/GltfMesh.hpp"

#include "engine/TaskSystem.hpp"

// glTF parsing is cgltf's job (review item A1). This replaced a hand-rolled
// scanner that built a std::regex per field lookup and could not read more
// than one buffer, a sparse accessor, any KHR_* extension, a glTF material,
// or an animation's interpolation mode.
//
// The implementation is compiled here rather than in a translation unit of
// its own, which is how stb_image is compiled into ImageData.cpp and
// stb_truetype into FontAtlas.cpp. third_party/cgltf is a SYSTEM include, so
// /W4 and clang-tidy leave those seven thousand lines alone while ASan and
// UBSan still instrument them - which is where a parser fed files off the
// internet belongs.
//
// Nothing above this file knows cgltf exists, and it must stay that way:
// GltfMesh.hpp is library-agnostic on purpose so that swapping the parser
// again is a change to one translation unit. See the fastgltf note under
// Important Design Decisions in HANDOFF.md.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

// ---------------------------------------------------------------------------
// Document handling
// ---------------------------------------------------------------------------

// cgltf speaks `const char*` paths and would otherwise fopen them directly.
// Every path here is a std::filesystem::path built from the asset root, and
// on Windows that root can contain characters the narrow ANSI code page
// cannot represent - a player whose user directory is not ASCII is not a
// hypothetical. Handing cgltf UTF-8 and rebuilding a path from it in the read
// callback keeps the round trip lossless, and keeps every read going through
// the same std::filesystem that the rest of the engine uses.
std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path pathFromUtf8(const char* text)
{
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text)));
}

cgltf_result readFileForCgltf(
    const cgltf_memory_options* memoryOptions,
    const cgltf_file_options* fileOptions,
    const char* path,
    cgltf_size* size,
    void** data)
{
    (void)fileOptions;
    // Mirrors cgltf_default_file_read's ownership contract exactly: the block
    // handed back is released by releaseFileForCgltf below, through the same
    // allocator pair.
    auto* allocate = memoryOptions->alloc_func;
    auto* release = memoryOptions->free_func;

    std::ifstream stream(pathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!stream) {
        return cgltf_result_file_not_found;
    }
    const std::streamoff length = stream.tellg();
    if (length < 0) {
        return cgltf_result_io_error;
    }
    stream.seekg(0);

    const auto fileSize = static_cast<cgltf_size>(length);
    void* buffer = allocate
        ? allocate(memoryOptions->user_data, fileSize)
        : std::malloc(fileSize != 0 ? fileSize : 1);
    if (!buffer) {
        return cgltf_result_out_of_memory;
    }
    if (fileSize != 0 &&
        !stream.read(static_cast<char*>(buffer),
            static_cast<std::streamsize>(fileSize))) {
        if (release) {
            release(memoryOptions->user_data, buffer);
        } else {
            std::free(buffer);
        }
        return cgltf_result_io_error;
    }

    *size = fileSize;
    *data = buffer;
    return cgltf_result_success;
}

void releaseFileForCgltf(
    const cgltf_memory_options* memoryOptions,
    const cgltf_file_options* fileOptions,
    void* data)
{
    (void)fileOptions;
    if (memoryOptions->free_func) {
        memoryOptions->free_func(memoryOptions->user_data, data);
    } else {
        std::free(data);
    }
}

std::string_view resultName(cgltf_result result)
{
    switch (result) {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF 1.0";
    default: return "unknown error";
    }
}

struct DocumentDeleter {
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using Document = std::unique_ptr<cgltf_data, DocumentDeleter>;

cgltf_options cgltfOptions()
{
    cgltf_options options {};
    options.file.read = &readFileForCgltf;
    options.file.release = &releaseFileForCgltf;
    return options;
}

// Structure only: the JSON or the GLB's JSON chunk, with no buffer resolved.
// Enough to answer what a file contains, which is all listGltfAnimationNames
// wants - and it deliberately still answers for a file whose .bin is missing.
Document parseDocument(const std::filesystem::path& path)
{
    const cgltf_options options = cgltfOptions();
    const std::string narrowPath = utf8Path(path);

    cgltf_data* parsed = nullptr;
    const cgltf_result result =
        cgltf_parse_file(&options, narrowPath.c_str(), &parsed);
    Document document(parsed);
    if (result != cgltf_result_success) {
        if (result == cgltf_result_file_not_found) {
            throw std::runtime_error(
                "Failed to open glTF file: " + path.string());
        }
        throw std::runtime_error(
            "Failed to parse glTF file: " + path.string() + " (" +
            std::string(resultName(result)) + ")");
    }
    return document;
}

// Structure plus data. cgltf_load_buffers is what makes a GLB chunk, an
// external .bin and a base64 data URI all arrive the same way, and what
// retired the previous loader's one-buffer-only limit.
Document loadDocument(const std::filesystem::path& path)
{
    Document document = parseDocument(path);
    const cgltf_options options = cgltfOptions();
    const std::string narrowPath = utf8Path(path);
    const cgltf_result result =
        cgltf_load_buffers(&options, document.get(), narrowPath.c_str());
    if (result != cgltf_result_success) {
        throw std::runtime_error(
            "Failed to open glTF buffer: " + path.string() + " (" +
            std::string(resultName(result)) + ")");
    }
    return document;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
//
// Thin wrappers that keep the shape and the messages the hand-rolled readers
// had. What is gone from underneath them is the part that was worth deleting:
// component-type conversion, byte strides, normalized integers and sparse
// accessors are cgltf's problem now.

Vec3 readVec3(const cgltf_accessor& accessor, size_t index)
{
    std::array<float, 3> values {};
    if (accessor.type != cgltf_type_vec3 ||
        !cgltf_accessor_read_float(&accessor, index, values.data(), 3)) {
        throw std::runtime_error("Expected a float VEC3 glTF accessor");
    }
    return { values[0], values[1], values[2] };
}

Vec2 readVec2(const cgltf_accessor& accessor, size_t index)
{
    std::array<float, 2> values {};
    if (accessor.type != cgltf_type_vec2 ||
        !cgltf_accessor_read_float(&accessor, index, values.data(), 2)) {
        throw std::runtime_error("Expected a float VEC2 glTF accessor");
    }
    return { values[0], values[1] };
}

Vec4 readVec4(const cgltf_accessor& accessor, size_t index)
{
    std::array<float, 4> values {};
    if (accessor.type != cgltf_type_vec4 ||
        !cgltf_accessor_read_float(&accessor, index, values.data(), 4)) {
        throw std::runtime_error("Expected a float VEC4 glTF accessor");
    }
    return { values[0], values[1], values[2], values[3] };
}

float readScalarFloat(const cgltf_accessor& accessor, size_t index)
{
    float value = 0.0f;
    if (accessor.type != cgltf_type_scalar ||
        !cgltf_accessor_read_float(&accessor, index, &value, 1)) {
        throw std::runtime_error("Expected a float scalar glTF accessor");
    }
    return value;
}

Mat4 readMat4(const cgltf_accessor& accessor, size_t index)
{
    Mat4 matrix;
    if (accessor.type != cgltf_type_mat4 ||
        !cgltf_accessor_read_float(
            &accessor, index, matrix.values.data(), matrix.values.size())) {
        throw std::runtime_error("Expected a float MAT4 glTF accessor");
    }
    return matrix;
}

std::array<uint16_t, 4> readJoints(const cgltf_accessor& accessor, size_t index)
{
    std::array<cgltf_uint, 4> values {};
    if (accessor.type != cgltf_type_vec4 ||
        !cgltf_accessor_read_uint(&accessor, index, values.data(), 4)) {
        throw std::runtime_error(
            "Expected an integer VEC4 JOINTS_0 glTF accessor");
    }
    return {
        static_cast<uint16_t>(values[0]),
        static_cast<uint16_t>(values[1]),
        static_cast<uint16_t>(values[2]),
        static_cast<uint16_t>(values[3]),
    };
}

// cgltf scales integer weights only when the accessor says `normalized`,
// where the previous reader always divided by the component maximum. The two
// differ by one factor common to all four weights, and the caller divides by
// their sum immediately afterwards, so the result is the same either way.
std::array<float, 4> readWeights(const cgltf_accessor& accessor, size_t index)
{
    std::array<float, 4> values {};
    if (accessor.type != cgltf_type_vec4 ||
        !cgltf_accessor_read_float(&accessor, index, values.data(), 4)) {
        throw std::runtime_error("Expected a VEC4 WEIGHTS_0 glTF accessor");
    }
    return values;
}

uint32_t readIndex(const cgltf_accessor& accessor, size_t index)
{
    return static_cast<uint32_t>(cgltf_accessor_read_index(&accessor, index));
}

const cgltf_accessor* findAttribute(
    const cgltf_primitive& primitive,
    cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        // Set 0 only. A second UV set or a second joint influence set is F3's
        // problem, not this file's.
        if (attribute.type == type && attribute.index == 0) {
            return attribute.data;
        }
    }
    return nullptr;
}

const cgltf_accessor& requiredAttribute(
    const cgltf_primitive& primitive,
    cgltf_attribute_type type,
    std::string_view name)
{
    const cgltf_accessor* accessor = findAttribute(primitive, type);
    if (!accessor) {
        throw std::runtime_error(
            "glTF primitive is missing the " + std::string(name) +
            " attribute");
    }
    return *accessor;
}

// The skeleton, flattened. cgltf hands back the parent pointer directly, so
// the child-list walk the previous loader needed to invert is gone.
std::vector<SkeletonNode> skeletonNodes(const cgltf_data& data)
{
    std::vector<SkeletonNode> nodes;
    nodes.reserve(data.nodes_count);
    for (cgltf_size i = 0; i < data.nodes_count; ++i) {
        const cgltf_node& node = data.nodes[i];
        // Defaults are stated rather than assumed: a node carrying a matrix
        // instead of a TRS triple leaves these unset, and the previous loader
        // treated that as identity. Reading the matrix instead is a change in
        // behaviour and belongs with the work that needs it.
        const Vec3 translation = node.has_translation
            ? Vec3 { node.translation[0], node.translation[1], node.translation[2] }
            : Vec3 {};
        const Vec4 rotation = node.has_rotation
            ? Vec4 { node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] }
            : Vec4 { 0.0f, 0.0f, 0.0f, 1.0f };
        const Vec3 scale = node.has_scale
            ? Vec3 { node.scale[0], node.scale[1], node.scale[2] }
            : Vec3 { 1.0f, 1.0f, 1.0f };
        nodes.push_back({
            .name = node.name ? std::string(node.name) : std::string(),
            .parent = node.parent
                ? static_cast<int>(cgltf_node_index(&data, node.parent))
                : -1,
            .translation = translation,
            .rotation = normalize(quatFromVec4(rotation)),
            .scale = scale,
        });
    }
    return nodes;
}

// Which manifest slot a primitive's material maps to. A primitive with no
// material resolves to slot 0, which is what the previous loader did.
uint32_t materialSlotIndex(
    const cgltf_data& data,
    const cgltf_primitive& primitive)
{
    return primitive.material
        ? static_cast<uint32_t>(cgltf_material_index(&data, primitive.material))
        : 0U;
}

struct SourceBounds {
    Vec3 minimum {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maximum {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
};

Vec4 sampleKeyframes(const AnimationKeyframes& keyframes, float timeSeconds, bool rotation)
{
    if (keyframes.times.empty() || keyframes.values.empty()) {
        return rotation ? Vec4 { 0.0f, 0.0f, 0.0f, 1.0f } : Vec4 {};
    }
    if (keyframes.times.size() == 1 || timeSeconds <= keyframes.times.front()) {
        return keyframes.values.front();
    }
    if (timeSeconds >= keyframes.times.back()) {
        return keyframes.values.back();
    }

    const auto upper = std::upper_bound(keyframes.times.begin(), keyframes.times.end(), timeSeconds);
    const size_t rightIndex = static_cast<size_t>(std::distance(keyframes.times.begin(), upper));
    const size_t leftIndex = rightIndex - 1;
    const float leftTime = keyframes.times[leftIndex];
    const float rightTime = keyframes.times[rightIndex];
    const float t = rightTime > leftTime
        ? (timeSeconds - leftTime) / (rightTime - leftTime)
        : 0.0f;
    if (!rotation) {
        return lerp(keyframes.values[leftIndex], keyframes.values[rightIndex], t);
    }
    return toVec4(slerp(
        quatFromVec4(keyframes.values[leftIndex]),
        quatFromVec4(keyframes.values[rightIndex]),
        t));
}

void includeBounds(SourceBounds& bounds, Vec3 position)
{
    bounds.minimum.x = std::min(bounds.minimum.x, position.x);
    bounds.minimum.y = std::min(bounds.minimum.y, position.y);
    bounds.minimum.z = std::min(bounds.minimum.z, position.z);
    bounds.maximum.x = std::max(bounds.maximum.x, position.x);
    bounds.maximum.y = std::max(bounds.maximum.y, position.y);
    bounds.maximum.z = std::max(bounds.maximum.z, position.z);
}

MeshVertex normalizedVertex(
    Vec3 position,
    Vec3 normal,
    Vec2 uv,
    uint32_t textureIndex,
    SourceBounds bounds,
    GltfMeshLoadOptions options)
{
    const float sourceHeight = std::max(bounds.maximum.y - bounds.minimum.y, 0.000001f);
    const Vec3 extent {
        std::max(bounds.maximum.x - bounds.minimum.x, 0.000001f),
        sourceHeight,
        std::max(bounds.maximum.z - bounds.minimum.z, 0.000001f),
    };
    const Vec3 center {
        (bounds.minimum.x + bounds.maximum.x) * 0.5f,
        (bounds.minimum.y + bounds.maximum.y) * 0.5f,
        (bounds.minimum.z + bounds.maximum.z) * 0.5f,
    };

    MeshVertex vertex;
    if (options.preserveSourceScale) {
        vertex.position = {
            position.x,
            -position.z,
            position.y,
        };
    } else if (options.preserveAspectRatio) {
        vertex.position = {
            0.5f + (position.x - center.x) / sourceHeight,
            0.5f - (position.z - center.z) / sourceHeight,
            (position.y - bounds.minimum.y) / sourceHeight,
        };
    } else {
        vertex.position = {
            (position.x - bounds.minimum.x) / extent.x,
            (bounds.maximum.z - position.z) / extent.z,
            (position.y - bounds.minimum.y) / extent.y,
        };
    }
    vertex.normal = normalizeOr(
        Vec3 { normal.x, -normal.z, normal.y },
        Vec3 { 0.0f, 0.0f, 1.0f });
    if (options.rotateHalfTurn) {
        if (options.preserveSourceScale) {
            vertex.position.x = -vertex.position.x;
            vertex.position.y = -vertex.position.y;
        } else {
            vertex.position.x = 1.0f - vertex.position.x;
            vertex.position.y = 1.0f - vertex.position.y;
        }
        vertex.normal.x = -vertex.normal.x;
        vertex.normal.y = -vertex.normal.y;
    }
    vertex.uv = uv;
    vertex.textureIndex = textureIndex;
    return vertex;
}

} // namespace

MeshData loadGltfMesh(const std::filesystem::path& path, GltfMeshLoadOptions options)
{
    const Document document = loadDocument(path);
    const cgltf_data& data = *document;
    if (data.meshes_count == 0) {
        throw std::runtime_error("Unsupported or empty glTF document");
    }

    MeshData mesh;
    SourceBounds bounds;

    // Every mesh, every primitive, in document order, with node transforms
    // ignored - which is what the previous loader did and what the manifest's
    // scale options are written against. Applying the scene graph here would
    // move every static model.
    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        const cgltf_mesh& sourceMesh = data.meshes[meshIndex];
        for (cgltf_size primitiveIndex = 0;
             primitiveIndex < sourceMesh.primitives_count;
             ++primitiveIndex) {
            const cgltf_primitive& primitive =
                sourceMesh.primitives[primitiveIndex];
            // The old scanner never looked at the primitive mode and would
            // have read a strip or a fan as though it were a list, quietly
            // producing wrong triangles. cgltf reports the mode, so say so.
            if (primitive.type != cgltf_primitive_type_triangles ||
                !primitive.indices) {
                throw std::runtime_error(
                    "Only non-empty triangle-list glTF meshes are supported");
            }
            const cgltf_accessor& positions = requiredAttribute(
                primitive, cgltf_attribute_type_position, "POSITION");
            const cgltf_accessor& normals = requiredAttribute(
                primitive, cgltf_attribute_type_normal, "NORMAL");
            const cgltf_accessor& uvs = requiredAttribute(
                primitive, cgltf_attribute_type_texcoord, "TEXCOORD_0");
            const cgltf_accessor& indices = *primitive.indices;

            uint32_t textureIndex = 0;
            uint32_t materialFlags = PrimitiveMaterialNone;
            if (!options.primitiveMaterials.empty()) {
                const uint32_t slot = materialSlotIndex(data, primitive);
                if (slot >= options.primitiveMaterials.size()) {
                    throw std::runtime_error(
                        "glTF primitive material " + std::to_string(slot) +
                        " has no texture mapping in the asset manifest");
                }
                const PrimitiveMaterialBinding& material =
                    options.primitiveMaterials[slot];
                textureIndex = material.textureIndex + 1;
                materialFlags = material.flags;
            }

            if (positions.count != normals.count ||
                positions.count != uvs.count) {
                throw std::runtime_error(
                    "Incompatible glTF primitive accessors");
            }

            const uint32_t baseVertex =
                static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + positions.count);
            for (size_t index = 0; index < positions.count; ++index) {
                const Vec3 position = readVec3(positions, index);
                mesh.vertices.push_back({
                    .position = position,
                    .normal = readVec3(normals, index),
                    .uv = readVec2(uvs, index),
                    .textureIndex = textureIndex,
                    .materialFlags = materialFlags,
                });
                includeBounds(bounds, position);
            }

            mesh.indices.reserve(mesh.indices.size() + indices.count);
            for (size_t index = 0; index < indices.count; ++index) {
                const uint32_t sourceIndex = readIndex(indices, index);
                if (sourceIndex >= positions.count) {
                    throw std::runtime_error(
                        "glTF index references an invalid vertex");
                }
                mesh.indices.push_back(baseVertex + sourceIndex);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.indices.size() % 3 != 0) {
        throw std::runtime_error(
            "Only non-empty triangle-list glTF meshes are supported");
    }

    // Second pass: every mode below normalizes against the bounds of the
    // whole file, not of one primitive. Spliced verbatim from the loader this
    // replaced - the arithmetic is what decides where every static model
    // sits, and rewriting it was not part of changing the parser.
    const Vec3 minimum = bounds.minimum;
    const Vec3 maximum = bounds.maximum;
    const float sourceHeight = std::max(maximum.y - minimum.y, 0.000001f);
    const Vec3 extent {
        std::max(maximum.x - minimum.x, 0.000001f),
        sourceHeight,
        std::max(maximum.z - minimum.z, 0.000001f),
    };
    const Vec3 center {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f,
    };

    for (MeshVertex& vertex : mesh.vertices) {
        if (options.preserveSourceScale) {
            vertex.position = {
                vertex.position.x,
                -vertex.position.z,
                vertex.position.y,
            };
        } else if (options.preserveAspectRatio) {
            vertex.position = {
                0.5f + (vertex.position.x - center.x) / sourceHeight,
                0.5f - (vertex.position.z - center.z) / sourceHeight,
                (vertex.position.y - minimum.y) / sourceHeight,
            };
        } else {
            vertex.position = {
                (vertex.position.x - minimum.x) / extent.x,
                (maximum.z - vertex.position.z) / extent.z,
                (vertex.position.y - minimum.y) / extent.y,
            };
        }
        vertex.normal = normalizeOr(
            Vec3 { vertex.normal.x, -vertex.normal.z, vertex.normal.y },
            Vec3 { 0.0f, 0.0f, 1.0f });
        if (options.rotateHalfTurn) {
            if (options.preserveSourceScale) {
                vertex.position.x = -vertex.position.x;
                vertex.position.y = -vertex.position.y;
            } else {
                vertex.position.x = 1.0f - vertex.position.x;
                vertex.position.y = 1.0f - vertex.position.y;
            }
            vertex.normal.x = -vertex.normal.x;
            vertex.normal.y = -vertex.normal.y;
        }
    }
    return mesh;
}

SkinnedMeshData loadGltfSkinnedMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options)
{
    const Document document = loadDocument(path);
    const cgltf_data& data = *document;
    if (data.meshes_count == 0 || data.skins_count == 0) {
        throw std::runtime_error("Unsupported or empty skinned glTF document");
    }

    SkinnedMeshData mesh;
    mesh.preserveAspectRatio = options.preserveAspectRatio;
    mesh.preserveSourceScale = options.preserveSourceScale;
    mesh.rotateHalfTurn = options.rotateHalfTurn;
    mesh.nodes = skeletonNodes(data);

    const cgltf_skin& skin = data.skins[0];
    if (skin.joints_count == 0) {
        throw std::runtime_error("Skinned glTF document has no joints");
    }
    mesh.jointNodeIndices.reserve(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        mesh.jointNodeIndices.push_back(
            static_cast<uint32_t>(cgltf_node_index(&data, skin.joints[i])));
    }

    if (!skin.inverse_bind_matrices) {
        throw std::runtime_error(
            "Skin references an invalid inverse bind accessor");
    }
    const cgltf_accessor& inverseBind = *skin.inverse_bind_matrices;
    if (inverseBind.count != mesh.jointNodeIndices.size()) {
        throw std::runtime_error(
            "Skin inverse bind matrix count does not match joints");
    }
    mesh.inverseBindMatrices.reserve(inverseBind.count);
    for (size_t i = 0; i < inverseBind.count; ++i) {
        mesh.inverseBindMatrices.push_back(readMat4(inverseBind, i));
    }

    // Skinned vertices stay in source space. Normalization happens per pose,
    // in skinWithPoses, because the bounds a pose occupies are not the bounds
    // of the bind pose.
    SourceBounds bounds;
    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        const cgltf_mesh& sourceMesh = data.meshes[meshIndex];
        for (cgltf_size primitiveIndex = 0;
             primitiveIndex < sourceMesh.primitives_count;
             ++primitiveIndex) {
            const cgltf_primitive& primitive =
                sourceMesh.primitives[primitiveIndex];
            if (primitive.type != cgltf_primitive_type_triangles ||
                !primitive.indices) {
                throw std::runtime_error(
                    "Only non-empty triangle-list skinned glTF meshes are "
                    "supported");
            }
            const cgltf_accessor& positions = requiredAttribute(
                primitive, cgltf_attribute_type_position, "POSITION");
            const cgltf_accessor& normals = requiredAttribute(
                primitive, cgltf_attribute_type_normal, "NORMAL");
            const cgltf_accessor& uvs = requiredAttribute(
                primitive, cgltf_attribute_type_texcoord, "TEXCOORD_0");
            const cgltf_accessor& joints = requiredAttribute(
                primitive, cgltf_attribute_type_joints, "JOINTS_0");
            const cgltf_accessor& weights = requiredAttribute(
                primitive, cgltf_attribute_type_weights, "WEIGHTS_0");
            const cgltf_accessor& indices = *primitive.indices;

            if (positions.count != normals.count ||
                positions.count != uvs.count ||
                positions.count != joints.count ||
                positions.count != weights.count) {
                throw std::runtime_error(
                    "Incompatible skinned glTF primitive accessors");
            }

            const uint32_t baseVertex =
                static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.reserve(mesh.vertices.size() + positions.count);
            for (size_t index = 0; index < positions.count; ++index) {
                const Vec3 position = readVec3(positions, index);
                auto jointValues = readJoints(joints, index);
                auto weightValues = readWeights(weights, index);
                const float totalWeight = weightValues[0] + weightValues[1] +
                    weightValues[2] + weightValues[3];
                if (totalWeight > 0.000001f) {
                    for (float& weight : weightValues) {
                        weight /= totalWeight;
                    }
                } else {
                    jointValues = {};
                    weightValues = { 1.0f, 0.0f, 0.0f, 0.0f };
                }
                mesh.vertices.push_back({
                    .position = position,
                    .normal = readVec3(normals, index),
                    .uv = readVec2(uvs, index),
                    .joints = jointValues,
                    .weights = weightValues,
                });
                includeBounds(bounds, position);
            }

            mesh.indices.reserve(mesh.indices.size() + indices.count);
            for (size_t index = 0; index < indices.count; ++index) {
                const uint32_t sourceIndex = readIndex(indices, index);
                if (sourceIndex >= positions.count) {
                    throw std::runtime_error(
                        "glTF index references an invalid skinned vertex");
                }
                mesh.indices.push_back(baseVertex + sourceIndex);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.indices.size() % 3 != 0) {
        throw std::runtime_error(
            "Only non-empty triangle-list skinned glTF meshes are supported");
    }
    mesh.sourceMinimum = bounds.minimum;
    mesh.sourceMaximum = bounds.maximum;
    return mesh;
}

void addSkinnedAttachment(
    SkinnedMeshData& mesh,
    MeshData attachment,
    std::string_view nodeName)
{
    if (attachment.vertices.empty() || attachment.indices.empty()) {
        throw std::runtime_error(
            "Cannot attach an empty mesh to skeleton node '" +
            std::string(nodeName) + "'");
    }
    const auto node = std::ranges::find_if(
        mesh.nodes,
        [&](const SkeletonNode& candidate) {
            return candidate.name == nodeName;
        });
    if (node == mesh.nodes.end()) {
        throw std::runtime_error(
            "Skinned model has no attachment node named '" +
            std::string(nodeName) + "'");
    }
    mesh.attachments.push_back({
        .mesh = std::move(attachment),
        .nodeIndex = static_cast<uint32_t>(
            std::distance(mesh.nodes.begin(), node)),
    });
}

std::vector<std::string> listGltfAnimationNames(const std::filesystem::path& path)
{
    std::vector<std::string> names;
    try {
        const Document document = parseDocument(path);
        const cgltf_data& data = *document;
        names.reserve(data.animations_count);
        for (cgltf_size i = 0; i < data.animations_count; ++i) {
            const char* name = data.animations[i].name;
            names.push_back(name
                ? std::string(name)
                : "animation " + std::to_string(i + 1));
        }
    } catch (const std::exception&) {
        names.clear();
    }

    return names;
}

GltfAnimationClip loadGltfAnimationClip(
    const std::filesystem::path& path,
    uint32_t animationIndex)
{
    const Document document = loadDocument(path);
    const cgltf_data& data = *document;
    if (data.animations_count == 0) {
        throw std::runtime_error(
            "Unsupported or empty animation glTF document");
    }
    if (animationIndex >= data.animations_count) {
        throw std::runtime_error(
            "Requested glTF animation index is out of range");
    }
    const cgltf_animation& animation = data.animations[animationIndex];

    GltfAnimationClip clip;
    clip.name = animation.name ? std::string(animation.name) : std::string();
    clip.channels.reserve(animation.channels_count);
    for (cgltf_size channelIndex = 0;
         channelIndex < animation.channels_count;
         ++channelIndex) {
        const cgltf_animation_channel& source =
            animation.channels[channelIndex];
        if (!source.sampler || !source.target_node) {
            throw std::runtime_error(
                "glTF animation channel references an invalid sampler or "
                "node");
        }

        AnimationChannelPath channelPath = AnimationChannelPath::Translation;
        switch (source.target_path) {
        case cgltf_animation_path_type_translation:
            channelPath = AnimationChannelPath::Translation;
            break;
        case cgltf_animation_path_type_rotation:
            channelPath = AnimationChannelPath::Rotation;
            break;
        case cgltf_animation_path_type_scale:
            channelPath = AnimationChannelPath::Scale;
            break;
        default:
            // Morph target weights. Nothing samples them yet, and skipping
            // them is what the previous loader did.
            continue;
        }

        const cgltf_animation_sampler& sampler = *source.sampler;
        if (!sampler.input || !sampler.output ||
            sampler.input->count != sampler.output->count) {
            // A CUBICSPLINE sampler fails here, because its output holds an
            // in-tangent and an out-tangent alongside every value. That is a
            // known gap rather than a corrupt file; sampler.interpolation is
            // still not read at all.
            throw std::runtime_error(
                "Incompatible glTF animation sampler accessors");
        }

        AnimationChannel channel;
        channel.targetNodeName = source.target_node->name
            ? std::string(source.target_node->name)
            : std::string();
        channel.path = channelPath;
        channel.keyframes.times.reserve(sampler.input->count);
        channel.keyframes.values.reserve(sampler.output->count);
        for (size_t i = 0; i < sampler.input->count; ++i) {
            const float keyTime = readScalarFloat(*sampler.input, i);
            channel.keyframes.times.push_back(keyTime);
            clip.durationSeconds = std::max(clip.durationSeconds, keyTime);
            if (channelPath == AnimationChannelPath::Rotation) {
                channel.keyframes.values.push_back(toVec4(normalize(
                    quatFromVec4(readVec4(*sampler.output, i)))));
            } else {
                const Vec3 value = readVec3(*sampler.output, i);
                channel.keyframes.values.push_back(
                    { value.x, value.y, value.z, 0.0f });
            }
        }
        clip.channels.push_back(std::move(channel));
    }

    return clip;
}

namespace {

struct NodePose {
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

std::vector<NodePose> sampleAnimationPoses(const SkinnedMeshData& mesh, const GltfAnimationClip& animation, float timeSeconds)
{
    if (animation.durationSeconds > 0.000001f) {
        timeSeconds = std::fmod(timeSeconds, animation.durationSeconds);
        if (timeSeconds < 0.0f) {
            timeSeconds += animation.durationSeconds;
        }
    } else {
        timeSeconds = 0.0f;
    }

    std::vector<NodePose> poses;
    poses.reserve(mesh.nodes.size());
    std::unordered_map<std::string, size_t> nodeByName;
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        poses.push_back({
            .translation = mesh.nodes[i].translation,
            .rotation = mesh.nodes[i].rotation,
            .scale = mesh.nodes[i].scale,
        });
        if (!mesh.nodes[i].name.empty()) {
            nodeByName[mesh.nodes[i].name] = i;
        }
    }

    for (const AnimationChannel& channel : animation.channels) {
        const auto nodeIt = nodeByName.find(channel.targetNodeName);
        if (nodeIt == nodeByName.end()) {
            continue;
        }
        NodePose& pose = poses[nodeIt->second];
        const Vec4 value = sampleKeyframes(channel.keyframes, timeSeconds, channel.path == AnimationChannelPath::Rotation);
        switch (channel.path) {
        case AnimationChannelPath::Translation:
            pose.translation = { value.x, value.y, value.z };
            break;
        case AnimationChannelPath::Rotation:
            pose.rotation = normalize(quatFromVec4(value));
            break;
        case AnimationChannelPath::Scale:
            pose.scale = { value.x, value.y, value.z };
            break;
        }
    }

    return poses;
}

Vec3 lerpVec3(Vec3 a, Vec3 b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

Quat blendRotation(Quat a, Quat b, float t)
{
    return slerp(a, b, t);
}

SkinnedPoseMatrices poseMatricesFromPoses(
    const SkinnedMeshData& mesh,
    const std::vector<NodePose>& poses);

MeshData skinWithPoses(const SkinnedMeshData& mesh, const std::vector<NodePose>& poses)
{
    if (mesh.nodes.empty() || mesh.jointNodeIndices.empty() || mesh.inverseBindMatrices.size() != mesh.jointNodeIndices.size()) {
        throw std::runtime_error("Cannot skin an incomplete glTF mesh");
    }

    const SkinnedPoseMatrices pose = poseMatricesFromPoses(mesh, poses);

    SourceBounds bounds;
    bounds.minimum = mesh.sourceMinimum;
    bounds.maximum = mesh.sourceMaximum;
    GltfMeshLoadOptions options {
        .preserveAspectRatio = mesh.preserveAspectRatio,
        .preserveSourceScale = mesh.preserveSourceScale,
        .rotateHalfTurn = mesh.rotateHalfTurn,
    };

    MeshData result;
    result.indices = mesh.indices;
    result.vertices.resize(mesh.vertices.size());
    // Each vertex writes only its own output slot, so chunks parallelize
    // freely; small meshes run inline via the minChunk threshold.
    taskSystem().parallelFor(mesh.vertices.size(), 2048, [&](size_t begin, size_t end) {
        for (size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
            const SkinnedVertex& source = mesh.vertices[vertexIndex];
            Vec3 skinnedPosition {};
            Vec3 skinnedNormal {};
            for (size_t i = 0; i < 4; ++i) {
                const float weight = source.weights[i];
                const uint16_t joint = source.joints[i];
                if (weight <= 0.0f || joint >= pose.jointMatrices.size()) {
                    continue;
                }
                skinnedPosition +=
                    transformPoint(pose.jointMatrices[joint], source.position) * weight;
                skinnedNormal +=
                    transformVector(pose.jointMatrices[joint], source.normal) * weight;
            }
            if (skinnedNormal.x == 0.0f && skinnedNormal.y == 0.0f && skinnedNormal.z == 0.0f) {
                skinnedNormal = source.normal;
            }
            result.vertices[vertexIndex] = normalizedVertex(
                skinnedPosition,
                normalizeOr(skinnedNormal, Vec3 { 0.0f, 0.0f, 1.0f }),
                source.uv,
                0u,
                bounds,
                options);
        }
    });

    for (const SkinnedAttachment& attachment : mesh.attachments) {
        if (attachment.nodeIndex >= pose.nodeMatrices.size()) {
            throw std::runtime_error("Skinned attachment references an invalid node");
        }
        const uint32_t baseVertex = static_cast<uint32_t>(result.vertices.size());
        result.vertices.reserve(
            result.vertices.size() + attachment.mesh.vertices.size());
        for (const MeshVertex& vertex : attachment.mesh.vertices) {
            // Source-scale static meshes use the engine's axis convention;
            // convert them back to glTF space before applying the skeleton's
            // sampled node matrix, then normalize once with the actor.
            const Vec3 sourcePosition {
                vertex.position.x,
                vertex.position.z,
                -vertex.position.y,
            };
            const Vec3 sourceNormal {
                vertex.normal.x,
                vertex.normal.z,
                -vertex.normal.y,
            };
            MeshVertex transformed = normalizedVertex(
                transformPoint(
                    pose.nodeMatrices[attachment.nodeIndex], sourcePosition),
                normalizeOr(
                    transformVector(
                        pose.nodeMatrices[attachment.nodeIndex], sourceNormal),
                    Vec3 { 0.0f, 0.0f, 1.0f }),
                vertex.uv,
                vertex.textureIndex,
                bounds,
                options);
            transformed.materialFlags = vertex.materialFlags;
            result.vertices.push_back(transformed);
        }
        result.indices.reserve(
            result.indices.size() + attachment.mesh.indices.size());
        for (const uint32_t index : attachment.mesh.indices) {
            if (index >= attachment.mesh.vertices.size()) {
                throw std::runtime_error(
                    "Skinned attachment contains an invalid vertex index");
            }
            result.indices.push_back(baseVertex + index);
        }
    }

    return result;
}

SkinnedPoseMatrices poseMatricesFromPoses(
    const SkinnedMeshData& mesh,
    const std::vector<NodePose>& poses)
{
    if (poses.size() != mesh.nodes.size() || mesh.nodes.empty() ||
        mesh.jointNodeIndices.empty() ||
        mesh.inverseBindMatrices.size() != mesh.jointNodeIndices.size()) {
        throw std::runtime_error("Cannot sample an incomplete glTF skeleton");
    }
    std::vector<Mat4> localMatrices(mesh.nodes.size(), mat4Identity);
    SkinnedPoseMatrices result;
    result.nodeMatrices.assign(mesh.nodes.size(), mat4Identity);
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        localMatrices[i] = mat4FromTrs(
            poses[i].translation, poses[i].rotation, poses[i].scale);
    }
    std::vector<bool> globalComputed(mesh.nodes.size(), false);
    auto computeGlobal = [&](auto&& self, size_t nodeIndex) -> Mat4 {
        if (globalComputed[nodeIndex]) {
            return result.nodeMatrices[nodeIndex];
        }
        const int parent = mesh.nodes[nodeIndex].parent;
        result.nodeMatrices[nodeIndex] =
            parent >= 0 && static_cast<size_t>(parent) < result.nodeMatrices.size()
            ? self(self, static_cast<size_t>(parent)) * localMatrices[nodeIndex]
            : localMatrices[nodeIndex];
        globalComputed[nodeIndex] = true;
        return result.nodeMatrices[nodeIndex];
    };
    for (size_t i = 0; i < mesh.nodes.size(); ++i) {
        computeGlobal(computeGlobal, i);
    }
    result.jointMatrices.reserve(mesh.jointNodeIndices.size());
    for (size_t i = 0; i < mesh.jointNodeIndices.size(); ++i) {
        const uint32_t nodeIndex = mesh.jointNodeIndices[i];
        result.jointMatrices.push_back(nodeIndex < result.nodeMatrices.size()
                ? result.nodeMatrices[nodeIndex] * mesh.inverseBindMatrices[i]
                : mat4Identity);
    }
    return result;
}

} // namespace

MeshData skinGltfMesh(const SkinnedMeshData& mesh, const GltfAnimationClip& animation, float timeSeconds)
{
    return skinWithPoses(mesh, sampleAnimationPoses(mesh, animation, timeSeconds));
}

MeshData skinGltfMeshBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend)
{
    blend = std::clamp(blend, 0.0f, 1.0f);
    std::vector<NodePose> poses = sampleAnimationPoses(mesh, fromAnimation, fromTimeSeconds);
    const std::vector<NodePose> target = sampleAnimationPoses(mesh, toAnimation, toTimeSeconds);
    for (size_t i = 0; i < poses.size() && i < target.size(); ++i) {
        poses[i].translation = lerpVec3(poses[i].translation, target[i].translation, blend);
        poses[i].rotation = blendRotation(poses[i].rotation, target[i].rotation, blend);
        poses[i].scale = lerpVec3(poses[i].scale, target[i].scale, blend);
    }
    return skinWithPoses(mesh, poses);
}

SkinnedPoseMatrices sampleGltfSkinPose(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& animation,
    float timeSeconds)
{
    return poseMatricesFromPoses(mesh, sampleAnimationPoses(mesh, animation, timeSeconds));
}

SkinnedPoseMatrices sampleGltfSkinPoseBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend)
{
    blend = std::clamp(blend, 0.0f, 1.0f);
    std::vector<NodePose> poses =
        sampleAnimationPoses(mesh, fromAnimation, fromTimeSeconds);
    const std::vector<NodePose> target =
        sampleAnimationPoses(mesh, toAnimation, toTimeSeconds);
    for (size_t i = 0; i < poses.size() && i < target.size(); ++i) {
        poses[i].translation = lerpVec3(poses[i].translation, target[i].translation, blend);
        poses[i].rotation = blendRotation(poses[i].rotation, target[i].rotation, blend);
        poses[i].scale = lerpVec3(poses[i].scale, target[i].scale, blend);
    }
    return poseMatricesFromPoses(mesh, poses);
}

} // namespace sokoban
