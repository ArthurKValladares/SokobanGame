#include "engine/DecorationGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sokoban {
namespace {

constexpr float axisHitRadiusPixels = 11.0f;
constexpr float ringHitRadiusPixels = 9.0f;
constexpr float rotationDegreesPerPixel = 0.75f;
constexpr std::array<DecorationGizmo::Axis, 3> gizmoAxes {
    DecorationGizmo::Axis::X,
    DecorationGizmo::Axis::Y,
    DecorationGizmo::Axis::Z,
};

float dot(Vec2 left, Vec2 right)
{
    return left.x * right.x + left.y * right.y;
}

Vec2 subtract(Vec2 left, Vec2 right)
{
    return { left.x - right.x, left.y - right.y };
}

float length(Vec2 value)
{
    return std::sqrt(dot(value, value));
}

Vec2 normalized(Vec2 value)
{
    const float magnitude = length(value);
    return magnitude > 0.0001f
        ? Vec2 { value.x / magnitude, value.y / magnitude }
        : Vec2 { 1.0f, 0.0f };
}

float distanceToSegment(Vec2 point, Vec2 start, Vec2 end)
{
    const Vec2 segment = subtract(end, start);
    const float lengthSquared = dot(segment, segment);
    if (lengthSquared <= 0.0001f) {
        return length(subtract(point, start));
    }
    const float t = std::clamp(
        dot(subtract(point, start), segment) / lengthSquared,
        0.0f,
        1.0f);
    const Vec2 nearest {
        start.x + segment.x * t,
        start.y + segment.y * t,
    };
    return length(subtract(point, nearest));
}

std::size_t axisIndex(DecorationGizmo::Axis axis)
{
    return static_cast<std::size_t>(axis);
}

float& component(Vec3& value, DecorationGizmo::Axis axis)
{
    switch (axis) {
    case DecorationGizmo::Axis::X: return value.x;
    case DecorationGizmo::Axis::Y: return value.y;
    case DecorationGizmo::Axis::Z: return value.z;
    }
    return value.x;
}

} // namespace

void DecorationGizmo::setMode(Mode mode)
{
    if (!drag_) {
        mode_ = mode;
    }
}

DecorationGizmo::Mode DecorationGizmo::mode() const
{
    return mode_;
}

std::optional<DecorationGizmo::Axis> DecorationGizmo::hoveredAxis(
    const Geometry& geometry,
    Vec2 pointer) const
{
    float nearestDistance = std::numeric_limits<float>::max();
    std::optional<Axis> nearest;
    for (const Axis axis : gizmoAxes) {
        const std::size_t index = axisIndex(axis);
        float distance = std::numeric_limits<float>::max();
        if (mode_ == Mode::Rotate) {
            const std::vector<Vec2>& ring = geometry.rings[index];
            for (std::size_t point = 1; point < ring.size(); ++point) {
                distance = std::min(
                    distance,
                    distanceToSegment(pointer, ring[point - 1], ring[point]));
            }
        } else {
            distance = distanceToSegment(
                pointer,
                geometry.axes[index].start,
                geometry.axes[index].end);
        }
        const float radius = mode_ == Mode::Rotate
            ? ringHitRadiusPixels
            : axisHitRadiusPixels;
        if (distance <= radius && distance < nearestDistance) {
            nearestDistance = distance;
            nearest = axis;
        }
    }
    return nearest;
}

bool DecorationGizmo::beginDrag(
    const Geometry& geometry,
    Vec2 pointer,
    const Level::Decoration& decoration)
{
    if (drag_) {
        return false;
    }
    const std::optional<Axis> hit = hoveredAxis(geometry, pointer);
    if (!hit) {
        return false;
    }

    Vec2 direction;
    float unitsPerPixel = 1.0f;
    const std::size_t index = axisIndex(*hit);
    if (mode_ == Mode::Rotate) {
        const std::vector<Vec2>& ring = geometry.rings[index];
        float nearestDistance = std::numeric_limits<float>::max();
        for (std::size_t point = 1; point < ring.size(); ++point) {
            const float distance = distanceToSegment(
                pointer, ring[point - 1], ring[point]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                direction = normalized(subtract(ring[point], ring[point - 1]));
            }
        }
        unitsPerPixel = rotationDegreesPerPixel;
    } else {
        const AxisHandle& handle = geometry.axes[index];
        const Vec2 projectedAxis = subtract(handle.end, handle.start);
        const float projectedLength = std::max(length(projectedAxis), 1.0f);
        direction = normalized(projectedAxis);
        unitsPerPixel = mode_ == Mode::Translate
            ? handle.worldLength / projectedLength
            : 1.0f / projectedLength;
    }

    drag_ = DragState {
        .axis = *hit,
        .startPointer = pointer,
        .pixelDirection = direction,
        .unitsPerPixel = unitsPerPixel,
        .startDecoration = decoration,
    };
    return true;
}

std::optional<Level::Decoration> DecorationGizmo::updateDrag(Vec2 pointer) const
{
    if (!drag_) {
        return std::nullopt;
    }
    Level::Decoration result = drag_->startDecoration;
    const float amount = dot(
        subtract(pointer, drag_->startPointer),
        drag_->pixelDirection) * drag_->unitsPerPixel;
    switch (mode_) {
    case Mode::Translate:
        component(result.position, drag_->axis) += amount;
        break;
    case Mode::Rotate:
        component(result.rotationDegrees, drag_->axis) += amount;
        break;
    case Mode::Scale: {
        float& scale = component(result.scale, drag_->axis);
        scale = std::max(scale + amount, 0.001f);
        break;
    }
    }
    return result;
}

void DecorationGizmo::endDrag()
{
    drag_.reset();
}

bool DecorationGizmo::dragging() const
{
    return drag_.has_value();
}

std::optional<DecorationGizmo::Axis> DecorationGizmo::activeAxis() const
{
    return drag_ ? std::optional<Axis>(drag_->axis) : std::nullopt;
}

} // namespace sokoban
