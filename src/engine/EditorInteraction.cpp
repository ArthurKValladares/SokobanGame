#include "engine/EditorInteraction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sokoban {
namespace {

constexpr int brushSegments = 48;
constexpr int brushRings = 12;
constexpr float brushPreviewAlpha = 0.72f;

Vec3 addScaled(Vec3 origin, Vec3 axis, float amount)
{
    return {
        origin.x + axis.x * amount,
        origin.y + axis.y * amount,
        origin.z + axis.z * amount,
    };
}

float pixelDistance(Vec2 left, Vec2 right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return std::sqrt(x * x + y * y);
}

} // namespace

EditorInteraction::BrushPreview EditorInteraction::brushPreview(
    const SplatCanvas::Brush& brush,
    Vec3 brushPoint,
    const ProjectToPixels& project)
{
    BrushPreview preview;
    if (brush.radiusTiles <= 0.0f) {
        return preview;
    }

    preview.vertices.reserve((brushRings + 1) * brushSegments);
    preview.indices.reserve(brushRings * brushSegments * 6);
    preview.rim.reserve(brushSegments);

    const auto projectRingPoint = [&](float radius, int segment) {
        const float angle = static_cast<float>(segment) *
            (2.0f * std::numbers::pi_v<float>) /
            static_cast<float>(brushSegments);
        return project({
            brushPoint.x + std::cos(angle) * radius,
            brushPoint.y + std::sin(angle) * radius,
            brushPoint.z,
        }).value_or(Vec2 {});
    };

    for (int ring = 0; ring <= brushRings; ++ring) {
        const float t =
            static_cast<float>(ring) / static_cast<float>(brushRings);
        const float radius = brush.radiusTiles * t;
        const float opacity = std::clamp(
            SplatCanvas::coverageAt(radius, brush) * brushPreviewAlpha,
            0.0f,
            1.0f);
        for (int segment = 0; segment < brushSegments; ++segment) {
            preview.vertices.push_back({
                .position = projectRingPoint(radius, segment),
                .opacity = opacity,
            });
        }
    }

    for (int ring = 0; ring < brushRings; ++ring) {
        for (int segment = 0; segment < brushSegments; ++segment) {
            const int next = (segment + 1) % brushSegments;
            const auto inner = static_cast<std::uint32_t>(
                ring * brushSegments);
            const auto outer = static_cast<std::uint32_t>(
                (ring + 1) * brushSegments);
            preview.indices.insert(preview.indices.end(), {
                inner + static_cast<std::uint32_t>(segment),
                inner + static_cast<std::uint32_t>(next),
                outer + static_cast<std::uint32_t>(next),
                inner + static_cast<std::uint32_t>(segment),
                outer + static_cast<std::uint32_t>(next),
                outer + static_cast<std::uint32_t>(segment),
            });
        }
    }

    for (int segment = 0; segment < brushSegments; ++segment) {
        preview.rim.push_back(
            projectRingPoint(brush.radiusTiles, segment));
    }
    return preview;
}

std::optional<DecorationGizmo::Geometry>
EditorInteraction::decorationGizmoGeometry(
    const Level::Decoration& decoration,
    const ProjectToPixels& project)
{
    const std::optional<Vec2> projectedOrigin = project(decoration.position);
    if (!projectedOrigin) {
        return std::nullopt;
    }

    constexpr float targetAxisLengthPixels = 92.0f;
    constexpr float ringRadiusScale = 0.72f;
    const std::array<Vec3, 3> axes {
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
    };

    DecorationGizmo::Geometry geometry;
    geometry.origin = *projectedOrigin;
    std::array<float, 3> worldLengths {};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const std::optional<Vec2> projectedUnit = project(
            addScaled(decoration.position, axes[axis], 1.0f));
        if (!projectedUnit) {
            return std::nullopt;
        }
        const float unitPixels = std::max(
            pixelDistance(*projectedUnit, *projectedOrigin), 1.0f);
        worldLengths[axis] = std::clamp(
            targetAxisLengthPixels / unitPixels, 0.05f, 100.0f);
        const std::optional<Vec2> endpoint = project(addScaled(
            decoration.position, axes[axis], worldLengths[axis]));
        if (!endpoint) {
            return std::nullopt;
        }
        geometry.axes[axis] = {
            .start = *projectedOrigin,
            .end = *endpoint,
            .worldLength = worldLengths[axis],
        };
    }

    constexpr int ringSegments = 64;
    const std::array<std::array<std::size_t, 2>, 3> ringAxes {
        std::array<std::size_t, 2> { 1, 2 },
        std::array<std::size_t, 2> { 0, 2 },
        std::array<std::size_t, 2> { 0, 1 },
    };
    for (std::size_t ring = 0; ring < geometry.rings.size(); ++ring) {
        std::vector<Vec2>& points = geometry.rings[ring];
        points.reserve(ringSegments + 1);
        for (int segment = 0; segment <= ringSegments; ++segment) {
            const float angle = static_cast<float>(segment) *
                2.0f * std::numbers::pi_v<float> /
                static_cast<float>(ringSegments);
            const std::size_t first = ringAxes[ring][0];
            const std::size_t second = ringAxes[ring][1];
            Vec3 world = addScaled(
                decoration.position,
                axes[first],
                std::cos(angle) * worldLengths[first] * ringRadiusScale);
            world = addScaled(
                world,
                axes[second],
                std::sin(angle) * worldLengths[second] * ringRadiusScale);
            const std::optional<Vec2> pixel = project(world);
            if (!pixel) {
                return std::nullopt;
            }
            points.push_back(*pixel);
        }
    }
    return geometry;
}

Vec2 EditorInteraction::pointerPixels(
    Vec2 pointer,
    Vec2 windowSize,
    Vec2 pixelSize)
{
    return {
        windowSize.x > 0.0f
            ? pointer.x * pixelSize.x / windowSize.x
            : pointer.x,
        windowSize.y > 0.0f
            ? pointer.y * pixelSize.y / windowSize.y
            : pointer.y,
    };
}

std::vector<EditorInteraction::SelectorLabel>
EditorInteraction::selectorLabels(
    const std::vector<Level::ScreenSelector>& selectors,
    const ProjectToPixels& project)
{
    std::vector<SelectorLabel> labels;
    labels.reserve(selectors.size());
    for (const Level::ScreenSelector& selector : selectors) {
        const std::optional<Vec2> anchor = project({
            static_cast<float>(selector.cell.x) + 0.5f,
            static_cast<float>(selector.cell.y) + 0.5f,
            static_cast<float>(selector.cell.z) + 1.25f,
        });
        if (!anchor) {
            continue;
        }
        labels.push_back({
            .id = selector.id,
            .text = "Selector " + std::to_string(selector.id),
            .anchor = *anchor,
        });
    }
    return labels;
}

} // namespace sokoban
