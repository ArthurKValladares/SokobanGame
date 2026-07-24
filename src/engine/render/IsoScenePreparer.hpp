#pragma once

#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"

#include <array>
#include <cstddef>
#include <optional>
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

struct PreparedIsoFace {
    std::array<Vec3, 4> vertices {};
    std::array<Vec4, 4> shadowVertices {};
    Vec3 normal {};
    Vec4 color {};
    GridPosition3 cell {};
    GridPosition pickBoundsCell {};
    bool blurBehind = false;
    bool showGrid = false;
    bool isEditorPreview = false;
    bool pickable = false;
    Vec2 gridSize {};
    float depth = 0.0f;
};

// CPU scene work shared by every pass in one submitted frame.
// Index lists point into the source RenderFrameData or the face pool and keep
// pass recording free of geometry regeneration, culling, and sorting.
struct PreparedRenderScene {
    Vec2 renderExtent { 1.0f, 1.0f };
    TileRenderLayout tileLayout;
    IsoRenderLayout isoLayout;
    ShadowRenderLayout shadowLayout;
    bool hasBlurredTiles = false;
    std::vector<PreparedIsoFace> isoFaces;
    std::vector<std::size_t> opaqueFaceIndices;
    std::vector<std::size_t> translucentFaceIndices;
    std::vector<std::size_t> pickFaceIndices;
    std::vector<std::size_t> opaqueModelIndices;
    std::vector<std::size_t> translucentModelIndices;
    std::vector<std::array<Vec4, 4>> shadowFaces;
    std::vector<std::size_t> shadowModelIndices;
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

    [[nodiscard]] std::optional<GridPosition3> pickGridCell(
        const PreparedRenderScene& scene,
        Vec2 pixelPosition,
        Vec2 outputExtent,
        uint32_t levelWidth,
        uint32_t levelHeight) const;

    [[nodiscard]] static Vec3 projectIsoPoint(
        const IsoRenderLayout& layout,
        Vec2 renderExtent,
        Vec3 point);
    [[nodiscard]] static Vec4 projectShadowPoint(
        const ShadowRenderLayout& layout,
        Vec3 point);
};

} // namespace sokoban
