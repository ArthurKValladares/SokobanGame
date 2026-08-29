#pragma once

#include "engine/Geometry.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sokoban {

struct TileRenderLayout {
    Vec2 boardBottomLeft {};
    Vec2 tileSize {};
};

struct IsoRenderLayout {
    Vec3 cameraPosition {};
    Vec3 cameraRight {};
    Vec3 cameraUp {};
    Vec3 cameraForward {};
    Vec2 projectedCenter {};
    float focalLength = 1.0f;
    float fitScale = 1.0f;
    float nearestDepth = 0.0f;
    float farthestDepth = 1.0f;
};

// The same transform as projectIsoPointToClip, as a matrix.
//
// The two exist together on purpose. The matrix is what the GPU gets, so that
// vertices can arrive in world space and every shader can ask where it is.
// The scalar form stays because the CPU still projects for picking and for
// camera fitting, and because it clamps view-space z to a small positive
// value where the matrix cannot - a matrix has no way to express "and if this
// point is behind the camera, pretend it is just in front". Nothing rendered
// is ever behind the camera, so the clamp does not change any drawn pixel;
// picking rays are the case that relies on it.
//
// IsoScenePreparerTests pins the two against each other across a grid of
// points and camera states. Change one and the test tells you about the
// other.
[[nodiscard]] Mat4 isoClipFromWorld(
    const IsoRenderLayout& layout,
    Vec2 renderExtent);
// Projection without the rigid view transform. SSAO uses it and its inverse
// to round-trip copied depth through view space in physical scene units.
[[nodiscard]] Mat4 isoClipFromView(
    const IsoRenderLayout& layout,
    Vec2 renderExtent);


struct ShadowRenderLayout {
    Vec3 lightRight {};
    Vec3 lightUp {};
    Vec3 lightForward {};
    Vec3 center {};
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
    float nearestDepth = 0.0f;
    float farthestDepth = 1.0f;
};

// The sun's orthographic shadow transform, as a matrix. Same relationship to
// projectShadowPoint that isoClipFromWorld has to projectIsoPointToClip, with
// one caveat that matters: projectShadowPoint *clamps* the depth it returns to
// [0, 1], and a matrix cannot. Whoever uses this has to clamp z themselves, or
// geometry outside the sun's depth range samples past the edge of the shadow
// map rather than at it. The shaders do it on the line after the multiply.
[[nodiscard]] Mat4 shadowClipFromWorld(const ShadowRenderLayout& layout);

struct ModelTransformPoints {
    Vec3 origin {};
    Vec3 xPoint {};
    Vec3 yPoint {};
    Vec3 zPoint {};
};

enum class PreparedSurfaceMaterial {
    Standard,
    Water,
    MirrorEnergy,
    // Splat-mapped ground: only the upward-facing top of a ground tile uses
    // it, so the sides keep the flat tile color.
    GroundSplat,
};

struct PreparedIsoFace {
    // What the GPU draws. World space since C1; the vertex shader applies the
    // camera.
    std::array<Vec3, 4> worldVertices {};
    // What picking uses: the same corners in normalised device coordinates,
    // with the view-space w that produced them. Rendering no longer reads
    // these, but pickGridCell and pickGroundPoint work in screen space and
    // still do.
    std::array<Vec3, 4> vertices {};
    std::array<float, 4> clipW { 1.0f, 1.0f, 1.0f, 1.0f };
    Vec3 normal {};
    Vec4 color {};
    GridPosition3 cell {};
    GridPosition pickBoundsCell {};
    bool blurBehind = false;
    bool showGrid = false;
    bool isEditorPreview = false;
    bool pickable = false;
    Vec2 gridSize {};
    Vec2 worldOrigin {};
    // World Z of the face's plane. Tile tops are flat, so one value covers
    // the whole face. Needed to draw overlays that sit on the surface rather
    // than at an assumed height.
    float worldHeight = 0.0f;
    PreparedSurfaceMaterial material = PreparedSurfaceMaterial::Standard;
    uint32_t shorelineMask = 0;
    float depth = 0.0f;
};

struct PreparedParticle {
    // World space, like every other scene quad since C1. The billboard is
    // built from the camera basis, but the corners it produces are ordinary
    // world points and the vertex shader projects them.
    std::array<Vec3, 4> vertices {};
    Vec4 color {};
    RenderTexture texture = noTexture;
    float depth = 0.0f;
    bool drawOnTop = false;
};

// Persistent, Vulkan-free identity and world bounds for one source
// renderable. PreparedRenderScene owns a frame-local snapshot of these values;
// the preparer's cache may therefore advance while an older prepared frame is
// still leased by the renderer.
struct PreparedRenderable {
    enum class Kind {
        Tile,
        WaterSurface,
        IsoFace,
    };

    Kind kind = Kind::Tile;
    std::size_t sourceIndex = 0;
    uint64_t identity = 0;
    uint64_t boundsRevision = 0;
    Aabb worldBounds;
    bool boundsReused = false;
    // Main color/depth visibility only. Picking and shadow lists deliberately
    // do not consume this classification yet.
    bool mainSceneVisible = true;
};

// CPU scene work shared by every pass in one submitted frame.
// Index lists point into the source RenderFrameData or the face pool and keep
// pass recording free of geometry regeneration, culling, and sorting.
struct PreparedRenderScene {
    Vec2 renderExtent { 1.0f, 1.0f };
    TileRenderLayout tileLayout;
    IsoRenderLayout isoLayout;
    ShadowRenderLayout shadowLayout;
    bool hasTranslucentContent = false;
    std::vector<PreparedIsoFace> isoFaces;
    std::vector<std::size_t> opaqueFaceIndices;
    std::vector<std::size_t> translucentFaceIndices;
    // Where the blended tail of opaqueFaceIndices begins. Faces before it
    // are fully opaque and sorted nearest first; faces from it on carry a
    // sub-1.0 alpha, need the blend unit, and are sorted farthest first.
    // Equal to the list's size when the whole list is in painter's order.
    std::size_t opaqueBlendedFirst = 0;
    std::vector<std::size_t> pickFaceIndices;
    std::vector<std::size_t> opaqueModelIndices;
    std::vector<std::size_t> translucentModelIndices;
    std::vector<PreparedParticle> particles;
    std::vector<std::array<Vec3, 4>> shadowFaces;
    std::vector<std::size_t> shadowModelIndices;
    std::vector<PreparedRenderable> renderables;
    bool frustumCullingEnabled = true;
    uint32_t reusedRenderableBounds = 0;
    uint32_t rebuiltRenderableBounds = 0;
    uint32_t visibleRenderables = 0;
    uint32_t culledRenderables = 0;
};

// Owns all Vulkan-free projection, culling, sorting, and picking behavior.
// prepare() reuses the capacities in its output, allowing the renderer to
// retain one scratch scene per CPU frame slot without function-static state.
class IsoScenePreparer {
public:
    void prepare(
        const RenderFrameData& frameData,
        Vec2 renderExtent,
        PreparedRenderScene& output) const;

    // Developer toggle for the opaque face order.
    //
    // Front-to-back is the default: it lets the depth test reject occluded
    // fragments before the fragment shader runs. Back-to-front is the
    // painter's order this renderer used previously, and it hides a whole
    // class of defect, because with VK_COMPARE_OP_LESS_OR_EQUAL two
    // coincident opaque surfaces are resolved by whichever draws last. Tiles
    // emit all four sides with no neighbour-based face removal, and faces
    // appended through RenderFrameData::isoFaces are not CPU back-face culled
    // at all, so exact ties do occur - most often where faces are near
    // edge-on, which is the far row at the top of the screen.
    //
    // Exposed so an ordering artefact can be ruled in or out in one click
    // rather than by editing code and rebuilding.
    [[nodiscard]] bool opaqueFrontToBackSort() const
    {
        return opaqueFrontToBackSort_;
    }
    void setOpaqueFrontToBackSort(bool enabled)
    {
        opaqueFrontToBackSort_ = enabled;
    }

    [[nodiscard]] bool frustumCulling() const
    {
        return frustumCulling_;
    }
    void setFrustumCulling(bool enabled)
    {
        frustumCulling_ = enabled;
    }

    [[nodiscard]] std::optional<GridPosition3> pickGridCell(
        const PreparedRenderScene& scene,
        Vec2 pixelPosition,
        Vec2 outputExtent,
        uint32_t levelWidth,
        uint32_t levelHeight,
        uint32_t gridPickBorder = 0) const;

    // Continuous world-tile position under the pointer on a splattable ground
    // top, for brush painting. Unlike pickGridCell this resolves *within* a
    // tile - a brush has to land where the pointer is, not at a cell centre -
    // and is perspective-correct, so a stroke does not drift toward the far
    // edge of a tile under the isometric projection.
    //
    // Returns nothing when the pointer is not over paintable ground.
    //
    // The result carries the surface's world Z as well as its tile position.
    // Painting only needs x/y, but an overlay drawn at an assumed height sits
    // visibly off the surface under an isometric projection, and the error
    // grows with distance from the camera.
    [[nodiscard]] std::optional<Vec3> pickGroundPoint(
        const PreparedRenderScene& scene,
        Vec2 pixelPosition,
        Vec2 outputExtent) const;

    [[nodiscard]] static Vec3 projectIsoPoint(
        const IsoRenderLayout& layout,
        Vec2 renderExtent,
        Vec3 point);
    [[nodiscard]] static ModelTransformPoints modelTransformPoints(
        const RenderFrameData::Tile& tile);
    // worldFromModel for a model-backed tile: the three axis columns of the
    // authored transform, then its origin. Composing this with the camera's
    // clipFromWorld gives exactly what modelClipTransform returns, which is
    // what IsoScenePreparerTests checks.
    [[nodiscard]] static std::array<Vec4, 4> modelWorldTransform(
        const RenderFrameData::Tile& tile);
    // The same thing with the camera already baked in. The renderer stopped
    // using this at C1 and it survives as the reference the world-space form
    // is tested against, plus the CPU-side answer for anything that wants a
    // model's screen position without a round trip through the GPU.
    [[nodiscard]] static std::array<Vec4, 4> modelClipTransform(
        const IsoRenderLayout& layout,
        Vec2 renderExtent,
        const RenderFrameData::Tile& tile);
    [[nodiscard]] static Vec4 projectShadowPoint(
        const ShadowRenderLayout& layout,
        Vec3 point);

private:
    struct CachedRenderable {
        std::array<Vec3, 8> points {};
        std::size_t pointCount = 0;
        GridPosition3 semanticCell {};
        RenderModel model {};
        uint64_t semanticId = 0;
        uint32_t semanticTag = 0;
        uint64_t identity = 0;
        uint64_t boundsRevision = 0;
        Aabb worldBounds;
    };

    PreparedRenderable reconcileRenderable(
        PreparedRenderable::Kind kind,
        std::size_t sourceIndex,
        std::span<const Vec3> points,
        GridPosition3 semanticCell,
        RenderModel model,
        uint64_t semanticId,
        uint32_t semanticTag) const;
    [[nodiscard]] std::vector<CachedRenderable>& cacheFor(
        PreparedRenderable::Kind kind) const;

    mutable std::vector<CachedRenderable> tileRenderableCache_;
    mutable std::vector<CachedRenderable> waterRenderableCache_;
    mutable std::vector<CachedRenderable> isoFaceRenderableCache_;
    mutable uint64_t nextRenderableIdentity_ = 1;
    bool opaqueFrontToBackSort_ = true;
    bool frustumCulling_ = true;
};

} // namespace sokoban
