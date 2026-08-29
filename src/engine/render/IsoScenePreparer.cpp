#include "engine/render/IsoScenePreparer.hpp"

#include "engine/BoardLayout.hpp"
#include "engine/render/CameraConfig.hpp"
#include "engine/render/LightingConfig.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace sokoban {
namespace {

Vec4 projectIsoPointToClip(
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
    const float nearDepth = std::max(layout.nearestDepth, 0.001f);
    const float farDepth =
        std::max(layout.farthestDepth, nearDepth + 0.001f);
    const float depthRange = farDepth - nearDepth;

    // Keep perspective division in the GPU. Reconstructing a model matrix
    // from points that have already been divided cannot preserve shared edges.
    return {
        layout.fitScale *
            (layout.focalLength * cameraX / aspect -
                layout.projectedCenter.x * cameraZ),
        layout.fitScale *
            (layout.focalLength * cameraY -
                layout.projectedCenter.y * cameraZ),
        farDepth * (cameraZ - nearDepth) / depthRange,
        cameraZ,
    };
}

} // namespace

Mat4 isoClipFromView(const IsoRenderLayout& layout, Vec2 renderExtent)
{
    const float aspect =
        std::max(renderExtent.x, 1.0f) / std::max(renderExtent.y, 1.0f);
    const float nearDepth = std::max(layout.nearestDepth, 0.001f);
    const float farDepth = std::max(layout.farthestDepth, nearDepth + 0.001f);
    return mat4PerspectiveOffCenter(
        layout.focalLength,
        aspect,
        nearDepth,
        farDepth,
        layout.projectedCenter,
        layout.fitScale);
}

Mat4 isoClipFromWorld(const IsoRenderLayout& layout, Vec2 renderExtent)
{
    return isoClipFromView(layout, renderExtent) *
        mat4View(
            layout.cameraPosition,
            layout.cameraRight,
            layout.cameraUp,
            layout.cameraForward);
}

Mat4 shadowClipFromWorld(const ShadowRenderLayout& layout)
{
    const float halfWidth = std::max(layout.halfWidth, 0.001f);
    const float halfHeight = std::max(layout.halfHeight, 0.001f);
    const float depthRange = std::max(
        layout.farthestDepth - layout.nearestDepth, 0.001f);
    // Note the depth row measures along lightForward from the world origin,
    // not from the layout's centre - that is what projectShadowPoint does, and
    // the difference is a constant offset that would silently shift every
    // shadow if it were "tidied up".
    const Mat4 result { {
        layout.lightRight.x / halfWidth,
        layout.lightUp.x / halfHeight,
        layout.lightForward.x / depthRange,
        0.0f,

        layout.lightRight.y / halfWidth,
        layout.lightUp.y / halfHeight,
        layout.lightForward.y / depthRange,
        0.0f,

        layout.lightRight.z / halfWidth,
        layout.lightUp.z / halfHeight,
        layout.lightForward.z / depthRange,
        0.0f,

        -dot(layout.center, layout.lightRight) / halfWidth,
        -dot(layout.center, layout.lightUp) / halfHeight,
        -layout.nearestDepth / depthRange,
        1.0f,
    } };
    return result;
}

namespace {

std::array<Vec3, 8> logicalTileCorners(const RenderFrameData::Tile& tile)
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

std::array<Vec3, 8> tileCorners(const RenderFrameData::Tile& tile)
{
    if (tile.modelTransform) {
        const ModelTransformPoints transform =
            IsoScenePreparer::modelTransformPoints(tile);
        const Vec3 xAxis = subtract(transform.xPoint, transform.origin);
        const Vec3 yAxis = subtract(transform.yPoint, transform.origin);
        const Vec3 zAxis = subtract(transform.zPoint, transform.origin);
        const Vec3 xy = add(add(transform.origin, xAxis), yAxis);
        return {
            transform.origin,
            transform.xPoint,
            xy,
            transform.yPoint,
            transform.zPoint,
            add(transform.xPoint, zAxis),
            add(xy, zAxis),
            add(transform.yPoint, zAxis),
        };
    }

    return logicalTileCorners(tile);
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
    const float pitch = frameData.cameraPitchDegrees.value_or(
        config::cameraPitchDegrees) * radiansPerDegree;
    const float yaw = config::cameraYawDegrees * radiansPerDegree;
    const RenderFrameData::CameraExtent sourceCameraExtent =
        frameData.cameraExtent.value_or(RenderFrameData::CameraExtent {
            .width = frameData.levelWidth,
            .height = frameData.levelHeight,
            .depth = frameData.levelDepth,
        });
    struct ContinuousCameraExtent {
        float originX = 0.0f;
        float originY = 0.0f;
        float originZ = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
        float depth = 1.0f;
    } cameraExtent {
        static_cast<float>(sourceCameraExtent.originX) +
            frameData.cameraOffset.x,
        static_cast<float>(sourceCameraExtent.originY) +
            frameData.cameraOffset.y,
        static_cast<float>(sourceCameraExtent.originZ),
        static_cast<float>(sourceCameraExtent.width),
        static_cast<float>(sourceCameraExtent.height),
        static_cast<float>(sourceCameraExtent.depth),
    };
    if (frameData.cameraExtentTransitionTarget) {
        const RenderFrameData::CameraExtent& target =
            *frameData.cameraExtentTransitionTarget;
        const float progress = std::clamp(
            frameData.cameraExtentTransitionProgress, 0.0f, 1.0f);
        const auto interpolate = [progress](float from, float to) {
            return from + (to - from) * progress;
        };
        cameraExtent.originX = interpolate(
            cameraExtent.originX, static_cast<float>(target.originX));
        cameraExtent.originY = interpolate(
            cameraExtent.originY, static_cast<float>(target.originY));
        cameraExtent.originZ = interpolate(
            cameraExtent.originZ, static_cast<float>(target.originZ));
        cameraExtent.width = interpolate(
            cameraExtent.width, static_cast<float>(target.width));
        cameraExtent.height = interpolate(
            cameraExtent.height, static_cast<float>(target.height));
        cameraExtent.depth = interpolate(
            cameraExtent.depth, static_cast<float>(target.depth));
    }
    const float cameraDistance = std::max(
        std::max(cameraExtent.width, cameraExtent.height),
        1.0f) * config::cameraDistanceScale *
        std::max(frameData.cameraDistanceMultiplier.value_or(1.0f), 0.01f);
    const Vec3 target {
        cameraExtent.originX + cameraExtent.width * 0.5f,
        cameraExtent.originY + cameraExtent.height * 0.5f,
        cameraExtent.originZ +
            (std::max(cameraExtent.depth, 1.0f) - 1.0f) * 0.5f,
    };
    const float horizontalDistance =
        std::sin(pitch) * cameraDistance;
    const Vec3 cameraPosition {
        target.x + std::sin(yaw) * horizontalDistance,
        target.y + std::cos(yaw) * horizontalDistance,
        target.z + std::cos(pitch) * cameraDistance,
    };
    const Vec3 cameraForward = normalize(subtract(target, cameraPosition));
    const Vec3 cameraRight = std::abs(horizontalDistance) > 0.0001f
        ? normalize(cross({ 0.0f, 0.0f, 1.0f }, cameraForward))
        : Vec3 { std::cos(yaw), -std::sin(yaw), 0.0f };
    const Vec3 cameraUp = normalize(cross(cameraForward, cameraRight));

    IsoRenderLayout layout {
        .cameraPosition = cameraPosition,
        .cameraRight = cameraRight,
        .cameraUp = cameraUp,
        .cameraForward = cameraForward,
        .focalLength = 1.0f / std::tan(
            config::cameraVerticalFovDegrees * radiansPerDegree * 0.5f),
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

    // The depth range and the on-screen fit are separate questions and must
    // stay separate. The fit is authored: an explicit cameraExtent decides
    // what the camera frames, and tiles outside it are deliberately excluded
    // so a decoration cannot zoom the board out. The depth range is not a
    // matter of taste - it has to cover everything that will actually be
    // drawn, because projectIsoPoint *clamps* z to [0, 1] rather than
    // clipping. Geometry past the far plane therefore does not disappear, it
    // lands on z = 1.0 exactly, along with every other such surface, and the
    // depth test can no longer tell those surfaces apart.
    auto includeDepth = [&](Vec3 worldPoint) {
        const float cameraDepth =
            dot(subtract(worldPoint, layout.cameraPosition),
                layout.cameraForward);
        nearestDepth = std::min(nearestDepth, cameraDepth);
        farthestDepth = std::max(farthestDepth, cameraDepth);
    };
    auto includePoint = [&](Vec3 worldPoint) {
        const Vec3 projected =
            IsoScenePreparer::projectIsoPoint(
                layout, renderExtent, worldPoint);
        minPoint.x = std::min(minPoint.x, projected.x);
        minPoint.y = std::min(minPoint.y, projected.y);
        maxPoint.x = std::max(maxPoint.x, projected.x);
        maxPoint.y = std::max(maxPoint.y, projected.y);
        includeDepth(worldPoint);
    };

    const float left = cameraExtent.originX;
    const float nearY = cameraExtent.originY;
    const float right = left + cameraExtent.width;
    const float farY = nearY + cameraExtent.height;
    const float bottom = cameraExtent.originZ;
    const float top = bottom + std::max(cameraExtent.depth, 1.0f);
    for (Vec3 point : std::array<Vec3, 8> {
             Vec3 { left, nearY, bottom },
             Vec3 { right, nearY, bottom },
             Vec3 { right, farY, bottom },
             Vec3 { left, farY, bottom },
             Vec3 { left, nearY, top },
             Vec3 { right, nearY, top },
             Vec3 { right, farY, top },
             Vec3 { left, farY, top },
         }) {
        includePoint(point);
    }
    // Every tile is walked for depth. Only the ones that own the framing are
    // also walked for the fit, and only when the fit is not authored.
    const bool fitToContent = !frameData.cameraExtent;
    for (const RenderFrameData::Tile& tile : frameData.tiles) {
        const bool framesTheCamera = fitToContent &&
            !tile.isEditorPreview && tile.affectsCameraFit;
        for (Vec3 point : tileCorners(tile)) {
            if (framesTheCamera) {
                includePoint(point);
            } else {
                includeDepth(point);
            }
        }
    }
    for (const RenderFrameData::IsoFace& face : frameData.isoFaces) {
        for (Vec3 point : face.vertices) {
            if (fitToContent) {
                includePoint(point);
            } else {
                includeDepth(point);
            }
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
        config::cameraFitScale *
        std::min(1.0f / sceneSize.x, 1.0f / sceneSize.y);
    layout.nearestDepth =
        nearestDepth - config::cameraDepthPaddingTiles;
    layout.farthestDepth =
        std::max(
            farthestDepth + config::cameraDepthPaddingTiles,
            layout.nearestDepth + 0.001f);
    return layout;
}

struct PlaneFootprint {
    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    bool valid = false;
};

PlaneFootprint visiblePlaneFootprint(
    const IsoRenderLayout& layout,
    Vec2 renderExtent,
    float planeHeight)
{
    constexpr float viewportOverscan = 1.02f;
    constexpr float worldPadding = 0.5f;
    const float aspect =
        std::max(renderExtent.x, 1.0f) /
        std::max(renderExtent.y, 1.0f);
    const float inverseFitScale =
        1.0f / std::max(layout.fitScale, 0.0001f);
    const float inverseFocalLength =
        1.0f / std::max(layout.focalLength, 0.0001f);

    PlaneFootprint footprint;
    for (Vec2 clipCorner : std::array<Vec2, 4> {
             Vec2 { -viewportOverscan, -viewportOverscan },
             Vec2 { viewportOverscan, -viewportOverscan },
             Vec2 { viewportOverscan, viewportOverscan },
             Vec2 { -viewportOverscan, viewportOverscan },
         }) {
        const float cameraXOverDepth = aspect *
            (clipCorner.x * inverseFitScale + layout.projectedCenter.x) *
            inverseFocalLength;
        const float cameraYOverDepth =
            (clipCorner.y * inverseFitScale + layout.projectedCenter.y) *
            inverseFocalLength;
        const Vec3 rayDirection = add(
            add(
                layout.cameraForward,
                multiply(layout.cameraRight, cameraXOverDepth)),
            multiply(layout.cameraUp, cameraYOverDepth));
        if (std::abs(rayDirection.z) <= 0.0001f) {
            continue;
        }
        const float rayDistance =
            (planeHeight - layout.cameraPosition.z) / rayDirection.z;
        if (rayDistance <= 0.0f) {
            continue;
        }
        const Vec3 point = add(
            layout.cameraPosition,
            multiply(rayDirection, rayDistance));
        footprint.left = std::min(footprint.left, point.x);
        footprint.top = std::min(footprint.top, point.y);
        footprint.right = std::max(footprint.right, point.x);
        footprint.bottom = std::max(footprint.bottom, point.y);
        footprint.valid = true;
    }

    if (footprint.valid) {
        footprint.left -= worldPadding;
        footprint.top -= worldPadding;
        footprint.right += worldPadding;
        footprint.bottom += worldPadding;
    }
    return footprint;
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

// Screen-space barycentric weights of `point` within a triangle, or nothing
// when the point is outside it or the triangle is degenerate.
std::optional<Vec3> triangleWeights(Vec2 point, Vec2 a, Vec2 b, Vec2 c)
{
    constexpr float epsilon = 0.00001f;
    const float area = cross2D(subtract(b, a), subtract(c, a));
    if (std::abs(area) <= epsilon) {
        return std::nullopt;
    }
    const float weightA =
        cross2D(subtract(b, point), subtract(c, point)) / area;
    const float weightB =
        cross2D(subtract(c, point), subtract(a, point)) / area;
    const float weightC = 1.0f - weightA - weightB;
    if (weightA < -epsilon || weightB < -epsilon || weightC < -epsilon) {
        return std::nullopt;
    }
    return Vec3 { weightA, weightB, weightC };
}

// Face-local coordinates of the four quad corners, matching the faceCoords
// table in triangle.vert.glsl. Painting has to agree with the shader about
// which corner is the origin, or strokes land mirrored.
constexpr std::array<Vec2, 4> faceCornerCoords {
    Vec2 { 0.0f, 0.0f },
    Vec2 { 1.0f, 0.0f },
    Vec2 { 1.0f, 1.0f },
    Vec2 { 0.0f, 1.0f },
};

// Face-local coordinate under `point`, perspective-corrected using the stored
// clip w of each corner. Affine interpolation would bias every stroke toward
// the corner nearest the camera.
std::optional<Vec2> faceCoordInQuad(
    Vec2 point,
    const std::array<Vec2, 4>& pixelQuad,
    const std::array<float, 4>& clipW)
{
    constexpr std::array<std::array<std::size_t, 3>, 2> triangles {
        std::array<std::size_t, 3> { 0, 1, 2 },
        std::array<std::size_t, 3> { 0, 2, 3 },
    };
    for (const auto& triangle : triangles) {
        const std::optional<Vec3> weights = triangleWeights(
            point,
            pixelQuad[triangle[0]],
            pixelQuad[triangle[1]],
            pixelQuad[triangle[2]]);
        if (!weights) {
            continue;
        }
        const std::array<float, 3> screenWeights {
            weights->x, weights->y, weights->z,
        };
        float total = 0.0f;
        Vec2 accumulated {};
        for (std::size_t i = 0; i < triangle.size(); ++i) {
            const std::size_t corner = triangle[i];
            const float w = screenWeights[i] / std::max(clipW[corner], 0.001f);
            total += w;
            accumulated.x += faceCornerCoords[corner].x * w;
            accumulated.y += faceCornerCoords[corner].y * w;
        }
        if (std::abs(total) <= 0.00001f) {
            continue;
        }
        return Vec2 { accumulated.x / total, accumulated.y / total };
    }
    return std::nullopt;
}

} // namespace

Vec3 IsoScenePreparer::projectIsoPoint(
    const IsoRenderLayout& layout,
    Vec2 renderExtent,
    Vec3 point)
{
    const Vec4 clip = projectIsoPointToClip(layout, renderExtent, point);
    return {
        clip.x / clip.w,
        clip.y / clip.w,
        std::clamp(clip.z / clip.w, 0.0f, 1.0f),
    };
}

ModelTransformPoints IsoScenePreparer::modelTransformPoints(
    const RenderFrameData::Tile& tile)
{
    if (tile.modelTransform) {
        const RenderFrameData::ModelTransform& authored =
            *tile.modelTransform;
        const Vec3 xAxis = rotateEulerXyz(
            { authored.scale.x, 0.0f, 0.0f },
            authored.rotationRadians);
        const Vec3 yAxis = rotateEulerXyz(
            { 0.0f, authored.scale.y, 0.0f },
            authored.rotationRadians);
        const Vec3 zAxis = rotateEulerXyz(
            { 0.0f, 0.0f, authored.scale.z },
            authored.rotationRadians);
        const Vec3 pivotOffset = add(
            add(
                multiply(xAxis, authored.pivot.x),
                multiply(yAxis, authored.pivot.y)),
            multiply(zAxis, authored.pivot.z));
        const Vec3 origin = subtract(
            authored.translation, pivotOffset);
        return {
            .origin = origin,
            .xPoint = add(origin, xAxis),
            .yPoint = add(origin, yAxis),
            .zPoint = add(origin, zAxis),
        };
    }

    const float x = tile.position.x;
    const float y = tile.position.y;
    const float z = tile.baseElevation;
    const float width = tile.size.x;
    const float depth = tile.size.y;
    const float height = std::max(tile.height, 0.0f);

    ModelTransformPoints result;
    switch (tile.modelRotationQuarterTurns % 4) {
    case 0:
        result.origin = { x, y, z };
        result.xPoint = { x + width, y, z };
        result.yPoint = { x, y + depth, z };
        break;
    case 1:
        result.origin = { x + width, y, z };
        result.xPoint = { x + width, y + depth, z };
        result.yPoint = { x, y, z };
        break;
    case 2:
        result.origin = { x + width, y + depth, z };
        result.xPoint = { x, y + depth, z };
        result.yPoint = { x + width, y, z };
        break;
    case 3:
        result.origin = { x, y + depth, z };
        result.xPoint = { x, y, z };
        result.yPoint = { x + width, y + depth, z };
        break;
    }
    if (std::abs(tile.modelRotationOffsetRadians) > 0.0001f) {
        const float centerX = x + width * 0.5f;
        const float centerY = y + depth * 0.5f;
        const float cosine = std::cos(tile.modelRotationOffsetRadians);
        const float sine = std::sin(tile.modelRotationOffsetRadians);
        auto rotateAroundCenter = [&](Vec3& modelPoint) {
            const float offsetX = modelPoint.x - centerX;
            const float offsetY = modelPoint.y - centerY;
            modelPoint.x = centerX + cosine * offsetX - sine * offsetY;
            modelPoint.y = centerY + sine * offsetX + cosine * offsetY;
        };
        rotateAroundCenter(result.origin);
        rotateAroundCenter(result.xPoint);
        rotateAroundCenter(result.yPoint);
    }
    result.zPoint = {
        result.origin.x,
        result.origin.y,
        z + height,
    };
    return result;
}

std::array<Vec4, 4> IsoScenePreparer::modelWorldTransform(
    const RenderFrameData::Tile& tile)
{
    const ModelTransformPoints points = modelTransformPoints(tile);
    const auto axis = [&points](Vec3 endPoint) {
        return Vec4 {
            endPoint.x - points.origin.x,
            endPoint.y - points.origin.y,
            endPoint.z - points.origin.z,
            0.0f,
        };
    };
    return {
        axis(points.xPoint),
        axis(points.yPoint),
        axis(points.zPoint),
        Vec4 { points.origin.x, points.origin.y, points.origin.z, 1.0f },
    };
}

std::array<Vec4, 4> IsoScenePreparer::modelClipTransform(
    const IsoRenderLayout& layout,
    Vec2 renderExtent,
    const RenderFrameData::Tile& tile)
{
    const ModelTransformPoints points = modelTransformPoints(tile);
    auto difference = [](Vec4 left, Vec4 right) {
        return Vec4 {
            left.x - right.x,
            left.y - right.y,
            left.z - right.z,
            left.w - right.w,
        };
    };

    const Vec4 origin =
        projectIsoPointToClip(layout, renderExtent, points.origin);
    return {
        difference(
            projectIsoPointToClip(layout, renderExtent, points.xPoint),
            origin),
        difference(
            projectIsoPointToClip(layout, renderExtent, points.yPoint),
            origin),
        difference(
            projectIsoPointToClip(layout, renderExtent, points.zPoint),
            origin),
        origin,
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

std::vector<IsoScenePreparer::CachedRenderable>&
IsoScenePreparer::cacheFor(PreparedRenderable::Kind kind) const
{
    switch (kind) {
    case PreparedRenderable::Kind::Tile:
        return tileRenderableCache_;
    case PreparedRenderable::Kind::WaterSurface:
        return waterRenderableCache_;
    case PreparedRenderable::Kind::IsoFace:
        return isoFaceRenderableCache_;
    }
    return tileRenderableCache_;
}

PreparedRenderable IsoScenePreparer::reconcileRenderable(
    PreparedRenderable::Kind kind,
    std::size_t sourceIndex,
    std::span<const Vec3> points,
    GridPosition3 semanticCell,
    RenderModel model,
    uint64_t semanticId,
    uint32_t semanticTag) const
{
    std::vector<CachedRenderable>& cache = cacheFor(kind);
    if (cache.size() <= sourceIndex) {
        cache.resize(sourceIndex + 1);
    }
    CachedRenderable& cached = cache[sourceIndex];

    const bool sameSemanticSource = cached.identity != 0 &&
        cached.semanticCell == semanticCell && cached.model == model &&
        cached.semanticId == semanticId &&
        cached.semanticTag == semanticTag;
    bool samePoints = sameSemanticSource &&
        cached.pointCount == points.size();
    for (std::size_t index = 0; samePoints && index < points.size(); ++index) {
        samePoints = cached.points[index] == points[index];
    }

    if (!sameSemanticSource) {
        cached.identity = nextRenderableIdentity_++;
        cached.boundsRevision = 1;
    } else if (!samePoints) {
        ++cached.boundsRevision;
    }
    if (!samePoints) {
        cached.pointCount = points.size();
        std::ranges::copy(points, cached.points.begin());
        cached.semanticCell = semanticCell;
        cached.model = model;
        cached.semanticId = semanticId;
        cached.semanticTag = semanticTag;
        cached.worldBounds = aabbFromPoints(points);
    }

    return {
        .kind = kind,
        .sourceIndex = sourceIndex,
        .identity = cached.identity,
        .boundsRevision = cached.boundsRevision,
        .worldBounds = cached.worldBounds,
        .boundsReused = samePoints,
    };
}

void IsoScenePreparer::prepare(
    const RenderFrameData& frameData,
    Vec2 renderExtent,
    PreparedRenderScene& scene) const
{
    scene.isoFaces.clear();
    scene.opaqueFaceIndices.clear();
    scene.opaqueBlendedFirst = 0;
    scene.translucentFaceIndices.clear();
    scene.pickFaceIndices.clear();
    scene.opaqueModelIndices.clear();
    scene.translucentModelIndices.clear();
    scene.particles.clear();
    scene.shadowFaces.clear();
    scene.shadowModelIndices.clear();
    scene.renderables.clear();
    scene.reusedRenderableBounds = 0;
    scene.rebuiltRenderableBounds = 0;
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
            !frameData.particles.empty() ||
            std::ranges::any_of(
                frameData.tiles,
                [](const RenderFrameData::Tile& tile) {
                    return tile.blurBehind ||
                        tile.color.w < 1.0f ||
                        tile.effect == RenderSurfaceEffect::MirrorEnergy;
                }) ||
            std::ranges::any_of(
                frameData.isoFaces,
                [](const RenderFrameData::IsoFace& face) {
                    return face.effect ==
                        RenderSurfaceEffect::MirrorEnergy;
                }));

    scene.isoFaces.reserve(
        frameData.tiles.size() * 5 + frameData.waterSurfaces.size());
    scene.opaqueFaceIndices.reserve(frameData.tiles.size() * 3);
    scene.translucentFaceIndices.reserve(
        frameData.tiles.size() + frameData.waterSurfaces.size());
    scene.pickFaceIndices.reserve(
        frameData.tiles.size() * 3 + frameData.waterSurfaces.size());
    scene.shadowFaces.reserve(frameData.tiles.size() * 5);
    scene.particles.reserve(frameData.particles.size());
    scene.renderables.reserve(
        frameData.tiles.size() + frameData.waterSurfaces.size() +
        frameData.isoFaces.size());

    tileRenderableCache_.resize(frameData.tiles.size());
    waterRenderableCache_.resize(frameData.waterSurfaces.size());
    isoFaceRenderableCache_.resize(frameData.isoFaces.size());

    const auto appendRenderable = [&scene](PreparedRenderable renderable) {
        if (renderable.boundsReused) {
            ++scene.reusedRenderableBounds;
        } else {
            ++scene.rebuiltRenderableBounds;
        }
        scene.renderables.push_back(renderable);
    };
    for (std::size_t tileIndex = 0;
         tileIndex < frameData.tiles.size();
         ++tileIndex) {
        const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
        const std::array<Vec3, 8> corners = tileCorners(tile);
        appendRenderable(reconcileRenderable(
            PreparedRenderable::Kind::Tile,
            tileIndex,
            corners,
            tile.renderableId == 0 ? tile.cell : GridPosition3 {},
            tile.model,
            tile.renderableId,
            static_cast<uint32_t>(tile.effect)));
    }
    for (std::size_t faceIndex = 0;
         faceIndex < frameData.isoFaces.size();
         ++faceIndex) {
        const RenderFrameData::IsoFace& face = frameData.isoFaces[faceIndex];
        appendRenderable(reconcileRenderable(
            PreparedRenderable::Kind::IsoFace,
            faceIndex,
            face.vertices,
            {},
            cubeModel,
            0,
            static_cast<uint32_t>(face.effect)));
    }

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
                             PreparedSurfaceMaterial material,
                             uint32_t shorelineMask) {
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
            .worldHeight = vertices[0].z,
            .material = material,
            .shorelineMask = shorelineMask,
            .depth = faceDepth(scene.isoLayout, vertices),
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            face.worldVertices[i] = vertices[i];
            face.vertices[i] = projectIsoPoint(
                scene.isoLayout, scene.renderExtent, vertices[i]);
            face.clipW[i] = std::max(
                dot(
                    subtract(vertices[i], scene.isoLayout.cameraPosition),
                    scene.isoLayout.cameraForward),
                0.001f);
        }
        const std::size_t index = scene.isoFaces.size();
        scene.isoFaces.push_back(face);
        if (drawable) {
            (blurBehind ||
                    material == PreparedSurfaceMaterial::Water ||
                    material == PreparedSurfaceMaterial::MirrorEnergy
                    ? scene.translucentFaceIndices
                    : scene.opaqueFaceIndices)
                .push_back(index);
        }
        if (pickable) {
            scene.pickFaceIndices.push_back(index);
        }
    };

    auto appendShadowFace = [&](const std::array<Vec3, 4>& vertices) {
        scene.shadowFaces.push_back(vertices);
    };

    if (frameData.viewMode == RenderViewMode::Isometric3D) {
        for (std::size_t tileIndex = 0;
             tileIndex < frameData.tiles.size();
             ++tileIndex) {
            const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
            const float width = tile.size.x;
            const float depth = tile.size.y;
            const float height = std::max(tile.height, 0.0f);
            const bool drawCube = tile.model.isCube() && !tile.pickOnly;
            // Authored model transforms describe how mesh-local coordinates
            // reach the world, but their unit cube is not necessarily the
            // model's logical editor hit box. Model-backed editor objects such
            // as selector flags carry an explicit centered position/size/
            // height for picking, so use that volume for their invisible
            // faces while leaving their visual transform untouched.
            const std::array<Vec3, 8> corners = drawCube
                ? tileCorners(tile)
                : logicalTileCorners(tile);
            const bool pickable =
                tile.pickable &&
                !tile.isEditorPreview &&
                tile.effect != RenderSurfaceEffect::MirrorEnergy;
            const PreparedSurfaceMaterial tileMaterial =
                tile.effect == RenderSurfaceEffect::MirrorEnergy
                ? PreparedSurfaceMaterial::MirrorEnergy
                : PreparedSurfaceMaterial::Standard;
            // Splatting is a top-surface treatment: the sides of a ground
            // block keep the flat tile material.
            const PreparedSurfaceMaterial topMaterial =
                tile.effect == RenderSurfaceEffect::GroundSplat && drawCube
                ? PreparedSurfaceMaterial::GroundSplat
                : tileMaterial;
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
                    topMaterial,
                    0);
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
                    tileMaterial,
                    0);
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
                    tileMaterial,
                    0);
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
                    tileMaterial,
                    0);
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
                    tileMaterial,
                    0);
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
                    topMaterial,
                    0);
            }

            if (!tile.model.isCube() && !tile.pickOnly) {
                (tile.blurBehind || tile.color.w < 1.0f ||
                        tile.effect == RenderSurfaceEffect::MirrorEnergy
                        ? scene.translucentModelIndices
                        : scene.opaqueModelIndices)
                    .push_back(tileIndex);
            }
        }

        for (const RenderFrameData::WaterSurface& water :
             frameData.waterSurfaces) {
            const std::size_t waterIndex = static_cast<std::size_t>(
                &water - frameData.waterSurfaces.data());
            float left = water.position.x;
            float top = water.position.y;
            float right = left + water.size.x;
            float bottom = top + water.size.y;

            // The exterior strips are deliberately excluded from camera fit,
            // but still need to reach every visible ray/plane intersection.
            // Extending only these large, non-pickable strips preserves the
            // authored shoreline cells while avoiding a far-edge seam at
            // wide aspect ratios or low camera pitches.
            if (!water.pickable &&
                (water.size.x > 1.0f || water.size.y > 1.0f)) {
                const PlaneFootprint footprint = visiblePlaneFootprint(
                    scene.isoLayout,
                    scene.renderExtent,
                    water.elevation);
                if (footprint.valid) {
                    const float boardRight =
                        static_cast<float>(frameData.levelWidth);
                    const float boardBottom =
                        static_cast<float>(frameData.levelHeight);
                    if (right <= -1.0f) {
                        left = std::min(left, footprint.left);
                        top = std::min(top, footprint.top);
                        bottom = std::max(bottom, footprint.bottom);
                    } else if (left >= boardRight + 1.0f) {
                        right = std::max(right, footprint.right);
                        top = std::min(top, footprint.top);
                        bottom = std::max(bottom, footprint.bottom);
                    } else if (bottom <= -1.0f) {
                        left = std::min(left, footprint.left);
                        right = std::max(right, footprint.right);
                        top = std::min(top, footprint.top);
                    } else if (top >= boardBottom + 1.0f) {
                        left = std::min(left, footprint.left);
                        right = std::max(right, footprint.right);
                        bottom = std::max(bottom, footprint.bottom);
                    }
                }
            }
            const std::array<Vec3, 4> waterVertices {
                    Vec3 { left, top, water.elevation },
                    Vec3 { right, top, water.elevation },
                    Vec3 { right, bottom, water.elevation },
                    Vec3 { left, bottom, water.elevation },
            };
            appendRenderable(reconcileRenderable(
                PreparedRenderable::Kind::WaterSurface,
                waterIndex,
                waterVertices,
                water.cell,
                cubeModel,
                0,
                water.shorelineMask));
            appendIsoFace(
                waterVertices,
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
                water.pickable && !water.isEditorPreview,
                true,
                { right - left, bottom - top },
                PreparedSurfaceMaterial::Water,
                water.shorelineMask);
        }

        for (const RenderFrameData::IsoFace& source : frameData.isoFaces) {
            const Vec3 normal = normalize(cross(
                subtract(source.vertices[1], source.vertices[0]),
                subtract(source.vertices[2], source.vertices[0])));
            PreparedIsoFace face {
                .normal = normal,
                .color = source.color,
                .material =
                    source.effect == RenderSurfaceEffect::MirrorEnergy
                    ? PreparedSurfaceMaterial::MirrorEnergy
                    : PreparedSurfaceMaterial::Standard,
                .depth = faceDepth(scene.isoLayout, source.vertices),
            };
            for (std::size_t i = 0; i < source.vertices.size(); ++i) {
                face.worldVertices[i] = source.vertices[i];
                face.vertices[i] = projectIsoPoint(
                    scene.isoLayout, scene.renderExtent, source.vertices[i]);
            }
            (face.material == PreparedSurfaceMaterial::MirrorEnergy
                    ? scene.translucentFaceIndices
                    : scene.opaqueFaceIndices)
                .push_back(scene.isoFaces.size());
            scene.isoFaces.push_back(face);
        }

        auto fartherFirst = [&](std::size_t left, std::size_t right) {
            return scene.isoFaces[left].depth > scene.isoFaces[right].depth;
        };
        auto nearerFirst = [&](std::size_t left, std::size_t right) {
            return scene.isoFaces[left].depth < scene.isoFaces[right].depth;
        };
        // Opaque geometry draws nearest first so the depth test rejects
        // occluded fragments before they are shaded. Back-to-front is the
        // painter's-algorithm order, which guarantees the opposite: every
        // hidden surface is shaded and then overwritten. Picking is unaffected
        // - it walks pickFaceIndices, which is unsorted and compares depth
        // explicitly.
        //
        // Two things make the opaque order matter anyway, and front-to-back
        // has to handle both or it silently breaks geometry that the
        // painter's order was covering for:
        //
        // 1. A face in this list may carry a sub-1.0 alpha - the editor's
        //    ladder-rung preview does. Those blend, and they write depth.
        //    Drawn front to back, such a face writes depth before the
        //    geometry behind it is drawn, so it blends against the
        //    background instead of against that geometry, and the result is
        //    a hole rather than a tint. They are partitioned off and drawn
        //    last, farthest first, which is the order the whole list used to
        //    be in.
        // 2. Coincident fully opaque surfaces are resolved by the depth
        //    test, and LESS_OR_EQUAL hands the pixel to whichever drew last.
        //    Reversing the order therefore reverses every tie. The recorder
        //    answers that by running this prefix on LESS; the partition is
        //    what tells it where the prefix ends.
        if (opaqueFrontToBackSort_) {
            const auto blendedBegin = std::stable_partition(
                scene.opaqueFaceIndices.begin(),
                scene.opaqueFaceIndices.end(),
                [&scene](std::size_t index) {
                    return scene.isoFaces[index].color.w >= 1.0f;
                });
            std::sort(
                scene.opaqueFaceIndices.begin(), blendedBegin, nearerFirst);
            std::sort(
                blendedBegin, scene.opaqueFaceIndices.end(), fartherFirst);
            scene.opaqueBlendedFirst = static_cast<std::size_t>(
                blendedBegin - scene.opaqueFaceIndices.begin());
        } else {
            // Legacy order: one back-to-front list, blended faces left
            // interleaved, and no LESS prefix for the recorder to apply.
            std::ranges::sort(scene.opaqueFaceIndices, fartherFirst);
            scene.opaqueBlendedFirst = scene.opaqueFaceIndices.size();
        }
        // Translucent geometry has no such freedom: blending is order
        // dependent, so it must stay farthest first.
        std::ranges::sort(scene.translucentFaceIndices, fartherFirst);

        for (const RenderFrameData::Particle& source : frameData.particles) {
            if (source.texture.isNone() || source.color.w <= 0.0f ||
                source.size.x <= 0.0f || source.size.y <= 0.0f) {
                continue;
            }
            const float cosine = std::cos(source.rotationRadians);
            const float sine = std::sin(source.rotationRadians);
            const Vec3 right = add(
                multiply(
                    scene.isoLayout.cameraRight,
                    cosine * source.size.x * 0.5f),
                multiply(
                    scene.isoLayout.cameraUp,
                    sine * source.size.x * 0.5f));
            const Vec3 up = add(
                multiply(
                    scene.isoLayout.cameraUp,
                    cosine * source.size.y * 0.5f),
                multiply(
                    scene.isoLayout.cameraRight,
                    -sine * source.size.y * 0.5f));
            const std::array worldVertices {
                subtract(source.position, add(right, up)),
                add(subtract(source.position, up), right),
                add(source.position, add(right, up)),
                add(subtract(source.position, right), up),
            };
            PreparedParticle particle {
                .color = source.color,
                .texture = source.texture,
                .depth = dot(
                    subtract(
                        source.position,
                        scene.isoLayout.cameraPosition),
                    scene.isoLayout.cameraForward),
                .drawOnTop = source.drawOnTop,
            };
            particle.vertices = worldVertices;
            scene.particles.push_back(particle);
        }
        std::ranges::sort(
            scene.particles,
            [](const PreparedParticle& left, const PreparedParticle& right) {
                if (left.drawOnTop != right.drawOnTop) {
                    return !left.drawOnTop;
                }
                return left.depth > right.depth;
            });
    }

    for (std::size_t tileIndex = 0;
         tileIndex < frameData.tiles.size();
         ++tileIndex) {
        const RenderFrameData::Tile& tile = frameData.tiles[tileIndex];
        if (tile.isEditorPreview || tile.pickOnly ||
            tile.effect == RenderSurfaceEffect::MirrorEnergy) {
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
        if (face.effect != RenderSurfaceEffect::MirrorEnergy) {
            appendShadowFace(face.vertices);
        }
    }

}

std::optional<GridPosition3> IsoScenePreparer::pickGridCell(
    const PreparedRenderScene& scene,
    Vec2 pixelPosition,
    Vec2 outputExtent,
    uint32_t levelWidth,
    uint32_t levelHeight,
    uint32_t gridPickBorder) const
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
        const int pickBorder = static_cast<int>(gridPickBorder);
        if (!depth || *depth >= pickedDepth ||
            face.pickBoundsCell.x < -pickBorder ||
            face.pickBoundsCell.y < -pickBorder ||
            face.pickBoundsCell.x >=
                static_cast<int>(levelWidth) + pickBorder ||
            face.pickBoundsCell.y >=
                static_cast<int>(levelHeight) + pickBorder) {
            continue;
        }
        picked = face.cell;
        pickedDepth = *depth;
    }
    return picked;
}

std::optional<Vec3> IsoScenePreparer::pickGroundPoint(
    const PreparedRenderScene& scene,
    Vec2 pixelPosition,
    Vec2 outputExtent) const
{
    if (outputExtent.x <= 0.0f || outputExtent.y <= 0.0f) {
        return std::nullopt;
    }

    std::optional<Vec3> picked;
    float pickedDepth = std::numeric_limits<float>::max();
    // Every face, not just scene.pickFaceIndices: that list excludes editor
    // previews, which is right for tile editing but wrong here. Ground on a
    // layer other than the active one is drawn as a preview, and painting its
    // texture has nothing to do with which tile layer is being edited - the
    // ground you can see is the ground you can paint.
    for (const PreparedIsoFace& face : scene.isoFaces) {
        // Only splattable tops are paintable. Block sides, non-ground tiles
        // and the editor's invisible pick planes all carry a different
        // material, and a brush stroke on them has nowhere to go.
        if (face.material != PreparedSurfaceMaterial::GroundSplat) {
            continue;
        }

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

        const std::optional<float> depth =
            pointDepthInQuad(pixelPosition, pixelQuad);
        if (!depth || *depth >= pickedDepth) {
            continue;
        }
        const std::optional<Vec2> faceCoord =
            faceCoordInQuad(pixelPosition, pixelQuad2D, face.clipW);
        if (!faceCoord) {
            continue;
        }

        // Same reconstruction the shader performs, so the painted texel is the
        // one under the cursor.
        picked = Vec3 {
            face.worldOrigin.x + faceCoord->x * face.gridSize.x,
            face.worldOrigin.y + faceCoord->y * face.gridSize.y,
            face.worldHeight,
        };
        pickedDepth = *depth;
    }
    return picked;
}

} // namespace sokoban
