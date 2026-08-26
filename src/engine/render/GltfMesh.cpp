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

    // Cheap - it walks the accessors, not the data - and it is the difference
    // between "cgltf could parse the JSON" and "every accessor actually fits
    // inside the buffer it points at". ContentPipeline loads every animation
    // at build time, so this is also what keeps a truncated or mis-sized
    // asset out of a package instead of out of a frame.
    const cgltf_result validation = cgltf_validate(document.get());
    if (validation != cgltf_result_success) {
        throw std::runtime_error(
            "Invalid glTF file: " + path.string() + " (" +
            std::string(resultName(validation)) + ")");
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

// `set` is glTF's attribute suffix: TEXCOORD_0 is set 0, TEXCOORD_1 set 1.
// Only UVs use anything but set 0 here; a second joint influence set would
// need a wider vertex than the skinning palette is built for.
const cgltf_accessor* findAttribute(
    const cgltf_primitive& primitive,
    cgltf_attribute_type type,
    cgltf_int set = 0)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.index == set) {
            return attribute.data;
        }
    }
    return nullptr;
}

// TANGENT and TEXCOORD_1 are both optional, and both have a defined answer
// when a file omits them: a tangent is derived from the UVs afterwards, and a
// second UV set falls back to the first.
Vec4 optionalTangent(const cgltf_accessor* accessor, size_t index)
{
    return accessor ? readVec4(*accessor, index) : Vec4 {};
}

Vec2 optionalUv(const cgltf_accessor* accessor, size_t index, Vec2 fallback)
{
    return accessor ? readVec2(*accessor, index) : fallback;
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

AnimationInterpolation interpolationFrom(cgltf_interpolation_type type)
{
    switch (type) {
    case cgltf_interpolation_type_step:
        return AnimationInterpolation::Step;
    case cgltf_interpolation_type_cubic_spline:
        return AnimationInterpolation::CubicSpline;
    default:
        return AnimationInterpolation::Linear;
    }
}

MaterialAlphaMode alphaModeFrom(cgltf_alpha_mode mode)
{
    switch (mode) {
    case cgltf_alpha_mode_mask:
        return MaterialAlphaMode::Mask;
    case cgltf_alpha_mode_blend:
        return MaterialAlphaMode::Blend;
    default:
        return MaterialAlphaMode::Opaque;
    }
}

// One glTF material plus whatever the manifest says its texture is. cgltf
// fills its defaults from the spec, so an absent field arrives as the value
// the spec says it has rather than as nothing.
MeshMaterial materialFrom(
    const cgltf_material* source,
    const PrimitiveMaterialBinding& binding,
    bool bound)
{
    MeshMaterial material;
    if (bound) {
        // One-based, matching the vertex path: zero means untextured.
        material.baseColorTexture = binding.textureIndex + 1;
        material.flags = binding.flags;
    }
    if (!source) {
        return material;
    }
    if (source->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr =
            source->pbr_metallic_roughness;
        material.baseColorFactor = {
            pbr.base_color_factor[0],
            pbr.base_color_factor[1],
            pbr.base_color_factor[2],
            pbr.base_color_factor[3],
        };
        material.metallicFactor = pbr.metallic_factor;
        material.roughnessFactor = pbr.roughness_factor;
        material.baseColorUvSet = static_cast<uint32_t>(
            std::max(pbr.base_color_texture.texcoord, 0));
    }
    material.emissiveFactor = {
        source->emissive_factor[0],
        source->emissive_factor[1],
        source->emissive_factor[2],
    };
    material.alphaCutoff = source->alpha_cutoff;
    material.alphaMode = alphaModeFrom(source->alpha_mode);
    material.doubleSided = source->double_sided != 0;
    return material;
}

// Every material in the file, in file order, so that a vertex's
// materialIndex is glTF's own index and nothing has to translate.
//
// A manifest slot missing for material N is not an error here, only when a
// primitive actually uses N - which is what the loader has always done, and
// what keeps a model whose manifest lists fewer slots than the file has
// materials loading exactly as it did.
std::vector<MeshMaterial> meshMaterials(
    const cgltf_data& data,
    const GltfMeshLoadOptions& options)
{
    const size_t count = std::max<size_t>(data.materials_count, 1);
    std::vector<MeshMaterial> materials;
    materials.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const bool bound = !options.primitiveMaterials.empty() &&
            i < options.primitiveMaterials.size();
        const PrimitiveMaterialBinding binding = bound
            ? options.primitiveMaterials[i]
            : PrimitiveMaterialBinding {};
        materials.push_back(materialFrom(
            i < data.materials_count ? &data.materials[i] : nullptr,
            binding,
            bound));
    }
    return materials;
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

// One keyframe's value, whichever layout the channel uses. A cubic-spline
// channel keeps a tangent either side of every value, so key N is at 3N + 1.
Vec4 keyframeValue(const AnimationKeyframes& keyframes, size_t index)
{
    return keyframes.interpolation == AnimationInterpolation::CubicSpline
        ? keyframes.values[index * 3 + 1]
        : keyframes.values[index];
}

// glTF's cubic spline is a Hermite curve whose stored tangents are per second
// and so are scaled by the segment's duration.
//
// Nothing here is slerped, even on a rotation channel: a tangent is a free
// vector beside a quaternion rather than a quaternion itself, and treating it
// as one bends the curve. The caller normalizes the result instead, which it
// already did.
Vec4 sampleCubicSpline(
    const AnimationKeyframes& keyframes,
    size_t leftIndex,
    size_t rightIndex,
    float t,
    float segmentSeconds)
{
    const Vec4 startValue = keyframes.values[leftIndex * 3 + 1];
    const Vec4 startTangent = keyframes.values[leftIndex * 3 + 2] * segmentSeconds;
    const Vec4 endTangent = keyframes.values[rightIndex * 3] * segmentSeconds;
    const Vec4 endValue = keyframes.values[rightIndex * 3 + 1];

    const float t2 = t * t;
    const float t3 = t2 * t;
    return startValue * (2.0f * t3 - 3.0f * t2 + 1.0f) +
        startTangent * (t3 - 2.0f * t2 + t) +
        endValue * (-2.0f * t3 + 3.0f * t2) +
        endTangent * (t3 - t2);
}

Vec4 sampleKeyframes(const AnimationKeyframes& keyframes, float timeSeconds, bool rotation)
{
    const size_t keyCount = keyframes.times.size();
    if (keyCount == 0 || keyframes.values.empty()) {
        return rotation ? Vec4 { 0.0f, 0.0f, 0.0f, 1.0f } : Vec4 {};
    }
    if (keyCount == 1 || timeSeconds <= keyframes.times.front()) {
        return keyframeValue(keyframes, 0);
    }
    if (timeSeconds >= keyframes.times.back()) {
        return keyframeValue(keyframes, keyCount - 1);
    }

    const auto upper = std::upper_bound(keyframes.times.begin(), keyframes.times.end(), timeSeconds);
    const size_t rightIndex = static_cast<size_t>(std::distance(keyframes.times.begin(), upper));
    const size_t leftIndex = rightIndex - 1;
    const float leftTime = keyframes.times[leftIndex];
    const float rightTime = keyframes.times[rightIndex];
    const float t = rightTime > leftTime
        ? (timeSeconds - leftTime) / (rightTime - leftTime)
        : 0.0f;

    switch (keyframes.interpolation) {
    case AnimationInterpolation::Step:
        // The value holds until the next key. Reading a step curve as linear
        // is the bug this had for as long as the field went unread.
        return keyframeValue(keyframes, leftIndex);
    case AnimationInterpolation::CubicSpline:
        return sampleCubicSpline(
            keyframes, leftIndex, rightIndex, t, rightTime - leftTime);
    case AnimationInterpolation::Linear:
        break;
    }

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

// Source space to engine space, for one vertex.
//
// This used to exist twice - once here for the skinning path and once inlined
// in loadGltfMesh - which was tolerable while it moved two vectors and stopped
// being so the moment a tangent had to travel the same road.
//
// Nothing here flips handedness, and that is worth stating because a mirrored
// transform would: the axis swap below has determinant +1, every scale factor
// is positive, and rotateHalfTurn negates two axes, which is a rotation and
// not a reflection. So `tangent.w` passes through untouched.
MeshVertex normalizedVertex(
    MeshVertex source,
    SourceBounds bounds,
    GltfMeshLoadOptions options)
{
    const Vec3 position = source.position;
    const Vec3 normal = source.normal;
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

    MeshVertex vertex = source;
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
    const Vec3 tangent = normalizeOr(
        Vec3 { source.tangent.x, -source.tangent.z, source.tangent.y },
        Vec3 {});
    vertex.tangent = { tangent.x, tangent.y, tangent.z, source.tangent.w };
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
        vertex.tangent.x = -vertex.tangent.x;
        vertex.tangent.y = -vertex.tangent.y;
    }
    return vertex;
}

// Derives a tangent frame from the UVs for every vertex that has no usable
// one. Runs after the transform above, so it works in the space the shader
// will see: the aspect-ratio and unit-box modes scale the axes unevenly, and
// a tangent derived before that would not lie in the surface afterwards.
//
// This is the standard per-triangle accumulate and Gram-Schmidt, not
// MikkTSpace. It agrees with MikkTSpace on ordinary geometry and can disagree
// where UV seams and mirrored islands meet, so a model whose normal map was
// baked against MikkTSpace should ship its own TANGENT and this will leave it
// alone. Vendoring MikkTSpace is a dependency decision nobody has taken yet.
template <typename Vertex>
void deriveMissingTangents(
    std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.size() < 3) {
        return;
    }
    std::vector<Vec3> tangents(vertices.size(), Vec3 {});
    std::vector<Vec3> bitangents(vertices.size(), Vec3 {});

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() ||
            i2 >= vertices.size()) {
            continue;
        }
        const Vec3 edge1 = vertices[i1].position - vertices[i0].position;
        const Vec3 edge2 = vertices[i2].position - vertices[i0].position;
        const Vec2 uv1 = vertices[i1].uv - vertices[i0].uv;
        const Vec2 uv2 = vertices[i2].uv - vertices[i0].uv;
        const float determinant = uv1.x * uv2.y - uv2.x * uv1.y;
        // A degenerate UV triangle says nothing about the surface's
        // orientation. Skipping it leaves its vertices to their other
        // triangles, and to the fallback below if they have none.
        if (std::abs(determinant) < 1e-12f) {
            continue;
        }
        const float inverse = 1.0f / determinant;
        const Vec3 tangent = (edge1 * uv2.y - edge2 * uv1.y) * inverse;
        const Vec3 bitangent = (edge2 * uv1.x - edge1 * uv2.x) * inverse;
        for (const uint32_t index : { i0, i1, i2 }) {
            tangents[index] += tangent;
            bitangents[index] += bitangent;
        }
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        Vertex& vertex = vertices[i];
        if (vertex.tangent.x != 0.0f || vertex.tangent.y != 0.0f ||
            vertex.tangent.z != 0.0f) {
            continue;
        }
        const Vec3 normal = vertex.normal;
        // Gram-Schmidt: the accumulated tangent is only approximately in the
        // surface, and a normal map wants it exactly there.
        const Vec3 projected =
            tangents[i] - normal * dot(normal, tangents[i]);
        Vec3 tangent = normalizeOr(projected, Vec3 {});
        if (tangent.x == 0.0f && tangent.y == 0.0f && tangent.z == 0.0f) {
            // No UV information reached this vertex. Any tangent lying in the
            // surface will do; a normal map on it would be wrong either way,
            // and a zero frame would be wrong everywhere else too.
            const Vec3 axis = std::abs(normal.z) < 0.9f
                ? Vec3 { 0.0f, 0.0f, 1.0f }
                : Vec3 { 1.0f, 0.0f, 0.0f };
            tangent = normalizeOr(cross(axis, normal), Vec3 { 1.0f, 0.0f, 0.0f });
        }
        const float handedness =
            dot(cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        vertex.tangent = { tangent.x, tangent.y, tangent.z, handedness };
    }
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
    mesh.materials = meshMaterials(data, options);
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
            const cgltf_accessor* tangents =
                findAttribute(primitive, cgltf_attribute_type_tangent);
            const cgltf_accessor* secondUvs =
                findAttribute(primitive, cgltf_attribute_type_texcoord, 1);
            const cgltf_accessor& indices = *primitive.indices;

            const uint32_t materialIndex = materialSlotIndex(data, primitive);
            uint32_t textureIndex = 0;
            uint32_t materialFlags = PrimitiveMaterialNone;
            if (!options.primitiveMaterials.empty()) {
                if (materialIndex >= options.primitiveMaterials.size()) {
                    throw std::runtime_error(
                        "glTF primitive material " +
                        std::to_string(materialIndex) +
                        " has no texture mapping in the asset manifest");
                }
                const PrimitiveMaterialBinding& material =
                    options.primitiveMaterials[materialIndex];
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
                const Vec2 uv = readVec2(uvs, index);
                mesh.vertices.push_back({
                    .position = position,
                    .normal = readVec3(normals, index),
                    .tangent = optionalTangent(tangents, index),
                    .uv = uv,
                    .uv1 = optionalUv(secondUvs, index, uv),
                    .textureIndex = textureIndex,
                    .materialFlags = materialFlags,
                    .materialIndex = materialIndex,
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
    for (MeshVertex& vertex : mesh.vertices) {
        vertex = normalizedVertex(vertex, bounds, options);
    }
    // After the transform, not before: the aspect-ratio and unit-box modes
    // scale the axes unevenly, and a tangent derived in source space would
    // not lie in the surface once they had.
    deriveMissingTangents(mesh.vertices, mesh.indices);
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
    mesh.materials = meshMaterials(data, options);
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
            const cgltf_accessor* tangents =
                findAttribute(primitive, cgltf_attribute_type_tangent);
            const cgltf_accessor* secondUvs =
                findAttribute(primitive, cgltf_attribute_type_texcoord, 1);
            const cgltf_accessor& indices = *primitive.indices;
            const uint32_t materialIndex = materialSlotIndex(data, primitive);

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
                const Vec2 uv = readVec2(uvs, index);
                mesh.vertices.push_back({
                    .position = position,
                    .normal = readVec3(normals, index),
                    .tangent = optionalTangent(tangents, index),
                    .uv = uv,
                    .uv1 = optionalUv(secondUvs, index, uv),
                    .materialIndex = materialIndex,
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
    // In source space here, unlike the static path: these vertices are posed
    // before they are normalized, so their tangents have to travel with them.
    deriveMissingTangents(mesh.vertices, mesh.indices);
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
    // The attachment is a separate glTF with its own material list, so its
    // indices mean nothing in the actor's. Append the lists and shift the
    // attachment's vertices onto the tail; leaving them alone would silently
    // point an axe at whatever material the character happened to have first.
    const uint32_t materialBase =
        static_cast<uint32_t>(mesh.materials.size());
    for (MeshVertex& vertex : attachment.vertices) {
        vertex.materialIndex += materialBase;
    }
    mesh.materials.insert(
        mesh.materials.end(),
        attachment.materials.begin(),
        attachment.materials.end());

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
        if (!sampler.input || !sampler.output) {
            throw std::runtime_error(
                "Incompatible glTF animation sampler accessors");
        }

        const AnimationInterpolation interpolation =
            interpolationFrom(sampler.interpolation);
        // A cubic-spline sampler stores an in-tangent and an out-tangent
        // either side of every value, so its output is three times as long as
        // its input. Insisting the two counts match is what used to make
        // these clips fail to load rather than play.
        const size_t valuesPerKey =
            interpolation == AnimationInterpolation::CubicSpline ? 3U : 1U;
        if (sampler.output->count != sampler.input->count * valuesPerKey) {
            throw std::runtime_error(
                "Incompatible glTF animation sampler accessors");
        }

        AnimationChannel channel;
        channel.targetNodeName = source.target_node->name
            ? std::string(source.target_node->name)
            : std::string();
        channel.path = channelPath;
        channel.keyframes.interpolation = interpolation;
        channel.keyframes.times.reserve(sampler.input->count);
        channel.keyframes.values.reserve(sampler.output->count);
        for (size_t i = 0; i < sampler.input->count; ++i) {
            const float keyTime = readScalarFloat(*sampler.input, i);
            channel.keyframes.times.push_back(keyTime);
            clip.durationSeconds = std::max(clip.durationSeconds, keyTime);
        }
        for (size_t i = 0; i < sampler.output->count; ++i) {
            if (channelPath == AnimationChannelPath::Rotation) {
                const Vec4 value = readVec4(*sampler.output, i);
                // Only a stored value is a quaternion. A cubic-spline tangent
                // sits either side of one and is a free vector; normalizing
                // it would bend the curve, so the sample is normalized after
                // the spline instead.
                const bool isQuaternion = valuesPerKey == 1U || (i % 3U) == 1U;
                channel.keyframes.values.push_back(isQuaternion
                    ? toVec4(normalize(quatFromVec4(value)))
                    : value);
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
    // Already merged with every attachment's list by addSkinnedAttachment,
    // so the indices the vertices carry are valid in it.
    result.materials = mesh.materials;
    result.vertices.resize(mesh.vertices.size());
    // Each vertex writes only its own output slot, so chunks parallelize
    // freely; small meshes run inline via the minChunk threshold.
    taskSystem().parallelFor(mesh.vertices.size(), 2048, [&](size_t begin, size_t end) {
        for (size_t vertexIndex = begin; vertexIndex < end; ++vertexIndex) {
            const SkinnedVertex& source = mesh.vertices[vertexIndex];
            Vec3 skinnedPosition {};
            Vec3 skinnedNormal {};
            Vec3 skinnedTangent {};
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
                skinnedTangent += transformVector(
                    pose.jointMatrices[joint],
                    Vec3 { source.tangent.x, source.tangent.y,
                        source.tangent.z }) * weight;
            }
            if (skinnedNormal.x == 0.0f && skinnedNormal.y == 0.0f && skinnedNormal.z == 0.0f) {
                skinnedNormal = source.normal;
            }
            const Vec3 tangent = normalizeOr(skinnedTangent, Vec3 {});
            result.vertices[vertexIndex] = normalizedVertex(
                MeshVertex {
                    .position = skinnedPosition,
                    .normal =
                        normalizeOr(skinnedNormal, Vec3 { 0.0f, 0.0f, 1.0f }),
                    .tangent = { tangent.x, tangent.y, tangent.z,
                        source.tangent.w },
                    .uv = source.uv,
                    .uv1 = source.uv1,
                    .materialIndex = source.materialIndex,
                },
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
            const Vec3 sourceTangent {
                vertex.tangent.x,
                vertex.tangent.z,
                -vertex.tangent.y,
            };
            const Vec3 posedTangent = normalizeOr(
                transformVector(
                    pose.nodeMatrices[attachment.nodeIndex], sourceTangent),
                Vec3 {});
            result.vertices.push_back(normalizedVertex(
                MeshVertex {
                    .position = transformPoint(
                        pose.nodeMatrices[attachment.nodeIndex],
                        sourcePosition),
                    .normal = normalizeOr(
                        transformVector(
                            pose.nodeMatrices[attachment.nodeIndex],
                            sourceNormal),
                        Vec3 { 0.0f, 0.0f, 1.0f }),
                    .tangent = { posedTangent.x, posedTangent.y,
                        posedTangent.z, vertex.tangent.w },
                    .uv = vertex.uv,
                    .uv1 = vertex.uv1,
                    .textureIndex = vertex.textureIndex,
                    // Carried explicitly, where it used to be patched back on
                    // after the fact because the old signature dropped it.
                    .materialFlags = vertex.materialFlags,
                    .materialIndex = vertex.materialIndex,
                },
                bounds,
                options));
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
