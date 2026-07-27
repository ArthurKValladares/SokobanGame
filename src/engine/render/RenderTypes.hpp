#pragma once

#include "engine/LevelCatalog.hpp"
#include "engine/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sokoban {

enum class RenderViewMode {
    TopDown2D,
    Isometric3D,
};

enum class RenderSurfaceEffect {
    Standard,
    MirrorEnergy,
    // Ground tops blend two ground textures through a splat map; see
    // shaders/ground_splat.frag.glsl.
    GroundSplat,
};

enum class WaterShorelineEdge : uint32_t {
    NegativeY = 1U << 0,
    PositiveX = 1U << 1,
    PositiveY = 1U << 2,
    NegativeX = 1U << 3,
};

enum class WaterShorelineCorner : uint32_t {
    NegativeXNegativeY = 1U << 4,
    PositiveXNegativeY = 1U << 5,
    PositiveXPositiveY = 1U << 6,
    NegativeXPositiveY = 1U << 7,
};

[[nodiscard]] constexpr uint32_t waterShorelineBit(
    WaterShorelineEdge edge)
{
    return static_cast<uint32_t>(edge);
}

[[nodiscard]] constexpr uint32_t waterShorelineBit(
    WaterShorelineCorner corner)
{
    return static_cast<uint32_t>(corner);
}

// Runtime model identity: 0 is the built-in untextured unit cube, and any
// other value addresses entry value-1 of the asset manifest's model list.
struct RenderModel {
    uint32_t value = 0;

    [[nodiscard]] constexpr bool isCube() const { return value == 0; }
    [[nodiscard]] constexpr std::size_t index() const { return value - 1; }
    friend constexpr bool operator==(RenderModel, RenderModel) = default;
};

inline constexpr RenderModel cubeModel {};

// Runtime animation identity: 0 means "no animation"; any other value
// addresses entry value-1 of the asset manifest's animation list.
struct RenderAnimation {
    uint32_t value = 0;

    [[nodiscard]] constexpr bool isNone() const { return value == 0; }
    [[nodiscard]] constexpr std::size_t index() const { return value - 1; }
    friend constexpr bool operator==(RenderAnimation, RenderAnimation) = default;
};

inline constexpr RenderAnimation noAnimation {};

// Runtime texture identity: 0 means no texture; any other value addresses
// entry value-1 of the asset manifest's descriptor texture list.
struct RenderTexture {
    uint32_t value = 0;

    [[nodiscard]] constexpr bool isNone() const { return value == 0; }
    [[nodiscard]] constexpr std::size_t index() const { return value - 1; }
    friend constexpr bool operator==(RenderTexture, RenderTexture) = default;
};

inline constexpr RenderTexture noTexture {};

// Manifest texture names for ground splatting. Declared here so the frame
// builder and the asset-requirement planner cannot drift apart: a texture
// that is resolved but never required would silently sample the fallback.
inline constexpr std::string_view groundSplatBaseTextureName = "GroundGrass";
inline constexpr std::string_view groundSplatDetailTextureName = "GroundRock";
inline constexpr std::string_view groundSplatMapTextureName = "GroundSplatMap";

// Each screen may declare its own splat map as "GroundSplatMap<level>_<screen>".
// Screens without one share `groundSplatMapTextureName`, which is also what the
// editor uses: it edits a document, which belongs to no screen.
[[nodiscard]] inline std::string groundSplatMapTextureNameForScreen(
    LevelLocation location)
{
    return std::string(groundSplatMapTextureName) +
        std::to_string(location.level) + '_' + std::to_string(location.screen);
}

// Manifest-relative asset path for a screen's map. Must match the file name
// tools/make_ground_textures.py writes, or the generator and the editor would
// create two different files for the same screen. AssetManifestTests pins the
// shipped manifest's paths against this.
[[nodiscard]] inline std::string groundSplatMapAssetPathForScreen(
    LevelLocation location)
{
    return "custom/textures/ground_splat_level" +
        std::to_string(location.level) + "_screen" +
        std::to_string(location.screen) + ".png";
}

// Textures blended on splatted ground tops. Unset ids fall back to the flat
// tile color, so a manifest without these entries still renders.
struct GroundSplatTextures {
    RenderTexture base = noTexture;
    RenderTexture detail = noTexture;
    RenderTexture splatMap = noTexture;

    [[nodiscard]] constexpr bool valid() const
    {
        return !base.isNone() && !detail.isNone() && !splatMap.isNone();
    }
};

// Single definition of "which textures does splatted ground sample", shared by
// the frame builder and the requirement planner so the two cannot disagree
// about which screen's map to use. `findTextureByName` takes a name and returns
// a RenderTexture (`noTexture` when the manifest has no such entry); it is a
// template so this header stays independent of AssetManifest.
template <typename FindTextureByName>
[[nodiscard]] GroundSplatTextures groundSplatTexturesForScreen(
    FindTextureByName findTextureByName,
    std::optional<LevelLocation> location)
{
    const RenderTexture screenMap = location
        ? findTextureByName(groundSplatMapTextureNameForScreen(*location))
        : noTexture;
    return {
        .base = findTextureByName(groundSplatBaseTextureName),
        .detail = findTextureByName(groundSplatDetailTextureName),
        .splatMap = screenMap.isNone()
            ? findTextureByName(groundSplatMapTextureName)
            : screenMap,
    };
}

struct RenderFrameData {
    struct CameraExtent {
        int32_t originX = 0;
        int32_t originY = 0;
        int32_t originZ = 0;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
    };

    struct WaterGridBounds {
        int32_t originX = 0;
        int32_t originY = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct DirectionalLight {
        Vec3 direction { 0.0f, 0.0f, 1.0f };
        Vec3 color { 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
    };

    struct AmbientLight {
        Vec3 color {};
        float intensity = 0.0f;
    };

    struct Lighting {
        struct Shadows {
            bool enabled = false;
            float opacity = 0.0f;
            float bias = 0.0f;
        };

        struct AmbientOcclusion {
            bool enabled = false;
            float strength = 0.0f;
            bool visualize = false;
        };

        DirectionalLight sun {};
        AmbientLight ambient {};
        Shadows shadows {};
        AmbientOcclusion ambientOcclusion {};
        float specularStrength = 0.0f;
        float specularPower = 1.0f;
        float modelShadowReceive = 0.0f;
    };

    struct Tile {
        GridPosition3 cell {};
        Vec2 position {};
        Vec2 size { 1.0f, 1.0f };
        Vec4 color {};
        float baseElevation = 0.0f;
        float height = 0.0f;
        bool blurBehind = false;
        bool pickOnly = false;
        bool showGrid = true;
        bool isEditorPreview = false;
        bool affectsCameraFit = true;
        RenderModel model = cubeModel;
        RenderAnimation animation = noAnimation;
        RenderAnimation animationFallback = noAnimation;
        bool animationLoops = true;
        float animationTimeSeconds = 0.0f;
        float beltScrollOffset = 0.0f;
        uint32_t modelRotationQuarterTurns = 0;
        float modelRotationOffsetRadians = 0.0f;
        RenderSurfaceEffect effect = RenderSurfaceEffect::Standard;
    };

    struct IsoFace {
        std::array<Vec3, 4> vertices {};
        Vec3 normal {};
        Vec4 color {};
        RenderSurfaceEffect effect = RenderSurfaceEffect::Standard;
    };

    struct WaterSurface {
        GridPosition3 cell {};
        Vec2 position {};
        Vec2 size { 1.0f, 1.0f };
        Vec4 color {};
        float elevation = 0.0f;
        uint32_t shorelineMask = 0;
        bool isEditorPreview = false;
        bool pickable = true;
    };

    struct Particle {
        Vec3 position {};
        Vec2 size { 1.0f, 1.0f };
        float rotationRadians = 0.0f;
        Vec4 color {};
        RenderTexture texture = noTexture;
        bool drawOnTop = false;
    };

    struct GridOverlay {
        Vec4 color {};
        float width = 0.0f;
    };

    RenderViewMode viewMode = RenderViewMode::TopDown2D;
    std::optional<float> cameraPitchDegrees;
    Lighting lighting {};
    GridOverlay gridOverlay {};
    uint32_t levelWidth = 0;
    uint32_t levelHeight = 0;
    uint32_t levelDepth = 1;
    uint32_t gridPickBorder = 0;
    std::optional<CameraExtent> cameraExtent;
    WaterGridBounds waterGridBounds;
    Vec2 playerPosition {};
    std::vector<Tile> tiles;
    std::vector<WaterSurface> waterSurfaces;
    std::vector<IsoFace> isoFaces;
    std::vector<Particle> particles;
    GroundSplatTextures groundSplat {};
    float waterAnimationTimeSeconds = 0.0f;
    float effectAnimationTimeSeconds = 0.0f;
};

struct RenderStats {
    uint64_t frameIndex = 0;
    uint32_t totalTiles = 0;
    uint32_t scenePreparations = 0;
    uint32_t preparedIsoFaces = 0;
    uint32_t preparedShadowFaces = 0;
    uint32_t preparedModels = 0;
    uint32_t preparedParticles = 0;
    uint32_t visibleFaces = 0;
    uint32_t drawCalls = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t pipelineBinds = 0;
    uint32_t renderPasses = 0;
    uint32_t imageBarriers = 0;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;
    uint32_t swapchainImages = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t renderScalePercent = 100;
    uint32_t activeSamples = 1;
    bool wireframeEnabled = false;
    float wireframeLineWidth = 1.0f;
    uint64_t pipelineRebuilds = 0;
    uint64_t swapchainRecreations = 0;
    uint64_t swapchainRecreationDeferrals = 0;
    uint64_t renderResourceReconfigurations = 0;
    uint64_t presentQueueRetirementWaits = 0;
    uint32_t retiredRenderResourceSets = 0;
    bool rendererReconfigurationPending = false;
};

} // namespace sokoban
