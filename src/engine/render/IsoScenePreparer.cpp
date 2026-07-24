#include "engine/render/IsoScenePreparer.hpp"

#include "engine/BoardLayout.hpp"
#include "engine/Config.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace sokoban {
namespace {

Vec3 subtract(Vec3 left, Vec3 right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

Vec2 subtract(Vec2 left, Vec2 right)
{
    return { left.x - right.x, left.y - right.y };
}

Vec3 add(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

Vec3 multiply(Vec3 value, float scalar)
{
    return { value.x * scalar, value.y * scalar, value.z * scalar };
}

float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

float cross2D(Vec2 left, Vec2 right)
{
    return left.x * right.y - left.y * right.x;
}

Vec3 normalize(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    return length <= 0.0001f ? Vec3 {} : multiply(value, 1.0f / length);
}

std::array<Vec3, 8> tileCorners(const RenderFrameData::Tile& tile)
{
    const float x = tile.position.x;
    const float y = tile.position.y;
    const float width = tile.size.x;
    const float depth = tile.size.y;
    const float base = tile.baseElevation;
    const float top = base + std::max(tile.height, 0.0f);
    return {
        Vec3 { x, y, base },
        Vec3 { x + width, y, base },
        Vec3 { x + width, y + depth, base },
        Vec3 { x, y + depth, base },
        Vec3 { x, y, top },
        Vec3 { x + width, y, top },
        Vec3 { x + width, y + depth, top },
        Vec3 { x, y + depth, top },
    };
}

TileRenderLayout calculateTileLayout(
    const RenderFrameData& frameData,
    Vec2 renderExtent)
{
    const BoardPixelLayout pixelLayout = calculateBoardPixelLayout(
        renderExtent, frameData.levelWidth, frameData.levelHeight);
    const Vec2 safeExtent {
        std::max(renderExtent.x, 1.0f),
        std::max(renderExtent.y, 1.0f),
    };
    return {
        .boardBottomLeft = {
            -1.0f + 2.0f * pixelLayout.bottomLeft.x / safeExtent.x,
            1.0f - 2.0f * pixelLayout.bottomLeft.y / safeExtent.y,
        },
        .tileSize = {
            2.0f * pixelLayout.tileSize / safeExtent.x,
            2.0f * pixelLayout.tileSize / safeExtent.y,
        },
    };
}

IsoRenderLayout calculateIsoLayout(
    const RenderFrameData& frameData,
    Vec2 renderExtent)
{
    if (frameData.levelWidth == 0 || frameData.levelHeight == 0) {
        return {};
    }

    constexpr float radiansPerDegree =
        3.14159265358979323846f / 180.0f;
    const float pitch = config::boardPitchDegrees * radiansPerDegree;
    const float cameraDistance = std::max(
        static_cast<float>(
            std::max(frameData.levelWidth, frameData.levelHeight)),
        1.0f) * config::perspectiveCameraDistanceScale;
    const Vec3 target {
        static_cast<float>(frameData.levelWidth) * 0.5f,
        static_cast<float>(frameData.levelHeight) * 0.5f,
        static_cast<float>(std::max(frameData.levelDepth, 1U) - 1U) * 0.5f,
    };
    const Vec3 cameraPosition {
        target.x,
        target.y + std::sin(pitch) * cameraDistance,
        target.z + std::cos(pitch) * cameraDistance,
    };
    const Vec3 cameraForward = normalize(subtract(target, cameraPosition));
    const Vec3 cameraRight =
        normalize(cross({ 0.0f, 0.0f, 1.0f }, cameraForward));
    const Vec3 cameraUp = normalize(cross(cameraForward, cameraRight));

    IsoRenderLayout layout {
        .cameraPosition = cameraPosition,
        .cameraRight = cameraRight,
        .cameraUp = cameraUp,
        .cameraForward = cameraForward,
        .focalLength = 1.0f / std::tan(
            config::perspectiveFovDegrees * radiansPerDegree * 0.5f),
    };

    Vec2 minPoint {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec2 maxPoint {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    float nearestDepth = std::numeric_limits<float>::max();
    float farthestDepth = std::numeric_limits<float>::lowest();

    auto includePoint = [&](Vec3 worldPoint) {
        const Vec3 projected =
            IsoScenePreparer::projectIsoPoint(
                layout, renderExtent, worldPoint);
        minPoint.x = std::min(minPoint.x, projected.x);
        minPoint.y = std::min(minPoint.y, projected.y);
        maxPoint.x = std::max(maxPoint.x, projected.x);
        maxPoint.y = std::max(maxPoint.y, projected.y);
        const float cameraDepth =
            dot(subtract(worldPoint, layout.cameraPosition),
                layout.cameraForward);
        nearestDepth = std::min(nearestDepth, cameraDepth);
        farthestDepth = std::max(farthestDepth, cameraDepth);
    };

    const float width = static_cast<float>(frameData.levelWidth);
    const float height = static_cast<float>(frameData.levelHeight);
    const float top =
        static_cast<float>(std::max(frameData.levelDepth, 1U));
    for (Vec3 point : std::array<Vec3, 8> {
             Vec3 { 0.0f, 0.0f, 0.0f },
             Vec3 { width, 0.0f, 0.0f },
             Vec3 { width, height, 0.0f },
             Vec3 { 0.0f, height, 0.0f },
             Vec3 { 0.0f, 0.0f, top },
             Vec3 { width, 0.0f, top },
             Vec3 { width, height, top },
             Vec3 { 0.0f, height, top },
         }) {
        includePoint(point);
    }
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (!tile.isEditorPreview) {
            for (Vec3 point : tileCorners(tile)) {
                includePoint(point);
            }
        }
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        for (Vec3 point : face.vertices) {
            includePoint(point);
        }
    }

    const Vec2 sceneSize {
        std::max(maxPoint.x - minPoint.x, 0.001f),
        std::max(maxPoint.y - minPoint.y, 0.001f),
    };
    layout.projectedCenter = {
        (minPoint.x + maxPoint.x) * 0.5f,
        (minPoint.y + maxPoint.y) * 0.5f,
    };
    layout.fitScale =
        1.82f * std::min(1.0f / sceneSize.x, 1.0f / sceneSize.y);
    layout.nearestDepth = nearestDepth;
    layout.farthestDepth =
        std::max(farthestDepth, nearestDepth + 0.001f);
    return layout;
}

ShadowRenderLayout calculateShadowLayout(const RenderFrameData& frameData)
{
    const Vec3 lightDirection =
        normalize(frameData.lighting.sun.direction);
    const Vec3 lightForward = normalize(multiply(
        lightDirection.x == 0.0f &&
                lightDirection.y == 0.0f &&
                lightDirection.z == 0.0f
            ? Vec3 { 0.0f, 0.0f, 1.0f }
            : lightDirection,
        -1.0f));
    const Vec3 referenceUp = std::abs(lightForward.z) > 0.9f
        ? Vec3 { 0.0f, 1.0f, 0.0f }
        : Vec3 { 0.0f, 0.0f, 1.0f };
    const Vec3 lightRight = normalize(cross(referenceUp, lightForward));
    const Vec3 lightUp = normalize(cross(lightForward, lightRight));

    Vec3 minPoint {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 maxPoint {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    auto includePoint = [&](Vec3 worldPoint) {
        const Vec3 lightPoint {
            dot(worldPoint, lightRight),
            dot(worldPoint, lightUp),
            dot(worldPoint, lightForward),
        };
        minPoint.x = std::min(minPoint.x, lightPoint.x);
        minPoint.y = std::min(minPoint.y, lightPoint.y);
        minPoint.z = std::min(minPoint.z, lightPoint.z);
        maxPoint.x = std::max(maxPoint.x, lightPoint.x);
        maxPoint.y = std::max(maxPoint.y, lightPoint.y);
        maxPoint.z = std::max(maxPoint.z, lightPoint.z);
    };

    bool hasBounds = false;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        if (!tile.isEditorPreview) {
            for (Vec3 point : tileCorners(tile)) {
                includePoint(point);
            }
            hasBounds = true;
        }
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        for (Vec3 point : face.vertices) {
            includePoint(point);
        }
        hasBounds = true;
    }
    if (!hasBounds) {
        minPoint = { -1.0f, -1.0f, -1.0f };
        maxPoint = { 1.0f, 1.0f, 1.0f };
    }

    const float padding = config::shadowMapPadding;
    const float centerX = (minPoint.x + maxPoint.x) * 0.5f;
    const float centerY = (minPoint.y + maxPoint.y) * 0.5f;
    const float centerZ = (minPoint.z + maxPoint.z) * 0.5f;
    return {
        .lightRight = lightRight,
        .lightUp = lightUp,
        .lightForward = lightForward,
        .center = add(
            add(multiply(lightRight, centerX),
                multiply(lightUp, centerY)),
            multiply(lightForward, centerZ)),
        .halfWidth =
            std::max((maxPoint.x - minPoint.x) * 0.5f + padding, 0.5f),
        .halfHeight =
            std::max((maxPoint.y - minPoint.y) * 0.5f + padding, 0.5f),
        .nearestDepth = minPoint.z - padding,
        .farthestDepth =
            std::max(maxPoint.z + padding, minPoint.z + 0.001f),
    };
}

bool faceVisible(
    const IsoRenderLayout& layout,
    const std::array<Vec3, 4>& vertices,
    Vec3 normal)
{
    const Vec3 center = multiply(
        add(add(vertices[0], vertices[1]), add(vertices[2], vertices[3])),
        0.25f);
    return dot(normal, subtract(layout.cameraPosition, center)) > 0.0f;
}

float faceDepth(
    const IsoRenderLayout& layout,
    const std::array<Vec3, 4>& vertices)
{
    float depth = 0.0f;
    for (Vec3 vertex : vertices) {
        depth += dot(
            subtract(vertex, layout.cameraPosition),
            layout.cameraForward);
    }
    return depth * 0.25f;
}

bool pointInTriangle(Vec2 point, Vec2 a, Vec2 b, Vec2 c)
{
    constexpr float epsilon = 0.001f;
    const float ab = cross2D(subtract(b, a), subtract(point, a));
    const float bc = cross2D(subtract(c, b), subtract(point, b));
    const float ca = cross2D(subtract(a, c), subtract(point, c));
    const bool hasNegative =
        ab < -epsilon || bc < -epsilon || ca < -epsilon;
    const bool hasPositive =
        ab > epsilon || bc > epsilon || ca > epsilon;
    return !(hasNegative && hasPositive);
}

std::optional<float> pointDepthInTriangle(
    Vec2 point,
    Vec3 a,
    Vec3 b,
    Vec3 c)
{
    constexpr float epsilon = 0.00001f;
    const Vec2 a2 { a.x, a.y };
    const Vec2 b2 { b.x, b.y };
    const Vec2 c2 { c.x, c.y };
    const float area = cross2D(subtract(b2, a2), subtract(c2, a2));
    if (std::abs(area) <= epsilon) {
        return std::nullopt;
    }
    const float weightA =
        cross2D(subtract(b2, point), subtract(c2, point)) / area;
    const float weightB =
        cross2D(subtract(c2, point), subtract(a2, point)) / area;
    const float weightC = 1.0f - weightA - weightB;
    if (weightA < -epsilon ||
        weightB < -epsilon ||
        weightC < -epsilon) {
        return std::nullopt;
    }
    return weightA * a.z + weightB * b.z + weightC * c.z;
}

std::optional<float> pointDepthInQuad(
    Vec2 point,
    const std::array<Vec3, 4>& quad)
{
    if (const std::optional<float> depth =
            pointDepthInTriangle(point, quad[0], quad[1], quad[2])) {
        return depth;
    }
    return pointDepthInTriangle(point, quad[0], quad[2], quad[3]);
}

} // namespace

Vec3 IsoScenePreparer::projectIsoPoint(
    const IsoRenderLayout& layout,
    Vec2 renderExtent,
    Vec3 point)
{
    const Vec3 relative = subtract(point, layout.cameraPosition);
    const float cameraX = dot(relative, layout.cameraRight);
    const float cameraY = dot(relative, layout.cameraUp);
    const float cameraZ =
        std::max(dot(relative, layout.cameraForward), 0.001f);
    const float aspect =
        std::max(renderExtent.x, 1.0f) /
        std::max(renderExtent.y, 1.0f);
    const Vec2 projected {
        layout.focalLength * cameraX / (cameraZ * aspect),
        layout.focalLength * cameraY / cameraZ,
    };
    const float depthRange =
        std::max(layout.farthestDepth - layout.nearestDepth, 0.001f);
    const float normalizedDepth = std::clamp(
        (cameraZ - layout.nearestDepth) / depthRange, 0.0f, 1.0f);
    return {
        (projected.x - layout.projectedCenter.x) * layout.fitScale,
        (projected.y - layout.projectedCenter.y) * layout.fitScale,
        normalizedDepth,
    };
}

Vec4 IsoScenePreparer::projectShadowPoint(
    const ShadowRenderLayout& layout,
    Vec3 point)
{
    const Vec3 relative = subtract(point, layout.center);
    const float x =
        dot(relative, layout.lightRight) / std::max(layout.halfWidth, 0.001f);
    const float y =
        dot(relative, layout.lightUp) / std::max(layout.halfHeight, 0.001f);
    const float depth = dot(point, layout.lightForward);
    const float depthRange =
        std::max(layout.farthestDepth - layout.nearestDepth, 0.001f);
    const float z = std::clamp(
        (depth - layout.nearestDepth) / depthRange, 0.0f, 1.0f);
    return { x, y, z, 1.0f };
}

void IsoScenePreparer::prepare(
    const RenderFrameData& frameData,
    Vec2 renderExtent,
    PreparedRenderScene& scene) const
{
    scene.isoFaces.clear();
    scene.opaqueFaceIndices.clear();
    scene.translucentFaceIndices.clear();
    scene.pickFaceIndices.clear();
    scene.opaqueModelIndices.clear();
    scene.translucentModelIndices.clear();
    scene.shadowFaces.clear();
    scene.shadowModelIndices.clear();
    scene.renderExtent = {
        std::max(renderExtent.x, 1.0f),
        std::max(renderExtent.y, 1.0f),
    };
    scene.tileLayout = calculateTileLayout(frameData, scene.renderExtent);
    scene.isoLayout = calculateIsoLayout(frameData, scene.renderExtent);
    scene.shadowLayout = calculateShadowLayout(frameData);
    scene.hasTranslucentContent =
        frameData.viewMode == RenderViewMode::Isometric3D &&
        (!frameData.waterSurfaces.empty() ||
            std::ranges::any_of(
                frameData.tiles,
                [](const RenderFrameData::Tile& tile) {
                    return tile.blurBehind;
                }));

    scene.isoFaces.reserve(
        frameData.tiles.size() * 5 + frameData.waterSurfaces.size());
    scene.opaqueFaceIndices.reserve(frameData.tiles.size() * 3);
    scene.translucentFaceIndices.reserve(
        frameData.tiles.size() + frameData.waterSurfaces.size());
    scene.pickFaceIndices.reserve(
        frameData.tiles.size() * 3 + frameData.waterSurfaces.size());
    scene.shadowFaces.reserve(frameData.tiles.size() * 5);

    auto appendIsoFace = [&](
                             const std::array<Vec3, 4>& vertices,
                             Vec3 normal,
                             Vec4 color,
                             GridPosition3 cell,
                             GridPosition pickBoundsCell,
                             bool blurBehind,
                             bool showGrid,
                             bool editorPreview,
                             bool pickable,
                             bool drawable,
                             Vec2 gridSize,
                             PreparedSurfaceMaterial material) {
        if (!faceVisible(scene.isoLayout, vertices, normal)) {
            return;
        }
        PreparedIsoFace face {
            .normal = normal,
            .color = color,
            .cell = cell,
            .pickBoundsCell = pickBoundsCell,
            .blurBehind = blurBehind,
            .showGrid = showGrid,
            .isEditorPreview = editorPreview,
            .pickable = pickable,
            .gridSize = gridSize,
            .worldOrigin = {
                vertices[0].x,
                vertices[0].y,
            },
            .material = material,
            .depth = faceDepth(scene.isoLayout, vertices),
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            face.vertices[i] = projectIsoPoint(
                scene.isoLayout, scene.renderExtent, vertices[i]);
            face.shadowVertices[i] =
                projectShadowPoint(scene.shadowLayout, vertices[i]);
        }
        const std::size_t index = scene.isoFaces.size();
        scene.isoFaces.push_back(face);
        if (drawable) {
            (blurBehind || material == PreparedSurfaceMaterial::Water
                    ? scene.translucentFaceIndices
                    : scene.opaqueFaceIndices)
                .push_back(index);
        }
        if (pickable) {
            scene.pickFaceIndices.push_back(index);
        }
    };

    auto appendShadowFace = [&](const std::array<Vec3, 4>& vertices) {
        std::array<Vec4, 4> projected {};
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            projected[i] =
                projectShadowPoint(scene.shadowLayout, vertices[i]);
        }
        scene.shadowFaces.push_back(projected);
    };

    if (frameData.viewMode == RenderViewMode::Isometric3D) {
        for (std::size_t tileIndex = 0;
             tileIndex < frameData.tiles.size();
             ++tileIndex) {
            const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
            const std::array<Vec3, 8> corners = tileCorners(tile);
            const float width = tile.size.x;
            const float depth = tile.size.y;
            const float height = std::max(tile.height, 0.0f);
            const bool drawCube = tile.model.isCube() && !tile.pickOnly;
            const bool pickable = !tile.isEditorPreview;
            const GridPosition pickBoundsCell {
                static_cast<int>(
                    std::floor(tile.position.x + 0.0001f)),
                static_cast<int>(
                    std::floor(tile.position.y + 0.0001f)),
            };

            if (height <= 0.0f) {
                appendIsoFace(
                    { corners[0], corners[1], corners[2], corners[3] },
                    { 0.0f, 0.0f, 1.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { width, depth },
                    PreparedSurfaceMaterial::Standard);
            } else {
                appendIsoFace(
                    { corners[0], corners[1], corners[5], corners[4] },
                    { 0.0f, -1.0f, 0.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { width, height },
                    PreparedSurfaceMaterial::Standard);
                appendIsoFace(
                    { corners[1], corners[2], corners[6], corners[5] },
                    { 1.0f, 0.0f, 0.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { depth, height },
                    PreparedSurfaceMaterial::Standard);
                appendIsoFace(
                    { corners[2], corners[3], corners[7], corners[6] },
                    { 0.0f, 1.0f, 0.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { width, height },
                    PreparedSurfaceMaterial::Standard);
                appendIsoFace(
                    { corners[3], corners[0], corners[4], corners[7] },
                    { -1.0f, 0.0f, 0.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { depth, height },
                    PreparedSurfaceMaterial::Standard);
                appendIsoFace(
                    { corners[4], corners[5], corners[6], corners[7] },
                    { 0.0f, 0.0f, 1.0f },
                    tile.color,
                    tile.cell,
                    pickBoundsCell,
                    tile.blurBehind,
                    tile.showGrid,
                    tile.isEditorPreview,
                    pickable,
                    drawCube,
                    { width, depth },
                    PreparedSurfaceMaterial::Standard);
            }

            if (!tile.model.isCube() && !tile.pickOnly) {
                (tile.blurBehind
                        ? scene.translucentModelIndices
                        : scene.opaqueModelIndices)
                    .push_back(tileIndex);
            }
        }

        for (const RenderFrameData::WaterSurface& water :
             frameData.waterSurfaces) {
            const float left = water.position.x;
            const float top = water.position.y;
            const float right = left + water.size.x;
            const float bottom = top + water.size.y;
            appendIsoFace(
                {
                    Vec3 { left, top, water.elevation },
                    Vec3 { right, top, water.elevation },
                    Vec3 { right, bottom, water.elevation },
                    Vec3 { left, bottom, water.elevation },
                },
                { 0.0f, 0.0f, 1.0f },
                water.color,
                water.cell,
                {
                    static_cast<int>(std::floor(left + 0.0001f)),
                    static_cast<int>(std::floor(top + 0.0001f)),
                },
                false,
                false,
                water.isEditorPreview,
                !water.isEditorPreview,
                true,
                water.size,
                PreparedSurfaceMaterial::Water);
        }

        for (const RenderFrameData::IsoFace& source : frameData.isoFaces) {
            const Vec3 normal = normalize(cross(
                subtract(source.vertices[1], source.vertices[0]),
                subtract(source.vertices[2], source.vertices[0])));
            PreparedIsoFace face {
                .normal = normal,
                .color = source.color,
                .depth = faceDepth(scene.isoLayout, source.vertices),
            };
            for (std::size_t i = 0; i < source.vertices.size(); ++i) {
                face.vertices[i] = projectIsoPoint(
                    scene.isoLayout, scene.renderExtent, source.vertices[i]);
                face.shadowVertices[i] =
                    projectShadowPoint(scene.shadowLayout, source.vertices[i]);
            }
            scene.opaqueFaceIndices.push_back(scene.isoFaces.size());
            scene.isoFaces.push_back(face);
        }

        auto fartherFirst = [&](std::size_t left, std::size_t right) {
            return scene.isoFaces[left].depth > scene.isoFaces[right].depth;
        };
        std::ranges::sort(scene.opaqueFaceIndices, fartherFirst);
        std::ranges::sort(scene.translucentFaceIndices, fartherFirst);
    }

    for (std::size_t tileIndex = 0;
         tileIndex < frameData.tiles.size();
         ++tileIndex) {
        const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
        if (tile.isEditorPreview || tile.pickOnly) {
            continue;
        }
        if (!tile.model.isCube()) {
            scene.shadowModelIndices.push_back(tileIndex);
            continue;
        }

        const std::array<Vec3, 8> corners = tileCorners(tile);
        if (std::max(tile.height, 0.0f) <= 0.0f) {
            appendShadowFace(
                { corners[0], corners[1], corners[2], corners[3] });
            continue;
        }
        appendShadowFace(
            { corners[0], corners[1], corners[5], corners[4] });
        appendShadowFace(
            { corners[1], corners[2], corners[6], corners[5] });
        appendShadowFace(
            { corners[2], corners[3], corners[7], corners[6] });
        appendShadowFace(
            { corners[3], corners[0], corners[4], corners[7] });
        appendShadowFace(
            { corners[4], corners[5], corners[6], corners[7] });
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        appendShadowFace(face.vertices);
    }

}

std::optional<GridPosition3> IsoScenePreparer::pickGridCell(
    const PreparedRenderScene& scene,
    Vec2 pixelPosition,
    Vec2 outputExtent,
    uint32_t levelWidth,
    uint32_t levelHeight) const
{
    if (outputExtent.x <= 0.0f || outputExtent.y <= 0.0f) {
        return std::nullopt;
    }

    std::optional<GridPosition3> picked;
    float pickedDepth = std::numeric_limits<float>::max();
    for (std::size_t faceIndex : scene.pickFaceIndices) {
        const PreparedIsoFace& face = scene.isoFaces[faceIndex];
        std::array<Vec3, 4> pixelQuad {};
        std::array<Vec2, 4> pixelQuad2D {};
        for (std::size_t i = 0; i < face.vertices.size(); ++i) {
            const Vec3 clip = face.vertices[i];
            pixelQuad[i] = {
                (clip.x + 1.0f) * 0.5f * outputExtent.x,
                (1.0f - clip.y) * 0.5f * outputExtent.y,
                clip.z,
            };
            pixelQuad2D[i] = { pixelQuad[i].x, pixelQuad[i].y };
        }
        if (!(pointInTriangle(
                  pixelPosition,
                  pixelQuad2D[0],
                  pixelQuad2D[1],
                  pixelQuad2D[2]) ||
                pointInTriangle(
                    pixelPosition,
                    pixelQuad2D[0],
                    pixelQuad2D[2],
                    pixelQuad2D[3]))) {
            continue;
        }
        const std::optional<float> depth =
            pointDepthInQuad(pixelPosition, pixelQuad);
        if (!depth || *depth >= pickedDepth ||
            face.pickBoundsCell.x < 0 ||
            face.pickBoundsCell.y < 0 ||
            face.pickBoundsCell.x >= static_cast<int>(levelWidth) ||
            face.pickBoundsCell.y >= static_cast<int>(levelHeight)) {
            continue;
        }
        picked = face.cell;
        pickedDepth = *depth;
    }
    return picked;
}

} // namespace sokoban
