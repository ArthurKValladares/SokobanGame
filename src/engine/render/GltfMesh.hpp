#pragma once

#include "engine/Math.hpp"
#include "engine/TextureSource.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

struct MeshVertex {
    Vec3 position {};
    Vec3 normal {};
    // xyz is the tangent; w is the bitangent's handedness, +1 or -1. That is
    // glTF's own layout, and it is what a normal map needs to be read in the
    // space it was baked in. The loader reads TANGENT when a file supplies
    // one and derives it from the UVs otherwise, so this is never zero on a
    // mesh with usable texture coordinates.
    Vec4 tangent {};
    Vec2 uv {};
    // The second UV set, for a material that lights and textures a surface
    // from different unwraps. Falls back to `uv` when a file has only one,
    // so a material asking for set 1 on a single-set mesh reads something
    // sensible rather than nothing.
    Vec2 uv1 {};
    // Which entry of the owning mesh's `materials` this vertex belongs to.
    // Per vertex rather than per draw because one draw covers a whole model
    // and a model's primitives can carry different materials. It replaced a
    // per-vertex texture index and flag word in F3b: those were the same
    // value for every vertex of a primitive, and the material entry they were
    // copied from now travels to the shader instead.
    uint32_t materialIndex = 0;
};

enum PrimitiveMaterialFlag : uint32_t {
    PrimitiveMaterialNone = 0,
    PrimitiveMaterialScrollV = 1U << 0U,
};

struct PrimitiveMaterialBinding {
    uint32_t textureIndex = 0;
    // Kept adjacent to textureIndex for source compatibility with the
    // original two-field aggregate.
    uint32_t flags = PrimitiveMaterialNone;
    // Optional zero-based descriptor indices for glTF-authored material maps.
    // Content discovery resolves these independently because one source image
    // can require different colour-space or sampler interpretations per use.
    std::optional<uint32_t> normalTextureIndex;
    std::optional<uint32_t> metallicRoughnessTextureIndex;
    std::optional<uint32_t> emissiveTextureIndex;
    std::optional<uint32_t> occlusionTextureIndex;
};

// glTF's alpha handling, which decides whether a primitive belongs in the
// opaque pass, is cut out against a threshold, or blends.
enum class MaterialAlphaMode : uint32_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

// One glTF material, as authored.
//
// Factors, UV selections, and map-specific scalars come from the glTF. Map
// handles come from content resolution: the manifest continues to own the
// base-colour override while glTF dependency discovery supplies the other
// maps. Handles are one-based descriptor indices, with zero meaning absent.
struct MeshMaterial {
    Vec4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    Vec3 emissiveFactor {};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float alphaCutoff = 0.5f;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    bool doubleSided = false;
    // One-based descriptor index; zero means untextured.
    uint32_t baseColorTexture = 0;
    // Which UV set the base colour texture reads.
    uint32_t baseColorUvSet = 0;
    uint32_t normalTexture = 0;
    uint32_t normalUvSet = 0;
    float normalScale = 1.0f;
    uint32_t metallicRoughnessTexture = 0;
    uint32_t metallicRoughnessUvSet = 0;
    uint32_t emissiveTexture = 0;
    uint32_t emissiveUvSet = 0;
    uint32_t occlusionTexture = 0;
    uint32_t occlusionUvSet = 0;
    float occlusionStrength = 1.0f;
    uint32_t flags = PrimitiveMaterialNone;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    // Indexed by MeshVertex::materialIndex. Never empty: a file with no
    // materials at all still gets one default entry, so nothing downstream
    // has to special-case the absence.
    std::vector<MeshMaterial> materials;
};

struct SkinnedVertex {
    Vec3 position {};
    Vec3 normal {};
    // Bind-pose tangent, skinned by the same palette as the normal.
    Vec4 tangent {};
    Vec2 uv {};
    Vec2 uv1 {};
    uint32_t materialIndex = 0;
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
};

struct SkeletonNode {
    std::string name;
    int parent = -1;
    Vec3 translation {};
    Quat rotation {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
};

struct SkinnedAttachment {
    MeshData mesh;
    uint32_t nodeIndex = 0;
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshMaterial> materials;
    std::vector<SkeletonNode> nodes;
    std::vector<uint32_t> jointNodeIndices;
    std::vector<Mat4> inverseBindMatrices;
    std::vector<SkinnedAttachment> attachments;
    Vec3 sourceMinimum {};
    Vec3 sourceMaximum {};
    bool preserveAspectRatio = false;
    bool preserveSourceScale = false;
    bool rotateHalfTurn = false;
};

// The source vertex layout consumed by the GPU skinning pipelines. Attachment
// vertices use attachmentNodeIndex; regular skin vertices leave it at UINT32_MAX.
struct GpuSkinnedVertex {
    Vec3 position {};
    Vec3 normal {};
    Vec4 tangent {};
    Vec2 uv {};
    Vec2 uv1 {};
    uint32_t materialIndex = 0;
    std::array<uint16_t, 4> joints {};
    std::array<float, 4> weights {};
    uint32_t attachmentNodeIndex = UINT32_MAX;
};

struct SkinnedPoseMatrices {
    std::vector<Mat4> jointMatrices;
    std::vector<Mat4> nodeMatrices;
};

// How a channel reads between its keyframes. The loader ignored glTF's
// `interpolation` field entirely until A1 step two, so a STEP curve played as
// though it were LERP and nothing said so, and a CUBICSPLINE clip failed to
// load at all.
enum class AnimationInterpolation {
    Linear,
    Step,
    CubicSpline,
};

struct AnimationKeyframes {
    std::vector<float> times;
    // Linear and Step store one value per time. CubicSpline stores three, in
    // glTF's own order - in-tangent, value, out-tangent - so `values` is
    // three times as long as `times` and must not be indexed by key directly.
    std::vector<Vec4> values;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
};

enum class AnimationChannelPath {
    Translation,
    Rotation,
    Scale,
};

struct AnimationChannel {
    std::string targetNodeName;
    AnimationChannelPath path = AnimationChannelPath::Translation;
    AnimationKeyframes keyframes;
};

struct GltfAnimationClip {
    std::string name;
    float durationSeconds = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct GltfMeshLoadOptions {
    bool preserveAspectRatio = false;
    bool preserveSourceScale = false;
    bool rotateHalfTurn = false;
    // Entry N is the resolved render behavior for glTF material N.
    std::vector<PrimitiveMaterialBinding> primitiveMaterials;
};

// Read-only metadata used by content discovery before any image, buffer, or
// GPU resource is loaded. These types deliberately describe glTF concepts in
// engine-owned terms; cgltf remains private to GltfMesh.cpp.
enum class GltfBufferSourceKind {
    ExternalUri,
    DataUri,
    EmbeddedGlb,
};

struct GltfBufferDependency {
    std::string name;
    GltfBufferSourceKind sourceKind = GltfBufferSourceKind::EmbeddedGlb;
    // Empty only for the GLB BIN chunk.
    std::string uri;
    uint64_t byteLength = 0;
};

enum class GltfImageSourceKind {
    ExternalUri,
    DataUri,
    BufferView,
};

struct GltfImageDependency {
    std::string name;
    GltfImageSourceKind sourceKind = GltfImageSourceKind::ExternalUri;
    // Set for URI-backed images, including data URIs.
    std::string uri;
    std::string mimeType;
    // Set for buffer-view images. Offset and length are relative to the
    // identified buffer, as authored by the glTF document.
    std::optional<uint32_t> bufferViewIndex;
    std::optional<uint32_t> bufferIndex;
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
};

// Numeric values intentionally match glTF 2.0's sampler constants. The
// names, defaults, and representation are ours rather than cgltf's.
enum class GltfSamplerFilter : uint32_t {
    Unspecified = 0,
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987,
};

enum class GltfSamplerWrap : uint32_t {
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
    Repeat = 10497,
};

struct GltfSamplerDependency {
    std::string name;
    GltfSamplerFilter magFilter = GltfSamplerFilter::Unspecified;
    GltfSamplerFilter minFilter = GltfSamplerFilter::Unspecified;
    GltfSamplerWrap wrapS = GltfSamplerWrap::Repeat;
    GltfSamplerWrap wrapT = GltfSamplerWrap::Repeat;
};

struct GltfTextureTransformDependency {
    Vec2 offset {};
    Vec2 scale { 1.0f, 1.0f };
    float rotation = 0.0f;
    std::optional<uint32_t> texcoord;
};

struct GltfMaterialTextureDependency {
    MaterialTextureSemantic semantic = MaterialTextureSemantic::BaseColor;
    uint32_t textureIndex = 0;
    std::string textureName;
    // A texture can omit its core image when it is supplied only by an
    // extension. Keeping this optional lets the later semantic-validation
    // step report that case with model/material context.
    std::optional<uint32_t> imageIndex;
    std::optional<uint32_t> samplerIndex;
    uint32_t texcoord = 0;
    // Normal scale for Normal, occlusion strength for Occlusion, 1 otherwise.
    float scale = 1.0f;
    std::optional<GltfTextureTransformDependency> transform;
};

struct GltfMaterialDependency {
    std::string name;
    std::vector<GltfMaterialTextureDependency> textures;
};

struct GltfAssetDependencies {
    std::vector<GltfBufferDependency> buffers;
    std::vector<GltfImageDependency> images;
    std::vector<GltfSamplerDependency> samplers;
    std::vector<GltfMaterialDependency> materials;
};

// Parses and validates document structure only. External buffers and images
// are intentionally not opened, and no render resources are created.
[[nodiscard]] GltfAssetDependencies inspectGltfAssetDependencies(
    const std::filesystem::path& path);

[[nodiscard]] MeshData loadGltfMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

[[nodiscard]] SkinnedMeshData loadGltfSkinnedMesh(
    const std::filesystem::path& path,
    GltfMeshLoadOptions options = {});

// Binds source-scale static geometry to a named skeleton node. The attachment
// is transformed by the sampled node pose and merged into every skinned frame.
void addSkinnedAttachment(
    SkinnedMeshData& mesh,
    MeshData attachment,
    std::string_view nodeName);

[[nodiscard]] GltfAnimationClip loadGltfAnimationClip(
    const std::filesystem::path& path,
    uint32_t animationIndex);

// Names of all animations in a glTF/GLB file, in index order (unnamed clips
// get "animation N"). Returns an empty list for files without animations or
// files this loader cannot parse; never throws.
[[nodiscard]] std::vector<std::string> listGltfAnimationNames(
    const std::filesystem::path& path);

[[nodiscard]] MeshData skinGltfMesh(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& animation,
    float timeSeconds);

// Skins with a pose blended between two clips (0 = from, 1 = to); used for
// short crossfades when the active animation changes.
[[nodiscard]] MeshData skinGltfMeshBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend);

// Samples the skeleton without transforming vertices. GPU skinning uploads
// these palettes instead of rebuilding every vertex on the CPU.
[[nodiscard]] SkinnedPoseMatrices sampleGltfSkinPose(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& animation,
    float timeSeconds);
[[nodiscard]] SkinnedPoseMatrices sampleGltfSkinPoseBlended(
    const SkinnedMeshData& mesh,
    const GltfAnimationClip& fromAnimation,
    float fromTimeSeconds,
    const GltfAnimationClip& toAnimation,
    float toTimeSeconds,
    float blend);

} // namespace sokoban
